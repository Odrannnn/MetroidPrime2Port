#include "Kyoto/CMemoryCardSys.hpp"

#include "Kyoto/CCRC32.hpp"
#include "Kyoto/Graphics/CGraphicsPalette.hpp"
#include "Kyoto/Graphics/CTexture.hpp"

#include "dolphin/os/OSCache.h"

#include "Kyoto/MemoryCopy.hpp"
#ifdef TARGET_PC
#include "port_entry.h"
#include "port_save.h"
#include "port_save_framing.h"

#ifdef TARGET_PC
#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_video.h>
#include <aurora/card.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>
#endif
#endif

#ifdef TARGET_PC
namespace {
// The card base Aurora mounted at CARDInit (the port's own card), and the one mounted now.
std::string sDefaultCardBase;
std::string sCardBase;

// Aurora's mounted path is <base>/<region>/Card A (GCI folder) or <base>/MemoryCardA.<region>.raw.
std::string BaseOfMountedCard() {
  char buffer[1024];
  const size_t size = aurora_card_get_path(port::entry::CurrentGameCode(), aurora_card_get_type(0),
                                           0, buffer, sizeof(buffer));
  if (size == 0 || size > sizeof(buffer)) {
    return {};
  }
  std::filesystem::path path(buffer);
  if (aurora_card_get_type(0) == AURORA_CARD_GCI_DIRECTORY) {
    path = path.parent_path();
  }
  return path.parent_path().string();
}
} // namespace
#endif

static uint RoundUp(uint value, uint align) { return (value + align - 1) & ~(align - 1); }

CMemoryCardSys::CCardFileInfo::Icon::Icon(CAssetId id, int speed, CSimplePool& pool)
: mId(id), mSpeed(speed), mTex(pool.GetObj(SObjectTag('TXTR', id))) {}

CMemoryCardSys::CCardFileInfo::SSaveSlot::SSaveSlot() : mGeneration(0), mCorrupt(false) {}

void CMemoryCardSys::CCardFileInfo::SSaveSlot::CheckCrc() {
#ifdef TARGET_PC
  uint generation = 0;
  mCorrupt = !PortSave::Framing::ReadSlot(mData.data(), mData.size(), generation);
  mGeneration = generation;
#else
  const uint* data = reinterpret_cast< const uint* >(mData.data());
  const uint crc = data[0];
  mCorrupt = CCRC32::Calculate(data + 1, mData.size() - 4, 0xFFFFFFFF) != crc;
  mGeneration = data[1];
#endif
}

CMemoryCardSys::CCardFileInfo::CCardFileInfo(EMemoryCardPort port, const rstl::string& name)
: mSlotBlocks(0)
, mAlignedUserDataSize(0)
, mSlot(0)
, mGeneration(0)
, mState(kS_Standby)
, mNewFile(false)
, mFileName(name)
, x4c_(0)
, mBannerTex(kInvalidAssetId)
, mHeaderBuffer(0x2000, uchar())
, mSlots(2, SSaveSlot()) {
  mFileInfo.chan = port;
  mFileInfo.fileNo = -1;
}

CMemoryCardSys::CCardFileInfo::~CCardFileInfo() {
  if (mFileInfo.fileNo != -1) {
    CARDClose(&mFileInfo);
  }
}

ECardResult CMemoryCardSys::CCardFileInfo::Open() {
  const EMemoryCardPort port = GetPort();
  ECardResult result = static_cast< ECardResult >(CARDOpen(port, mFileName.data(), &mFileInfo));
  mFileInfo.chan = port;
  if (result == kCR_READY) {
    CardStat stat;
    ECardResult statResult = CMemoryCardSys::GetStatus(GetPort(), GetFileNo(), stat);
    if (statResult != kCR_READY) {
      return statResult;
    }
    const int blocks = stat.GetFileLength() / 0x2000;
    mSlotBlocks = (blocks - 1) / 2;
    mAlignedUserDataSize = mSlotBlocks * 0x2000;
  }
  return result;
}

ECardResult CMemoryCardSys::CCardFileInfo::CreateFile() {
  mNewFile = true;
  mAlignedUserDataSize = RoundUp(mUserDataWrite.size(), 0x2000);
  mSlotBlocks = mAlignedUserDataSize / 0x2000;
  const int size = GetTotalNumBlocks() * 0x2000;
  return static_cast< ECardResult >(
      CARDCreateAsync(GetPort(), mFileName.data(), size, &mFileInfo, nullptr));
}

