#include "musyx/platform.h"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_timer.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "musyx/adsr.h"
#include "musyx/assert.h"
#include "musyx/hardware.h"
#include "musyx/musyx.h"
#include "musyx/sal.h"
#include "musyx/synth.h"
#include "musyx/port_voices.h"
#include "musyx/pc_audio_math.h"

// Audio parameters
#define SAL_SAMPLES_PER_FRAME 160
#define SAL_SUBFRAMES 5
#define SAL_SAMPLES_PER_SUBFRAME (SAL_SAMPLES_PER_FRAME / SAL_SUBFRAMES)
#define SAL_STEREO_SAMPLES (SAL_SAMPLES_PER_FRAME * 2)
#define SAL_BUFFER_BYTES (SAL_STEREO_SAMPLES * sizeof(s16))
#define SRC_STAGING_SIZE 256
#define POLYPHASE_PHASES 128
#define POLYPHASE_TAPS 4
#define SINC_PHASES 512
#define SINC_TAPS 8
#define SINC_HALF_TAPS (SINC_TAPS / 2)

// Mode 0: GC polyphase, mode 1: windowed sinc
#ifndef SAL_RESAMPLE_MODE
#define SAL_RESAMPLE_MODE 0
#endif

#if SAL_RESAMPLE_MODE != 0 && SAL_RESAMPLE_MODE != 1
#error "SAL_RESAMPLE_MODE must be 0 (GC polyphase) or 1 (windowed sinc)"
#endif

// Double buffer for output
static s16 salOutputBuffers[2][SAL_STEREO_SAMPLES];
static u8 salOutputIndex = 0;

// SDL3 audio
static SDL_AudioStream* salAudioStream = NULL;
static SDL_Thread* salAudioThread = NULL;
static SDL_AtomicInt salAudioThreadRunning;
static SDL_Mutex* globalMutex;
static bool salAudioSubsystemInitialized;
static bool salTraceAudio;
static u64 salStatsStart, salStatsFrames, salStatsClipped;
static int salStatsPeak;

// Runtime mute (used by the port's debug overlay). The mixer still runs so the
// engine's timing is unchanged; only the pushed output is silenced.
static SDL_AtomicInt salMuted;
void salSetMuted(int muted) { SDL_SetAtomicInt(&salMuted, muted != 0); }

// The port's volume boost (F1 > Game > Audio above 100 percent, `music_boost`/`sfx_boost`): a
// linear factor on the decoded voice, 1.0 for the game's own maximum. The mix thread reads it once
// per voice, so a change lands between voices; a plain float is enough for that.
static float salVolumeBoostMusic = 1.0f;
static float salVolumeBoostSfx = 1.0f;

static SND_SOME_CALLBACK userCallback = NULL;

extern float gPortListenerHeading[3];

// Sample ids the user has muted from the debug overlay (seeded from
// MP_MUTE_SMP=65535,93). Muting silences the voice but keeps it running so its
// lifetime and timing are unchanged.
#define SAL_MAX_MUTED_SAMPLES 64
static u16 sMutedSamples[SAL_MAX_MUTED_SAMPLES];
static int sMutedSampleCount = 0;
static int sMuteListInitialized = 0;

static void salInitMuteList(void) {
  if (sMuteListInitialized) {
    return;
  }
  sMuteListInitialized = 1;
  const char* env = getenv("MP_MUTE_SMP");
  if (env == NULL) {
    return;
  }
  char buf[256];
  strncpy(buf, env, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  char* tok = strtok(buf, ",");
  while (tok != NULL && sMutedSampleCount < SAL_MAX_MUTED_SAMPLES) {
    sMutedSamples[sMutedSampleCount++] = (u16)strtoul(tok, NULL, 0);
    tok = strtok(NULL, ",");
  }
}

int MusyxPortIsSampleMuted(unsigned smpId) {
  salInitMuteList();
  for (int i = 0; i < sMutedSampleCount; ++i) {
    if (sMutedSamples[i] == (u16)smpId) {
      return 1;
    }
  }
  return 0;
}

void MusyxPortSetSampleMuted(unsigned smpId, int muted) {
  salInitMuteList();
  const u16 id = (u16)smpId;
  int idx = -1;
  for (int i = 0; i < sMutedSampleCount; ++i) {
    if (sMutedSamples[i] == id) {
      idx = i;
      break;
    }
  }
  if (muted && idx < 0) {
    if (sMutedSampleCount < SAL_MAX_MUTED_SAMPLES) {
      sMutedSamples[sMutedSampleCount++] = id;
    }
  } else if (!muted && idx >= 0) {
    sMutedSamples[idx] = sMutedSamples[--sMutedSampleCount];
  }
}

void MusyxPortClearSampleMutes(void) { sMutedSampleCount = 0; }

// The port's two volume boosts: the music path (a streamed voice) and everything else. 1.0 is no
// boost, so the default mix stays exactly the game's.
void MusyxPortSetVolumeBoost(float music, float sfx) {
  salVolumeBoostMusic = music > 0.0f ? music : 1.0f;
  salVolumeBoostSfx = sfx > 0.0f ? sfx : 1.0f;
}

int MusyxPortGetMutedSamples(unsigned* out, int maxIds) {
  salInitMuteList();
  const int count = sMutedSampleCount < maxIds ? sMutedSampleCount : maxIds;
  for (int i = 0; i < count; ++i) {
    out[i] = sMutedSamples[i];
  }
  return count;
}

// ADPCM decode state per voice
static s16 adpcmYn1[SYNTH_MAX_VOICES];
static s16 adpcmYn2[SYNTH_MAX_VOICES];
static u8 adpcmPS[SYNTH_MAX_VOICES];
static u8 adpcmRestart[SYNTH_MAX_VOICES];

typedef struct VoiceResamplerState {
  s32 srcBuf[SRC_STAGING_SIZE];
  u32 srcCount;
  u32 srcConsumed;
  u32 curPos;
  u32 srcPosHi;
  s16 lastSamples[POLYPHASE_TAPS];
  u8 ended;
} VoiceResamplerState;

static VoiceResamplerState voiceResampler[SYNTH_MAX_VOICES];
static u32 voiceEffPitch[SYNTH_MAX_VOICES];
static s32 voiceRms[SYNTH_MAX_VOICES];
static s16 polyphaseTable[POLYPHASE_PHASES][POLYPHASE_TAPS];
static s16 sincTable[SINC_PHASES][SINC_TAPS];
static u8 resampleTablesInitialized = 0;

// Mix accumulation buffers
static s32 mixBufferL[SAL_SAMPLES_PER_FRAME];
static s32 mixBufferR[SAL_SAMPLES_PER_FRAME];
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 1)
static s32 filterState[SYNTH_MAX_VOICES];
#endif

