// #include "dolphin/os/OSArena.h"
#include "dolphin/types.h"
#include "stddef.h"
#include <Kyoto/Alloc/CGameAllocator.hpp>
#ifdef TARGET_PC
#include <Kyoto/Alloc/CMemory.hpp>

#include <cstdint>
#include <cstdio>
#endif

#include <Kyoto/Alloc/CCallStack.hpp>
#include <Kyoto/Alloc/CMediumAllocPool.hpp>
#include <Kyoto/Alloc/CSmallAllocPool.hpp>
#include <Kyoto/Basics/COsContext.hpp>
#include <Kyoto/Basics/CStopwatch.hpp>

#include <stdint.h>

/* Here just to make sure the data section matches */
static const char* string_NULL = "<NULL>";
static const char* string_SOURCE_MODULE_UNLOADED = "<SOURCE MODULE UNLOADED>";
static const char* string_ = "";
static int gAllocatorTime = 0;

template < typename U1, typename U2 >
static inline U1 T_round_up(U2 val, int align) {
  return (val + (align - 1)) & ~(align - 1);
}

// Every normal block's length must be a whole number of SGameMemInfo headers, or a split leaves a
// header straddling the next block. 32 under mwcceppc; 0x40 on the 64-bit host. Under TARGET_PC
// so the decomp build keeps the literal it always compiled (Alloc is in main.dol).
#ifdef TARGET_PC
static const int kGameAllocGranule = sizeof(CGameAllocator::SGameMemInfo);
#endif

CGameAllocator::SGameMemInfo* CGameAllocator::GetMemInfoFromBlockPtr(const void* ptr) const {
  return reinterpret_cast< SGameMemInfo* >(reinterpret_cast< uintptr_t >(ptr) -
                                           sizeof(SGameMemInfo));
}

CGameAllocator::CGameAllocator()
: x4_(0)
, mHeapSize(0)
, mFirst(nullptr)
, mLast(nullptr)
, x54_(0)
, mOomCallback(nullptr)
, mOomTarget(nullptr)
, mSmallAllocPool(nullptr)
, mSmallAllocMainData(nullptr)
, mSmallAllocBookKeeping(nullptr)
, x6c_(false)
, x70_(0)
, mMediumPool(nullptr)
, x80_(0)
, x84_(0)
, x88_(0)
, x8c_(0)
, mHeapSize2(0)
, x94_(0)
, x98_(0)
, x9c_(0)
, xa0_(0)
, xa4_(0)
, xa8_(0)
, xac_(0)
, xb0_(0)
, xb4_(0)
, mPhysicalAddr(nullptr)
, mFakeStatics(0)
, xc0_(0) {}

CGameAllocator::~CGameAllocator() {
  if (mMediumPool) {
    mMediumPool->ClearPuddles();
    FreeNormalAllocation(mMediumPool);
    mMediumPool = nullptr;
  }
}