ECardResult CMemoryCardSys::CCardFileInfo::StartRead() {
  CardStat stat;
  ECardResult result = CMemoryCardSys::GetStatus(GetPort(), GetFileNo(), stat);
  if (result != kCR_READY) {
    return result;
  }
  const uint size = stat.GetFileLength();
  mLoadedData = rstl::vector< uchar >();
  mHeaderBuffer.assign(0x2000);
  mSlots[0].mData.assign(mAlignedUserDataSize);
  mSlots[1].mData.assign(mAlignedUserDataSize);
  result = static_cast< ECardResult >(CARDRead(&mFileInfo, mHeaderBuffer.data(), 0x2000, 0));
  if (result == kCR_READY) {
    mState = kS_ReadHeader;
  }
  return result;
}

ECardResult CMemoryCardSys::CCardFileInfo::PumpCardRead() {
  if (mState == kS_Standby) {
    return kCR_READY;
  }
  ECardResult result = GetResultCode(GetPort());
  if (result != kCR_READY) {
    return result;
  }
  if (mState == kS_ReadHeader || mState == kS_RepairHeader) {
    if (mState == kS_ReadHeader && CheckHeaderCrc() != kCR_READY) {
      BuildHeaderBuffer();
      mState = kS_RepairHeader;
      void* data = mHeaderBuffer.data();
      DCStoreRange(data, 0x2000);
      result = static_cast< ECardResult >(CARDWriteAsync(&mFileInfo, data, 0x2000, 0, nullptr));
      if (result == kCR_READY) {
        return kCR_BUSY;
      }
      return result;
    } else {
      mHeaderBuffer = rstl::vector< uchar, rstl::aligned_allocator >();
      mState = kS_ReadSlotA;
      result = static_cast< ECardResult >(
          CARDRead(&mFileInfo, mSlots[0].mData.data(), mAlignedUserDataSize, 0x2000));
      if (result == kCR_READY) {
        return kCR_BUSY;
      }
      return result;
    }
  } else if (mState == kS_ReadSlotA) {
    mState = kState_ReadingData1;
    result = static_cast< ECardResult >(CARDRead(
        &mFileInfo, mSlots[1].mData.data(), mAlignedUserDataSize, mAlignedUserDataSize + 0x2000));
    if (result == kCR_READY) {
      return kCR_BUSY;
    }
    return result;
  } else {
    return SelectSaveSlot();
  }
}

ECardResult CMemoryCardSys::CCardFileInfo::WriteSaveSlot(int slot) {
  const int offset = slot * mAlignedUserDataSize + 0x2000;
  void* data = mSlots[mSlot].mData.data();
  DCStoreRange(data, mAlignedUserDataSize);
  return static_cast< ECardResult >(
      CARDWriteAsync(&mFileInfo, data, mAlignedUserDataSize, offset, nullptr));
}

ECardResult CMemoryCardSys::CCardFileInfo::WriteFile() {
#ifdef TARGET_PC
  // The port's card backup (MP2_CARD_BACKUP, F1 > Saves): a copy of the file as it is on disk
  // before this write replaces it. A no-op unless the setting is on.
  PortSave::BeforeCardWrite(mFileName.data());
#endif
  ECardResult result;
  BuildSaveSlot();
  if (mNewFile) {
    BuildHeaderBuffer();
    void* data = mHeaderBuffer.data();
    DCStoreRange(data, 0x2000);
    result = static_cast< ECardResult >(CARDWriteAsync(&mFileInfo, data, 0x2000, 0, nullptr));
    mState = kS_WriteHeader;
  } else {
    result = WriteSaveSlot(mSlot);
    mState = kS_WriteSlot;
  }
  return result;
}