#define SURROUND_DOWNMIX_GAIN 23170

static inline s16 clamp16(s32 v) {
  if (v > 32767)
    return 32767;
  if (v < -32768)
    return -32768;
  return (s16)v;
}

static inline s16 clampCoeff(s32 v) {
  if (v > 32767)
    return 32767;
  if (v < -32768)
    return -32768;
  return (s16)v;
}

static inline double sincUnit(double x) {
  if (SDL_fabs(x) < 1e-12)
    return 1.0;

  const double pix = x * SDL_PI_D;
  return SDL_sin(pix) / pix;
}

static void initResampleTables(void) {
  if (resampleTablesInitialized)
    return;

  double rawPolyphase[POLYPHASE_PHASES * POLYPHASE_TAPS];
  double maxPolyphaseSum = 0.0;

  for (u32 i = 0; i < SDL_arraysize(rawPolyphase); ++i) {
    const double phase = (double)i / (double)(SDL_arraysize(rawPolyphase) - 1);
    const double x = -2.0 + 4.0 * phase;
    const double window =
        0.54 - 0.46 * SDL_cos((2.0 * SDL_PI_D * i) / (SDL_arraysize(rawPolyphase) - 1));
    rawPolyphase[i] = sincUnit(x * 0.5) * window;
  }

  for (u32 phase = 0; phase < POLYPHASE_PHASES; ++phase) {
    const double sum = rawPolyphase[POLYPHASE_PHASES - 1 - phase] +
                       rawPolyphase[POLYPHASE_PHASES * 2 - 1 - phase] +
                       rawPolyphase[POLYPHASE_PHASES * 3 - 1 - phase] +
                       rawPolyphase[POLYPHASE_PHASES * 4 - 1 - phase];
    if (sum > maxPolyphaseSum)
      maxPolyphaseSum = sum;
  }

  if (maxPolyphaseSum <= 0.0)
    maxPolyphaseSum = 1.0;

  for (u32 phase = 0; phase < POLYPHASE_PHASES; ++phase) {
    polyphaseTable[phase][0] = clampCoeff(
        (s32)SDL_lround((rawPolyphase[POLYPHASE_PHASES - 1 - phase] / maxPolyphaseSum) * 32767.0));
    polyphaseTable[phase][1] = clampCoeff((s32)SDL_lround(
        (rawPolyphase[POLYPHASE_PHASES * 2 - 1 - phase] / maxPolyphaseSum) * 32767.0));
    polyphaseTable[phase][2] = clampCoeff((s32)SDL_lround(
        (rawPolyphase[POLYPHASE_PHASES * 3 - 1 - phase] / maxPolyphaseSum) * 32767.0));
    polyphaseTable[phase][3] = clampCoeff((s32)SDL_lround(
        (rawPolyphase[POLYPHASE_PHASES * 4 - 1 - phase] / maxPolyphaseSum) * 32767.0));
  }

  for (u32 phase = 0; phase < SINC_PHASES; ++phase) {
    const double frac = (double)phase / (double)SINC_PHASES;
    double taps[SINC_TAPS];
    double sum = 0.0;

    for (u32 tap = 0; tap < SINC_TAPS; ++tap) {
      const double x = (double)((s32)tap - (SINC_HALF_TAPS - 1)) - frac;
      double value = 0.0;

      if (SDL_fabs(x) < (double)SINC_HALF_TAPS)
        value = sincUnit(x) * sincUnit(x / (double)SINC_HALF_TAPS);

      taps[tap] = value;
      sum += value;
    }

    if (SDL_fabs(sum) < 1e-12) {
      memset(sincTable[phase], 0, sizeof(sincTable[phase]));
      sincTable[phase][SINC_HALF_TAPS - 1] = 0x7FFF;
      continue;
    }

    const double scale = 32767.0 / sum;
    s32 accum = 0;
    for (u32 tap = 0; tap < SINC_TAPS - 1; ++tap) {
      const s16 q = clampCoeff((s32)SDL_lround(taps[tap] * scale));
      sincTable[phase][tap] = q;
      accum += q;
    }

    sincTable[phase][SINC_TAPS - 1] = clampCoeff(32767 - accum);
  }

  resampleTablesInitialized = 1;
}

static inline u16 applyDeltaToLastVol(u16 lastVol, s16 deltaVol) {
  return (u16)((s32)(s16)lastVol + (s32)deltaVol * SAL_SAMPLES_PER_FRAME);
}

static inline void mixRampChannel(s32* dest, const s32* src, int nSamples, s16 startVol,
                                  s16 deltaVol, s16 envStart, s16 envDelta) {
  if (dest == NULL || (startVol == 0 && deltaVol == 0))
    return;

  for (int i = 0; i < nSamples; ++i) {
    s32 env = CLAMP((s32)envStart + (s32)envDelta * i, 0, 0x7FFF);
    s32 vol = startVol + deltaVol * i;
    s32 mixVol = musyxPcScaleQ15(vol, env);
    dest[i] = musyxPcClamp32((int64_t)dest[i] + musyxPcScaleQ15(src[i], mixVol));
  }
}

static inline void addThreeChannelBuffer(s32* dst, const s32* src, u16 vol) {
  if (dst == NULL || src == NULL || vol == 0)
    return;

  for (int i = 0; i < SAL_SAMPLES_PER_FRAME * 3; ++i)
    dst[i] = musyxPcClamp32((int64_t)dst[i] + musyxPcScaleQ15(src[i], vol));
}

static void downmixStudioToStereo(const s32* left, const s32* right, const s32* surround) {
  for (int i = 0; i < SAL_SAMPLES_PER_FRAME; ++i) {
    mixBufferL[i] = musyxPcClamp32((int64_t)mixBufferL[i] + left[i]);
    mixBufferR[i] = musyxPcClamp32((int64_t)mixBufferR[i] + right[i]);
    if (surround != NULL) {
      s32 surroundMix = musyxPcScaleQ15(surround[i], SURROUND_DOWNMIX_GAIN);
      mixBufferL[i] = musyxPcClamp32((int64_t)mixBufferL[i] + surroundMix);
      mixBufferR[i] = musyxPcClamp32((int64_t)mixBufferR[i] + surroundMix);
    }
  }
}

