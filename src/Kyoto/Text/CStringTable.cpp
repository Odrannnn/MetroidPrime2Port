#include "Kyoto/Text/CStringTable.hpp"

#include "Kyoto/Alloc/CMemory.hpp"
#include "Kyoto/CDvdRequest.hpp"
#include "Kyoto/CFactoryMgr.hpp"
#include "Kyoto/Streams/CInputStream.hpp"

#include "rstl/algorithm.hpp"
#include "rstl/reserved_vector.hpp"

#include <string.h>

#ifdef TARGET_PC
#include "port_entry.h"

#include <algorithm>
#include <utility>
#include <wchar.h>
#include <vector>

static uint ReadBE32(const uchar* p) {
  return uint(p[0]) << 24 | uint(p[1]) << 16 | uint(p[2]) << 8 | uint(p[3]);
}

// How much stream is left. The reader has no bounds checks, so every length this file takes out of
// the data itself has to be held against this before it is used.
static long StreamRemaining(const CInputStream& in) {
  const long size = static_cast< long >(in.GetBufferSize());
  const long pos = static_cast< long >(in.GetReadPosition());
  return size > pos ? size - pos : 0;
}
#endif

static const wchar_t skInvalidString[] = L"Invalid";
static const FourCC skLanguages[] = {'ENGL', 'GERM', 'FREN', 'SPAN', 'ITAL', 'DUTC', 'JAPN'};
static FourCC mCurrentLanguage = skLanguages[0];

struct SLanguageInfo {
  FourCC mLanguage;
  uint mOffset;
  uint mSize;
};

struct SStringNameLess {
  template < typename T >
  bool operator()(const T& a, const T& b) const {
    return strcmp(a.mName, b.mName) < 0;
  }
};

#ifdef TARGET_PC
void CStringTable::SetLanguage(int language) {
  // Port: any value is safe, and tables already loaded follow it (GetString picks the section).
  if (language < 0 || language >= int(sizeof(skLanguages) / sizeof(skLanguages[0]))) {
    language = 0;
  }
  mCurrentLanguage = skLanguages[language];
}
#else
void CStringTable::SetLanguage(int language) { mCurrentLanguage = skLanguages[language]; }
#endif

CStringTable::CStringTable(CInputStream& in)
: mStringCount(0)
, mNameCount(0)
, mData(nullptr)
, mNames(nullptr)
, mStrings(nullptr)
, mReloadData(nullptr) {
  Load(in);
}

CStringTable::~CStringTable() {}