ECardResult CMemoryCardSys::CCardFileInfo::PumpCardTransfer() {
  if (mState == kS_Standby) {
    return kCR_READY;
  }
  ECardResult result = GetResultCode(GetPort());
  if (result != kCR_READY) {
    return result;
  }
  if (mState == kS_WriteHeader) {
    mState = mNewFile ? kS_WriteFirstSlot : kS_WriteSlot;
    result = WriteSaveSlot(mSlot);
    if (result == kCR_READY) {
      return kCR_BUSY;
    }
    return result;
  } else if (mState == kS_WriteFirstSlot) {
    mState = kS_WriteSlot;
    result = WriteSaveSlot(1 - mSlot);
    if (result == kCR_READY) {
      return kCR_BUSY;
    }
    return result;
  } else if (mState == kS_WriteSlot) {
    mState = kState_WritingStatus;
    CardStat stat;
    result = GetStatus(stat);
    if (result != kCR_READY) {
      return result;
    }
    result = SetStatus(GetPort(), GetFileNo(), stat);
    if (result == kCR_READY) {
      return kCR_BUSY;
    }
    return result;
  } else {
    mState = kS_Standby;
    return kCR_READY;
  }
}

CMemoryCardSys::EMemoryCardPort CMemoryCardSys::CCardFileInfo::GetPort() {
  return static_cast< EMemoryCardPort >(mFileInfo.chan);
}

int CMemoryCardSys::CCardFileInfo::GetFileNo() { return mFileInfo.fileNo; }

int CMemoryCardSys::CCardFileInfo::GetTotalNumBlocks() { return mSlotBlocks * 2 + 1; }

void CMemoryCardSys::CCardFileInfo::ResetHeaderInfo() {
  mComment = rstl::string();
  mBannerTex = kInvalidAssetId;
  mBannerTok = rstl::optional_object< TLockedToken< CTexture > >();
  mIconToks = rstl::reserved_vector< Icon, 8 >();
}

void CMemoryCardSys::CCardFileInfo::SetComment(const rstl::string& comment) { mComment = comment; }

void CMemoryCardSys::CCardFileInfo::LockBannerToken(CAssetId bannerTxtr, CSimplePool& pool) {
  mBannerTex = bannerTxtr;
  mBannerTok = TLockedToken< CTexture >(pool.GetObj(SObjectTag('TXTR', mBannerTex)));
}

void CMemoryCardSys::CCardFileInfo::LockIconToken(CAssetId iconTxtr, int speed, CSimplePool& pool) {
  mIconToks.push_back(Icon(iconTxtr, speed, pool));
}

ECardResult CMemoryCardSys::CCardFileInfo::GetStatus(CardStat& stat) {
  ECardResult result = CMemoryCardSys::GetStatus(GetPort(), GetFileNo(), stat);
  if (result != kCR_READY) {
    return result;
  }
  stat.SetCommentAddr(4);
  stat.SetIconAddr(68);
  int bannerFormat;
  if (mBannerTex == kInvalidAssetId) {
    bannerFormat = CARD_STAT_BANNER_NONE;
  } else if ((*mBannerTok.data())->GetTexelFormat() == kTF_RGB5A3) {
    bannerFormat = CARD_STAT_BANNER_RGB5A3;
  } else {
    bannerFormat = CARD_STAT_BANNER_C8;
  }
  stat.SetBannerFormat(bannerFormat);
  const int count = mIconToks.size();
  int i = 0;
  for (; i < count; ++i) {
    int format = CARD_STAT_ICON_C8;
    if ((*mIconToks[i].mTex)->GetTexelFormat() == kTF_RGB5A3) {
      format = CARD_STAT_ICON_RGB5A3;
    }
    stat.SetIconFormat(format, i);
    stat.SetIconSpeed(mIconToks[i].mSpeed, i);
  }
  if (i < 8) {
    stat.SetIconFormat(CARD_STAT_ICON_NONE, i);
    stat.SetIconSpeed(CARD_STAT_SPEED_END, i);
  }
  return kCR_READY;
}

void CMemoryCardSys::CCardFileInfo::WriteBannerData(COutputStream& out) {
  if (mBannerTex != kInvalidAssetId) {
    const CTexture& texture = **mBannerTok.data();
    const ETexelFormat format = texture.GetTexelFormat();
    const void* data = texture.GetConstBitMapData(0);
    const uint size = format == kTF_RGB5A3 ? 6144 : 3072;
    out.Put(data, size);
    if (format == kTF_C8) {
      out.Put(texture.GetPalette()->GetPaletteData(), 512);
    }
  }
}

