#include "Kyoto/Alloc/LockedCache.hpp"
#include "Kyoto/Basics/CBasics.hpp"
#include "Kyoto/Basics/CInterruptGuard.hpp"
#include "Kyoto/Math/CVector3f.hpp"

#include <Kyoto/Graphics/CMoviePlayer.hpp>

#include "Kyoto/Audio/CAudioSys.hpp"
#include "Kyoto/Audio/DolphinCRSFAudio.hpp"
#include "Kyoto/Graphics/CGraphics.hpp"
#include "Kyoto/Math/CMath.hpp"
#include "dolphin/PPCArch.h"
#include "dolphin/ai.h"
#include "dolphin/gx/GXVert.h"
#include "dolphin/os.h"
#include "dolphin/thp/THPDec.h"
#include <Kyoto/Graphics/CGX.hpp>
#include <Kyoto/Graphics/CTexture.hpp>
#include <math.h>
#include <rstl/math.hpp>
#include <string.h>

#include "dolphin/gx/GXGeometry.h"
#include "dolphin/gx/GXTev.h"
#include "dolphin/gx/GXTexture.h"

#ifdef TARGET_PC
#include <mutex>

#include "port_overlay.h"

// Port: Aurora keeps a file's open handle in `cb.userData`, so the player that owns a pending
// frame read is tracked here instead of being stored there.
namespace {
struct SPendingMovieRead {
  DVDFileInfo* mInfo;
  CMoviePlayer* mPlayer;
};
std::mutex sPendingMovieReadsLock;
SPendingMovieRead sPendingMovieReads[16];

void SetMovieReadOwner(DVDFileInfo* info, CMoviePlayer* player) {
  std::lock_guard< std::mutex > lock(sPendingMovieReadsLock);
  for (SPendingMovieRead& read : sPendingMovieReads) {
    if (read.mInfo == nullptr || read.mInfo == info) {
      read.mInfo = info;
      read.mPlayer = player;
      return;
    }
  }
}

CMoviePlayer* TakeMovieReadOwner(DVDFileInfo* info) {
  std::lock_guard< std::mutex > lock(sPendingMovieReadsLock);
  for (SPendingMovieRead& read : sPendingMovieReads) {
    if (read.mInfo == info) {
      CMoviePlayer* player = read.mPlayer;
      read.mInfo = nullptr;
      read.mPlayer = nullptr;
      return player;
    }
  }
  return nullptr;
}

void DropMovieReadOwner(CMoviePlayer* player) {
  std::lock_guard< std::mutex > lock(sPendingMovieReadsLock);
  for (SPendingMovieRead& read : sPendingMovieReads) {
    if (read.mPlayer == player) {
      read.mInfo = nullptr;
      read.mPlayer = nullptr;
    }
  }
}
} // namespace
#endif

static rstl::string SelectMoviePath(const char* path) {
  rstl::string name(path);
  if (CGraphics::Is50Hz()) {
    rstl::string palName = name + "_pal";
    if (CDvdFile::FileExists(palName.data())) {
      return palName;
    }
  }
  return name;
}

static bool ShouldEnableLockedCache() { return LCGetBase() != GetLockedCacheAllocationBase(); }

static int sNumReferences = 0;
static CMoviePlayer* sAudioPlayer;
static const short* curAudioBuffer;
static int soundBufferIndex;
static short soundBuffer[2][320] ATTRIBUTE_ALIGN(32);
static bool sAudioEnabled = true;
static uchar sSfxVolume = 127;
#ifdef TARGET_PC
// Port: the movie's audio is read from the decoded frames' slots by the AI callback. Retail's AI
// takes 5 ms at a time in real time, a frame or two behind the decoder (3 slots, 2 decoded ahead).
// The port's AI pump (platform/ai_dma.cpp) keeps ~64 ms queued for the audio device, so the reader
// runs about four frames ahead of the picture and kept reaching slots not decoded yet: it skipped
// them and padded with silence, ~2 gaps a second (lastStandFlashback, the G.F.S. Tyr cutscene,
// sounded staticky and warbled). More slots, decoded further ahead, give the reader decoded audio
// to read (MixAudio paces it); they are host memory, not the game heap.
static const int kPortMovieSlots = 8;
static const int kPortMovieDecodeAhead = kPortMovieSlots - 1;
#define MOVIE_DECODE_AHEAD kPortMovieDecodeAhead
// The movie whose first frame is being held (Update) and for how many updates so far.
static const CMoviePlayer* sPortHoldPlayer = nullptr;
static int sPortHoldUpdates = 0;
namespace {
// The port's movie audio reader (CMoviePlayer::MixAudio). Retail's AI plays a frame's samples on
// the same hardware clock the movie's frames are timed by; on the port the frames advance with the
// game's updates and the samples leave at the audio device's rate, and the two drift apart (~0.3%
// measured), so a reader that takes exactly 160 samples per callback either catches up with the
// decoder (a gap of silence, a click, a few a second) or falls behind it. It reads at a rate
// nudged by up to 0.5% instead, keeping a steady amount of decoded audio ahead of it, with linear
// interpolation between samples.
struct SPortMovieMix {
  const CMoviePlayer* mOwner = nullptr;
  bool mRunning = false;
  uint mFrac = 0;          // 16.16 position between mPrev and mCur
  short mPrev[2] = {0, 0};
  short mCur[2] = {0, 0};
  float mAvailAvg = -1.f;  // smoothed decoded samples ahead of the reader
};
SPortMovieMix sPortMix;
} // namespace
#else
#define MOVIE_DECODE_AHEAD 2
#endif

struct CMoviePlayer::SIndexLoad {
  rstl::single_ptr< CDvdRequest > mHeaderRequest;
  rstl::single_ptr< CDvdRequest > mVideoRequest;
  rstl::single_ptr< CDvdRequest > mAudioRequest;
  rstl::single_ptr< uchar > mBuffer;
  int mState;

  ~SIndexLoad() {}

  SIndexLoad()
  : mBuffer(static_cast< uchar* >(CMemory::Alloc(64, IAllocator::kHI_RoundUpLen))), mState(0) {}
};

const unsigned char skInterlacePattern[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static void MyTHPGXRestore() {
  CGX::SetZMode(TRUE, GX_ALWAYS, FALSE);
  CGX::SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_SET);
  CGX::SetNumTexGens(1);
  CGX::SetNumChans(0);
  CGX::SetNumTevStages(1);
  CGX::SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
  CGX::SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
}