static void foldStereoToOutput(const s32* left, const s32* right) {
  for (int i = 0; i < SAL_SAMPLES_PER_FRAME; ++i) {
    mixBufferL[i] = musyxPcClamp32((int64_t)mixBufferL[i] + left[i]);
    mixBufferR[i] = musyxPcClamp32((int64_t)mixBufferR[i] + right[i]);
  }
}

static const s16 zeroCoefTab[8][2] = {{0}};

static inline int isVoiceADPCM(u8 compType) {
  return compType == 0 || compType == 1 || compType == 4 || compType == 5;
}

static const s16 (*getVoiceCoefTab(SAMPLE_INFO* smp))[2] {
  if (!isVoiceADPCM(smp->compType))
    return NULL;

  DSPADPCMplusInfo* adpcmInfo = smp->extraData;
  return adpcmInfo ? adpcmInfo->coefTab : zeroCoefTab;
}

static void resetVoiceLoopState(SAMPLE_INFO* smp, u32 voiceIdx) {
  if (!isVoiceADPCM(smp->compType))
    return;

  if (musyxPcStreamedAdpcm(smp->compType)) {
    // A stream wraps a circular buffer of consecutive audio, not the song.
    // Match DSP loopType=1: retain predictor history and read the fresh header.
    adpcmRestart[voiceIdx] = 0;
    return;
  }

  DSPADPCMplusInfo* adpcmInfo = smp->extraData;
  if (adpcmInfo != NULL) {
    adpcmYn1[voiceIdx] = adpcmInfo->loopY1;
    adpcmYn2[voiceIdx] = adpcmInfo->loopY0;
    adpcmPS[voiceIdx] = adpcmInfo->loopPS;
  } else {
    adpcmYn1[voiceIdx] = 0;
    adpcmYn2[voiceIdx] = 0;
    adpcmPS[voiceIdx] = ((const u8*)smp->addr)[(smp->loop / 14) * 8];
  }
  adpcmRestart[voiceIdx] = 1;
}

static void updateCurrentAddr(DSPvoice* vp, u32 srcPosHi) {
  SAMPLE_INFO* smp = &vp->smp_info;

  switch (smp->compType) {
  case 0:
  case 1:
  case 4:
  case 5:
    vp->currentAddr = (u32)((uintptr_t)smp->addr * 2 + (srcPosHi / 14) * 16 + 2 + (srcPosHi % 14));
    break;
  case 2:
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
  case 6:
#endif
    vp->currentAddr = (u32)((uintptr_t)smp->addr / 2 + srcPosHi);
    break;
  case 3:
    vp->currentAddr = (u32)((uintptr_t)smp->addr + srcPosHi);
    break;
  default:
    break;
  }
}

static s32 sampleAtPos(SAMPLE_INFO* smp, u32 voiceIdx, u32 posHi, const s16 coefTab[8][2]) {
  switch (smp->compType) {
  case 0:
  case 1:
  case 4:
  case 5: {
    const u8* block = (const u8*)smp->addr + (posHi / 14) * 8;
    const unsigned sample = posHi % 14;
    if (!adpcmRestart[voiceIdx] && sample == 0) adpcmPS[voiceIdx] = block[0];
    adpcmRestart[voiceIdx] = 0;
    // Decode only the samples actually consumed. Decoding a whole block here
    // corrupts history when a loop starts or ends partway through that block.
    return musyxPcAdpcmSample(block, sample, adpcmPS[voiceIdx], coefTab,
                              &adpcmYn1[voiceIdx], &adpcmYn2[voiceIdx]);
  }
  case 2:
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
  case 6:
#endif
  {
    /* PCM payloads remain in the GameCube's big-endian sample buffer. */
    const u8* sample = (const u8*)smp->addr + posHi * 2;
    return (s16)((u16)sample[0] << 8 | sample[1]);
  }
  case 3:
    return musyxPcPcm8(((u8*)smp->addr)[posHi]);
  default:
    return 0;
  }
}

/*
 * Decode sequential source-rate samples into a staging buffer.
 */
static int decodeSourceSamples(DSPvoice* vp, u32 voiceIdx, s32* out, int maxSamples, int* hitEnd) {
  SAMPLE_INFO* smp = &vp->smp_info;
  VoiceResamplerState* state = &voiceResampler[voiceIdx];
  const s16(*coefTab)[2] = getVoiceCoefTab(smp);
  int count = 0;

  *hitEnd = 0;
  const u32 end = musyxPcLoopEnd(smp->length, smp->loop, smp->loopLength);

  for (; count < maxSamples; ++count) {
    if (state->srcPosHi >= end) {
      if (smp->loopLength > 0 && smp->loop < end && end - smp->loop <= smp->loopLength &&
          smp->loopLength <= end - smp->loop + 1) {
        state->srcPosHi = smp->loop + ((state->srcPosHi - end) % (end - smp->loop));
        resetVoiceLoopState(smp, voiceIdx);
      } else {
        *hitEnd = 1;
        state->ended = 1;
        break;
      }
    }

    out[count] = sampleAtPos(smp, voiceIdx, state->srcPosHi, coefTab);
    ++state->srcPosHi;
  }

  for (int i = count; i < maxSamples; ++i)
    out[i] = 0;

  return count;
}

static int fillSourceBuffer(DSPvoice* vp, u32 voiceIdx, int outputSamples, u32 pitch) {
  VoiceResamplerState* state = &voiceResampler[voiceIdx];
  const u32 history = state->srcConsumed < SINC_HALF_TAPS ? state->srcConsumed : SINC_HALF_TAPS;
  const u32 available = state->srcCount - state->srcConsumed;
  u32 needed = (u32)(((u64)outputSamples * pitch + 0xFFFFu) >> 16);

  needed += SINC_HALF_TAPS + 1;
  if (available >= needed || state->ended)
    return state->ended;

  if (available + history > 0) {
    memmove(state->srcBuf, state->srcBuf + state->srcConsumed - history,
            (available + history) * sizeof(state->srcBuf[0]));
  }

  state->srcCount = available + history;
  state->srcConsumed = history;

  u32 targetCount = history + needed;
  if (targetCount > SRC_STAGING_SIZE)
    targetCount = SRC_STAGING_SIZE;

  if (targetCount > state->srcCount) {
    int hitEnd = 0;
    state->srcCount += (u32)decodeSourceSamples(vp, voiceIdx, state->srcBuf + state->srcCount,
                                                (int)(targetCount - state->srcCount), &hitEnd);
  }

  updateCurrentAddr(vp, state->srcPosHi);
  return state->ended;
}