void CMemoryCardSys::CCardFileInfo::WriteIconData(COutputStream& out) {
  const void* palette = nullptr;
  const int count = mIconToks.size();
  for (int i = 0; i < count; ++i) {
    const CTexture& texture = **mIconToks[i].mTex;
    const ETexelFormat format = texture.GetTexelFormat();
    const void* data = texture.GetConstBitMapData(0);
    const uint size = format == kTF_RGB5A3 ? 2048 : 1024;
    out.Put(data, size);
    if (format == kTF_C8) {
      palette = texture.GetPalette()->GetPaletteData();
    }
  }
  if (palette != nullptr) {
    out.Put(palette, 512);
  }
}

void CMemoryCardSys::CCardFileInfo::BuildHeaderBuffer() {
  mHeaderBuffer.assign(0x2000);
  uint* data = reinterpret_cast< uint* >(mHeaderBuffer.data());
  {
    CMemoryStreamOut out(data, 0x2000);
    out.WriteInt32(0);
    char comment[64];
    strncpy(comment, mComment.data(), sizeof(comment));
    out.Put(comment, sizeof(comment));
    WriteBannerData(out);
    WriteIconData(out);
  }
#ifdef TARGET_PC
  PortSave::Framing::FinishBlock(mHeaderBuffer.data(), mHeaderBuffer.size());
#else
  data[0] = CCRC32::Calculate(data + 1, 0x2000 - 4, 0xFFFFFFFF);
#endif
}

void CMemoryCardSys::CCardFileInfo::BuildSaveSlot() {
  SSaveSlot& slot = mSlots[mSlot];
  slot.mData.assign(mAlignedUserDataSize);
  uint* data = reinterpret_cast< uint* >(slot.mData.data());
  (memcpy)(data + 2, mUserDataWrite.data(), mUserDataWrite.size());
#ifdef TARGET_PC
  PortSave::Framing::FinishSlot(slot.mData.data(), slot.mData.size(), mGeneration);
#else
  data[1] = mGeneration;
  data[0] = CCRC32::Calculate(data + 1, mAlignedUserDataSize - 4, 0xFFFFFFFF);
#endif
  mUserDataWrite = rstl::vector< uchar >();
}

ECardResult CMemoryCardSys::CCardFileInfo::SelectSaveSlot() {
  SSaveSlot& slotA = mSlots[0];
  SSaveSlot& slotB = mSlots[1];
  slotA.CheckCrc();
  slotB.CheckCrc();

  ECardResult result = kCR_READY;
  int slot = -1;
  if (slotA.mCorrupt) {
    if (slotB.mCorrupt) {
      result = kCR_CRC_MISMATCH;
    } else {
      slot = 1;
      mGeneration = slotB.mGeneration;
    }
  } else if (slotB.mCorrupt) {
    slot = 0;
    mGeneration = 1 - slotA.mGeneration;
  } else {
    slot = slotA.mGeneration ^ slotB.mGeneration;
    mGeneration = slot != 0 ? slotB.mGeneration : 1 - slotA.mGeneration;
  }

  mLoadedData = rstl::vector< uchar >();
  if (slot != -1) {
    mSlot = 1 - slot;
    const int size = mAlignedUserDataSize - 8;
    mLoadedData.resize(size);
    (memcpy)(mLoadedData.data(), mSlots[slot].mData.data() + 8, size);
  }
  slotA.mData = rstl::vector< uchar, rstl::aligned_allocator >();
  slotB.mData = rstl::vector< uchar, rstl::aligned_allocator >();
  return result;
}

ECardResult CMemoryCardSys::CCardFileInfo::CheckHeaderCrc() {
#ifdef TARGET_PC
  return PortSave::Framing::CheckBlockCrc(mHeaderBuffer.data(), mHeaderBuffer.size())
             ? kCR_READY : kCR_CRC_MISMATCH;
#else
  const uint* data = reinterpret_cast< const uint* >(mHeaderBuffer.data());
  const uint crc = data[0];
  return crc == CCRC32::Calculate(data + 1, 0x2000 - 4, 0xFFFFFFFF) ? kCR_READY : kCR_CRC_MISMATCH;
#endif
}

uint CardStat::GetFileLength() { return mStat.length; }

int CardStat::GetTime() const { return mStat.time; }

int CardStat::GetCommentAddr() const { return mStat.commentAddr; }

void CardStat::SetBannerFormat(int format) { CARDSetBannerFormat(&mStat, format); }

void CardStat::SetIconFormat(int format, int idx) { CARDSetIconFormat(&mStat, idx, format); }

void CardStat::SetIconSpeed(int speed, int idx) { CARDSetIconSpeed(&mStat, idx, speed); }

