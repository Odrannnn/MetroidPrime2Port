/**
 * The port's `CLZOSupport::Inflate`: an LZO1X decompressor written for the port from the stream
 * format, under the repository's MIT licence. **This file is port-only**: `configure.py` does not
 * declare it, so mwcceppc never sees it.
 *
 * Upstream's `src/Kyoto/Streams/CLZOSupport.cpp` calls the GPL LZO 1.08 library, which upstream
 * vendors in `src/LZO/`. This tree removes that library (the matching build links the original
 * objects for those units instead, as NonMatching) and the host build excludes `CLZOSupport.cpp`.
 *
 * Unlike retail's `lzo1x_decompress`, this one bounds-checks: `destLen` is the output capacity on
 * entry (every caller passes the space left) and the byte count written on return. A corrupt stream
 * stops at the first bad instruction and reports what was written so far.
 */
#include "Kyoto/Streams/CLZOSupport.hpp"

#include <string.h>

namespace {

// A run length past its instruction's bit field: each zero byte adds 255, the first non-zero ends it.
bool ReadExtendedLength(const uchar*& ip, const uchar* ipEnd, uint& len) {
  while (ip < ipEnd && *ip == 0) {
    len += 255;
    ++ip;
  }
  if (ip >= ipEnd) {
    return false;
  }
  len += *ip++;
  return true;
}

} // namespace

void CLZOSupport::Inflate(const uchar* source, uint sourceLen, uchar* dest, uint& destLen) {
  const uchar* ip = source;
  const uchar* const ipEnd = source + sourceLen;
  uchar* op = dest;
  uchar* const opEnd = dest + destLen;
  // Literals the last instruction copied: 0-3 from a match's low bits, 4 after a literal run.
  // It decides what an instruction below 16 means.
  uint state = 0;

#define LZO_NEED_IN(n) \
  if (uint(ipEnd - ip) < uint(n)) goto done
#define LZO_NEED_OUT(n) \
  if (uint(opEnd - op) < uint(n)) goto done

  if (ip < ipEnd && *ip > 17) {
    const uint len = *ip++ - 17;
    LZO_NEED_IN(len);
    LZO_NEED_OUT(len);
    memcpy(op, ip, len);
    ip += len;
    op += len;
    state = len < 4 ? len : 4;
  }

  while (ip < ipEnd) {
    const uint inst = *ip++;
    uint len;
    uint dist;
    uint next;
    if (inst >= 64) {
      // M2: 3-8 bytes, up to 2 KiB back.
      LZO_NEED_IN(1);
      len = (inst >> 5) + 1;
      dist = ((inst >> 2) & 7) + (uint(*ip++) << 3) + 1;
      next = inst & 3;
    } else if (inst >= 32) {
      // M3: up to 16 KiB back.
      len = inst & 31;
      if (len == 0) {
        len = 31;
        if (!ReadExtendedLength(ip, ipEnd, len)) goto done;
      }
      len += 2;
      LZO_NEED_IN(2);
      const uint d = ip[0] | (uint(ip[1]) << 8);
      ip += 2;
      dist = (d >> 2) + 1;
      next = d & 3;
    } else if (inst >= 16) {
      // M4: 16-48 KiB back; distance 0 ends the stream.
      len = inst & 7;
      if (len == 0) {
        len = 7;
        if (!ReadExtendedLength(ip, ipEnd, len)) goto done;
      }
      len += 2;
      LZO_NEED_IN(2);
      const uint d = ip[0] | (uint(ip[1]) << 8);
      ip += 2;
      dist = ((inst & 8) << 11) + (d >> 2);
      if (dist == 0) {
        break;
      }
      dist += 0x4000;
      next = d & 3;
    } else if (state == 0) {
      // Literal run of 4 or more bytes.
      len = inst;
      if (len == 0) {
        len = 15;
        if (!ReadExtendedLength(ip, ipEnd, len)) goto done;
      }
      len += 3;
      LZO_NEED_IN(len);
      LZO_NEED_OUT(len);
      memcpy(op, ip, len);
      ip += len;
      op += len;
      state = 4;
      continue;
    } else {
      // M1: a 2-byte match right after short literals, or 3 bytes 2-3 KiB back after a long run.
      LZO_NEED_IN(1);
      len = state == 4 ? 3 : 2;
      dist = (inst >> 2) + (uint(*ip++) << 2) + (state == 4 ? 2049 : 1);
      next = inst & 3;
    }

    if (dist > uint(op - dest)) goto done;
    LZO_NEED_OUT(len);
    // Byte by byte: a match may overlap the bytes it is producing.
    const uchar* mp = op - dist;
    for (uint i = 0; i < len; ++i) {
      op[i] = mp[i];
    }
    op += len;

    LZO_NEED_IN(next);
    LZO_NEED_OUT(next);
    for (uint i = 0; i < next; ++i) {
      *op++ = *ip++;
    }
    state = next;
  }

#undef LZO_NEED_IN
#undef LZO_NEED_OUT

done:
  destLen = uint(op - dest);
}