static inline s32 readSourceBufferSample(const VoiceResamplerState* state, s32 idx) {
  if (idx < 0 || (u32)idx >= state->srcCount)
    return 0;

  return state->srcBuf[idx];
}

static int resampleVoice(u32 voiceIdx, s32* out, int numSamples, u32 pitch) {
  VoiceResamplerState* state = &voiceResampler[voiceIdx];

#if SAL_RESAMPLE_MODE == 0
  s16 temp[POLYPHASE_TAPS];
  u32 idx = 0;

  temp[idx++ & 3] = state->lastSamples[0];
  temp[idx++ & 3] = state->lastSamples[1];
  temp[idx++ & 3] = state->lastSamples[2];
  temp[idx++ & 3] = state->lastSamples[3];

  for (int i = 0; i < numSamples; ++i) {
    state->curPos += pitch;
    while (state->curPos >= 0x10000) {
      s32 sample = 0;
      if (state->srcConsumed < state->srcCount)
        sample = state->srcBuf[state->srcConsumed++];

      temp[idx++ & 3] = clamp16(sample);
      state->curPos -= 0x10000;
    }

    const u32 phase = (state->curPos & 0xFFFF) >> 9;
    const s16* c = polyphaseTable[phase];
    const long long t0 = temp[idx++ & 3];
    const long long t1 = temp[idx++ & 3];
    const long long t2 = temp[idx++ & 3];
    const long long t3 = temp[idx++ & 3];
    const long long sample = (t0 * c[0] + t1 * c[1] + t2 * c[2] + t3 * c[3]) >> 15;

    out[i] = clamp16((s32)sample);
  }

  state->lastSamples[3] = temp[--idx & 3];
  state->lastSamples[2] = temp[--idx & 3];
  state->lastSamples[1] = temp[--idx & 3];
  state->lastSamples[0] = temp[--idx & 3];
#else
  for (int i = 0; i < numSamples; ++i) {
    const u32 phase = (state->curPos & 0xFFFF) >> 7;
    const s16* c = sincTable[phase];
    long long sample = 0;

    for (u32 tap = 0; tap < SINC_TAPS; ++tap) {
      const s32 srcIdx = (s32)state->srcConsumed + (s32)tap - (SINC_HALF_TAPS - 1);
      sample += (long long)readSourceBufferSample(state, srcIdx) * c[tap];
    }

    out[i] = clamp16((s32)(sample >> 15));

    const u32 step = state->curPos + pitch;
    state->srcConsumed += step >> 16;
    state->curPos = step & 0xFFFF;
  }
#endif

  return numSamples;
}

/*
 * Software voice rendering for one frame (SAL_SAMPLES_PER_FRAME samples).
 * Decodes samples, then mixes into main and AUX buffers with per-channel volumes.
 */
static s32 voiceDecodeBuf[SAL_SAMPLES_PER_FRAME];

// Returns 1 if voice finished playing (end of non-looping sample), 0 otherwise.
static int renderVoiceSegment(DSPvoice* vp, s32* mainL, s32* mainR, s32* mainS, s32* auxAL,
                              s32* auxAR, s32* auxAS, s32* auxBL, s32* auxBR, s32* auxBS,
                              u32 voiceIdx, int frameOffset, int frameSamples, u16 adsrStart,
                              s16 adsrDelta, s16 dVolL, s16 dVolR, s16 dVolS, s16 dVolLa,
                              s16 dVolRa, s16 dVolSa, s16 dVolLb, s16 dVolRb, s16 dVolSb) {
  if (vp->state == 0)
    return 0;

  const SAMPLE_INFO* smp = &vp->smp_info;
  if (smp->addr == NULL)
    return 0;

  u32 pitch = vp->playInfo.pitch;
  if (pitch == 0)
    pitch = vp->pitch[0];
  if (pitch == 0)
    return 0;

  /* Decode samples into temp buffer */
  VoiceResamplerState* state = &voiceResampler[voiceIdx];
  fillSourceBuffer(vp, voiceIdx, frameSamples, pitch);
  int nSamples = resampleVoice(voiceIdx, voiceDecodeBuf, frameSamples, pitch);
  voiceEffPitch[voiceIdx] = pitch;
  {
    long long sum = 0;
    for (int i = 0; i < nSamples; ++i)
      sum += (long long)voiceDecodeBuf[i] * voiceDecodeBuf[i];
    voiceRms[voiceIdx] = nSamples > 0 ? (s32)sqrt((double)sum / nSamples) : 0;
  }
  if (MusyxPortIsSampleMuted(vp->smp_id)) {
    memset(voiceDecodeBuf, 0, (size_t)nSamples * sizeof(s32));
  }
  vp->playInfo.posHi = state->srcPosHi;
  vp->playInfo.posLo = state->curPos;
  int voiceDone = state->ended && state->srcConsumed >= state->srcCount;

#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 1)
  if (vp->filter.on) {
    s32 yn = filterState[voiceIdx];
    for (int i = 0; i < nSamples; ++i) {
      yn = ((s32)vp->filter.coefA * voiceDecodeBuf[i] + (s32)vp->filter.coefB * yn) >> 15;
      voiceDecodeBuf[i] = yn;
    }
    filterState[voiceIdx] = yn;
  }
#endif

  /* The port's volume boost above the game's own maximum: scale this voice, the music path (a
   * stream) or the effects, and saturate to the decode buffer's 16-bit range. No boost means no
   * multiply at all, so the default output stays bit-identical. */
  {
    const float boost =
        musyxPcStreamedAdpcm(smp->compType) ? salVolumeBoostMusic : salVolumeBoostSfx;
    if (boost != 1.0f) {
      for (int i = 0; i < nSamples; ++i) {
        const float scaled = (float)voiceDecodeBuf[i] * boost;
        voiceDecodeBuf[i] =
            scaled >= 32767.0f ? 32767 : scaled <= -32768.0f ? -32768 : (s32)scaled;
      }
    }
  }

  mixRampChannel(mainL ? mainL + frameOffset : NULL, voiceDecodeBuf, nSamples,
                 (s16)vp->lastVolL + dVolL * frameOffset, dVolL, (s16)adsrStart, adsrDelta);
  mixRampChannel(mainR ? mainR + frameOffset : NULL, voiceDecodeBuf, nSamples,
                 (s16)vp->lastVolR + dVolR * frameOffset, dVolR, (s16)adsrStart, adsrDelta);
  mixRampChannel(mainS ? mainS + frameOffset : NULL, voiceDecodeBuf, nSamples,
                 (s16)vp->lastVolS + dVolS * frameOffset, dVolS, (s16)adsrStart, adsrDelta);
  mixRampChannel(auxAL ? auxAL + frameOffset : NULL, voiceDecodeBuf, nSamples,
                 (s16)vp->lastVolLa + dVolLa * frameOffset, dVolLa, (s16)adsrStart, adsrDelta);
  mixRampChannel(auxAR ? auxAR + frameOffset : NULL, voiceDecodeBuf, nSamples,
                 (s16)vp->lastVolRa + dVolRa * frameOffset, dVolRa, (s16)adsrStart, adsrDelta);
  mixRampChannel(auxAS ? auxAS + frameOffset : NULL, voiceDecodeBuf, nSamples,
                 (s16)vp->lastVolSa + dVolSa * frameOffset, dVolSa, (s16)adsrStart, adsrDelta);
  mixRampChannel(auxBL ? auxBL + frameOffset : NULL, voiceDecodeBuf, nSamples,
                 (s16)vp->lastVolLb + dVolLb * frameOffset, dVolLb, (s16)adsrStart, adsrDelta);
  mixRampChannel(auxBR ? auxBR + frameOffset : NULL, voiceDecodeBuf, nSamples,
                 (s16)vp->lastVolRb + dVolRb * frameOffset, dVolRb, (s16)adsrStart, adsrDelta);
  mixRampChannel(auxBS ? auxBS + frameOffset : NULL, voiceDecodeBuf, nSamples,
                 (s16)vp->lastVolSb + dVolSb * frameOffset, dVolSb, (s16)adsrStart, adsrDelta);

  return voiceDone;
}