void CardStat::SetIconAddr(int addr) { CARDSetIconAddress(&mStat, addr); }

void CardStat::SetCommentAddr(int addr) { CARDSetCommentAddress(&mStat, addr); }

CMemoryCardSys::CMemoryCardSys() {
  Initialize();
  mIsCardSysExists = true;
}

#ifdef TARGET_PC
// Port: each release keeps its own saves, so after switching discs (USA to PAL, GameCube to Trilogy) the card looks
// empty. Offer, once per card, to copy the newest other release's save over, converted. The
// overlay's Memory card section (Restore from ...) and `gci restore` do the same later.
static void OfferRestoreFromOtherRelease() {
  if (std::getenv("MP2_NO_DISC_DIALOG") != nullptr || std::getenv("MP2_CONSOLE") != nullptr) {
    return; // scripted runs: nobody to answer
  }
  const std::vector< PortSave::OtherRelease > others = PortSave::RestoreToOffer();
  if (others.empty()) {
    return;
  }
  int windowCount = 0;
  SDL_Window** windows = SDL_GetWindows(&windowCount);
  SDL_Window* window = windows != nullptr && windowCount > 0 ? windows[0] : nullptr;
  SDL_free(windows);
  if (window == nullptr) {
    return;
  }
  const PortSave::OtherRelease& newest = others.front();
  const std::string message = "This disc has no saves yet, but there are saves from\n" + newest.label +
                              ".\n\nCopy them over? They are converted for this disc;\nthe originals stay "
                              "where they are.\n\nYou can also do it later: overlay, System tab,\nMemory card.";
  enum { kSkip, kCopy };
  const SDL_MessageBoxButtonData buttons[] = {
      {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, kCopy, "Copy saves"},
      {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, kSkip, "Start fresh"},
  };
  SDL_MessageBoxData data{};
  data.flags = SDL_MESSAGEBOX_INFORMATION;
  data.window = window;
  data.title = "Metroid Prime 2: saves from another version";
  data.message = message.c_str();
  data.numbuttons = 2;
  data.buttons = buttons;
  int answer = -1;
  if (!SDL_ShowMessageBox(&data, &answer)) {
    std::fprintf(stderr, "port: memory card: could not offer the other release's saves: %s\n",
                 SDL_GetError());
    return;
  }
  PortSave::MarkRestoreOffered();
  if (answer != kCopy) {
    return;
  }
  std::string result;
  const bool ok = PortSave::RestoreFrom(newest, result);
  std::fprintf(stderr, "port: memory card: from %s: %s\n", newest.label.c_str(), result.c_str());
  if (!ok) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Metroid Prime 2: saves not copied",
                             result.c_str(), window);
  }
}
#endif

void CMemoryCardSys::Initialize() {
  if (!mIsInitialized) {
#ifdef TARGET_PC
    // Aurora's CARDInit takes the game id and maker code; the card lives under
    // Aurora's user path (MP2_USER_PATH), in the region folder of the mounted disc's game code
    // (G2ME -> USA, G2MP -> EUR), so PAL and USA saves never share a card.
    CARDInit(port::entry::CurrentGameCode(), "01");
    // F1 > System > Memory card > "Share memory card with emulator": mount the emulator's card
    // base instead, so the port and Dolphin/PrimeHack use the same saves.
    sDefaultCardBase = BaseOfMountedCard();
    sCardBase = sDefaultCardBase;
    PortSwitchCard(PortSave::SharedCardBase().c_str());
    OfferRestoreFromOtherRelease();
#else
    CARDInit();
#endif
    mIsInitialized = true;
  }
}

#ifdef TARGET_PC
bool CMemoryCardSys::PortSwitchCard(const char* directory) {
  if (!mIsInitialized && sDefaultCardBase.empty()) {
    return false;
  }
  const std::string target =
      directory != nullptr && directory[0] != '\0' ? std::string(directory) : sDefaultCardBase;
  if (target.empty() || target == sCardBase) {
    return false;
  }
  std::error_code error;
  std::filesystem::create_directories(target, error);
  const bool mounted = aurora_card_set_base_path(target.c_str());
  // Taken even when it did not mount, so a bad directory is not retried every frame; the front
  // end then shows no card, which is the truth.
  sCardBase = target;
  std::fprintf(stderr, "port: memory card: switched to %s%s\n", target.c_str(),
               mounted ? "" : " (no card could be mounted there)");
  return true;
}