static void MyTHPGXYuv2RgbSetup(bool field, bool deinterlace) {
  GXVtxDescList attr[3] = {
      {GX_VA_POS, GX_DIRECT},
      {GX_VA_TEX0, GX_DIRECT},
      {GX_VA_NULL, GX_NONE},
  };
#ifdef TARGET_PC
  // Port: this function builds the descriptor and never applies it, so the movie's four vertices
  // arrive as a position plus a texture coordinate while the decoder's descriptor still says
  // position only. It then consumes 12 of the 16 bytes per vertex, reads the leftovers as GX
  // commands and Aurora aborts with "unsupported primitive type" - asynchronously, on its FIFO
  // thread, with nothing pointing at the movie player. Apply the list this function made.
  CGX::SetVtxDescv(attr);
#endif
  CGX::SetZMode(TRUE, GX_ALWAYS, FALSE);
  CGX::SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
  CGX::SetNumChans(0);
  CGX::SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, FALSE, GX_PTIDENTITY);
  CGX::SetTexCoordGen(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, FALSE, GX_PTIDENTITY);

  if (deinterlace) {
    CGX::SetNumTexGens(2);
    CGX::SetNumTevStages(4);
  } else {
    CGX::SetNumTexGens(3);
    CGX::SetTexCoordGen(GX_TEXCOORD2, GX_TG_MTX2x4, GX_TG_POS, GX_TEXMTX0, FALSE, GX_PTIDENTITY);

    float n = field ? 0.25f : 0.f;
    float mtx[8] = {0.125f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.25f, n};
    GXLoadTexMtxImm(reinterpret_cast< MtxPtr >(mtx), GX_TEXMTX0, GX_MTX2x4);
    GXTexObj obj;
    GXInitTexObj(&obj, skInterlacePattern, 8, 4, GX_TF_I8, GX_REPEAT, GX_REPEAT, FALSE);
    GXInitTexObjLOD(&obj, GX_NEAR, GX_NEAR, 0.f, 0.f, 0.f, FALSE, FALSE, GX_ANISO_1);
    GXLoadTexObj(&obj, GX_TEXMAP3);
    CTexture::InvalidateTexmap(GX_TEXMAP3);
    CGX::SetTevOrder(GX_TEVSTAGE4, GX_TEXCOORD2, GX_TEXMAP3, GX_COLOR_NULL);
    CGX::SetStandardTevColorAlphaOp(GX_TEVSTAGE4);
    CGX::SetTevColorIn(GX_TEVSTAGE4, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
    CGX::SetTevAlphaIn(GX_TEVSTAGE4, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA);
    CGX::SetAlphaCompare(GX_LESS, 128, GX_AOP_AND, GX_ALWAYS, 0);
    CGX::SetNumTevStages(5);
  }

  CGX::SetVtxDescv(attr);
  GXSetColorUpdate(TRUE);
  GXSetAlphaUpdate(FALSE);
  GXInvalidateTexAll();
  GXSetVtxAttrFmt(GX_VTXFMT7, GX_VA_POS, GX_CLR_RGBA, GX_F32, 0);
  GXSetVtxAttrFmt(GX_VTXFMT7, GX_VA_TEX0, GX_CLR_RGBA, GX_RGBX8, 0);
  CGX::SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD1, GX_TEXMAP1, GX_COLOR_NULL);
  CGX::SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_C0);
  CGX::SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, false, GX_TEVPREV);
  CGX::SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_KONST, GX_CA_A0);
  CGX::SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1, false, GX_TEVPREV);
  CGX::SetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_K0);
  CGX::SetTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_K0_A);
  CGX::SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP2, GX_COLOR_NULL);
  CGX::SetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_CPREV);
  CGX::SetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, false, GX_TEVPREV);
  CGX::SetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_TEXA, GX_CA_KONST, GX_CA_APREV);
  CGX::SetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1, false, GX_TEVPREV);
  CGX::SetTevKColorSel(GX_TEVSTAGE1, GX_TEV_KCSEL_K1);
  CGX::SetTevKAlphaSel(GX_TEVSTAGE1, GX_TEV_KASEL_K1_A);
  CGX::SetTevOrder(GX_TEVSTAGE2, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
  CGX::SetTevColorIn(GX_TEVSTAGE2, GX_CC_ZERO, GX_CC_TEXC, GX_CC_ONE, GX_CC_CPREV);
  CGX::SetTevColorOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, true, GX_TEVPREV);
  CGX::SetTevAlphaIn(GX_TEVSTAGE2, GX_CA_TEXA, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
  CGX::SetTevAlphaOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, true, GX_TEVPREV);
  CGX::SetTevOrder(GX_TEVSTAGE3, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
  CGX::SetTevColorIn(GX_TEVSTAGE3, GX_CC_APREV, GX_CC_CPREV, GX_CC_KONST, GX_CC_ZERO);
  CGX::SetTevColorOp(GX_TEVSTAGE3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, true, GX_TEVPREV);
  CGX::SetTevAlphaIn(GX_TEVSTAGE3, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
  CGX::SetTevAlphaOp(GX_TEVSTAGE3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, true, GX_TEVPREV);
  CGX::SetTevKColorSel(GX_TEVSTAGE3, GX_TEV_KCSEL_K2);
  const GXColorS10 color = {-90, 0, -114, 135};
  GXSetTevColorS10(GX_TEVREG0, color);
  const GXColor kColor0 = {0, 0, 226, 88};
  CGX::SetTevKColor(GX_KCOLOR0, kColor0);
  const GXColor kColor1 = {179, 0, 0, 182};
  CGX::SetTevKColor(GX_KCOLOR1, kColor1);
  const GXColor kColor2 = {255, 0, 255, 128};
  CGX::SetTevKColor(GX_KCOLOR2, kColor2);
}

static void MyTHPYuv2RgbTextureSetup(void* y, void* u, void* v, ushort width, ushort height) {
  GXTexObj yTex;
  GXInitTexObj(&yTex, y, width, height, GX_TF_I8, GX_CLAMP, GX_CLAMP, FALSE);
  GXInitTexObjLOD(&yTex, GX_NEAR, GX_NEAR, 0.f, 0.f, 0.f, FALSE, FALSE, GX_ANISO_1);
  GXLoadTexObj(&yTex, GX_TEXMAP0);

  GXTexObj uTex;
  GXInitTexObj(&uTex, u, width / 2, height / 2, GX_TF_I8, GX_CLAMP, GX_CLAMP, FALSE);
  GXInitTexObjLOD(&uTex, GX_NEAR, GX_NEAR, 0.f, 0.f, 0.f, FALSE, FALSE, GX_ANISO_1);
  GXLoadTexObj(&uTex, GX_TEXMAP1);

  GXTexObj vTex;
  GXInitTexObj(&vTex, v, static_cast< s16 >(width / 2), height / 2, GX_TF_I8, GX_CLAMP, GX_CLAMP,
               FALSE);
  GXInitTexObjLOD(&vTex, GX_NEAR, GX_NEAR, 0.f, 0.f, 0.f, FALSE, FALSE, GX_ANISO_1);
  GXLoadTexObj(&vTex, GX_TEXMAP2);

  CTexture::InvalidateTexmap(GX_TEXMAP0);
  CTexture::InvalidateTexmap(GX_TEXMAP1);
  CTexture::InvalidateTexmap(GX_TEXMAP2);
}

CMoviePlayer::CMoviePlayer(const char* path, const float preLoadSeconds, const bool loop,
                           const bool deinterlace)
: mDvdFile(SelectMoviePath(path).data())
, mIndexLoad(rs_new SIndexLoad)
, mNextReadSize(0)
, mNextReadOff(0)
, mReadSizeWrapped(0)
, mReadOffWrapped(0)
, mCurLoadFrame(0)
, mRequestFrameWrapped(0)
, mCurFrame(0)
, mDecodedTexSlot(0)
, mDrawTexSlot(-1)
, mAudioSlot(-1)
, mDecodedTexCount(0)
, mFrameRem(0.f)
, mPrefetchFrame(0)
, mPrefetchOff(0)
, mPrefetchSize(0)
, mPlayMode(kPM_Playing)
, mTotalSeconds(0.f)
, mCurSeconds(0.f)
, mPreLoadSeconds(preLoadSeconds)
, mPreLoadFrames(0)
, mLoop(loop)
, mDeinterlace(deinterlace)
, mIs60Hz(false)
, mHasAudio(false)
, mFieldFlip(false)
, mCachedBytes(0)
, mFieldIndex(0)
, mVolume(127) {
  if (sNumReferences == 0 && ShouldEnableLockedCache()) {
    LCEnable();
  }

  static bool sThpInitialized = false;
  if (!sThpInitialized) {
    sThpInitialized = true;
    THPInit();
  }
  ++sNumReferences;
  VerifyCallbackStatus();
  mIndexLoad->mHeaderRequest = mDvdFile.SyncRead(mIndexLoad->mBuffer.get(), 64);
}

bool CMoviePlayer::ContinueLoading() {
  if (mIndexLoad.null()) {
    return false;
  }
  uchar* const buffer = mIndexLoad->mBuffer.get();
  bool hasVideo;
  bool hasAudio;
  switch (mIndexLoad->mState) {
  case 0:
    if (mIndexLoad->mHeaderRequest->IsComplete()) {
      memcpy(&mHeader, buffer, sizeof(THPHeader));
      mHeader.mVersion = CBasics::SwapBytes(static_cast< uint >(mHeader.mVersion));
      mHeader.mBufferSize = CBasics::SwapBytes(static_cast< uint >(mHeader.mBufferSize));
      mHeader.mAudioMaxSamples = CBasics::SwapBytes(static_cast< uint >(mHeader.mAudioMaxSamples));
      mHeader.mFrameRate = CBasics::SwapBytes(mHeader.mFrameRate);
      mHeader.mNumFrames = CBasics::SwapBytes(static_cast< uint >(mHeader.mNumFrames));
      mHeader.mFirstFrameSize = CBasics::SwapBytes(static_cast< uint >(mHeader.mFirstFrameSize));
      mHeader.mMovieDataSize = CBasics::SwapBytes(static_cast< uint >(mHeader.mMovieDataSize));
      mHeader.mCompInfoDataOffsets =
          CBasics::SwapBytes(static_cast< uint >(mHeader.mCompInfoDataOffsets));
      mHeader.mOffsetDataOffsets =
          CBasics::SwapBytes(static_cast< uint >(mHeader.mOffsetDataOffsets));
      mHeader.mMovieDataOffsets =
          CBasics::SwapBytes(static_cast< uint >(mHeader.mMovieDataOffsets));
      mHeader.mFinalFrameDataOffsets =
          CBasics::SwapBytes(static_cast< uint >(mHeader.mFinalFrameDataOffsets));
      if (fabsf(mHeader.mFrameRate - 29.97f) < 0.00001f) {
        mHeader.mFrameRate = 30.f;
      }
      if (fabsf(mHeader.mFrameRate - 59.94f) < 0.00001f) {
        mHeader.mFrameRate = 60.f;
      }
      if (fabsf(mHeader.mFrameRate - 60.f) < 0.00001f) {
        mIs60Hz = true;
      }
      mIndexLoad->mHeaderRequest =
          mDvdFile.AsyncSeekRead(buffer, 32, kSO_Set, mHeader.mCompInfoDataOffsets);
      ++mIndexLoad->mState;
    } else {
      return true;
    }
  case 1: {
    if (mIndexLoad->mHeaderRequest->IsComplete()) {
      memcpy(&mThpComponents, buffer, sizeof(THPFrameCompInfo));
      mThpComponents.mNumComponents =
          CBasics::SwapBytes(static_cast< uint >(mThpComponents.mNumComponents));
      mIndexLoad->mHeaderRequest = nullptr;
      uchar* audioBuffer = buffer + 32;
      int offset = mHeader.mCompInfoDataOffsets + sizeof(THPFrameCompInfo);
      for (uint i = 0; i < mThpComponents.mNumComponents; ++i) {
        switch (mThpComponents.mFrameComp[i]) {
        case 0:
          mIndexLoad->mVideoRequest = mDvdFile.AsyncSeekRead(buffer, 32, kSO_Set, offset);
          offset += sizeof(THPVideoInfo);
          break;
        case 1:
          mIndexLoad->mAudioRequest = mDvdFile.AsyncSeekRead(audioBuffer, 32, kSO_Set, offset);
          offset += sizeof(THPAudioInfo);
          mHasAudio = true;
          break;
        }
      }
      ++mIndexLoad->mState;
    } else {
      return true;
    }
  }
  case 2: {
    bool complete = true;
    hasVideo = mIndexLoad->mVideoRequest.get() != nullptr;
    hasAudio = mIndexLoad->mAudioRequest.get() != nullptr;
    if (hasVideo && !mIndexLoad->mVideoRequest->IsComplete()) {
      complete = false;
    }
    if (hasAudio && !mIndexLoad->mAudioRequest->IsComplete()) {
      complete = false;
    }
    if (!complete) {
      return true;
    }
    if (hasVideo) {
      memcpy(&mVideoInfo, buffer, sizeof(THPVideoInfo));
      mVideoInfo.mXSize = CBasics::SwapBytes(static_cast< uint >(mVideoInfo.mXSize));
      mVideoInfo.mYSize = CBasics::SwapBytes(static_cast< uint >(mVideoInfo.mYSize));
    }
    if (hasAudio) {
      memcpy(&mAudioInfo, buffer + 32, sizeof(THPAudioInfo));
      mAudioInfo.mSndChannels = CBasics::SwapBytes(static_cast< uint >(mAudioInfo.mSndChannels));
      mAudioInfo.mSndFrequency = CBasics::SwapBytes(static_cast< uint >(mAudioInfo.mSndFrequency));
      mAudioInfo.mSndNumSamples =
          CBasics::SwapBytes(static_cast< uint >(mAudioInfo.mSndNumSamples));
    }
  }
  }

  mIndexLoad = nullptr;
#ifdef TARGET_PC
  mTextures.reserve(kPortMovieSlots);
#else
  mTextures.reserve(3);
#endif
  mNextReadOff = mHeader.mMovieDataOffsets;
  mNextReadSize = mHeader.mFirstFrameSize;
  mReadSizeWrapped = mHeader.mFirstFrameSize;
  mReadOffWrapped = mHeader.mMovieDataOffsets;
  mTotalSeconds = mHeader.mNumFrames / mHeader.mFrameRate;
  if (mPreLoadSeconds < 0.f) {
    mPreLoadSeconds = mTotalSeconds;
    mPreLoadFrames = mHeader.mNumFrames;
  } else if (mPreLoadSeconds > 0.f) {
    mPreLoadFrames = rstl::min_val(static_cast< uint >(mHeader.mNumFrames),
                                   static_cast< uint >(mPreLoadSeconds * mHeader.mFrameRate));
    mPreLoadSeconds = rstl::min_val(mPreLoadSeconds, mTotalSeconds);
  }
  if (mPreLoadFrames > 0) {
    mRequestQueue.reserve(mPreLoadFrames);
  }
  PostDVDReadRequestIfNeeded();
  return false;
}

CMoviePlayer::~CMoviePlayer() {
  CancelReadRequests();
#ifdef TARGET_PC
  if (sPortHoldPlayer == this) {
    sPortHoldPlayer = nullptr;
  }
  // A movie allocated at this address later must not take over this one's reader state.
  if (sPortMix.mOwner == this) {
    sPortMix = SPortMovieMix();
  }
#endif
#ifdef TARGET_PC
  DropMovieReadOwner(this);
#endif
  --sNumReferences;
  VerifyCallbackStatus();
  if (sNumReferences == 0 && ShouldEnableLockedCache()) {
    LCDisable();
  }
  if (sAudioPlayer == this) {
    sAudioPlayer = nullptr;
  }
}

void CMoviePlayer::InitializeTextures() {
#ifdef TARGET_PC
  // Port (MP1 6b7384a85): the decoder writes whole 8x4 texture tiles, which a size the disc never
  // uses (900 lines: 450 of chroma) does not divide into.
  const uint chromaWidth = (mVideoInfo.mXSize + 1) / 2;
  const uint chromaHeight = (mVideoInfo.mYSize + 1) / 2;
  const uint ySize = ((mVideoInfo.mXSize + 7) & ~7) * ((mVideoInfo.mYSize + 3) & ~3);
  const uint uvSize = ((chromaWidth + 7) & ~7) * ((chromaHeight + 3) & ~3);
#else
  const uint ySize = OSRoundUp32B(mVideoInfo.mXSize * mVideoInfo.mYSize);
  const uint uvSize = OSRoundUp32B(mVideoInfo.mXSize * mVideoInfo.mYSize / 4);
#endif
  const uint audioSize = mHeader.mAudioMaxSamples * 4;
  for (int i = 0; i < mTextures.capacity(); ++i) {
#ifdef TARGET_PC
    // Host memory (kPortMovieSlots): the auto_ptrs' delete routes it back by address.
    void* y = ::operator new(ySize);
    void* u = ::operator new(uvSize);
    void* v = ::operator new(uvSize);
    void* audio = ::operator new(audioSize);
#else
    void* y = CMemory::Alloc(ySize, IAllocator::kHI_RoundUpLen);
    void* u = CMemory::Alloc(uvSize, IAllocator::kHI_RoundUpLen);
    void* v = CMemory::Alloc(uvSize, IAllocator::kHI_RoundUpLen);
    void* audio = CMemory::Alloc(audioSize, IAllocator::kHI_RoundUpLen);
#endif
    DCFlushRangeNoSync(y, ySize);
    DCFlushRangeNoSync(u, uvSize);
    DCFlushRangeNoSync(v, uvSize);
    DCFlushRangeNoSync(audio, audioSize);
    mTextures.push_back_unsafe(CTHPTextureSet(y, u, v, audio));
  }

  PPCSync();
  mDecodedTexSlot = 0;
  mDrawTexSlot = -1;
  mAudioSlot = -1;
}

void CMoviePlayer::PostDVDReadRequestIfNeeded() {
  CInterruptGuard interrupts;
  if (mCurLoadFrame < mHeader.mNumFrames) {
    bool usedPrefetch = false;
    if (!mPrefetchRequest.null()) {
      if (mPrefetchOff == mNextReadOff && mNextReadSize == mPrefetchSize &&
          mPrefetchFrame == mCurLoadFrame) {
        mRequestBuffer = rstl::auto_ptr< uchar >(mPrefetchBuffer.release());
        mRequest = mPrefetchRequest;
        if (mRequest->IsComplete()) {
          PrefetchNextFrame();
        }
        usedPrefetch = true;
      } else {
        mPrefetchRequest = nullptr;
        mPrefetchBuffer = nullptr;
        mPrefetchFrame = mHeader.mNumFrames;
      }
    }
    if (!usedPrefetch) {
      mRequestBuffer = rstl::auto_ptr< uchar >(
          static_cast< uchar* >(CMemory::Alloc(mNextReadSize, IAllocator::kHI_RoundUpLen)));
      rstl::single_ptr< CRealDvdRequest > request(rs_new CRealDvdRequest);
      DVDOpen(const_cast< char* >(mDvdFile.GetFilename().data()), &request->FileInfo());
#ifdef TARGET_PC
      SetMovieReadOwner(&request->FileInfo(), this);
#else
      request->FileInfo().cb.userData = this;
#endif
      DVDReadAsyncPrio(&request->FileInfo(), mRequestBuffer.get(), mNextReadSize, mNextReadOff,
                       DVDCallback, 2);
      mRequest = request;
    }
  }
}

void CMoviePlayer::DVDCallback(s32 result, DVDFileInfo* info) {
#ifdef TARGET_PC
  CMoviePlayer* player = TakeMovieReadOwner(info);
  if (result == 10 || player == nullptr) {
    return;
  }
  gDvdActivity = true;
  player->HandleDVDInterrupt(info);
#else
  if (result == 10) {
    return;
  }
  DCInvalidateRange(info->cb.addr, info->cb.length);
  gDvdActivity = true;
  static_cast< CMoviePlayer* >(info->cb.userData)->HandleDVDInterrupt(info);
#endif
}

void CMoviePlayer::HandleDVDInterrupt(DVDFileInfo* info) {
  if (!mPrefetchRequest.null() || mRequest.null() || &mRequest->FileInfo() != info) {
    return;
  }
  PrefetchNextFrame();
}

void CMoviePlayer::PrefetchNextFrame() {
  mPrefetchFrame = mCurLoadFrame + 1;
  if (mPrefetchFrame != mHeader.mNumFrames) {
#ifdef TARGET_PC
    mPrefetchSize =
        CBasics::SwapBytes(*reinterpret_cast< const uint* >(mRequestBuffer.get()));
#else
    mPrefetchSize = *reinterpret_cast< const uint* >(mRequestBuffer.get());
#endif
    mPrefetchOff = mNextReadOff + mNextReadSize;
    mPrefetchBuffer =
        static_cast< uchar* >(CMemory::Alloc(mPrefetchSize, IAllocator::kHI_RoundUpLen));
    rstl::single_ptr< CRealDvdRequest > request(rs_new CRealDvdRequest);
    DVDOpen(const_cast< char* >(mDvdFile.GetFilename().data()), &request->FileInfo());
#ifdef TARGET_PC
    SetMovieReadOwner(&request->FileInfo(), this);
#else
    request->FileInfo().cb.userData = this;
#endif
    DVDReadAsyncPrio(&request->FileInfo(), mPrefetchBuffer.get(), mPrefetchSize, mPrefetchOff,
                     DVDCallback, 2);
    mPrefetchRequest = request;
  }
}

void CMoviePlayer::ReadCompleted() {
  CInterruptGuard interrupts;
  mRequest = nullptr;
  if (mCurLoadFrame == mRequestQueue.size() && mPreLoadFrames > mCurLoadFrame) {
    mRequestQueue.push_back_unsafe(mRequestBuffer);
    mCachedBytes += mNextReadSize;
  }
  mNextReadOff += mNextReadSize;
  mNextReadSize = CBasics::SwapBytes(*reinterpret_cast< const uint* >(mRequestBuffer.get()));
  ++mCurLoadFrame;
  if (mCurLoadFrame == mPreLoadFrames) {
    if (mCurLoadFrame == mHeader.mNumFrames) {
      mReadSizeWrapped = mHeader.mFirstFrameSize;
      mReadOffWrapped = mHeader.mMovieDataOffsets;
    } else {
      mReadSizeWrapped = mNextReadSize;
      mReadOffWrapped = mNextReadOff;
    }
  }
  if (mCurLoadFrame >= mHeader.mNumFrames && mLoop) {
    mNextReadOff = mReadOffWrapped;
    mNextReadSize = mReadSizeWrapped;
    mCurLoadFrame = mPreLoadFrames;
  }
}

void CMoviePlayer::DecodeFromRead(const void* ptr) {
  uchar work[4096 + 32];
  void* alignedWork = reinterpret_cast< void* >((reinterpret_cast< uintptr_t >(work) + 31) & ~31);
  if (mTextures.empty()) {
    InitializeTextures();
  }
  CTHPTextureSet& texture = mTextures[mDecodedTexSlot];
  const uint* sizes = static_cast< const uint* >(ptr) + 2;
  const uchar* data = static_cast< const uchar* >(ptr) + 8 + mThpComponents.mNumComponents * 4;
  uint offset = 0;
  texture.SetAudioSamplesConsumed(0);
  texture.SetAudioSamples(0);
  for (uint i = 0; i < mThpComponents.mNumComponents; ++i) {
    if (mThpComponents.mFrameComp[i] == 0) {
      THPVideoDecode(const_cast< uchar* >(data + offset), texture.Y(), texture.U(), texture.V(),
                     alignedWork);
    } else if (mThpComponents.mFrameComp[i] == 1) {
      const uint samples = THPAudioDecode(static_cast< short* >(texture.Audio()),
                                          const_cast< uchar* >(data + offset), 0);
      const BOOL interrupts = OSDisableInterrupts();
      texture.SetAudioSamples(samples);
      texture.SetAudioSamplesConsumed(0);
      OSRestoreInterrupts(interrupts);
    }
    offset += CBasics::SwapBytes(*sizes++);
  }
  if (++mDecodedTexSlot == mTextures.size()) {
    mDecodedTexSlot = 0;
  }
}

void CMoviePlayer::Update(float dt) {
  if (mCurLoadFrame < mPreLoadFrames) {
    if (!mRequest.null() && mRequest->IsComplete()) {
      ReadCompleted();
      if (mCurLoadFrame >= mRequestQueue.size() && mCurLoadFrame < mPreLoadFrames &&
          mRequestQueue.size() < mHeader.mNumFrames) {
        PostDVDReadRequestIfNeeded();
      }
    }
  } else if (!mRequest.null()) {
#ifdef TARGET_PC
    // Port: catch up on every read that has arrived, not one frame per update, so the decoder
    // gets (and stays) MOVIE_DECODE_AHEAD frames ahead.
    while (!mRequest.null() && mDecodedTexCount < MOVIE_DECODE_AHEAD &&
           mRequestFrameWrapped >= mRequestQueue.size() && mCurLoadFrame >= mRequestQueue.size() &&
           mRequest->IsComplete()) {
#else
    const bool canDecode =
        mRequestFrameWrapped >= mRequestQueue.size() && mCurLoadFrame >= mRequestQueue.size();
    if (mDecodedTexCount < 2 && canDecode && mRequest->IsComplete()) {
#endif
      ReadCompleted();
      rstl::auto_ptr< uchar > buffer;
      if (mCurLoadFrame < mHeader.mNumFrames) {
        buffer = mRequestBuffer;
      } else {
        buffer = rstl::auto_ptr< uchar >(mRequestBuffer.get());
        buffer.release();
      }
      PostDVDReadRequestIfNeeded();
      DecodeFromRead(buffer.get());
      ++mDecodedTexCount;
      ++mRequestFrameWrapped;
      if (mRequestFrameWrapped >= mHeader.mNumFrames && mLoop) {
        mRequestFrameWrapped = 0;
      }
    }
  }
  if (mRequest.null() && mPlayMode == kPM_Playing && mRequestQueue.size() < mHeader.mNumFrames) {
    PostDVDReadRequestIfNeeded();
  }
  if (mDecodedTexCount < MOVIE_DECODE_AHEAD && mPlayMode == kPM_Playing &&
      mRequestFrameWrapped < mPreLoadFrames) {
    const int frame = rstl::min_val(mRequestFrameWrapped, mRequestQueue.size() - 1);
    if (frame == -1) {
      return;
    }
    DecodeFromRead(mRequestQueue[frame].get());
    ++mDecodedTexCount;
    ++mRequestFrameWrapped;
    if (mRequestFrameWrapped >= mHeader.mNumFrames && mLoop) {
      mRequestFrameWrapped = 0;
    }
  }
#ifdef TARGET_PC
  // Port: start the picture (and with it the audio reader) only once the decoder is ahead, so the
  // reader's ~64 ms lead is decoded from the first frame. A movie shorter than that, or reads that
  // stall, start anyway after a few updates.
  if (mDrawTexSlot == -1 && mPlayMode == kPM_Playing && mHasAudio) {
    const bool moreToDecode = !mRequest.null() || mRequestFrameWrapped < mPreLoadFrames;
    if (sPortHoldPlayer != this) {
      sPortHoldPlayer = this;
      sPortHoldUpdates = 0;
    }
    if (mDecodedTexCount < MOVIE_DECODE_AHEAD && moreToDecode && sPortHoldUpdates < 30) {
      ++sPortHoldUpdates;
      return;
    }
  }
#endif
  if (mDecodedTexCount > 0 && mPlayMode == kPM_Playing) {
    mCurSeconds += dt;
    if (mLoop) {
      mCurSeconds = CMath::ModF(mCurSeconds, mTotalSeconds);
    } else {
      mCurSeconds = rstl::min_val(mCurSeconds, mTotalSeconds);
    }
    float remainder = mFrameRem - dt;
    const float frameDt = 1.f / mHeader.mFrameRate;
    if (remainder <= 0.f) {
      if (!mFieldFlip) {
        if (++mDrawTexSlot >= mTextures.size()) {
          mDrawTexSlot = 0;
        }
        const BOOL interrupts = OSDisableInterrupts();
        if (mAudioSlot == -1) {
          mAudioSlot = 0;
        }
        OSRestoreInterrupts(interrupts);
        --mDecodedTexCount;
        ++mCurFrame;
        if (mCurFrame == mHeader.mNumFrames && mLoop) {
          mCurFrame = 0;
        }
        remainder += frameDt;
        mFieldIndex = 0;
      } else {
        remainder += dt;
        mFieldFlip = false;
      }
    }
    mFrameRem = remainder;
  }
}

void CMoviePlayer::DrawFrame(const CVector3f& v1, const CVector3f& v2, const CVector3f& v3,
                             const CVector3f& v4) {
  if (mDrawTexSlot == -1) {
    return;
  }
  CGraphics::SetUseVideoFilter(mDeinterlace);
  const BOOL interrupts = OSDisableInterrupts();
  sAudioPlayer = this;
  OSRestoreInterrupts(interrupts);
  CTHPTextureSet& texture = mTextures[mDrawTexSlot];
  const bool field = CGraphics::GetDolphinLastFrameAbove();
#ifdef TARGET_PC
  // Port: the host is progressive, so the interlace-pattern path (every other line, field chosen by
  // the last frame's parity, which never alternates here) leaves a 30 fps movie half its lines.
  // USA's openPartOne is 59.94 fps and never took it; PAL's is 29.97 and showed scanline stripes.
  MyTHPGXYuv2RgbSetup(field, true);
#else
  MyTHPGXYuv2RgbSetup(field, mDeinterlace || mIs60Hz);
#endif
  MyTHPYuv2RgbTextureSetup(texture.Y(), texture.U(), texture.V(), mVideoInfo.mXSize,
                           mVideoInfo.mYSize);
  CGX::Begin(GX_TRIANGLEFAN, GX_VTXFMT7, 4);
  GXPosition3f32(v1.GetX(), v1.GetY(), v1.GetZ());
  GXTexCoord2u16(0, 0);
  GXPosition3f32(v3.GetX(), v3.GetY(), v3.GetZ());
  GXTexCoord2u16(0, 1);
  GXPosition3f32(v4.GetX(), v4.GetY(), v4.GetZ());
  GXTexCoord2u16(1, 1);
  GXPosition3f32(v2.GetX(), v2.GetY(), v2.GetZ());
  GXTexCoord2u16(1, 0);
  CGX::End();
  MyTHPGXRestore();
#ifdef TARGET_PC
  // Port: the host is progressive and VIGetNextField is always 0, so the flip below would hold
  // every frame of a 30 fps movie for an extra game frame (video and audio 1.5x slow). On the
  // console fields alternate and it fires at most once, to line the movie up with a field.
#else
  if (mFieldIndex == 0 && !field && !mIs60Hz) {
    mFieldFlip = true;
  }
#endif
  ++mFieldIndex;
}

void CMoviePlayer::SetPlayMode(const EPlayMode mode) { mPlayMode = mode; }

float CMoviePlayer::GetTotalSeconds() const { return mTotalSeconds; }

float CMoviePlayer::GetPlayedSeconds() const { return mCurSeconds + mFrameRem; }

bool CMoviePlayer::GetIsFullyCached() const { return mRequestQueue.size() >= mPreLoadFrames; }

bool CMoviePlayer::GetIsMovieFinishedPlaying() const {
  return !mLoop && mCurFrame == mHeader.mNumFrames;
}

void CMoviePlayer::Rewind() {
  CancelReadRequests();
  mRequestBuffer = rstl::auto_ptr< uchar >(nullptr);
  mPrefetchBuffer = nullptr;
  mNextReadSize = mHeader.mFirstFrameSize;
  mNextReadOff = mHeader.mMovieDataOffsets;
  mReadSizeWrapped = mHeader.mFirstFrameSize;
  mReadOffWrapped = mHeader.mMovieDataOffsets;
  mCurLoadFrame = 0;
  mRequestFrameWrapped = 0;
  mCurFrame = 0;
  mDecodedTexSlot = 0;
  mDrawTexSlot = -1;
  mAudioSlot = -1;
  mDecodedTexCount = 0;
  mFrameRem = 0.f;
  mCurSeconds = 0.f;
#ifdef TARGET_PC
  if (sPortHoldPlayer == this) {
    sPortHoldPlayer = nullptr;
  }
  // A rewound movie starts its reader afresh.
  if (sPortMix.mOwner == this) {
    sPortMix = SPortMovieMix();
  }
#endif
  mTextures.clear();
}

#ifdef TARGET_PC
// Port: the SDK's AIGetDMAStartAddr truncates the host's 64-bit buffer pointer (ai_dma.cpp).
extern "C" uintptr_t AIPortGetDMAStartAddr(void);

#endif
void CMoviePlayer::StaticMyAudioCallback() {
  if (sAudioPlayer != nullptr && sAudioPlayer->mHasAudio) {
#ifdef TARGET_PC
    curAudioBuffer = reinterpret_cast< const short* >(AIPortGetDMAStartAddr());
#else
    curAudioBuffer = static_cast< const short* >(OSPhysicalToCached(AIGetDMAStartAddr()));
#endif
    soundBufferIndex ^= 1;
    short* buffer = soundBuffer[soundBufferIndex];
    AIInitDMA(reinterpret_cast< uintptr_t >(buffer), sizeof(soundBuffer[0]));
    const BOOL interrupts = OSEnableInterrupts();
    if (curAudioBuffer != nullptr) {
      DCInvalidateRange(const_cast< short* >(curAudioBuffer), sizeof(soundBuffer[0]));
    }
    sAudioPlayer->MixAudio(buffer, curAudioBuffer, 160);
    DCFlushRange(buffer, sizeof(soundBuffer[0]));
    OSRestoreInterrupts(interrupts);
  }
}

void CMoviePlayer::MixAudio(short* out, const short* in, unsigned long samples) {
  const short* input = in;
  if (mAudioSlot == -1) {
#ifdef TARGET_PC
    if (sPortMix.mOwner == this) {
      sPortMix.mRunning = false;
    }
#endif
    if (in != nullptr) {
      memcpy(out, in, samples * 4);
    } else {
      memset(out, 0, samples * 4);
    }
    return;
  }
  const uchar volume = rstl::min_val(127, mVolume * sSfxVolume * 100 >> 14);
  const ushort attenuation =
      sAudioEnabled ? static_cast< ushort >(CAudioSys::GetScaledVolume(volume)) : 0;
#ifdef TARGET_PC
  {
    SPortMovieMix& mix = sPortMix;
    const int slots = static_cast< int >(mTextures.size());
    // Takes the next decoded stereo sample, moving on to the next slot once one is used up; a
    // slot with nothing left is one the decoder has not reached yet, and the reader waits there.
    struct Reader {
      CMoviePlayer& player;
      bool Pull(short* lr) {
        CTHPTextureSet& texture = player.mTextures[player.mAudioSlot];
        const uint consumed = texture.GetAudioSamplesConsumed();
        const uint total = texture.GetAudioSamples();
        if (consumed >= total) {
          return false;
        }
        const short* audio = static_cast< const short* >(texture.Audio()) + consumed * 2;
        lr[0] = audio[0];
        lr[1] = audio[1];
        texture.SetAudioSamplesConsumed(consumed + 1);
        if (consumed + 1 == total && ++player.mAudioSlot == player.mTextures.size()) {
          player.mAudioSlot = 0;
        }
        return true;
      }
    } reader = {*this};
    if (mix.mOwner != this || !mix.mRunning) {
      mix = SPortMovieMix();
      mix.mOwner = this;
      mix.mRunning = true;
      reader.Pull(mix.mPrev);
      mix.mCur[0] = mix.mPrev[0];
      mix.mCur[1] = mix.mPrev[1];
      reader.Pull(mix.mCur);
    }

    uint avail = 0;
    for (int i = 0, slot = mAudioSlot; i < slots; ++i, slot = (slot + 1) % slots) {
      const uint left = mTextures[slot].GetAudioSamples() - mTextures[slot].GetAudioSamplesConsumed();
      if (left == 0) {
        break;
      }
      avail += left;
    }
    // Aim for the decoded audio the reader would have ahead of it if both clocks agreed: what the
    // decoder keeps ahead of the picture less the reader's own lead (the AI queue, ~64 ms, plus a
    // device period), so the movie's sound stays in step with its picture.
    const float rate = static_cast< float >(mAudioInfo.mSndFrequency != 0 ? mAudioInfo.mSndFrequency : 32000);
    const float perFrame = rate / (mHeader.mFrameRate > 1.f ? mHeader.mFrameRate : 30.f);
    const float target = rstl::max_val(perFrame, MOVIE_DECODE_AHEAD * perFrame - 0.084f * rate);
    mix.mAvailAvg = mix.mAvailAvg < 0.f ? static_cast< float >(avail)
                                        : mix.mAvailAvg + (static_cast< float >(avail) - mix.mAvailAvg) * 0.005f;
    float adjust = (mix.mAvailAvg - target) / target * 0.02f;
    adjust = adjust > 0.005f ? 0.005f : adjust < -0.005f ? -0.005f : adjust;
    const uint step = static_cast< uint >(65536.f * (1.f + adjust));

    for (unsigned long i = 0; i < samples; ++i) {
      mix.mFrac += step;
      while (mix.mFrac >= 0x10000) {
        mix.mFrac -= 0x10000;
        mix.mPrev[0] = mix.mCur[0];
        mix.mPrev[1] = mix.mCur[1];
        // Nothing decoded (a stall, or the movie's end): fade out rather than step to zero.
        if (!reader.Pull(mix.mCur)) {
          mix.mCur[0] = static_cast< short >(mix.mCur[0] * 7 / 8);
          mix.mCur[1] = static_cast< short >(mix.mCur[1] * 7 / 8);
        }
      }
      for (int ch = 0; ch < 2; ++ch) {
        const int interp =
            mix.mPrev[ch] + ((static_cast< int >(mix.mCur[ch]) - mix.mPrev[ch]) * static_cast< int >(mix.mFrac >> 1) >> 15);
        int sample = (attenuation * interp) >> 15;
        if (input != nullptr) {
          sample += *input++;
        }
        *out++ = static_cast< short >(sample < -32768 ? -32768 : sample > 32767 ? 32767 : sample);
      }
    }
    return;
  }
#endif
  for (int frame = 0; samples != 0 && frame < 3; ++frame) {
    CTHPTextureSet& texture = mTextures[mAudioSlot];
    uint count = texture.GetAudioSamples() - texture.GetAudioSamplesConsumed();
    if (count > samples) {
      count = samples;
    } else {
      if (++mAudioSlot == mTextures.size()) {
        mAudioSlot = 0;
      }
    }
    const uint consumed = texture.GetAudioSamplesConsumed();
    const short* audio = static_cast< short* >(texture.Audio()) + consumed * 2;
    texture.SetAudioSamplesConsumed(count + consumed);
    if (in != nullptr) {
      for (uint i = 0; i < count * 2; ++i) {
        int sample = *input + ((attenuation * *audio) >> 15);
        if (sample < -32768) {
          sample = -32768;
        } else if (sample > 32767) {
          sample = 32767;
        }
        *out = sample;
        ++out;
        ++input;
        ++audio;
      }
    } else {
      for (uint i = 0; i < count * 2; ++i) {
        int sample = (attenuation * *audio) >> 15;
        if (sample < -32768) {
          sample = -32768;
        } else if (sample > 32767) {
          sample = 32767;
        }
        *out = sample;
        ++out;
        ++audio;
      }
    }
    samples -= count;
  }
  if (samples != 0) {
    if (in != nullptr) {
      memcpy(out, input, samples * 4);
    } else {
      memset(out, 0, samples * 4);
    }
  }
}

void CMoviePlayer::VerifyCallbackStatus() {
  if (sNumReferences > 0) {
    CRSFAudio::RunDMACallback(StaticMyAudioCallback);
  } else {
    CRSFAudio::CancelDMACallback(StaticMyAudioCallback);
  }
}

uint CMoviePlayer::GetWidth() const { return mVideoInfo.mXSize; }

uint CMoviePlayer::GetHeight() const { return mVideoInfo.mYSize; }

#ifdef TARGET_PC
void CMoviePlayer::PortGetMargins(int vpWidth, int vpHeight, int& xMargin, int& yMargin) const {
  const int width = static_cast< int >(mVideoInfo.mXSize);
  const int height = static_cast< int >(mVideoInfo.mYSize);
  if ((width == 640 && height == 480) || width <= 0 || height <= 0 || vpWidth <= 0 ||
      vpHeight <= 0) {
    xMargin = (width - vpWidth) / 2;
    yMargin = (height - vpHeight) / 2;
    // Port (MP1 10d237a43): narrower than the movie's own 4:3 (a 640x480 viewport is 4:3), draw
    // it at the viewport's width, centred, rather than at 640 wide, which crops it.
    if (width > 0 && height > 0 && vpWidth > 0 && vpHeight > 0 &&
        vpWidth * height < vpHeight * width) {
      const int drawnHeight = vpWidth * height / width;
      xMargin = 0;
      yMargin = (drawnHeight - vpHeight) / 2;
    }
    return;
  }
  // The 4:3 mode shows its 640x448 at 4:3; the wider ones have square pixels.
  const float view = PortOverlay::AspectMode() == PortOverlay::kAspect_4_3
                         ? 4.f / 3.f
                         : static_cast< float >(vpWidth) / static_cast< float >(vpHeight);
  const float movie = static_cast< float >(width) / static_cast< float >(height);
  int drawnWidth = vpWidth;
  int drawnHeight = vpHeight;
  if (movie > view) {
    drawnHeight = static_cast< int >(static_cast< float >(vpHeight) * view / movie + 0.5f);
  } else {
    drawnWidth = static_cast< int >(static_cast< float >(vpWidth) * movie / view + 0.5f);
  }
  xMargin = (drawnWidth - vpWidth) / 2;
  yMargin = (drawnHeight - vpHeight) / 2;
}
#endif

void CMoviePlayer::SetAudioEnabled(bool enabled) { sAudioEnabled = enabled; }

bool CMoviePlayer::GetAudioEnabled() { return sAudioEnabled; }

void CMoviePlayer::SetSfxVolume(uchar volume) { sSfxVolume = rstl::min_val(uchar(127), volume); }

CMoviePlayer::EPlayMode CMoviePlayer::GetPlayMode() const { return mPlayMode; }

void CMoviePlayer::DrawFrame(int left, int right, int bottom, int top) {
  const float l = left;
  const float r = right;
  const float b = bottom;
  const float t = top;
  CVector3f v1(l, 0.f, t);
  CVector3f v2(r, 0.f, t);
  CVector3f v3(l, 0.f, b);
  CVector3f v4(r, 0.f, b);
  DrawFrame(v1, v2, v3, v4);
}

void CMoviePlayer::CancelReadRequests() {
  rstl::single_ptr< CRealDvdRequest > prefetch;
  rstl::single_ptr< CRealDvdRequest > request;
  {
    CInterruptGuard interrupts;
    prefetch = mPrefetchRequest;
    request = mRequest;
  }
  if (!prefetch.null()) {
    prefetch->PostCancelRequest();
    prefetch = nullptr;
  }
  if (!request.null()) {
    request->PostCancelRequest();
    request = nullptr;
  }
}