void CStringTable::Load(CInputStream& in) {
  in.ReadInt32();
  uint version = in.ReadInt32();
  int langCount = in.Get(TType< int >());
  mStringCount = in.Get(TType< int >());

#ifdef TARGET_PC
  // Port: a resource that is in no loaded pak is served as a buffer of zeros
  // (`CResLoader::WarnMissingResource`), and an empty string table is a harmless absence - every
  // string is "Invalid" and nothing dereferences it. A zero *language count* is what is not
  // harmless: `languages[0]` below then reads an uninitialised slot of the `reserved_vector`
  // (retail's slot, inlined on the stack), `offset` becomes garbage, and the skip before the size
  // read lands outside the buffer. That is the SIGSEGV in `CStringTable::Load+0x12c` a by-id world
  // change produced. A table the stream cannot even describe is taken as empty here.
  if (langCount <= 0 || langCount > 7 ||
      uint(langCount) * (version != 0 ? 12u : 8u) > uint(StreamRemaining(in))) {
    mStringCount = 0;
    return;
  }
#endif

  rstl::reserved_vector< SLanguageInfo, 7 > languages;
  for (int i = 0; i < langCount; ++i) {
    FourCC language = in.Get(TType< FourCC >());
    uint offset = in.Get(TType< uint >());
    uint size = version != 0 ? in.Get(TType< uint >()) : 0;
    SLanguageInfo info = {language, offset, size};
    languages.push_back(info);
  }
#ifndef TARGET_PC

  uint offset = languages[0].mOffset;
  uint size = languages[0].mSize;
  for (int i = 0; i < langCount; ++i) {
    if (languages[i].mLanguage == mCurrentLanguage) {
      offset = languages[i].mOffset;
      size = languages[i].mSize;
      break;
    }
  }
#endif

#ifdef TARGET_PC
  // Port: the disc holds 32-bit big-endian offsets and UTF-16BE text; a host has 64-bit pointers
  // and a 4-byte wchar_t. Read the raw names, then EVERY language section (so the language can
  // change while the game runs), and lay each out as host structures. mData holds
  // [SStringName x names][raw names blob]; a PortSection's block is
  // [const wchar_t* x strings][wchar_t text]. The names are the same in every language.
  // Every size below is bounded by the stream: a damaged (or absent) table must come out empty
  // rather than walk off the buffer.
  std::vector< uchar > rawNames;
  if (version != 0) {
    mNameCount = in.Get(TType< int >());
    uint namesSize = in.Get(TType< uint >());
    if (namesSize > uint(StreamRemaining(in))) {
      namesSize = uint(StreamRemaining(in));
    }
    rawNames.resize(namesSize);
    in.ReadBytes(rawNames.data(), namesSize);
  }
  if (mNameCount < 0 || size_t(mNameCount) * 8 > rawNames.size()) {
    mNameCount = 0;
  }
  if (mStringCount < 0) {
    mStringCount = 0;
  }

  const size_t namesBytes = size_t(mNameCount) * sizeof(SStringName);
  mData = rs_new uchar[namesBytes + rawNames.size()];
  uchar* base = mData.get();
  mNames = reinterpret_cast< SStringName* >(base);
  char* blob = reinterpret_cast< char* >(base + namesBytes);
  memcpy(blob, rawNames.data(), rawNames.size());
  for (int i = 0; i < mNameCount; ++i) {
    const uint nameAt = ReadBE32(&rawNames[i * 8]);
    mNames[i].mName = nameAt < rawNames.size() ? blob + nameAt : "";
    mNames[i].mIndex = int(ReadBE32(&rawNames[i * 8 + 4]));
  }

  // The sections lie in the stream in offset order, not necessarily in header order.
  std::vector< int > order;
  for (int i = 0; i < langCount; ++i) {
    order.push_back(i);
  }
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
    return languages[a].mOffset < languages[b].mOffset;
  });
  uint pos = 0;
  int nativeAt = -1;  // index into mPortSections of the English (else first listed) section
  int firstAt = -1;
  for (int idx : order) {
    uint offset = languages[idx].mOffset;
    uint size = languages[idx].mSize;
    if (offset < pos || offset > uint(StreamRemaining(in)) + pos) {
      continue;  // overlaps an earlier section or lies outside the data
    }
    for (; pos < offset; ++pos) {
      in.ReadInt8();
    }
    if (version == 0) {
      if (StreamRemaining(in) < 4) {
        break;
      }
      size = in.Get(TType< uint >());
      pos += 4;
    }
    if (size > uint(StreamRemaining(in))) {
      size = uint(StreamRemaining(in));
    }
    std::vector< uchar > section(size);
    in.ReadBytes(section.data(), size);
    pos += size;

    if (size_t(mStringCount) * 4 > section.size()) {
      continue;
    }
    size_t textUnits = 0;
    for (int i = 0; i < mStringCount; ++i) {
      for (size_t p = ReadBE32(&section[i * 4]); p + 1 < section.size(); p += 2, ++textUnits) {
        if (section[p] == 0 && section[p + 1] == 0) {
          break;
        }
      }
      ++textUnits;
    }
    PortSection port;
    port.mLanguage = uint(languages[idx].mLanguage);
    port.mBlock.resize(size_t(mStringCount) * sizeof(const wchar_t*) + textUnits * sizeof(wchar_t));
    const wchar_t** strings = port.Strings();
    wchar_t* text = reinterpret_cast< wchar_t* >(port.mBlock.data() +
                                                 size_t(mStringCount) * sizeof(const wchar_t*));
    for (int i = 0; i < mStringCount; ++i) {
      strings[i] = text;
      for (size_t p = ReadBE32(&section[i * 4]); p + 1 < section.size(); p += 2) {
        const wchar_t unit = wchar_t(section[p] << 8 | section[p + 1]);
        if (unit == 0) {
          break;
        }
        *text++ = unit;
      }
      *text++ = 0;
    }
    mPortSections.push_back(std::move(port));
    // Header order decides the first listed; ENGL beats it wherever it sits.
    const int at = int(mPortSections.size()) - 1;
    if (mPortSections[at].mLanguage == uint(skLanguages[0])) {
      nativeAt = at;
    }
  }
  // "First listed" is the first of the header that decoded.
  for (int i = 0; i < langCount && firstAt < 0; ++i) {
    for (size_t k = 0; k < mPortSections.size(); ++k) {
      if (mPortSections[k].mLanguage == uint(languages[i].mLanguage)) {
        firstAt = int(k);
        break;
      }
    }
  }
  if (nativeAt < 0) {
    nativeAt = firstAt;
  }
  if (nativeAt >= 0) {
    mStrings = mPortSections[nativeAt].Strings();
  } else {
    mStringCount = 0;
  }