bool CMemoryCardSys::PortOnDefaultCard() {
  return !sDefaultCardBase.empty() && sCardBase == sDefaultCardBase;
}
#endif

CMemoryCardSys::~CMemoryCardSys() {
  mIsCardSysExists = false;
  FreeCardWorkArea(kCS_SlotA);
  FreeCardWorkArea(kCS_SlotB);
}

ProbeResults CMemoryCardSys::IsMemoryCardInserted(EMemoryCardPort port) {
  ProbeResults result;
  result.mError =
      static_cast< ECardResult >(CARDProbeEx(port, &result.mCardSize, &result.mSectorSize));
  return result;
}

ECardResult CMemoryCardSys::GetResultCode(int port) {
  return static_cast< ECardResult >(CARDGetResultCode(port));
}

ECardResult CMemoryCardSys::MountCard(EMemoryCardPort port) {
  return static_cast< ECardResult >(
      CARDMountAsync(port, AllocCardWorkArea(port), nullptr, nullptr));
}

ECardResult CMemoryCardSys::UnmountCard(EMemoryCardPort port) {
  ECardResult result = static_cast< ECardResult >(CARDUnmount(port));
  FreeCardWorkArea(port);
  return result;
}

ECardResult CMemoryCardSys::FormatCard(EMemoryCardPort port) {
  return static_cast< ECardResult >(CARDFormatAsync(port, nullptr));
}

ECardResult CMemoryCardSys::GetNumFreeBytes(EMemoryCardPort port, uint& freeBytes,
                                            uint& freeFiles) {
  s32 bytes;
  s32 files;
  ECardResult result = static_cast< ECardResult >(CARDFreeBlocks(port, &bytes, &files));
  freeBytes = bytes;
  freeFiles = files;
  return result;
}

ECardResult CMemoryCardSys::FastDeleteFile(EMemoryCardPort port, int fileNo) {
  return static_cast< ECardResult >(CARDFastDeleteAsync(port, fileNo, nullptr));
}

ECardResult CMemoryCardSys::CheckCard(EMemoryCardPort port) {
  return static_cast< ECardResult >(CARDCheckAsync(port, nullptr));
}

ECardResult CMemoryCardSys::GetSerialNo(EMemoryCardPort port, long long& serialOut) {
  return static_cast< ECardResult >(CARDGetSerialNo(port, reinterpret_cast< u64* >(&serialOut)));
}

ECardResult CMemoryCardSys::GetStatus(EMemoryCardPort port, int fileNo, CardStat& statOut) {
  CARDStat stat;
  ECardResult result = static_cast< ECardResult >(CARDGetStatus(port, fileNo, &stat));
  memcpy(&statOut.mStat, &stat, sizeof(stat));
  return result;
}

ECardResult CMemoryCardSys::SetStatus(EMemoryCardPort port, int fileNo, const CardStat& stat) {
  return static_cast< ECardResult >(
      CARDSetStatusAsync(port, fileNo, const_cast< CARDStat* >(&stat.mStat), nullptr));
}

bool CMemoryCardSys::mIsInitialized;
bool CMemoryCardSys::mIsCardSysExists;
bool CMemoryCardSys::mIsCardBusy;
rstl::vector< char, rstl::aligned_allocator > CMemoryCardSys::mWorkAreaA;
rstl::vector< char, rstl::aligned_allocator > CMemoryCardSys::mWorkAreaB;

rstl::vector< char, rstl::aligned_allocator >&
CMemoryCardSys::WorkAreaVector(EMemoryCardPort port) {
  switch (port) {
  case kCS_SlotA:
    return mWorkAreaA;
  case kCS_SlotB:
    return mWorkAreaB;
  default:
    return mWorkAreaA;
  }
}

char* CMemoryCardSys::AllocCardWorkArea(EMemoryCardPort port) {
  rstl::vector< char, rstl::aligned_allocator >& area = WorkAreaVector(port);
  area.resize(0xa000);
  char* data = area.data();
  DCInvalidateRange(data, area.size());
  return data;
}

void CMemoryCardSys::FreeCardWorkArea(EMemoryCardPort port) {
  rstl::vector< char, rstl::aligned_allocator >& area = WorkAreaVector(port);
  area = rstl::vector< char, rstl::aligned_allocator >();
}