void salCtrlDsp(s16* dest) {
  u8 st;
  DSPstudioinfo* stp;

  memset(mixBufferL, 0, sizeof(mixBufferL));
  memset(mixBufferR, 0, sizeof(mixBufferR));

  for (st = 0, stp = dspStudio; st < salMaxStudioNum; ++st, ++stp) {
    if (stp->state != 1)
      continue;

    /* Clear the current frame buffers only. Previous-frame data feeds studio inputs. */
    if (stp->main[salFrame])
      memset(stp->main[salFrame], 0, SAL_SAMPLES_PER_FRAME * 3 * sizeof(s32));

    /* Clear AUX buffers for the current aux frame */
    if (stp->auxA[salAuxFrame])
      memset(stp->auxA[salAuxFrame], 0, SAL_SAMPLES_PER_FRAME * 3 * sizeof(s32));
    if (stp->auxB[salAuxFrame])
      memset(stp->auxB[salAuxFrame], 0, SAL_SAMPLES_PER_FRAME * 3 * sizeof(s32));

    /* Render all voices in this studio */
    DSPvoice* vp = stp->voiceRoot;
    while (vp != NULL) {
      DSPvoice* nextVp = vp->next; /* save in case voice is deactivated */
      if (vp->state != 0) {
        u32 voiceIdx = (u32)(vp - dspVoice);
        u8 mixStart = 0;
        int newVoice = 0;

        /* New voice initialization (mirrors salBuildCommandList state==1 path) */
        if (vp->state == 1) {
          if (adsrSetup(&vp->adsr) != 0) {
            salSynthSendMessage(vp, 0);
            salDeactivateVoice(vp);
            vp = nextVp;
            continue;
          }

          vp->lastVolL = vp->volL;
          vp->lastVolR = vp->volR;
          vp->lastVolS = vp->volS;
          vp->lastVolLa = vp->volLa;
          vp->lastVolRa = vp->volRa;
          vp->lastVolSa = vp->volSa;
          vp->lastVolLb = vp->volLb;
          vp->lastVolRb = vp->volRb;
          vp->lastVolSb = vp->volSb;

          /* Initialize playback position based on compression type */
          switch (vp->smp_info.compType) {
          case 0:
          case 4:
          case 5:
            vp->playInfo.posHi = 0;
            vp->playInfo.posLo = 0;
            break;
          case 1: {
            u32 offset = (vp->smp_info.offset + 0xD) / 14;
            vp->playInfo.posHi = offset * 0xE;
            vp->playInfo.posLo = 0;
          } break;
          case 2:
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
          case 6:
#endif
          case 3:
            vp->playInfo.posHi = vp->smp_info.offset;
            vp->playInfo.posLo = 0;
            break;
          }

          /* Reset ADPCM decode state for this voice */
          adpcmYn1[voiceIdx] = 0;
          adpcmYn2[voiceIdx] = 0;
          adpcmRestart[voiceIdx] = 1;
          adpcmPS[voiceIdx] = isVoiceADPCM(vp->smp_info.compType) && vp->smp_info.extraData != NULL
                                 ? ((DSPADPCMplusInfo*)vp->smp_info.extraData)->initialPS : 0;
          if (isVoiceADPCM(vp->smp_info.compType) && vp->smp_info.extraData == NULL && vp->smp_info.addr != NULL)
            adpcmPS[voiceIdx] = ((const u8*)vp->smp_info.addr)[(vp->playInfo.posHi / 14) * 8];
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 1)
          filterState[voiceIdx] = 0;
#endif

          mixStart = vp->singleOffset;
          /* A stolen slot keeps the old voice's deferred break bit (hwInitSamplePlayback/
           * salActivateVoice); retail's new-voice path (hw_dspctrl.c block_186) ignores
           * changed[mix_start] & 0x20 and only s > mix_start honours it. */
          newVoice = vp->startupBreak == 0;

          if (vp->smp_info.compType == 1) {
            u32 offset = (vp->smp_info.offset + 0xD) / 14;
            DSPADPCMplusInfo* adpcmInfo = (DSPADPCMplusInfo*)vp->smp_info.extraData;
            if (adpcmInfo != NULL) {
              adpcmYn2[voiceIdx] = adpcmInfo->blk[offset].Y0;
              adpcmYn1[voiceIdx] = adpcmInfo->blk[offset].Y1;
              adpcmPS[voiceIdx] = adpcmInfo->blk[offset].PS;
            }
          }

          voiceResampler[voiceIdx].srcCount = 0;
          voiceResampler[voiceIdx].srcConsumed = 0;
          voiceResampler[voiceIdx].curPos = 0;
          voiceResampler[voiceIdx].srcPosHi = vp->playInfo.posHi;
          memset(voiceResampler[voiceIdx].lastSamples, 0,
                 sizeof(voiceResampler[voiceIdx].lastSamples));
          voiceResampler[voiceIdx].ended = 0;
          updateCurrentAddr(vp, voiceResampler[voiceIdx].srcPosHi);

          vp->state = 2;
        }

        /* Get studio main buffer pointers */
        s32* studioMain = stp->main[salFrame];
        s32* studioL = studioMain;
        s32* studioR = studioMain ? studioMain + SAL_SAMPLES_PER_FRAME : NULL;
        s32* studioS = studioMain ? studioMain + SAL_SAMPLES_PER_FRAME * 2 : NULL;

        /* Get AUX buffer pointers */
        s32* auxACur = stp->auxA[salAuxFrame];
        s32* auxAL = auxACur;
        s32* auxAR = auxACur ? auxACur + SAL_SAMPLES_PER_FRAME : NULL;
        s32* auxAS = auxACur ? auxACur + SAL_SAMPLES_PER_FRAME * 2 : NULL;
        s32* auxBCur = stp->auxB[salAuxFrame];
        s32* auxBL = auxBCur;
        s32* auxBR = auxBCur ? auxBCur + SAL_SAMPLES_PER_FRAME : NULL;
        s32* auxBS = auxBCur ? auxBCur + SAL_SAMPLES_PER_FRAME * 2 : NULL;

        if (studioMain != NULL) {
          s16 dVolL = ((s16)vp->volL - (s16)vp->lastVolL) / SAL_SAMPLES_PER_FRAME;
          s16 dVolR = ((s16)vp->volR - (s16)vp->lastVolR) / SAL_SAMPLES_PER_FRAME;
          s16 dVolS = ((s16)vp->volS - (s16)vp->lastVolS) / SAL_SAMPLES_PER_FRAME;
          s16 dVolLa = ((s16)vp->volLa - (s16)vp->lastVolLa) / SAL_SAMPLES_PER_FRAME;
          s16 dVolRa = ((s16)vp->volRa - (s16)vp->lastVolRa) / SAL_SAMPLES_PER_FRAME;
          s16 dVolSa = ((s16)vp->volSa - (s16)vp->lastVolSa) / SAL_SAMPLES_PER_FRAME;
          s16 dVolLb = ((s16)vp->volLb - (s16)vp->lastVolLb) / SAL_SAMPLES_PER_FRAME;
          s16 dVolRb = ((s16)vp->volRb - (s16)vp->lastVolRb) / SAL_SAMPLES_PER_FRAME;
          s16 dVolSb = ((s16)vp->volSb - (s16)vp->lastVolSb) / SAL_SAMPLES_PER_FRAME;
          int finished = 0;

          for (u8 subframe = mixStart; subframe < SAL_SUBFRAMES; ++subframe) {
            if ((vp->changed[subframe] & 0x20) != 0 && !(newVoice && subframe == mixStart)) {
              adsrStartRelease(&vp->adsr, 10);
              vp->postBreak = 1;
            } else if (vp->postBreak == 0) {
              if ((vp->changed[subframe] & 0x40) != 0)
                adsrRelease(&vp->adsr);
              if ((vp->changed[subframe] & 8) != 0)
                vp->playInfo.pitch = vp->pitch[subframe];
            }

            if ((vp->changed[subframe] & 0x10) != 0) {
              if (adsrSetup(&vp->adsr) != 0) {
                salSynthSendMessage(vp, 0);
                salDeactivateVoice(vp);
                finished = 1;
                break;
              }
            }

            u16 adsrStart = 0;
            u16 adsrDelta = 0;
            u32 adsrDone = adsrHandle(&vp->adsr, &adsrStart, &adsrDelta);
            int sampleOffset = subframe * SAL_SAMPLES_PER_SUBFRAME;

            if (renderVoiceSegment(vp, studioL, studioR, studioS, auxAL, auxAR, auxAS, auxBL, auxBR,
                                   auxBS, voiceIdx, sampleOffset, SAL_SAMPLES_PER_SUBFRAME,
                                   adsrStart, (s16)adsrDelta, dVolL, dVolR, dVolS, dVolLa, dVolRa,
                                   dVolSa, dVolLb, dVolRb, dVolSb)) {
              salSynthSendMessage(vp, 0);
              salDeactivateVoice(vp);
              finished = 1;
              break;
            }

            if (adsrDone) {
              salSynthSendMessage(vp, 0);
              salDeactivateVoice(vp);
              finished = 1;
              break;
            }
          }

          vp->lastVolL = applyDeltaToLastVol(vp->lastVolL, dVolL);
          vp->lastVolR = applyDeltaToLastVol(vp->lastVolR, dVolR);
          vp->lastVolS = applyDeltaToLastVol(vp->lastVolS, dVolS);
          vp->lastVolLa = applyDeltaToLastVol(vp->lastVolLa, dVolLa);
          vp->lastVolRa = applyDeltaToLastVol(vp->lastVolRa, dVolRa);
          vp->lastVolSa = applyDeltaToLastVol(vp->lastVolSa, dVolSa);
          vp->lastVolLb = applyDeltaToLastVol(vp->lastVolLb, dVolLb);
          vp->lastVolRb = applyDeltaToLastVol(vp->lastVolRb, dVolRb);
          vp->lastVolSb = applyDeltaToLastVol(vp->lastVolSb, dVolSb);

          if (finished) {
            vp = nextVp;
            continue;
          }
        }
      }
      vp = nextVp;
    }

    if (stp->main[salFrame] != NULL) {
      s32* studioMain = stp->main[salFrame];
      s32* auxACur = stp->auxA[salAuxFrame];
      s32* auxBCur = stp->auxB[salAuxFrame];
      for (u8 inputIdx = 0; inputIdx < stp->numInputs; ++inputIdx) {
        DSPinput* input = &stp->in[inputIdx];
        DSPstudioinfo* srcStudio = &dspStudio[input->studio];
        s32* srcMain = srcStudio->main[salFrame ^ 1];
        if (srcMain == NULL)
          continue;

        addThreeChannelBuffer(studioMain, srcMain, input->vol);
        addThreeChannelBuffer(auxACur, srcMain, input->volA);
        addThreeChannelBuffer(auxBCur, srcMain, input->volB);
      }
    }

    {
      s32* auxAWork = stp->auxA[(salAuxFrame + 2) % 3];
      if (auxAWork != NULL && stp->auxAHandler != NULL) {
        SND_AUX_INFO info;
        info.data.bufferUpdate.left = auxAWork;
        info.data.bufferUpdate.right = auxAWork + SAL_SAMPLES_PER_FRAME;
        info.data.bufferUpdate.surround = auxAWork + SAL_SAMPLES_PER_FRAME * 2;
        stp->auxAHandler(SND_AUX_REASON_BUFFERUPDATE, &info, stp->auxAUser);
      }

      if (stp->type == SND_STUDIO_TYPE_STD) {
        s32* auxBWork = stp->auxB[(salAuxFrame + 2) % 3];
        if (auxBWork != NULL && stp->auxBHandler != NULL) {
          SND_AUX_INFO info;
          info.data.bufferUpdate.left = auxBWork;
          info.data.bufferUpdate.right = auxBWork + SAL_SAMPLES_PER_FRAME;
          info.data.bufferUpdate.surround = auxBWork + SAL_SAMPLES_PER_FRAME * 2;
          stp->auxBHandler(SND_AUX_REASON_BUFFERUPDATE, &info, stp->auxBUser);
        }
      }
    }

    /* Accumulate master studio into final mix */
    if (stp->isMaster) {
      s32* studioMain = stp->main[salFrame];
      if (studioMain != NULL) {
        s32* studioL = studioMain;
        s32* studioR = studioMain + SAL_SAMPLES_PER_FRAME;
        s32* studioS = studioMain + SAL_SAMPLES_PER_FRAME * 2;
        downmixStudioToStereo(studioL, studioR, stp->type == SND_STUDIO_TYPE_DPL2 ? NULL : studioS);
      }

      /* Add processed AUX A (reverb output) to mix */
      s32* auxProcessed = stp->auxA[(salAuxFrame + 2) % 3];
      if (auxProcessed && stp->auxAHandler) {
        s32* auxL = auxProcessed;
        s32* auxR = auxProcessed + SAL_SAMPLES_PER_FRAME;
        s32* auxS = auxProcessed + SAL_SAMPLES_PER_FRAME * 2;
        downmixStudioToStereo(auxL, auxR, auxS);
      }

      if (stp->type == SND_STUDIO_TYPE_DPL2) {
        s32* dpl2Rear = stp->auxB[salAuxFrame];
        if (dpl2Rear != NULL)
          foldStereoToOutput(dpl2Rear, dpl2Rear + SAL_SAMPLES_PER_FRAME);
      } else {
        /* Add processed AUX B to mix for standard studios only. */
        s32* auxBProcessed = stp->auxB[(salAuxFrame + 2) % 3];
        if (auxBProcessed && stp->auxBHandler) {
          s32* auxBL2 = auxBProcessed;
          s32* auxBR2 = auxBProcessed + SAL_SAMPLES_PER_FRAME;
          s32* auxBS2 = auxBProcessed + SAL_SAMPLES_PER_FRAME * 2;
          downmixStudioToStereo(auxBL2, auxBR2, auxBS2);
        }
      }
    }
  }

  {
    static int sVoiceTrace = -1;
    if (sVoiceTrace < 0)
      sVoiceTrace = getenv("MP_LOG_VOICES") != NULL;
    if (sVoiceTrace) {
      static u32 sVoiceTraceCount = 0;
      if (++sVoiceTraceCount % 60 == 0) {
        int active = 0;
        for (int v = 0; v < salNumVoices; ++v) {
          DSPvoice* vp = &dspVoice[v];
          if (vp->state == 0)
            continue;
          ++active;
          fprintf(stderr,
                  "[voice] v=%d st=%u smp=%u info=%08X addr=%p len=%u loop=%u loopLen=%u comp=%u "
                  "pitch=%u eff=%u rms=%d volL=%u volR=%u\n",
                  v, vp->state, vp->smp_id, vp->smp_info.info, vp->smp_info.addr,
                  vp->smp_info.length, vp->smp_info.loop, vp->smp_info.loopLength,
                  vp->smp_info.compType, vp->playInfo.pitch, voiceEffPitch[v], voiceRms[v],
                  vp->volL, vp->volR);
        }
        fprintf(stderr, "[voice] active=%d heading=(%.3f,%.3f,%.3f)\n", active,
                gPortListenerHeading[0], gPortListenerHeading[1], gPortListenerHeading[2]);
      }
    }
  }

  /* Write interleaved stereo s16 to dest */
  if (dest) {
    for (int i = 0; i < SAL_SAMPLES_PER_FRAME; i++) {
      dest[i * 2 + 0] = clamp16(mixBufferL[i]);
      dest[i * 2 + 1] = clamp16(mixBufferR[i]);
    }
  }
}

