#include "Kyoto/Audio/CAudioGroupSet.hpp"

#include "Kyoto/Alloc/CMemory.hpp"
#include "Kyoto/Basics/RAssertDolphin.hpp"
#include "Kyoto/CARAMManager.hpp"
#include "Kyoto/Streams/CMemoryInStream.hpp"

#include "rstl/StringExtras.hpp"
#include "rstl/string.hpp"

#include "dolphin/os.h"
#include "musyx/musyx.h"
#ifdef TARGET_PC
#include "Kyoto/Basics/CBasics.hpp"
#include "musyx/synthdata.h"
#endif

#include <string.h>

extern "C" {
u32 sndStackGetSize(void);
u32 sndStackAdd(void* buffer, const void* aramAddr, u32 size);
bool sndStackSetCurrent(u32 id);
void sndStackRemove(u32 id);
}

#ifdef TARGET_PC
// AGSC pool, project and sample-directory data are big-endian on disc, and MusyX reads them
// in place: convert them to host order (ported from the Prime 1 port's loader).
namespace {
ushort ReadBigU16(const uchar* ptr) {
  ushort value;
  memcpy(&value, ptr, sizeof(value));
  return CBasics::SwapBytes(value);
}

uint ReadBigU32(const uchar* ptr) {
  uint value;
  memcpy(&value, ptr, sizeof(value));
  return CBasics::SwapBytes(value);
}

void WriteU16(uchar* ptr, ushort value) { memcpy(ptr, &value, sizeof(value)); }

void WriteU32(uchar* ptr, uint value) { memcpy(ptr, &value, sizeof(value)); }

bool RecordOffset(uint* offsets, uint& count, uint capacity, uint offset) {
  for (uint i = 0; i < count; ++i) {
    if (offsets[i] == offset) {
      return false;
    }
  }
  if (count == capacity) {
    return false;
  }
  offsets[count++] = offset;
  return true;
}

void ConvertIdList(uchar* project, uint projectSize, uint offset,
                   uint* convertedLists, uint& convertedListCount) {
  if (offset >= projectSize ||
      !RecordOffset(convertedLists, convertedListCount, 512, offset)) {
    return;
  }

  while (offset + sizeof(ushort) <= projectSize) {
    const ushort value = ReadBigU16(project + offset);
    WriteU16(project + offset, value);
    offset += sizeof(ushort);
    if (value == 0xffff) {
      return;
    }
  }
}

void ConvertFxData(uchar* project, uint projectSize, uint offset,
                   uint* convertedFxData, uint& convertedFxDataCount) {
  if (offset + 4 > projectSize ||
      !RecordOffset(convertedFxData, convertedFxDataCount, 128, offset)) {
    return;
  }

  const ushort count = ReadBigU16(project + offset);
  WriteU16(project + offset, count);
  WriteU16(project + offset + 2, ReadBigU16(project + offset + 2));
  offset += 4;
  const uint available = (projectSize - offset) / sizeof(FX_TAB);
  const uint convertCount = count < available ? count : available;
  for (uint i = 0; i < convertCount; ++i, offset += sizeof(FX_TAB)) {
    WriteU16(project + offset, ReadBigU16(project + offset));
    WriteU16(project + offset + 2, ReadBigU16(project + offset + 2));
  }
}

void ConvertProject(uchar* project, uint projectSize) {
  uint convertedLists[512];
  uint convertedListCount = 0;
  uint convertedFxData[128];
  uint convertedFxDataCount = 0;
  uint offset = 0;

  for (uint groupCount = 0; groupCount < projectSize / 0x28; ++groupCount) {
    if (offset + 0x28 > projectSize) {
      return;
    }
    uchar* group = project + offset;
    const uint nextOffset = ReadBigU32(group);
    const ushort type = ReadBigU16(group + 6);
    const uint macroOffset = ReadBigU32(group + 8);
    const uint sampleOffset = ReadBigU32(group + 0xc);
    const uint curveOffset = ReadBigU32(group + 0x10);
    const uint keymapOffset = ReadBigU32(group + 0x14);
    const uint layerOffset = ReadBigU32(group + 0x18);
    const uint normpageOffset = ReadBigU32(group + 0x1c);

    WriteU32(group, nextOffset);
    WriteU16(group + 4, ReadBigU16(group + 4));
    WriteU16(group + 6, type);
    for (uint field = 8; field < 0x28; field += sizeof(uint)) {
      WriteU32(group + field, ReadBigU32(group + field));
    }

    if (nextOffset == 0xffffffff) {
      return;
    }

    ConvertIdList(project, projectSize, macroOffset, convertedLists, convertedListCount);
    ConvertIdList(project, projectSize, sampleOffset, convertedLists, convertedListCount);
    ConvertIdList(project, projectSize, curveOffset, convertedLists, convertedListCount);
    ConvertIdList(project, projectSize, keymapOffset, convertedLists, convertedListCount);
    ConvertIdList(project, projectSize, layerOffset, convertedLists, convertedListCount);
    if (type == 1) {
      ConvertFxData(project, projectSize, normpageOffset, convertedFxData,
                    convertedFxDataCount);
    }
    if (nextOffset == offset) {
      return;
    }
    offset = nextOffset;
  }
}

void ConvertPoolChain(uchar* pool, uint poolSize, uint offset, uint type) {
  for (uint recordCount = 0; recordCount < poolSize / 8; ++recordCount) {
    if (offset + 8 > poolSize) {
      return;
    }
    uchar* record = pool + offset;
    const uint nextOffset = ReadBigU32(record);
    WriteU32(record, nextOffset);

    if (nextOffset == 0xffffffff) {
      return;
    }
    if (nextOffset < 8 || nextOffset > poolSize - offset) {
      return;
    }
    WriteU16(record + 4, ReadBigU16(record + 4));
    WriteU16(record + 6, ReadBigU16(record + 6));

    const uint recordEnd = offset + nextOffset;
    if (type == 0) {
      for (uint pos = offset + 8; pos + sizeof(uint) <= recordEnd; pos += sizeof(uint)) {
        WriteU32(pool + pos, ReadBigU32(pool + pos));
      }
    } else if (type == 2) {
      for (uint i = 0, pos = offset + 8;
           i < 128 && pos + sizeof(KEYMAP) <= recordEnd; ++i, pos += sizeof(KEYMAP)) {
        WriteU16(pool + pos, ReadBigU16(pool + pos));
        WriteU16(pool + pos + 4, ReadBigU16(pool + pos + 4));
      }
    } else if (type == 3 && offset + 12 <= recordEnd) {
      const uint count = ReadBigU32(pool + offset + 8);
      WriteU32(pool + offset + 8, count);
      for (uint i = 0, pos = offset + 12;
           i < count && pos + sizeof(LAYER) <= recordEnd; ++i, pos += sizeof(LAYER)) {
        WriteU16(pool + pos, ReadBigU16(pool + pos));
        WriteU16(pool + pos + 6, ReadBigU16(pool + pos + 6));
      }
    }
    offset += nextOffset;
  }
}

void ConvertPool(uchar* pool, uint poolSize) {
  if (poolSize < sizeof(POOL_DATA)) {
    return;
  }

  uint offsets[4];
  for (uint i = 0; i < 4; ++i) {
    offsets[i] = ReadBigU32(pool + i * sizeof(uint));
    WriteU32(pool + i * sizeof(uint), offsets[i]);
  }

  for (uint i = 0; i < 4; ++i) {
    if (offsets[i] != 0) {
      ConvertPoolChain(pool, poolSize, offsets[i], i);
    }
  }
}

uint CountSampleDirEntries(const uchar* sampleDir, uint sampleDirSize) {
  const uint available = sampleDirSize / sizeof(SDIR_DATA_INTER);
  for (uint i = 0; i < available; ++i) {
    if (ReadBigU16(sampleDir + i * sizeof(SDIR_DATA_INTER)) == 0xffff) {
      return i + 1;
    }
  }
  return available + 1;
}

// Native `SDIR_DATA` is larger than the disc's `SDIR_DATA_INTER` (its `addr` is a
// host pointer), so the ADPCM info blocks that follow the disc entries have to be
// copied and converted, and each entry's `extraData` offset rebased onto the new
// entry array.
void ConvertAdpcmInfoBlocks(uchar* sampleDir, uint blocksOffset, uint blocksSize,
                            SDIR_DATA* entries, uint count, uint discEntryBytes) {
  // One slot per entry: a fixed-size table would leave blocks past its end big-endian.
  uint* converted = rs_new uint[count];
  uint convertedCount = 0;
  for (uint i = 0; i < count; ++i) {
    const uint oldOffset = entries[i].extraData;
    if (oldOffset < discEntryBytes) {
      continue;
    }
    const uint newOffset = blocksOffset + (oldOffset - discEntryBytes);
    if (newOffset + 0x28 > blocksOffset + blocksSize) {
      continue;
    }
    entries[i].extraData = newOffset;
    if (!RecordOffset(converted, convertedCount, count, newOffset)) {
      continue;
    }
    uchar* info = sampleDir + newOffset;
    WriteU16(info + 0x00, ReadBigU16(info + 0x00)); // numCoef
    WriteU16(info + 0x04, ReadBigU16(info + 0x04)); // loopY0
    WriteU16(info + 0x06, ReadBigU16(info + 0x06)); // loopY1
    for (uint c = 0; c < 16; ++c) {
      const uint at = 0x08 + c * 2;
      WriteU16(info + at, ReadBigU16(info + at)); // coefTab[8][2]
    }
  }
  delete[] converted;
}

void ConvertSampleDir(SDIR_DATA* dest, uint destCount, const uchar* source, uint sourceSize) {
  const uint sourceCount = sourceSize / sizeof(SDIR_DATA_INTER);
  uint i = 0;
  for (; i < sourceCount && i < destCount; ++i) {
    const uchar* entry = source + i * sizeof(SDIR_DATA_INTER);
    dest[i].id = ReadBigU16(entry);
    dest[i].ref_cnt = ReadBigU16(entry + 2);
    dest[i].offset = ReadBigU32(entry + 4);
    dest[i].addr = reinterpret_cast< void* >(static_cast< uintptr_t >(ReadBigU32(entry + 8)));
    dest[i].header.info = ReadBigU32(entry + 0xc);
    dest[i].header.length = ReadBigU32(entry + 0x10);
    dest[i].header.loopOffset = ReadBigU32(entry + 0x14);
    dest[i].header.loopLength = ReadBigU32(entry + 0x18);
    dest[i].extraData = ReadBigU32(entry + 0x1c);
    if (dest[i].id == 0xffff) {
      return;
    }
  }

  if (i < destCount) {
    memset(&dest[i], 0, sizeof(dest[i]));
    dest[i].id = 0xffff;
  }
}

// Builds the host sample directory: entries widened to the native SDIR_DATA (its `addr` is a
// host pointer) followed by the converted ADPCM info blocks.
uchar* BuildSampleDir(const uchar* disc, uint discSize) {
  const uint count = CountSampleDirEntries(disc, discSize);
  const uint entryBytes = count * sizeof(SDIR_DATA);
  // The disc directory ends in a 4-byte 0xFFFFFFFF terminator, not a full entry.
  const uint discEntryBytes = count > 0 ? (count - 1) * sizeof(SDIR_DATA_INTER) + 4 : 0;
  const uint blockBytes = discSize > discEntryBytes ? discSize - discEntryBytes : 0;
  uchar* dir = rs_new uchar[entryBytes + blockBytes];
  SDIR_DATA* entries = reinterpret_cast< SDIR_DATA* >(dir);
  ConvertSampleDir(entries, count, disc, discSize);
  if (blockBytes != 0) {
    memcpy(dir + entryBytes, disc + discEntryBytes, blockBytes);
  }
  ConvertAdpcmInfoBlocks(dir, entryBytes, blockBytes, entries, count, discEntryBytes);
  return dir;
}
} // namespace
#endif

