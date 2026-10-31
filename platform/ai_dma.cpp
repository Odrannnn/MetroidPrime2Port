// PC implementation of the GameCube audio-interface (AI) DMA path.
//
// MusyX has its own SDL stream in the vendored runtime, but streamed audio
// (front-end/in-game music via CStaticAudioPlayer, movie audio) is produced by
// filling an AI DMA buffer from the guest. There is no AI hardware, so the
// registered DMA callback is driven from the game's main loop at the buffer rate
// and the submitted buffer is fed to an SDL audio stream. Driving it on the main
// thread matters: the guest mixer is not thread-safe.

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <limits>

#include <SDL3/SDL.h>

#include <dolphin/ai.h>

namespace {
constexpr uint32_t kSampleRate = 32000;
// 16-bit stereo frames: 4 bytes per frame.
constexpr uint32_t kBytesPerFrame = 4;
constexpr uint64_t kDefaultFrameNs = 5000000ull; // 0x280 bytes at 32 kHz
constexpr int kTargetQueuedBytes = kSampleRate * kBytesPerFrame * 64 / 1000;

AIDCallback sCallback = nullptr;
uintptr_t sBuffer = 0;
uint32_t sLength = 0;
SDL_AudioStream* sStream = nullptr;
uint64_t sNextFrameNs = 0;
bool sStarted = false;
bool sOutputEnabled = true;
// The port's sound effects boost (`sfx_boost`, F1 > Game > Audio): this stream carries the movie
// audio, which the MusyX voices do not cover.
float sSfxBoost = 1.f;
bool sAudioInitialized = false;
bool sPlaying = true;
alignas(32) uint8_t sSilence[0x280] = {};

// Runs the registered DMA callback once. On the GameCube, MusyX's base AI
// callback submits a fresh mix buffer first, and later callbacks (streamed
// music, movie audio) read it through AIGetDMAStartAddr and mix on top. The port
// plays MusyX through its own stream, so hand them silence instead: otherwise
// they read back the buffer they submitted last time, and a mixer that adds to
// its input (the THP movie player, e.g. the game-over screen) feeds its own
// output back into itself until it distorts.
void RunCallback() {
  sBuffer = reinterpret_cast< uintptr_t >(sSilence);
  sLength = sizeof(sSilence);
  sCallback();
}

void EnsureStream() {
  if (sStream != nullptr || !sOutputEnabled) {
    return;
  }
  if (!sAudioInitialized) {
    sAudioInitialized = SDL_InitSubSystem(SDL_INIT_AUDIO);
    if (!sAudioInitialized) {
      std::fprintf(stderr, "AI audio initialization failed: %s\n", SDL_GetError());
      sOutputEnabled = false;
      return;
    }
  }
  SDL_AudioSpec spec{SDL_AUDIO_S16, 2, kSampleRate};
  sStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  if (sStream == nullptr) {
    std::fprintf(stderr, "AI audio device unavailable: %s\n", SDL_GetError());
    sOutputEnabled = false;
  } else {
    // A boost set before the stream existed lands here.
    SDL_SetAudioStreamGain(sStream, sSfxBoost);
    if (sPlaying) {
      SDL_ResumeAudioStreamDevice(sStream);
    }
  }
}

void EnsureStarted() {
  if (sStarted) {
    return;
  }
  sStarted = true;
  // Enabled by default; MP_DISABLE_AI_AUDIO=1 isolates streamed audio (music)
  // from the MusyX effects when diagnosing.
  sOutputEnabled = std::getenv("MP_DISABLE_AI_AUDIO") == nullptr;
  // Silence the AI is notionally playing before the first AIInitDMA, so the
  // guest's `AIGetDMAStartAddr` always yields a readable buffer.
  sBuffer = reinterpret_cast< uintptr_t >(sSilence);
  sLength = sizeof(sSilence);

  EnsureStream();
}
} // namespace

// The SDK's `AIGetDMAStartAddr` returns a 32-bit address, but on the port the DMA
// buffers are 64-bit host pointers. Guest code that needs the real pointer (the
// streamed-audio mixer) uses this instead.
extern "C" uintptr_t AIPortGetDMAStartAddr(void) { return sBuffer; }