// Render one 5 ms frame and, with a device, queue it.
static void salRenderFrame(SDL_AudioStream* stream, int queuedBytes) {
  if (userCallback) {
    userCallback();
  }

  s16* buf = salOutputBuffers[salOutputIndex];
  if (salTraceAudio) {
    ++salStatsFrames;
    for (int i = 0; i < SAL_STEREO_SAMPLES; ++i) {
      const int magnitude = abs((int)buf[i]);
      if (magnitude > salStatsPeak) salStatsPeak = magnitude;
      if (magnitude >= 32767) ++salStatsClipped;
    }
    const u64 now = SDL_GetTicksNS();
    if (now - salStatsStart >= 1000000000ull) {
      const double seconds = (double)(now - salStatsStart) / 1000000000.0;
      fprintf(stderr, "[musyx] rate=%.0f samples/s queued=%.1f ms peak=%d clipped=%.3f%%\n",
              salStatsFrames * SAL_SAMPLES_PER_FRAME / seconds,
              queuedBytes > 0 ? queuedBytes * 1000.0 / (32000 * 4) : 0.0,
              salStatsPeak, 100.0 * salStatsClipped / (salStatsFrames * SAL_STEREO_SAMPLES));
      salStatsStart = now;
      salStatsFrames = salStatsClipped = 0;
      salStatsPeak = 0;
    }
  }
  {
    // MP_AUDIO_DUMP=<path>: append the mixed 32 kHz s16 stereo output, to check offline what
    // was actually played.
    static FILE* sDump = NULL;
    static int sDumpOpened = 0;
    if (!sDumpOpened) {
      sDumpOpened = 1;
      const char* path = getenv("MP_AUDIO_DUMP");
      sDump = path != NULL ? fopen(path, "wb") : NULL;
    }
    if (sDump != NULL) {
      fwrite(buf, 1, SAL_BUFFER_BYTES, sDump);
    }
  }
  if (stream != NULL) {
    if (SDL_GetAtomicInt(&salMuted)) {
      memset(buf, 0, SAL_BUFFER_BYTES);
    }
    SDL_PutAudioStreamData(stream, buf, SAL_BUFFER_BYTES);
  }
}

