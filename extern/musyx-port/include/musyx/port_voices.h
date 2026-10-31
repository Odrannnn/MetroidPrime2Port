#ifndef MUSYX_PORT_VOICES_H
#define MUSYX_PORT_VOICES_H

#include <stdint.h>

// Port-only bridge so the debug overlay can list the live software voices and
// mute a sample by id. Implemented by hw_pc.c. This header must not pull in the
// MusyX headers: they leave `#pragma pack(1)` in effect, which breaks unrelated
// includes in the C++ overlay.
#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)
typedef struct PortMusyxVoice {
  uint16_t smpId;
  uint8_t compType;
  uint8_t looped;
  uint32_t length;
  uint32_t pitch;
  int32_t rms;
  uint16_t volL;
  uint16_t volR;
  uint32_t srcPos;
  uint32_t loopLength;
} PortMusyxVoice;
#pragma pack(pop)

// Copies the currently active voices (up to maxVoices) and returns the count.
int MusyxPortCopyVoices(PortMusyxVoice* out, int maxVoices);
int MusyxPortIsSampleMuted(unsigned smpId);
void MusyxPortSetSampleMuted(unsigned smpId, int muted);
void MusyxPortClearSampleMutes(void);
// The port's volume boost: a linear factor (1.0 = none) on the music path (a streamed voice) and
// on every other voice, applied to each voice's decoded samples in the mix.
void MusyxPortSetVolumeBoost(float music, float sfx);
// Copies the muted sample ids (up to maxIds) and returns the count.
int MusyxPortGetMutedSamples(unsigned* out, int maxIds);

#ifdef __cplusplus
}
#endif

#endif
