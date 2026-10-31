#ifndef _CSTRINGTABLE
#define _CSTRINGTABLE

#include "types.h"

#include "Kyoto/SObjectTag.hpp"

#include "rstl/auto_ptr.hpp"
#include "rstl/single_ptr.hpp"
#ifdef TARGET_PC
#include <vector>
#endif

class CInputStream;
class CResFactory;
class CDvdRequest;

class CStringTable {
public:
  struct SReloadData {
    uint mSize;
    rstl::single_ptr< CDvdRequest > mRequest;
    rstl::auto_ptr< uchar > mBuffer;

    SReloadData(CAssetId id, CResFactory& factory);
    ~SReloadData();
  };

  CStringTable(CInputStream& in);
  ~CStringTable();

  void Load(CInputStream& in);
#ifdef TARGET_PC
  // Trilogy only: renames the Wii button art in the few strings whose GC button differs from the
  // one the same art means elsewhere (the Spider Ball's Z, the Visor switch's D-pad); see #39.
  void PortMarkTrilogyPrompts(uint strgId);
#endif

  static void SetLanguage(int language);

  const wchar_t* GetString(int idx) const;
  const wchar_t* GetString(const char* name) const;
  int GetStringIndex(const char* name) const;
  int GetStringCount() const { return mStringCount; }

private:
  // Echoes STRG files can name strings; names are sorted for binary search.
  struct SStringName {
    const char* mName;
    int mIndex;
  };

  int mStringCount;
  int mNameCount;
  rstl::single_ptr< uchar > mData;
  SStringName* mNames;
  const wchar_t** mStrings;
  rstl::single_ptr< SReloadData > mReloadData;
#ifdef TARGET_PC
  // Port: every language section of the table, decoded, so the language can change while the game
  // runs. Each holds the host string pointers followed by the text; mStrings is the native one.
  struct PortSection {
    uint mLanguage;
    std::vector< uchar > mBlock;
    const wchar_t** Strings() { return reinterpret_cast< const wchar_t** >(mBlock.data()); }
  };
  std::vector< PortSection > mPortSections;
  const wchar_t* const* PortStrings() const;
#endif
};
CHECK_SIZEOF(CStringTable, 0x18)
NESTED_CHECK_SIZEOF(CStringTable, SReloadData, 0x10)

extern CStringTable* gpStringTable;

#endif // _CSTRINGTABLE