// The device pulls: render only what it asks for, so a new sound waits one device period rather
// than a producer-side queue (that queue held up to 60 ms and made rapid fire audibly late, #10).
static void SDLCALL salStreamCallback(void* userdata, SDL_AudioStream* stream, int additional,
                                      int total) {
  (void)userdata;
  (void)total;
  for (int left = additional; left > 0; left -= SAL_BUFFER_BYTES) {
    salRenderFrame(stream, salTraceAudio ? SDL_GetAudioStreamQueued(stream) : 0);
  }
}

// Without a device, keep the synth running in real time.
static int salAudioThreadFunc(void* data) {
  (void)data;
  while (SDL_GetAtomicInt(&salAudioThreadRunning)) {
    salRenderFrame(NULL, 0);
    SDL_Delay(5); // one 160-sample frame at 32 kHz
  }
  return 0;
}

bool salInitAi(SND_SOME_CALLBACK callback, u32 flags, u32* outFreq) {
  (void)flags;
  memset(salOutputBuffers, 0, sizeof(salOutputBuffers));
  salOutputIndex = 0;
  userCallback = callback;
  salTraceAudio = getenv("MP_AUDIO_STATS") != NULL;
  salStatsStart = SDL_GetTicksNS();
  salStatsFrames = salStatsClipped = 0;
  salStatsPeak = 0;

  memset(adpcmYn1, 0, sizeof(adpcmYn1));
  memset(adpcmYn2, 0, sizeof(adpcmYn2));
  memset(adpcmPS, 0, sizeof(adpcmPS));
  memset(adpcmRestart, 0, sizeof(adpcmRestart));
  memset(voiceResampler, 0, sizeof(voiceResampler));
  initResampleTables();

  salAudioSubsystemInitialized = SDL_InitSubSystem(SDL_INIT_AUDIO);

  SDL_AudioSpec spec = {
      .format = SDL_AUDIO_S16,
      .channels = 2,
      .freq = *outFreq,
  };
  if (salAudioSubsystemInitialized) {
    salAudioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                               salStreamCallback, NULL);
  }
  if (salAudioStream == NULL) {
    fprintf(stderr, "MusyX: audio device unavailable, running silently: %s\n", SDL_GetError());
  }

  synthInfo.numSamples = 0x20;
  *outFreq = spec.freq;
  return true;
}