static uchar* sUploadBuffer;
bool CAudioGrpSetLoc::sInSinglePlayer;

static void* UploadCallback(u32 offset, u32 bytes) {
#ifdef TARGET_PC
  memcpy(sUploadBuffer, reinterpret_cast< void* >(uintptr_t(offset)), bytes);
#else
  memcpy(sUploadBuffer, reinterpret_cast< void* >(offset), bytes);
#endif
  DCFlushRange(sUploadBuffer, bytes);
  return sUploadBuffer;
}

CFactoryFnReturn FAudioGroupSetLocDataFactory(const SObjectTag& tag,
                                              const rstl::auto_ptr< uchar >& data, int length,
                                              const CVParamTransfer& xfer) {
  return rs_new CAudioGrpSetLoc(data, length);
}

CAudioGrpSetLoc::CAudioGrpSetLoc(const rstl::auto_ptr< uchar >& data, int length)
: mStackId(-2), mAramAlloc(CARAMManager::GetInvalidAlloc()) {
  CMemoryInStream in(data.get(), length);
  if (in.ReadInt32() <= 1) {
    rstl::string name(in);
    const uint groupId = static_cast< ushort >(in.ReadUint16());
    const int poolSize = in.ReadInt32();
    const int projectSize = in.ReadInt32();
    const int sampleDirSize = in.ReadInt32();
    const int sampleSize = in.ReadInt32();

    mPool = rs_new uchar[poolSize];
    in.ReadBytes(mPool.get(), poolSize);
    mProject = rs_new uchar[projectSize];
    in.ReadBytes(mProject.get(), projectSize);
#ifdef TARGET_PC
    ConvertPool(mPool.get(), poolSize);
    ConvertProject(mProject.get(), projectSize);
    const uchar* discSampleDir = data.get() + in.GetReadPosition();
    mSampleDir = BuildSampleDir(discSampleDir, sampleDirSize);
    mSamples = rs_new uchar[sampleSize];
    memcpy(mSamples.get(), discSampleDir + sampleDirSize, sampleSize);
    uchar* samples = mSamples.get();
#else
    mSampleDir = rs_new uchar[sampleDirSize];
    in.ReadBytes(mSampleDir.get(), sampleDirSize);

    uchar* samples = data.get() + in.GetReadPosition();
#endif
    const int alignedSize = (sampleSize + 31) & ~31;
    mAramAlloc = CARAMManager::Alloc(alignedSize, 1);
    if (mAramAlloc == CARAMManager::GetInvalidAlloc()) {
      return;
    }

    mStackBuffer = rs_new uchar[sndStackGetSize()];
    mStackId = sndStackAdd(mStackBuffer.get(), mAramAlloc, alignedSize);
    if (mStackId == -1) {
      CARAMManager::Free(mAramAlloc, 1);
      mAramAlloc = CARAMManager::GetInvalidAlloc();
      return;
    }

    uchar uploadBuffer[0x1000 + 31];
#ifdef TARGET_PC
    sUploadBuffer = reinterpret_cast< uchar* >(
        (reinterpret_cast< uintptr_t >(uploadBuffer + 31) & ~uintptr_t(31)));
#else
    sUploadBuffer = reinterpret_cast< uchar* >(reinterpret_cast< uint >(uploadBuffer + 31) & ~31);
#endif
    sndSetSampleDataUploadCallback(UploadCallback, 0x1000);
    if (!sndStackSetCurrent(mStackId)) {
      CARAMManager::Free(mAramAlloc, 1);
      mAramAlloc = CARAMManager::GetInvalidAlloc();
      return;
    }

    if (!sndPushGroup(mProject.get(), groupId, samples, mSampleDir.get(), mPool.get())) {
      CARAMManager::Free(mAramAlloc, 1);
      mAramAlloc = CARAMManager::GetInvalidAlloc();
      sndStackRemove(mStackId);
      sndStackSetCurrent(-2);
      mStackId = -2;
      return;
    }

    sndSetSampleDataUploadCallback(nullptr, 0);
    sndStackSetCurrent(-2);

    if (sInSinglePlayer) {
      if (CStringExtras::IndexOfSubstring(name, rstl::string_l("_MP")) != -1 ||
          CStringExtras::IndexOfSubstring(name, rstl::string_l("Multiplayer")) != -1) {
        char* str = static_cast< char* >(__alloca(name.size() + 1));
        strcpy(str, name.data());
        rs_debugger_printf("Multi sound %s in single", str);
      }
    }
  }
}

CAudioGrpSetLoc::~CAudioGrpSetLoc() {
  if (mStackId != -2 && mStackId != -1) {
    sndStackSetCurrent(mStackId);
    sndPopGroup();
    sndStackRemove(mStackId);
    sndStackSetCurrent(-2);
  }
  if (mAramAlloc != CARAMManager::GetInvalidAlloc()) {
    CARAMManager::Free(mAramAlloc, 1);
  }
}