// Runs pending AI DMA callbacks. Must be called from the thread that owns the
// guest audio state (the main loop).
extern "C" void AIPortPoll(void) {
  EnsureStarted();
  if (sCallback == nullptr || !sPlaying) {
    // Nothing is playing; resynchronise so a later stream does not burst.
    sNextFrameNs = SDL_GetTicksNS();
    return;
  }

  // Keep enough decoded audio queued to absorb main-thread and disc-loading
  // jitter. A single 5 ms DMA buffer underruns whenever a frame runs long.
  if (sOutputEnabled && sStream != nullptr) {
    int queued = SDL_GetAudioStreamQueued(sStream);
    int budget = 16;
    while (sCallback != nullptr && queued >= 0 && queued < kTargetQueuedBytes && budget-- > 0) {
      RunCallback();
      const uintptr_t buffer = sBuffer;
      const uint32_t length = sLength;
      if (buffer == 0 || length == 0 ||
          !SDL_PutAudioStreamData(sStream, reinterpret_cast< const void* >(buffer), length)) {
        break;
      }
      // MP_AI_DUMP=<path>: append what the AI path plays (32 kHz s16 stereo; movie audio), the
      // counterpart of musyx-port's MP_AUDIO_DUMP.
      static FILE* sDump = [] {
        const char* path = std::getenv("MP_AI_DUMP");
        return path != nullptr ? std::fopen(path, "wb") : nullptr;
      }();
      if (sDump != nullptr) {
        std::fwrite(reinterpret_cast< const void* >(buffer), 1, length, sDump);
        std::fflush(sDump);
      }
      queued += static_cast<int>(length);
    }
    return;
  }

  const uint64_t now = SDL_GetTicksNS();
  if (sNextFrameNs == 0) {
    sNextFrameNs = now;
  }

  // Catch up to real time, bounded so a long stall cannot fire a burst.
  int budget = 8;
  while (sCallback != nullptr && sNextFrameNs <= now && budget-- > 0) {
    RunCallback();
    const uint32_t length = sLength;
    const uint64_t duration = length != 0
                                  ? static_cast< uint64_t >(length) * 1000000000ull /
                                        (static_cast< uint64_t >(kBytesPerFrame) * kSampleRate)
                                  : kDefaultFrameNs;
    sNextFrameNs += duration;
  }
  if (sNextFrameNs < now) {
    sNextFrameNs = now;
  }
}

extern "C" uint32_t AIGetDMAStartAddr(void) { return static_cast< uint32_t >(sBuffer); }

extern "C" void AIInit(u8* stack) {
  (void)stack;
  EnsureStarted();
}

extern "C" void AIInitDMA(uintptr_t start_addr, uint32_t length) {
  EnsureStarted();
  if (start_addr == 0 || length == 0 || length % kBytesPerFrame != 0 ||
      length > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
    sBuffer = reinterpret_cast<uintptr_t>(sSilence);
    sLength = sizeof(sSilence);
    return;
  }
  sBuffer = start_addr;
  sLength = length;
}

extern "C" AIDCallback AIRegisterDMACallback(AIDCallback callback) {
  EnsureStarted();
  AIDCallback previous = sCallback;
  sCallback = callback;
  // The previous owner may free its DMA buffer immediately after unregistering.
  sBuffer = reinterpret_cast<uintptr_t>(sSilence);
  sLength = sizeof(sSilence);
  sNextFrameNs = SDL_GetTicksNS();
  if (sStream != nullptr) {
    SDL_ClearAudioStream(sStream);
  }
  return previous;
}

extern "C" void AISetStreamPlayState(uint32_t state) {
  sPlaying = state != 0;
  if (sStream == nullptr) {
    return;
  }
  if (state == 0) {
    SDL_PauseAudioStreamDevice(sStream);
  } else {
    SDL_ResumeAudioStreamDevice(sStream);
  }
}

// Retail sets the AI DMA-control start bit here, i.e. "the buffer I registered is
// full, play it". The host analogue is resuming the stream, which is exactly what
// AISetStreamPlayState(1) does. Aurora's dolphin/ai.h declares AIStartDMA but
// implements none of the five AI DMA entry points, so the port owns all of them;
// without this one the port fails to link on RAssertDolphin.cpp:53.
extern "C" void AIStartDMA(void) { AISetStreamPlayState(1); }

// Runtime mute of the streamed-audio path (used by the debug overlay).
extern "C" void AIPortSetOutputEnabled(int enabled) {
  EnsureStarted();
  sOutputEnabled = enabled != 0;
  sNextFrameNs = SDL_GetTicksNS();
  if (sStream != nullptr) {
    SDL_ClearAudioStream(sStream);
  }
  EnsureStream();
}

extern "C" int AIPortOutputEnabled(void) { return sOutputEnabled && sStream != nullptr; }

// The port's volume boost for what this stream plays; 1.0 is none. Applied at once when the
// stream is open, kept for one that opens later.
extern "C" void AIPortSetOutputGain(float gain) {
  sSfxBoost = gain > 0.f ? gain : 1.f;
  if (sStream != nullptr) {
    SDL_SetAudioStreamGain(sStream, sSfxBoost);
  }
}

extern "C" void AIPortShutdown(void) {
  sCallback = nullptr;
  if (sStream != nullptr) {
    SDL_DestroyAudioStream(sStream);
    sStream = nullptr;
  }
  if (sAudioInitialized) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    sAudioInitialized = false;
  }
  sStarted = false;
  sBuffer = 0;
  sLength = 0;
  sNextFrameNs = 0;
  sPlaying = true;
}
