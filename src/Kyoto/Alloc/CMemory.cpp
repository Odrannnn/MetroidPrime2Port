#include "Kyoto/Alloc/CMemory.hpp"
#include "Kyoto/Alloc/CMemorySys.hpp"

#include "Kyoto/Alloc/CCallStack.hpp"
#include "Kyoto/Alloc/CGameAllocator.hpp"
#include "Kyoto/Basics/RAssertDolphin.hpp"

#include "dolphin/os.h"

#ifdef TARGET_PC
#include <mutex>
#include <thread>

namespace {
std::recursive_mutex sHeapMutex;
thread_local int sHeapDepth = 0; // how many times this thread holds sHeapMutex
std::thread::id sOomCallbackThread; // set with the callback, under sHeapMutex

// Held for one CMemory call; the CPortHeapUnlocked comment in CMemory.hpp says why.
struct SHeapLock {
  SHeapLock() {
    sHeapMutex.lock();
    ++sHeapDepth;
  }
  ~SHeapLock() {
    --sHeapDepth;
    sHeapMutex.unlock();
  }
};
} // namespace

CPortHeapUnlocked::CPortHeapUnlocked() : mDepth(sHeapDepth) {
  for (int i = 0; i < mDepth; ++i) {
    sHeapMutex.unlock();
  }
  sHeapDepth = 0;
}

CPortHeapUnlocked::~CPortHeapUnlocked() {
  for (int i = 0; i < mDepth; ++i) {
    sHeapMutex.lock();
  }
  sHeapDepth = mDepth;
}

bool PortHeapOnOomCallbackThread() { return std::this_thread::get_id() == sOomCallbackThread; }
#define PORT_HEAP_LOCK SHeapLock heapLock
#else
#define PORT_HEAP_LOCK
#endif

static CGameAllocator gGameAllocator;
IAllocator* CMemory::mpAllocator = &gGameAllocator;
bool CMemory::mInitialized;
uint gLeakCount = 0;
uint gLeakBytes = 0;

CMemorySys::CMemorySys(COsContext& ctx, IAllocator& allocator) {
  CMemory::Startup(ctx);
  CMemory::SetAllocator(ctx, allocator);
}

CMemorySys::~CMemorySys() { CMemory::Shutdown(); }

IAllocator& CMemorySys::GetGameAllocator() { return gGameAllocator; }

void CMemory::Startup(COsContext& ctx) { mInitialized = mpAllocator->Initialize(ctx); }

void CMemory::SetAllocator(COsContext& ctx, IAllocator& allocator) {
  if (mpAllocator != &allocator) {
    if (mpAllocator != nullptr) {
      mpAllocator->ReleaseAll();
    }
    mpAllocator = &allocator;
    mpAllocator->Initialize(ctx);
  }
}

static bool cmemory_enum_alloc_cb(const IAllocator::SAllocInfo& info, const void* ptr) {
  if (info.mIsAllocated && info.x9_ == 0) {
    ++gLeakCount;
    gLeakBytes += info.mLen;
  }
  return true;
}

void CMemory::Shutdown() {
  CMemory::mInitialized = false;

  if (mpAllocator->GetMetrics(0, false).x8_ != 0) {
    gLeakCount = 0;
    gLeakBytes = 0;
    mpAllocator->EnumAllocations((IAllocator::FEnumAllocationsCb)cmemory_enum_alloc_cb, nullptr,
                                 false);
  }
  mpAllocator->Shutdown();
}

void* CMemory::Alloc(size_t len, IAllocator::EHint hint, IAllocator::EScope scope,
                     IAllocator::EType type, const CCallStack& callstack) {
  PORT_HEAP_LOCK;
  volatile bool enabled = OSDisableInterrupts();
  void* ret = mpAllocator->Alloc(len, hint, scope, type, callstack);
  if (ret == nullptr) {
    const IAllocator::SMetrics metrics = mpAllocator->GetMetrics(0, false);
    rs_debugger_printf("Alloc failed - Size: %d, Used: %d, Free: %d", len, metrics.x10_,
                       metrics.mHeapSize2);
  }

  OSRestoreInterrupts(enabled);
  return ret;
}

void CMemory::Free(const void* ptr) {
  PORT_HEAP_LOCK;
  volatile bool enabled = OSDisableInterrupts();
  if (ptr != nullptr) {
    mpAllocator->Free(ptr);
  }
  OSRestoreInterrupts(enabled);
}

void CMemory::SetOutOfMemoryCallback(IAllocator::FOutOfMemoryCb cb, const void* context) {
  PORT_HEAP_LOCK;
#ifdef TARGET_PC
  sOomCallbackThread = std::this_thread::get_id();
#endif
  mpAllocator->SetOutOfMemoryCallback(cb, context);
}

IAllocator::SMetrics CMemory::GetMetrics(bool unk1, bool unk2) {
  PORT_HEAP_LOCK;
  return mpAllocator->GetMetrics(unk1, unk2);
}

void CMemory::OffsetFakeStatics(int offset) {
  PORT_HEAP_LOCK;
  mpAllocator->OffsetFakeStatics(offset);
}

void* operator new(size_t sz, const char* fileAndLine, const char* type) {
  return CMemory::Alloc(sz, IAllocator::kHI_None, IAllocator::kSC_Unk1, IAllocator::kTP_Heap,
                        CCallStack(-1, fileAndLine, type));
}
void* operator new[](size_t sz, const char* fileAndLine, const char* type) {
  return CMemory::Alloc(sz, IAllocator::kHI_None, IAllocator::kSC_Unk1, IAllocator::kTP_Array,
                        CCallStack(-1, fileAndLine, type));
}
