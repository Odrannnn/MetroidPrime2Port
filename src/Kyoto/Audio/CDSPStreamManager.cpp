#include "Kyoto/Audio/CDSPStreamManager.hpp"

#include "Kyoto/Alloc/CMemory.hpp"
#include "Kyoto/Basics/CInterruptGuard.hpp"
#include "Kyoto/CDvdFile.hpp"

#include "dolphin/os.h"
#ifdef TARGET_PC
#include "musyx/hardware.h"
#endif

#include <string.h>
#ifdef TARGET_PC

#include "Kyoto/Basics/CBasics.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#ifdef _WIN32
#include <SDL3/SDL_timer.h>
#endif

namespace {
bool DebugAudio() {
  static const bool enabled = getenv("MP2_DEBUG_AUDIO") != nullptr;
  return enabled;
}

// When each stream's last refill was issued, to measure how long the DVD took to deliver it.
double sRefillIssued[2];
double NowMs() {
  timespec ts;
#ifdef _WIN32
  timespec_get(&ts, TIME_UTC); // no CLOCK_MONOTONIC; only differences are logged
#else
  clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
  return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

// The .rsf header is disc data (big-endian); the sample data after it is ADPCM bytes, which MusyX
// decodes in disc order.
void SwapHeader(SRSFHeader& h) {
  h.mMagic = CBasics::SwapBytes(h.mMagic);
  h.mVersion = CBasics::SwapBytes(h.mVersion);
  h.mNumChannels = CBasics::SwapBytes(h.mNumChannels);
  h.x8_ = CBasics::SwapBytes(h.x8_);
  h.mSampleRate = CBasics::SwapBytes(h.mSampleRate);
  h.mDataSize = CBasics::SwapBytes(h.mDataSize);
  h.mLoopFlag = CBasics::SwapBytes(h.mLoopFlag);
  h.mLoopStart = CBasics::SwapBytes(h.mLoopStart);
  h.mLoopEnd = CBasics::SwapBytes(h.mLoopEnd);
  for (int c = 0; c < 2; ++c) {
    for (int j = 0; j < 8; ++j) {
      h.mAdpcmInfo[c].coefTab[j][0] = CBasics::SwapBytes(h.mAdpcmInfo[c].coefTab[j][0]);
      h.mAdpcmInfo[c].coefTab[j][1] = CBasics::SwapBytes(h.mAdpcmInfo[c].coefTab[j][1]);
    }
  }
}
// The console's interrupts are threads on the host: the DVD completion runs on Aurora's worker, the
// refill on MusyX's audio thread, the start/stop on the game's, and OSDisableInterrupts is a no-op,
// so CInterruptGuard guarded nothing. A completion landing between ReadData's IssueRead and its
// `mReadsPending += 2` would run UpdateARAM early (and again later), putting the halves' toggle out of
// step so one half of the ring replays stale audio. Use MusyX's own (recursive) lock instead: the
// audio thread already holds it around UpdateStream, so there is one lock and no lock order.
class CStreamLockGuard {
public:
  CStreamLockGuard() { hwDisableIrq(); }
  ~CStreamLockGuard() { hwEnableIrq(); }
};
} // namespace
#define CInterruptGuard CStreamLockGuard
#endif

CDSPStreamManager sStreams[2];
rstl::reserved_vector< SDSPStreamVoice, 4 > sVoices;
static bool sDeferCallbacks;
static int sDeferredCallbacks;

CDSPStreamManager::CDSPStreamManager()
: mDataOffset(0), mFileCur(0), mReadsPending(0), mType(1), mState(kSS_Idle) {}

SDSPStreamVoice::SDSPStreamVoice()
: mBufferSize(0), mNumSamples(0), mType(1), mStreamId(-1), mUpperHalf(false), mFree(true) {}

void CDSPStreamManager::Initialize() {
  for (int i = 0; i < 4; ++i) {
    sVoices.push_back(SDSPStreamVoice());
  }

  sStreams[0].mType = 0;
  for (int i = 0; i < 2; ++i) {
    sVoices[i].mBuffer = rstl::auto_ptr< uchar >(
        static_cast< uchar* >(CMemory::Alloc(0x11E00, IAllocator::kHI_RoundUpLen)));
    sVoices[i].mType = 0;
  }
  CMemory::OffsetFakeStatics(0x23C00);
}

void CDSPStreamManager::Shutdown() {
#ifdef TARGET_PC
  // Deallocate holds the stream lock while DVDClose waits for a read in flight, and that read's
  // completion (DVDCallback, on Aurora's DVD worker) needs the lock: a shutdown during a refill hung
  // for good (~1 harness run in 5). Deallocate only once no read is pending, checked under the same
  // lock so the audio thread's refill can't issue one in between; a later completion is the
  // worker's own and needs no lock to retire. Past ~2 s, fall through to the old path.
  for (int attempt = 0; attempt < 2000; ++attempt) {
    {
      CInterruptGuard guard;
      bool pending = false;
      for (int i = 0; i < 2; ++i) {
        pending |= sStreams[i].mState != kSS_Idle && sStreams[i].mReadsPending > 0;
      }
      if (!pending) {
        for (int i = 0; i < 2; ++i) {
          if (sStreams[i].mState != kSS_Idle) {
            sStreams[i].Deallocate();
          }
        }
        break;
      }
    }
#ifdef _WIN32
    SDL_Delay(1);
#else
    const timespec wait = {0, 1000000};
    nanosleep(&wait, nullptr);
#endif
  }
#endif
  for (int i = 0; i < 2; ++i) {
    if (sStreams[i].mState != kSS_Idle) {
      sStreams[i].Deallocate();
    }
  }

  for (int i = 0; i < 4; ++i) {
    if (sVoices[i].mType == 0) {
      sVoices[i].mBuffer = rstl::auto_ptr< uchar >();
    }
  }
  sVoices.clear();
  CMemory::OffsetFakeStatics(-0x23C00);
}

int CDSPStreamManager::StartStreaming(const rstl::string& fileName, int volume, int type) {
  CInterruptGuard guard;
  int i;
  for (i = 0; i < 2; ++i) {
    CDSPStreamManager& stream = sStreams[i];
    if (stream.mState != kSS_Idle || stream.mType != type) {
      continue;
    }

    rstl::string path(fileName);
    if (strncmp("mem:", fileName.data(), strlen("mem:")) == 0) {
      path = fileName.substr(4, -1);
      if (CDvdFile::FileExists(path.data())) {
        stream.mPreload = CStreamPreloadedToken(path);
        if (!stream.mPreload->IsReady()) {
          stream.mPreload = rstl::optional_object< CStreamPreloadedToken >();
        }
      }
    }

#ifdef TARGET_PC
    if (DebugAudio()) {
      fprintf(stderr, "[audio] dsp start '%s' vol=%d type=%d stream=%d preload=%d\n", path.data(),
              volume, type, i, stream.mPreload ? 1 : 0);
    }
#endif
    if (DVDOpen(const_cast< char* >(path.data()), &stream.mFiles[0])) {
      stream.mDataOffset = sizeof(SRSFHeader);
      stream.mState = kSS_ReadingHeader;
      stream.mFileCur = 0;
      stream.mVolume = volume;
      for (int j = 1; j < 10; ++j) {
        DVDOpen(const_cast< char* >(path.data()), &stream.mFiles[j]);
      }
      stream.mVoices.clear();
      stream.mReadsPending = 1;
      if (stream.mPreload) {
        stream.mPreload->Read(&stream.mHeader, 0, sizeof(SRSFHeader));
        DVDCallback(0, &stream.mFiles[0]);
      } else {
        DVDReadAsyncPrio(&stream.mFiles[0], &stream.mHeader, sizeof(SRSFHeader), 0, DVDCallback, 1);
      }
      break;
    }
  }

  return i < 2 ? i : -1;
}

int CDSPStreamManager::ActivateVoices() {
  CInterruptGuard guard;
  for (int* it = mVoices.begin(); it != mVoices.end(); ++it) {
    sndStreamActivate(sVoices[*it].mStreamId);
  }
  return 0;
}

int CDSPStreamManager::AllocateVoice(int volume, uchar pan, SND_ADPCMSTREAM_INFO* info,
                                     bool primary) {
  int idx = 0;
  CInterruptGuard guard;
  for (SDSPStreamVoice* it = sVoices.begin(); it != sVoices.end(); ++it, ++idx) {
    if (!it->mFree || mType != it->mType) {
      continue;
    }

    it->mFree = false;
    it->mUpperHalf = false;
    it->mBufferSize = 0x11E00;
    it->mPan = pan;
    it->mNumSamples = (it->mBufferSize / 8) * 14;
    if (it->mType != 0) {
      it->mBuffer = rstl::auto_ptr< uchar >(
          static_cast< uchar* >(CMemory::Alloc(it->mBufferSize, IAllocator::kHI_RoundUpLen)));
    }
#ifdef TARGET_PC
    // Port: a game heap too full for the buffer (CMemory::Alloc logs it) gave MusyX a null stream
    // buffer, and the first refill crashed in sndStreamARAMUpdate as a cinematic ended and its
    // music started. The voice stays free and the caller drops the stream.
    if (it->mBuffer.get() == nullptr) {
      it->mFree = true;
      return -1;
    }
#endif
    it->mStreamId = sndStreamAllocEx(0xFF, it->mBuffer.get(), it->mNumSamples, mHeader.mSampleRate,
                                     volume, it->mPan, 0, 0, 0, 0, 0x30001,
                                     primary ? UpdateStream : UpdateSecondaryStream,
#ifdef TARGET_PC
                                     // MusyX keeps `user` as a u32, which truncates a host
                                     // pointer: pass the stream's index instead.
                                     static_cast< u32 >(this - sStreams),
#else
                                     reinterpret_cast< u32 >(this),
#endif
                                     nullptr);
    sndStreamADPCMParameter(it->mStreamId, info);
    return idx;
  }
  return -1;
}

void CDSPStreamManager::DVDCallback(s32 result, DVDFileInfo* fileInfo) {
  static volatile bool sBusy = false;
  static volatile int sCount = 0;
  static volatile int sReadIdx = 0;
  static volatile int sWriteIdx = 0;
  static DVDFileInfo* volatile sQueue[8];
#ifdef TARGET_PC
  // On the console this runs as an interrupt, atomic against the game and the audio refill.
  CInterruptGuard callbackGuard;
#endif

  {
    CInterruptGuard guard;
    sQueue[sWriteIdx] = fileInfo;
    sWriteIdx = (sWriteIdx + 1) & 7;
    ++sCount;
    if (sBusy) {
      return;
    }
    sBusy = true;
  }

  for (;;) {
    DVDFileInfo* next;
    {
      CInterruptGuard guard;
      if (sCount == 0) {
        sBusy = false;
        return;
      }
      --sCount;
      next = sQueue[sReadIdx];
      sReadIdx = (sReadIdx + 1) & 7;
    }
    ReadCompleted(result, fileInfo);
  }
}

void CDSPStreamManager::ReadCompleted(s32 result, DVDFileInfo* fileInfo) {
  int i;
  for (i = 0; i < 2; ++i) {
    int j;
    for (j = 0; j < 10; ++j) {
      if (&sStreams[i].mFiles[j] == fileInfo) {
        break;
      }
    }
    if (j != 10) {
      break;
    }
  }
  if (i >= 2) {
    return;
  }

  CDSPStreamManager& stream = sStreams[i];
  --stream.mReadsPending;
#ifdef TARGET_PC
  if (DebugAudio() && stream.mReadsPending < 0) {
    fprintf(stderr, "[audio] dsp read completed with pending=%d (a completion raced ReadData)\n",
            stream.mReadsPending);
  }
#endif
  if (stream.mReadsPending > 0) {
    return;
  }

  if (stream.mPreload || DVDGetCommandBlockStatus(&fileInfo->cb) == 0) {
    switch (stream.mState) {
    case kSS_ReadingHeader:
      DCInvalidateRange(&stream.mHeader, sizeof(SRSFHeader));
#ifdef TARGET_PC
      SwapHeader(stream.mHeader);
      if (DebugAudio()) {
        fprintf(stderr,
                "[audio] dsp header stream=%d magic=%04x ver=%u ch=%u rate=%u size=%u loop=%u "
                "[%u,%u]\n",
                i, stream.mHeader.mMagic, stream.mHeader.mVersion, stream.mHeader.mNumChannels,
                stream.mHeader.mSampleRate, stream.mHeader.mDataSize, stream.mHeader.mLoopFlag,
                stream.mHeader.mLoopStart, stream.mHeader.mLoopEnd);
      }
#endif
      if (stream.mHeader.mMagic != 'RS' || stream.mHeader.mVersion != 3) {
        stream.Deallocate();
        return;
      }
#ifdef TARGET_PC
      {
        // Port: a voice that could not be had (none free, or no buffer: AllocateVoice) drops the
        // stream, as a bad header does, rather than playing from sVoices[-1] or a null buffer.
        const int channels = stream.mHeader.mNumChannels;
        if (channels != 1 && channels != 2) {
          stream.Deallocate();
          return;
        }
        const int first = stream.AllocateVoice(stream.mVolume, channels == 1 ? 0x40 : 0,
                                               &stream.mHeader.mAdpcmInfo[0], true);
        if (first >= 0) {
          stream.mVoices.push_back(first);
        }
        const int second = channels == 2 && first >= 0
                               ? stream.AllocateVoice(stream.mVolume, 0x7F,
                                                      &stream.mHeader.mAdpcmInfo[1], false)
                               : 0;
        if (channels == 2 && second >= 0) {
          stream.mVoices.push_back(second);
        }
        if (first < 0 || second < 0) {
          std::fprintf(stderr, "[audio] dsp stream=%d dropped: no voice or buffer for it\n", i);
          stream.Deallocate();
          return;
        }
      }
#else
      if (stream.mHeader.mNumChannels == 1) {
        stream.mVoices.push_back(
            stream.AllocateVoice(stream.mVolume, 0x40, &stream.mHeader.mAdpcmInfo[0], true));
      } else if (stream.mHeader.mNumChannels == 2) {
        stream.mVoices.push_back(
            stream.AllocateVoice(stream.mVolume, 0, &stream.mHeader.mAdpcmInfo[0], true));
        stream.mVoices.push_back(
            stream.AllocateVoice(stream.mVolume, 0x7F, &stream.mHeader.mAdpcmInfo[1], false));
      } else {
        stream.Deallocate();
        return;
      }
#endif
      stream.mState = kSS_Priming;
      stream.BufferStream();
      break;
    case kSS_Priming:
      stream.UpdateARAM();
      stream.ActivateVoices();
      stream.BufferStream();
      stream.mState = kSS_Playing;
      break;
    case kSS_Playing:
      stream.UpdateARAM();
      break;
    case kSS_Stopping:
      stream.Deallocate();
      break;
    }
  } else {
    stream.Deallocate();
  }
}

void CDSPStreamManager::UpdateARAM() {
#ifdef TARGET_PC
  if (DebugAudio() && !mVoices.empty()) {
    const int index = static_cast< int >(this - sStreams);
    fprintf(stderr, "[audio] dsp aram stream=%d flush %s half, fileCur=0x%x, %.0f ms after issue\n",
            index, sVoices[mVoices[0]].mUpperHalf ? "upper" : "lower", mFileCur,
            NowMs() - sRefillIssued[index & 1]);
  }
#endif
  for (int* it = mVoices.begin(); it != mVoices.end(); ++it) {
    SDSPStreamVoice& voice = sVoices[*it];
    sndStreamARAMUpdate(voice.mStreamId, voice.mUpperHalf ? voice.mNumSamples / 2 : 0,
                        voice.mNumSamples / 2, 0, 0);
    voice.mUpperHalf = !voice.mUpperHalf;
  }
}

void CDSPStreamManager::IssueRead(DVDFileInfo* file, void* dest, int length, int offset,
                                  const SDSPStreamVoice& voice, CDSPStreamManager& stream) {
  if (stream.mPreload) {
    stream.mPreload->Read(dest, offset, length);
    if (sDeferCallbacks) {
      ++sDeferredCallbacks;
    } else {
      DVDCallback(0, file);
    }
  } else {
    DVDReadAsyncPrio(file, dest, length, offset, DVDCallback, 1);
  }
}

void CDSPStreamManager::ReadData(uchar* left, uchar* right, uint length, int dataOffset,
                                 uint fileCur) {
  if (right != nullptr) {
    uint remaining = length;
    for (uint pos = fileCur; remaining != 0 && pos < length + fileCur;) {
      uint block = pos / 0x8F00;
      uint blockStart = block * 0x8F00;
      uint blockOffset = pos - blockStart;
      uint avail = 0x8F00 - blockOffset;
      uint chunk = remaining < avail ? remaining : avail;
      uint offset = dataOffset + (block * 0x11E00 + blockOffset);
      uint channelSize = 0x8F00;
      if ((block + 1) * 0x8F00 >= mHeader.mDataSize) {
        channelSize = mHeader.mDataSize - blockStart;
      }
      IssueRead(&mFiles[mReadsPending], left + pos - fileCur, chunk, offset, sVoices[mVoices[0]],
                *this);
      IssueRead(&mFiles[mReadsPending + 1], right + pos - fileCur, chunk, offset + channelSize,
                sVoices[mVoices[1]], *this);
      pos += chunk;
      remaining -= chunk;
      mReadsPending += 2;
    }
  } else {
    IssueRead(&mFiles[mReadsPending], left, length, dataOffset + fileCur, sVoices[mVoices[0]],
              *this);
    ++mReadsPending;
  }
}

void CDSPStreamManager::BufferStream() {
  int readLen = 0x8F00;
  bool stereo = mVoices.size() > 1;
  int end;
  if (mHeader.mLoopFlag != 0) {
    end = (mHeader.mLoopEnd + 31) & ~31;
  } else {
    end = mHeader.mDataSize;
  }

  int loopLen = 0;
  uchar* left = sVoices[mVoices[0]].GetCurrentBuffer();
  uchar* right = nullptr;
  if (stereo) {
    right = sVoices[mVoices[1]].GetCurrentBuffer();
  }

  if (mFileCur + 0x8F00 > end) {
    if (mFileCur < end) {
      readLen = end - mFileCur;
      if (mHeader.mLoopFlag != 0) {
        loopLen = 0x8F00 - readLen;
      } else {
        DCZeroRange(left + readLen, 0x8F00 - readLen);
        DCFlushRange(left + readLen, 0x8F00 - readLen);
        if (stereo) {
          DCZeroRange(right + readLen, 0x8F00 - readLen);
          DCFlushRange(right + readLen, 0x8F00 - readLen);
        }
      }
    } else if (mHeader.mLoopFlag != 0) {
      mFileCur = mHeader.mLoopStart;
    } else {
      readLen = 0;
      DCZeroRange(left, 0x8F00);
      DCFlushRange(left, 0x8F00);
      if (stereo) {
        DCZeroRange(right, 0x8F00);
        DCFlushRange(right, 0x8F00);
      }
      UpdateARAM();
    }
  }

#ifdef TARGET_PC
  if (DebugAudio()) {
    fprintf(stderr,
            "[audio] dsp refill stream=%d %s half: fileCur=0x%x read=0x%x loopLen=0x%x end=0x%x "
            "loopStart=0x%x\n",
            static_cast< int >(this - sStreams), sVoices[mVoices[0]].mUpperHalf ? "upper" : "lower",
            mFileCur, readLen, loopLen, end, mHeader.mLoopStart);
    sRefillIssued[(this - sStreams) & 1] = NowMs();
  }
#endif
  CInterruptGuard guard;
  sDeferCallbacks = true;
  if (readLen > 0) {
    ReadData(left, right, readLen, mDataOffset, mFileCur);
    if (loopLen != 0) {
      mFileCur = mHeader.mLoopStart;
      ReadData(left + readLen, stereo ? right + readLen : nullptr, loopLen, mDataOffset, mFileCur);
      mFileCur += loopLen;
    } else {
      mFileCur += readLen;
    }
  } else if (mState != kSS_Finishing) {
    mState = kSS_Finishing;
  } else if (mState != kSS_Stopping) {
    StopStream();
  }
  sDeferCallbacks = false;

  for (int i = 0; i < sDeferredCallbacks; ++i) {
    DVDCallback(0, &mFiles[0]);
  }
  sDeferredCallbacks = 0;
}

u32 CDSPStreamManager::UpdateStream(void* buf1, u32 len1, void* buf2, u32 len2, u32 user) {
#ifdef TARGET_PC
  if (user >= 2) {
    return 0;
  }
  CDSPStreamManager* stream = &sStreams[user];
#else
  CDSPStreamManager* stream = reinterpret_cast< CDSPStreamManager* >(user);
#endif
  len1 += len2;
  u32 half = sVoices[stream->mVoices[0]].mNumSamples / 2;
#ifdef TARGET_PC
  if (DebugAudio()) {
    static int sCalls = 0;
    if ((sCalls++ & 63) == 0) {
      fprintf(stderr, "[audio] dsp update #%d stream=%d played=%u half=%u pending=%d state=%d\n",
              sCalls, static_cast< int >(stream - sStreams), len1, half, stream->mReadsPending,
              stream->mState);
    }
  }
#endif
  if (len1 < half) {
    return 0;
  }
  if (stream->mReadsPending > 0) {
    return 0;
  }
  stream->BufferStream();
  return half;
}

u32 CDSPStreamManager::UpdateSecondaryStream(void* buf1, u32 len1, void* buf2, u32 len2, u32 user) {
  return 0;
}

void CDSPStreamManager::StopStream() {
  if (mReadsPending > 0) {
    mState = kSS_Stopping;
  } else {
    Deallocate();
  }
}

void CDSPStreamManager::Deallocate() {
  CInterruptGuard guard;
  for (int* it = mVoices.begin(); it != mVoices.end(); ++it) {
    SDSPStreamVoice& voice = sVoices[*it];
    sndStreamDeactivate(voice.mStreamId);
    sndStreamFree(voice.mStreamId);
    voice.mStreamId = -1;
    if (voice.mType != 0) {
      voice.mBuffer = rstl::auto_ptr< uchar >();
    }
    voice.mFree = true;
  }

  for (int i = 0; i < 10; ++i) {
    DVDClose(&mFiles[i]);
  }
  mVoices.clear();
  mPreload = rstl::optional_object< CStreamPreloadedToken >();
  mState = kSS_Idle;
}

void CDSPStreamManager::StopStreaming(int handle) {
  if (handle >= 0 && handle < 2) {
    CInterruptGuard guard;
    if (sStreams[handle].mState != kSS_Idle) {
      sStreams[handle].StopStream();
    }
  }
}

void CDSPStreamManager::UpdateVolume(int handle, int volume) {
  if (handle >= 0 && handle < 2) {
    CInterruptGuard guard;
    CDSPStreamManager& stream = sStreams[handle];
    if (stream.mState == kSS_Playing) {
      for (int* it = stream.mVoices.begin(); it != stream.mVoices.end(); ++it) {
        const SDSPStreamVoice& voice = sVoices[*it];
        sndStreamMixParameter(voice.mStreamId, volume, voice.mPan, 0, 0);
      }
    }
  }
}

bool CDSPStreamManager::IsStreamAvailable(int handle) {
  if (handle < 0 && handle >= 2) {
    return true;
  }
  CInterruptGuard guard;
  CDSPStreamManager& stream = sStreams[handle];
  int state = stream.mState;
  return state != kSS_ReadingHeader && state != kSS_Priming;
}

bool CDSPStreamManager::CanStop(int handle) {
  if (handle < 0 && handle >= 2) {
    return true;
  }
  CInterruptGuard guard;
  CDSPStreamManager& stream = sStreams[handle];
  return stream.mState == kSS_Idle;
}

int CDSPStreamManager::GetStreamState(int handle) {
  if (handle < 0 && handle >= 2) {
    return kS_Oneshot;
  }
  CInterruptGuard guard;
  CDSPStreamManager& stream = sStreams[handle];
  if (stream.mHeader.mLoopFlag != 0) {
    return kS_Looping;
  }
  return kS_Oneshot;
}
