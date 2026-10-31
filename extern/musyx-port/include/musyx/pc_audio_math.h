#ifndef MUSYX_PC_AUDIO_MATH_H
#define MUSYX_PC_AUDIO_MATH_H

#include <stdint.h>
#include <limits.h>

static inline int32_t musyxPcClamp32(int64_t value) {
  return value > INT32_MAX ? INT32_MAX : value < INT32_MIN ? INT32_MIN : (int32_t)value;
}

// Studio/reverb samples have more headroom than s16; their Q15 products cannot
// be evaluated in 32 bits without wrapping into loud discontinuities.
static inline int32_t musyxPcScaleQ15(int32_t sample, int32_t gain) {
  return musyxPcClamp32(((int64_t)sample * gain) >> 15);
}

static inline int32_t musyxPcPcm8(uint8_t value) {
  return (int32_t)(int8_t)value * 256; // GC DSP PCM8 is signed, not unsigned PCM
}

static inline int16_t musyxPcAdpcmSample(const uint8_t* block, unsigned sample,
                                        uint8_t ps, const int16_t coefficients[8][2],
                                        int16_t* history1, int16_t* history2) {
  const unsigned predictor = (ps >> 4) & 7;
  const int32_t scale = 1 << (ps & 15);
  int32_t nibble = (sample & 1) ? block[1 + sample / 2] & 15 : block[1 + sample / 2] >> 4;
  if (nibble >= 8) nibble -= 16;
  const int64_t value = ((int64_t)nibble * scale * 2048 +
                         (int64_t)coefficients[predictor][0] * *history1 +
                         (int64_t)coefficients[predictor][1] * *history2 + 1024) >> 11;
  const int16_t decoded = value > 32767 ? 32767 : value < -32768 ? -32768 : (int16_t)value;
  *history2 = *history1;
  *history1 = decoded;
  return decoded;
}

static inline uint32_t musyxPcLoopEnd(uint32_t length, uint32_t loop, uint32_t loopLength) {
  // Some samples (smp 428, Dark Agon Temple) have loop + loopLength one past length: the DSP's inclusive
  // end address (hw_dspctrl.c, loop + loopLength - 1) is the last sample. Keep looping, over the samples we have.
  return loopLength != 0 && loop < length && loopLength <= length - loop + 1 ? (loop + loopLength < length ? loop + loopLength : length) : length;
}

static inline int musyxPcStreamedAdpcm(uint8_t compression) {
  return compression == 4 || compression == 5;
}
#endif
