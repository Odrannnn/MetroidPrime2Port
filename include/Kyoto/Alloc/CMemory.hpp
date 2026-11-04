#ifndef _CMEMORY
#define _CMEMORY

#include "Kyoto/Alloc/CCallStack.hpp"
#include "Kyoto/Alloc/IAllocator.hpp"
#include "types.h"

class COsContext;
class CMemory {
  static IAllocator* mpAllocator;
  static bool mInitialized;

public:
  static void Startup(COsContext& ctx);
  static void Shutdown();
  static void SetAllocator(COsContext& ctx, IAllocator& allocator);
  static IAllocator* GetAllocator();
  static void* Alloc(size_t len, IAllocator::EHint hint = IAllocator::kHI_None,
                     IAllocator::EScope scope = IAllocator::kSC_Unk1,
                     IAllocator::EType type = IAllocator::kTP_Heap,
                     const CCallStack& callstack = CCallStack(-1, "??(??)"));
  static void Free(const void* ptr);
  static void SetOutOfMemoryCallback(IAllocator::FOutOfMemoryCb callback, const void* context);
  static IAllocator::SMetrics GetMetrics(bool unk1, bool unk2);
  static void OffsetFakeStatics(int);
};

#ifdef TARGET_PC
// Port: the game heap's lock. On the console CMemory guarded the allocator by disabling
// interrupts, the only other context that allocated; the port's OSDisableInterrupts does nothing,
// and those contexts are threads (a DVD completion on Aurora's worker allocates a streamed-music
// buffer), so two threads could be in the allocator at once. The out-of-memory callback runs with
// the lock released (CGameAllocator::Alloc): it frees game objects, which can take MusyX's lock,
// and a thread holding that lock may be waiting here.
struct CPortHeapUnlocked {
  CPortHeapUnlocked();
  ~CPortHeapUnlocked();
  int mDepth;
};
#endif

#if defined(__MWERKS__) || defined(CLANGD)
void* operator new(size_t sz, const char*, const char*);
void* operator new[](size_t sz, const char*, const char*);
// TODO remove

inline void* operator new(size_t sz) { return operator new(sz, "??(??)", nullptr); }
inline void* operator new[](size_t sz) { return operator new[](sz, "??(??)", nullptr); }
#else
/*__attribute__((weak)) void* operator new(size_t sz) { return operator new(sz, "??(??)", nullptr); }
__attribute__((weak)) void* operator new[](size_t sz) {
  return operator new[](sz, "??(??)", nullptr);
}*/
#endif

#ifdef TARGET_PC
// Port: the host runtime owns global new/delete, so the game's CMemory-backed
// replacements must not be declared. Declaring them next to <new> redeclares
// the standard placement new and the nothrow operator delete.
#include <new>
#else
// placement new
inline void* operator new(size_t n, void* ptr) { return ptr; };
#endif

#ifdef __MWERKS__
inline void operator delete(void* ptr) { CMemory::Free(ptr); }
inline void operator delete[](void* ptr) { CMemory::Free(ptr); }
#define NEW new ("??(??)", nullptr)
#define rs_new new ("\?\?(\?\?)", nullptr)
#else
#ifndef TARGET_PC
__attribute__((weak)) void operator delete(void* ptr) { CMemory::Free(ptr); }
__attribute__((weak)) void operator delete[](void* ptr) { CMemory::Free(ptr); }
#endif
#define NEW new
#define rs_new new
#endif

#endif // _CMEMORY