bool CGameAllocator::Initialize(COsContext& ctx) {
  mHeapSize = ctx.GetBaseFreeRam() - 2 * sizeof(SGameMemInfo);
#ifdef TARGET_PC
  // The first free block's length has to be a whole number of granules too.
  mHeapSize &= ~(kGameAllocGranule - 1);
#endif
  mFirst = static_cast< SGameMemInfo* >(OSAllocFromArenaLo(mHeapSize, sizeof(SGameMemInfo)));
  mPhysicalAddr = reinterpret_cast< void* >(
      reinterpret_cast< intptr_t >(mFirst) -
      (reinterpret_cast< uintptr_t >(mFirst) & kAllocatorPointerTopNybbleMask));
  OSGetArenaLo();
  mLast = reinterpret_cast< SGameMemInfo* >(reinterpret_cast< char* >(mFirst - 1) + mHeapSize);

  const SGameMemInfo& head = SGameMemInfo(
      nullptr, mLast, mLast, mHeapSize - sizeof(SGameMemInfo) * 2, "MemHead", "MemHead");
  *mFirst = head;
  const SGameMemInfo& tail = SGameMemInfo(mFirst, nullptr, nullptr, 0, "MemTail", "MemTail");
  *mLast = tail;
  for (uint i = 0; i < 16; i++) {
    mBins[i] = nullptr;
  }

  AddFreeEntryToFreeList(mFirst);
  x80_ = 0;
  x84_ = 0;
  x88_ = 0;
  x8c_ = 0;
  mHeapSize2 = mHeapSize;
  x94_ = 0;
  x98_ = 0;
  xa0_ = 0;
  xa4_ = 0;
  xa8_ = 0;
  xac_ = 0;
  x4_ = 1;

#ifdef TARGET_PC
  // The pool's 0x2c000 blocks are kAllocatorPointerSize bytes each: 8 on the host, so the main
  // data is twice retail's 0xb0000 or the pool's upper half overlaps other heap blocks.
  mSmallAllocMainData = Alloc(0x2c000 * kAllocatorPointerSize, kHI_None, kSC_Unk1, kTP_Heap,
#else
  mSmallAllocMainData = Alloc(0xb0000, kHI_None, kSC_Unk1, kTP_Heap,
#endif
                              CCallStack(0xffffffff, "SmallAllocMainData   ", " - Ignore"));

  mSmallAllocBookKeeping = Alloc(0x16000, kHI_None, kSC_Unk1, kTP_Heap,
                                 CCallStack(0xffffffff, "SmallAllocBookKeeping", " - Ignore"));

#ifdef TARGET_PC
  // The host objects outgrow retail's 0x20/0x1c cells; constructing them there overwrote the
  // next block's header.
  mSmallAllocPool = new (Alloc(sizeof(CSmallAllocPool), kHI_None, kSC_Unk1, kTP_Heap,
#else
  mSmallAllocPool = new (Alloc(0x20, kHI_None, kSC_Unk1, kTP_Heap,
#endif
                               CCallStack(0xffffffff, "SmallAllocClass      ", " - Ignore")))
      CSmallAllocPool(0x2c000, mSmallAllocMainData, mSmallAllocBookKeeping);

  mMediumPool =
#ifdef TARGET_PC
      new (Alloc(sizeof(CMediumAllocPool), kHI_None, kSC_Unk1, kTP_Heap,
#else
      new (Alloc(0x1c, kHI_None, kSC_Unk1, kTP_Heap,
#endif
                 CCallStack(0xffffffff, "MediumAllocClass      ", " - Ignore"))) CMediumAllocPool();

  uint mediumSize = CMediumAllocPool::GetAllocMemoryRequired(0x1000);
  mediumSize += CMediumAllocPool::GetBookKeepingMemoryRequired(0x1000);
  x78_ = Alloc(mediumSize, kHI_None, kSC_Unk1, kTP_Heap,
               CCallStack(0xffffffff, "MediumAllocMainData   ", " - Ignore"));
  xc0_ = 0xc6000;
  x84_ -= 4;
  return true;
}

void CGameAllocator::Shutdown() {
  ReleaseAll();
  x4_ = 0;
  x54_ = 0;
}

void* CGameAllocator::Alloc(size_t size, const EHint hint, const EScope scope, const EType type,
                            const CCallStack& callstack) {
  uint tmp = 0;
  void* buf = nullptr;
  const OSTick startTick = OSGetTick();

  if (hint & kHI_RoundUpLen) {
    size = T_round_up< size_t, size_t >(size, 32);
  }

  bool bVar1 = size <= 56 && !(hint & (kHI_RoundUpLen | kHI_TopOfHeap)) && mSmallAllocPool;

  if (bVar1 && x70_ > 0) {
    bVar1 = false;
    --x70_;
  }

  if (bVar1) {
    buf = mSmallAllocPool->Alloc(size);
    tmp = mSmallAllocPool->GetAllocatedSize();
    if (xb0_ < tmp) {
      xb0_ = tmp;
      static int sLastSmallAllocSize = 0;
      if (sLastSmallAllocSize + 128 < tmp) {
        sLastSmallAllocSize = tmp;
      }
    }

    if (buf != nullptr) {
      gAllocatorTime += (OSGetTick() - startTick);
      return buf;
    }
    x70_ = 25;
    x6c_ = true;
  }

#ifdef TARGET_PC
  // On the 64-bit host AddPuddle's list node (64 bytes) exceeds the small pool's ceiling and comes
  // back in here while the puddle list is still empty: unbounded recursion. Nested allocations
  // made while the pool grows take the normal block path, where retail's 52-byte node comes from.
  static bool sGrowingMediumPool = false;
#endif // TARGET_PC

  if (mMediumPool
#ifdef TARGET_PC
      && !sGrowingMediumPool
#endif // TARGET_PC
      && size <= 0x400 && !(hint & kHI_TopOfHeap)) {
#ifdef TARGET_PC
    sGrowingMediumPool = true;
#endif // TARGET_PC
    if (!mMediumPool->HasPuddles()) {
      buf = nullptr;
      mMediumPool->AddPuddle(0x1000, x78_, false);
      x78_ = buf;
    }

    buf = mMediumPool->Alloc(size);

    if (buf == nullptr) {
      void* puddlePtr = Alloc(CMediumAllocPool::GetAllocMemoryRequired(0x1000) +
                                  CMediumAllocPool::GetBookKeepingMemoryRequired(0x1000),
                              kHI_None, kSC_Unk1, kTP_Heap,
                              CCallStack(-1, "MediumAllocMainData   ", " - Ignore"));
      mMediumPool->AddPuddle(0x1000, puddlePtr, true);
      buf = mMediumPool->Alloc(size);
    }

#ifdef TARGET_PC
    sGrowingMediumPool = false;
#endif // TARGET_PC

    if (buf != nullptr) {
      gAllocatorTime += OSGetTick() - startTick;
      return buf;
    }

    if (!x7c_) {
      mMediumPool->GetTotalEntries();
      mMediumPool->GetNumAllocs();
      mMediumPool->GetNumBlocksAvailable();
    }
    x7c_ = true;
  }

  const bool topOfHeap = (hint & kHI_TopOfHeap) != 0;
#ifdef TARGET_PC
  uint roundedSize = T_round_up< uint, size_t >(size, kGameAllocGranule);
#else
  uint roundedSize = T_round_up< uint, size_t >(size, 32);
#endif
  SGameMemInfo* info = nullptr;

  if (topOfHeap) {
    info = FindFreeBlockFromTopOfHeap(roundedSize);
  } else {
    info = FindFreeBlock(roundedSize);
  }

  if (info == nullptr) {
    void* mediumBuf = nullptr;
#ifdef TARGET_PC
    // Port: the callback frees game objects, so only the thread that set it (the game thread)
    // runs it; another thread's allocation (a DVD completion's music buffer) just fails.
    if (mOomCallback && PortHeapOnOomCallbackThread()) {
#else
    if (mOomCallback) {
#endif
#ifdef TARGET_PC
      {
        CPortHeapUnlocked unlocked; // CMemory.hpp: the callback frees objects, which take other locks
        mOomCallback(mOomTarget, size);
      }
#else
      mOomCallback(mOomTarget, size);
#endif

      static bool bTriedCallback = false;
      if (!bTriedCallback) {
        bTriedCallback = true;
        mediumBuf = Alloc(size, hint, scope, type, callstack);
        bTriedCallback = false;
      } else {
        return nullptr;
      }
    }
    if (mediumBuf == nullptr) {
      (void)callstack.GetFileAndLineText();
      (void)callstack.GetTypeText();
      DumpAllocations();
      return nullptr;
    }
    return mediumBuf;
  }

  tmp = FixupAllocPtrs(info, size, roundedSize, hint, callstack);
  if (topOfHeap && !info->IsAllocated()) {
    info = info->GetNext();
  }

  UpdateAllocDebugStats(size, roundedSize, tmp);
  gAllocatorTime += OSGetTick() - startTick;
  return ++info;
}

CGameAllocator::SGameMemInfo* CGameAllocator::FindFreeBlock(uint len) {
  uint delta;
  CGameAllocator::SGameMemInfo* ret = nullptr;
  uint binIndex = GetFreeBinEntryForSize(len);

  uint chosenBin = 0;
  SGameMemInfo* previous = nullptr;
  uint bestDelta = 0x10000000;

  for (; binIndex < 16 && !ret; ++binIndex) {
    SGameMemInfo* candidate = mBins[binIndex];
    SGameMemInfo* last = nullptr;
    for (; candidate; last = candidate, candidate = candidate->GetNextFree()) {
      if (!candidate->IsAllocated() && candidate->mSize >= len) {
        delta = candidate->mSize - len;
        if (delta < bestDelta && candidate->GetNext()) {
          ret = candidate;
          previous = last;
          bestDelta = delta;
          chosenBin = binIndex;
          if (delta < sizeof(SGameMemInfo)) {
            break;
          }
        }
      }
    }
  }

  if (ret) {
    if (previous == NULL) {
      mBins[chosenBin] = ret->GetNextFree();
    } else {
      previous->SetNextFree(ret->GetNextFree());
    }
  }
  return ret;
}

CGameAllocator::SGameMemInfo* CGameAllocator::FindFreeBlockFromTopOfHeap(uint size) {
  SGameMemInfo* iter = mLast;
  SGameMemInfo* ret = nullptr;

  while (iter != nullptr) {
    if (!iter->IsAllocated() && iter->GetLength() >= size) {
      ret = iter;
      break;
    }
    iter = iter->GetPrev();
  }

  RemoveFreeEntryFromFreeList(ret);
  return ret;
}

uint CGameAllocator::FixupAllocPtrs(SGameMemInfo* info, const uint len, uint roundedLen, EHint hint,
                                    const CCallStack& cs) {

  const bool topOfHeap = (hint & kHI_TopOfHeap) != 0;
  uint ret = 0;
  const size_t blockLength = info->mSize;
  if (blockLength == roundedLen + sizeof(SGameMemInfo)) {
    ret = sizeof(SGameMemInfo);
    roundedLen += sizeof(SGameMemInfo);
  }

  SGameMemInfo* newPtr = info;
  if (blockLength != roundedLen) {
    SGameMemInfo* newInfo;

    SGameMemInfo* infoNext = info->GetNext();
    if (topOfHeap) {
      newInfo =
          reinterpret_cast< SGameMemInfo* >(reinterpret_cast< char* >(infoNext) - roundedLen) - 1;
      const SGameMemInfo& block = SGameMemInfo(info, infoNext, nullptr, len, "", "");
      *newInfo = block;
      info->mSize -= roundedLen + sizeof(SGameMemInfo);
      AddFreeEntryToFreeList(info);
      newPtr = newInfo;
    } else {
      uint offset = roundedLen + sizeof(SGameMemInfo);
      newInfo = reinterpret_cast< SGameMemInfo* >(reinterpret_cast< char* >(info) + offset);
      const SGameMemInfo& block =
          SGameMemInfo(info, infoNext, info->GetNextFree(),
                       info->mSize - roundedLen - sizeof(SGameMemInfo), "", "");
      *newInfo = block;
      AddFreeEntryToFreeList(newInfo);
    }
    newPtr->mFileAndLine = cs.GetFileAndLineText();
    newPtr->mType = cs.GetTypeText();
    ret = sizeof(SGameMemInfo);

    infoNext->SetPrev(newInfo);
    info->SetNext(newInfo);
  } else {
    info->mFileAndLine = cs.GetFileAndLineText();
    info->mType = cs.GetTypeText();
  }

  newPtr->SetTopOfHeapAllocated(topOfHeap);
  newPtr->SetAllocated(true);
  newPtr->mSize = len;
  return ret;
}

void CGameAllocator::UpdateAllocDebugStats(uint len, uint roundedLen, uint offset) {
  ++x84_;
  ++x80_;
  x88_ += len;
  x8c_ += roundedLen + offset;
  mHeapSize2 -= roundedLen + offset;

  if (x84_ > x94_) {
    x94_ = x84_;
  }

  if (x8c_ > x98_) {
    x98_ = x8c_;
  }

  if (x8c_ > x9c_) {
    x9c_ = x8c_;
  }

  if (len < xa0_) {
    xa0_ = len;
  }

  if (len > xa4_) {
    xa4_ = len;
  }
  xa8_ = (len + xa8_ * (x80_ - 1)) / x80_;
  if (len > 56) {
    return;
  }

  ++xac_;
}

bool CGameAllocator::Free(const void* ptr) {
  if (ptr == nullptr) {
    return true;
  }

#ifdef TARGET_PC
  // `CMemory::Shutdown` calls `Shutdown` below, so the heap is released - `mFirst` is null and
  // every block is back in the free list - before the process exits. On the console that is
  // the end of the story, but the host runs the game's own global destructors afterwards, from
  // `__run_exit_handlers` in `exit`: `CMain::RsMain` nulls `globalObjects`, whose `CMemorySys`
  // destroys the heap, and *then* `~single_ptr<CTweakPlayerRes>` frees the `CMayaSpline`
  // vectors it owns. With `mFirst` null that walks a null `SGameMemInfo` and dies. The freed
  // memory is the platform's to reclaim at that point, so drop the free.
  if (mFirst == nullptr) {
    return true;
  }
#endif

  if (mSmallAllocPool && mSmallAllocPool->PtrWithinPool(ptr)) {
    return mSmallAllocPool->Free(ptr);
  }

  if (mMediumPool) {
    int tmp = mMediumPool->Free(ptr);
    if (tmp != 1) {
      return tmp > 0;
    }
  }
  return FreeNormalAllocation(ptr);
}

bool CGameAllocator::FreeNormalAllocation(const void* ptr) {
  SGameMemInfo* info = GetMemInfoFromBlockPtr(ptr);
  size_t newLen = 0;
  const size_t infoLen = info->mSize;
  SGameMemInfo* k = info->GetNext();
  size_t len = 0;
  if (k) {
    len = reinterpret_cast< size_t >(k) - reinterpret_cast< size_t >(info) - sizeof(SGameMemInfo);
  }
  info->SetLength(len);

  SGameMemInfo* prev = info->GetPrev();
  SGameMemInfo* next = info->GetNext();

  if (prev && !prev->IsAllocated()) {
    RemoveFreeEntryFromFreeList(prev);
    prev->SetNext(next);
    if (next) {
      next->SetPrev(prev);
    }
    newLen = sizeof(SGameMemInfo);
    prev->mSize += info->mSize + sizeof(SGameMemInfo);
    info = prev;
  }

  if (next && !next->IsAllocated() && next->GetNext()) {
    RemoveFreeEntryFromFreeList(next);
    info->SetNext(next->GetNext());
    if (info->GetNext()) {
      info->GetNext()->SetPrev(info);
    }
    newLen += sizeof(SGameMemInfo);
    info->mSize += next->mSize + sizeof(SGameMemInfo);
    info->SetAllocated(false);
  } else {
    info->SetAllocated(false);
  }
  AddFreeEntryToFreeList(info);

  x84_ -= 1;
  x88_ -= infoLen;
  x8c_ -= (len + newLen);
  mHeapSize2 += (len + newLen);
  if (infoLen <= 56) {
    xac_ -= 1;
  }

  return true;
};

void CGameAllocator::ReleaseAll() {
  if (mMediumPool) {
    mMediumPool->ClearPuddles();
    FreeNormalAllocation(mMediumPool);
    mMediumPool = nullptr;
  }

  SGameMemInfo* iter = mFirst;
  while (iter != nullptr) {
    SGameMemInfo* next = iter->GetNext();
    if (iter->IsAllocated()) {
      FreeNormalAllocation(((uchar*)iter) + sizeof(SGameMemInfo));
    }
    iter = next;
  }

  mFirst = nullptr;
  mLast = nullptr;
};

void* CGameAllocator::AllocSecondary(size_t size, EHint hint, EScope scope, EType type,
                                     const CCallStack& callstack) {
  return Alloc(size, hint, scope, type, callstack);
};

bool CGameAllocator::FreeSecondary(const void* ptr) { return Free(ptr); };

void CGameAllocator::ReleaseAllSecondary() {};

void CGameAllocator::SetOutOfMemoryCallback(FOutOfMemoryCb cb, const void* target) {
  mOomCallback = cb;
  mOomTarget = target;
};

IAllocator::SAllocInfo CGameAllocator::GetAllocInfo(const void* ptr) const {
  const SGameMemInfo* info = GetMemInfoFromBlockPtr(ptr);

  return SAllocInfo(info, info->GetLength(), info->IsAllocated(), false, info->mFileAndLine,
                    info->mType);
};

IAllocator::SMetrics CGameAllocator::GetMetrics(bool unk1, bool unk2) const {
  uint mediumAllocTotalAllocated = mMediumPool != nullptr ? mMediumPool->GetTotalEntries() * 32 : 0;
  uint mediumAllocBlocksAvailable =
      mMediumPool != nullptr ? mMediumPool->GetNumBlocksAvailable() : 0;
  uint mediumAllocAllocatedSize =
      mMediumPool != nullptr ? mMediumPool->GetTotalEntries() - mMediumPool->GetNumBlocksAvailable()
                             : 0;
  const uint mediumAllocNumAllocs = mMediumPool != nullptr ? mMediumPool->GetNumAllocs() : 0;
  SMetrics ret(mHeapSize, x80_, x84_, x88_, x8c_, mHeapSize2, x94_, x98_, x9c_, xa0_, xa4_, xa8_,
               mSmallAllocPool != nullptr ? mSmallAllocPool->GetNumAllocs() : 0,
               mSmallAllocPool != nullptr ? mSmallAllocPool->GetAllocatedSize() : 0,
               mSmallAllocPool != nullptr ? mSmallAllocPool->GetNumBlocksAvailable() : 0,
               mediumAllocNumAllocs, mediumAllocAllocatedSize, mediumAllocBlocksAvailable,
               x80_ - xb4_, reinterpret_cast< uintptr_t >(mPhysicalAddr), xc0_,
               mediumAllocTotalAllocated, mFakeStatics);
  xb4_ = x80_;
  if (unk1) {
    x9c_ = 0;
  }
  if (unk2) {
    x98_ = 0;
  }
  return ret;
};

int CGameAllocator::EnumAllocations(FEnumAllocationsCb func, const void* ptr, bool b) const {

  int i = 0;
  const SGameMemInfo* iter = mFirst;

  while (iter != nullptr) {
    if (!iter->IsPostGuardBlockIntact()) {
      return -1;
    }

    if (!iter->IsPriorGuardBlockIntact()) {
      return -1;
    }

    const SGameMemInfo* next = iter->GetNext();
    SAllocInfo alloc(iter, iter->GetLength(), iter->IsAllocated(), false, iter->mFileAndLine,
                     iter->mType);
    func(alloc, ptr);
    ++i;
    iter = next;
  }

  return i;
};

uint CGameAllocator::GetFreeBinEntryForSize(const uint size) {
  uint maxLen = 0x20;
  uint bin = 0;

  while (maxLen < 0x200000) {
    if (size < maxLen) {
      return bin;
    }

    maxLen <<= 1;
    ++bin;
  }

  return 0xf;
}

void CGameAllocator::AddFreeEntryToFreeList(SGameMemInfo* info) {
  uint bin = GetFreeBinEntryForSize(info->GetLength());
  info->SetNextFree(mBins[bin]);
  mBins[bin] = info;
}

void CGameAllocator::RemoveFreeEntryFromFreeList(SGameMemInfo* memInfo) {
  uint bin = GetFreeBinEntryForSize(memInfo->GetLength());
  SGameMemInfo* curBin = nullptr;
  SGameMemInfo* binIt = mBins[bin];

  while (binIt != nullptr) {
    if (binIt == memInfo) {
      if (curBin == nullptr) {
        mBins[bin] = binIt->GetNextFree();
      } else {
        curBin->SetNextFree(binIt->GetNextFree());
      }
      return;
    }

    curBin = binIt;
    binIt = binIt->GetNextFree();
  }
}

static inline bool DoWait(int v) { return (v % 4) == 0; }

void CGameAllocator::DumpAllocations() const {
#ifdef TARGET_PC
  // Port: called when an allocation fails (Alloc). Retail walked the blocks and printed nothing
  // here; the port logs what the heap holds, so an out-of-memory says whether it is full, broken
  // into pieces, or leaking one thing: the free total and largest free block, then the allocated
  // bytes grouped by block size, largest first (most game allocations carry no file or type, so
  // a leak shows as many blocks of one size).
  {
    uint freeTotal = 0, freeLargest = 0, freeBlocks = 0, usedTotal = 0, usedBlocks = 0;
    struct SizeGroup {
      uint size;
      uint count;
    };
    static SizeGroup sGroups[512];
    uint groups = 0;
    for (const SGameMemInfo* it = mFirst; it != nullptr; it = it->GetNext()) {
      const uint len = it->GetLength();
      if (!it->IsAllocated()) {
        freeTotal += len;
        freeLargest = len > freeLargest ? len : freeLargest;
        ++freeBlocks;
        continue;
      }
      usedTotal += len;
      ++usedBlocks;
      uint g = 0;
      while (g < groups && sGroups[g].size != len) {
        ++g;
      }
      if (g == groups && groups < 512) {
        sGroups[groups++] = SizeGroup{len, 0};
      }
      if (g < groups) {
        ++sGroups[g].count;
      }
    }
    std::fprintf(stderr,
                 "[heap] out of memory: %u bytes used in %u blocks; %u free in %u blocks, the "
                 "largest %u\n",
                 usedTotal, usedBlocks, freeTotal, freeBlocks, freeLargest);
    for (int top = 0; top < 15 && groups > 0; ++top) {
      uint best = 0;
      for (uint g = 1; g < groups; ++g) {
        if (uint64_t(sGroups[g].size) * sGroups[g].count >
            uint64_t(sGroups[best].size) * sGroups[best].count) {
          best = g;
        }
      }
      if (sGroups[best].count == 0) {
        break;
      }
      std::fprintf(stderr, "[heap]   %5u x %8u bytes = %9llu\n", sGroups[best].count,
                   sGroups[best].size,
                   static_cast< unsigned long long >(uint64_t(sGroups[best].size) * sGroups[best].count));
      sGroups[best].count = 0;
    }
    return; // retail's walk only paced itself with waits
  }
#endif
  uint i = 0;
  SGameMemInfo* iter = mFirst;

  while (iter != nullptr) {
    ++i;

    if (DoWait(i)) {
      CStopwatch::Wait(0.005f);
    }
    iter = iter->GetNext();
  }
}

void CGameAllocator::OffsetFakeStatics(const int offset) { mFakeStatics += offset; }