#else
  if (version != 0) {
    mNameCount = in.Get(TType< int >());
    uint namesSize = in.Get(TType< uint >());
    mData = rs_new uchar[namesSize + size];
    mNames = reinterpret_cast< SStringName* >(mData.get());
    mStrings = reinterpret_cast< const wchar_t** >(mData.get() + namesSize);
    in.ReadBytes(mNames, namesSize);
  }

  for (uint i = 0; i < offset; ++i) {
    in.ReadInt8();
  }

  if (version == 0) {
    size = in.Get(TType< uint >());
    mData = rs_new uchar[size];
    mStrings = reinterpret_cast< const wchar_t** >(mData.get());
  }
  in.ReadBytes(mStrings, size);

  if (version != 0) {
    SStringName* entry = mNames;
    for (int i = 0; i < mNameCount; ++i, ++entry) {
      entry->mName += reinterpret_cast< uint >(mNames);
    }
  }

  uint* entry = reinterpret_cast< uint* >(mStrings);
  for (int i = 0; i < mStringCount; ++i, ++entry) {
    *entry += reinterpret_cast< uint >(mStrings);
  }
#endif
}
#ifdef TARGET_PC

// The section of the current language, else the native one (English; the first listed when the
// table has no ENGL), so a table that lacks the language shows English.
const wchar_t* const* CStringTable::PortStrings() const {
  for (size_t i = 0; i < mPortSections.size(); ++i) {
    if (mPortSections[i].mLanguage == uint(mCurrentLanguage)) {
      return const_cast< PortSection& >(mPortSections[i]).Strings();
    }
  }
  return mStrings;
}
#endif

#ifdef TARGET_PC
// Trilogy's strings use one Wii art id for actions that are different buttons on GameCube, and
// most are told apart by the shape of their tag (CTextParser). These few are not: the same
// sentence shape means Z (Spider Ball) or D-pad down (Visor), against L and Y elsewhere. They are
// named by STRG id and index, which every language of a table shares and which the GC discs of
// both regions agree on, so no wording is matched. `from` is replaced by `to` (same length).
void CStringTable::PortMarkTrilogyPrompts(uint strgId) {
  struct Mark {
    uint mStrg;
    int mIndex;
    const wchar_t* mFrom;
    const wchar_t* mTo;
  };
  static const Mark kMarks[] = {
      // Spider Ball (Z -> R)
      {3001190506u, 1, L"60ACB985", L"60ACB986"},
      {3001190506u, 2, L"60ACB985", L"60ACB986"},
      {2139891765u, 0, L"60ACB985", L"60ACB986"},
      {3546129389u, 0, L"60ACB985", L"60ACB986"},
      {1297092839u, 2, L"60ACB985", L"60ACB986"},
      {3887723722u, 1, L"60ACB985", L"60ACB986"},
      // Echo Visor / "activate Visor" (D-pad down stays D-pad down; elsewhere it is Y)
      {3542781720u, 2, L"13AD51E9", L"13AD51EA"},
      {4047364830u, 1, L"13AD51E9", L"13AD51EA"},
  };
  if (!port::entry::IsTrilogy(port::entry::CurrentDiscVersion())) {
    return;
  }
  for (const Mark& mark : kMarks) {
    if (mark.mStrg != strgId || mark.mIndex >= mStringCount) {
      continue;
    }
    for (PortSection& section : mPortSections) {
      wchar_t* text = const_cast< wchar_t* >(section.Strings()[mark.mIndex]);
      for (; *text != 0; ++text) {
        if (wcsncmp(text, mark.mFrom, 8) == 0) {
          memcpy(text, mark.mTo, 8 * sizeof(wchar_t));
        }
      }
    }
  }
}
#endif

const wchar_t* CStringTable::GetString(int idx) const {
  if (idx < 0 || idx >= mStringCount) {
    return skInvalidString;
  }
#ifdef TARGET_PC
  return PortStrings()[idx];
#else
  return mStrings[idx];
#endif
}

int CStringTable::GetStringIndex(const char* name) const {
  SStringName* begin = mNames;
  if (mNameCount > 0 && *name != '\0') {
    SStringName* end = begin + mNameCount;
    SStringName key = {name};
    SStringNameLess cmp;
    SStringName* it = rstl::lower_bound(begin, end, key, cmp);
    if (it != end && strcmp(it->mName, name) == 0) {
      return it->mIndex;
    }
  }
  return -1;
}

const wchar_t* CStringTable::GetString(const char* name) const {
  return GetString(GetStringIndex(name));
}

CStringTable::SReloadData::~SReloadData() {}

CFactoryFnReturn FStringTableFactory(const SObjectTag& tag, CInputStream& in,
                                     const CVParamTransfer& xfer) {
#ifdef TARGET_PC
  CStringTable* table = rs_new CStringTable(in);
  table->PortMarkTrilogyPrompts(static_cast< uint >(tag.GetId()));
  return table;
#else
  return rs_new CStringTable(in);
#endif
}
