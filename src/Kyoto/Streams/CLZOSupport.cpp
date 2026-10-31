#include "Kyoto/Streams/CLZOSupport.hpp"

// The LZO 1.08 library (GPL) is not vendored here; this unit is NonMatching and the matching build
// links the original objects. Its two entry points, as LZO 1.08 declares them for a 32-bit target:
extern "C" int __lzo_init2(unsigned, int, int, int, int, int, int, int, int, int);
extern "C" int lzo1x_decompress(const uchar* src, uint src_len, uchar* dst, uint* dst_len,
                                void* wrkmem);
#define lzo_init()                                                                                 \
  __lzo_init2(0x1080, (int)sizeof(short), (int)sizeof(int), (int)sizeof(long), 4,                 \
              (int)sizeof(uint), (int)sizeof(uchar*), (int)sizeof(char*), (int)sizeof(void*),     \
              (int)sizeof(void*))

extern "C" int __lzo_init_done;

void CLZOSupport::Inflate(const uchar* source, uint sourceLen, uchar* dest, uint& destLen) {
  if (!__lzo_init_done) {
    lzo_init();
  }
  lzo1x_decompress(source, sourceLen, dest, &destLen, nullptr);
}