bool salStartAi() {
  if (salAudioStream != NULL)
    return SDL_ResumeAudioStreamDevice(salAudioStream);

  SDL_SetAtomicInt(&salAudioThreadRunning, 1);
  salAudioThread = SDL_CreateThread(salAudioThreadFunc, "MusyX Audio", NULL);
  if (!salAudioThread) {
    SDL_SetAtomicInt(&salAudioThreadRunning, 0);
    return false;
  }
  return true;
}

bool salExitAi() {
  if (salAudioStream) {
    SDL_DestroyAudioStream(salAudioStream);
    salAudioStream = NULL;
  }
  if (salAudioThread) {
    SDL_SetAtomicInt(&salAudioThreadRunning, 0);
    SDL_WaitThread(salAudioThread, NULL);
    salAudioThread = NULL;
  }
  if (salAudioSubsystemInitialized) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    salAudioSubsystemInitialized = false;
  }
  return true;
}

void* salAiGetDest() {
  salOutputIndex ^= 1;
  return salOutputBuffers[salOutputIndex];
}

bool salInitDsp(u32 flags) {
  (void)flags;
  return true;
}

bool salExitDsp() { return true; }

void salStartDsp(u16* cmdList) {
  (void)cmdList;
  /* no-op */
}

void hwInitIrq() {
  globalMutex = SDL_CreateMutex();
  /* Start with IRQs disabled (locked), matching hwIrqLevel=1 on Dolphin.
   * hwEnableIrq() in hwInit() will unlock. */
  SDL_LockMutex(globalMutex);
}

void hwExitIrq() {
  if (globalMutex != NULL) {
    SDL_DestroyMutex(globalMutex);
    globalMutex = NULL;
  }
}

void hwEnableIrq() { SDL_UnlockMutex(globalMutex); }

void hwDisableIrq() { SDL_LockMutex(globalMutex); }

void hwIRQEnterCritical() { SDL_LockMutex(globalMutex); }

void hwIRQLeaveCritical() { SDL_UnlockMutex(globalMutex); }

// Snapshot of the active voices for the debug overlay. Reads guest state from
// the UI thread; a torn read only affects a diagnostic display.
int MusyxPortCopyVoices(PortMusyxVoice* out, int maxVoices) {
  if (dspVoice == NULL || out == NULL || maxVoices <= 0) {
    return 0;
  }
  int count = 0;
  for (int v = 0; v < salNumVoices && count < maxVoices; ++v) {
    const DSPvoice* vp = &dspVoice[v];
    if (vp->state == 0) {
      continue;
    }
    out[count].smpId = vp->smp_id;
    out[count].compType = vp->smp_info.compType;
    out[count].looped = vp->smp_info.loopLength != 0;
    out[count].length = vp->smp_info.length;
    out[count].pitch = vp->playInfo.pitch != 0 ? vp->playInfo.pitch : vp->pitch[0];
    out[count].rms = voiceRms[v];
    out[count].volL = vp->volL;
    out[count].volR = vp->volR;
    out[count].srcPos = voiceResampler[v].srcPosHi;
    out[count].loopLength = vp->smp_info.loopLength;
    ++count;
  }
  return count;
}
