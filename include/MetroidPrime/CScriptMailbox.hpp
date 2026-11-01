#ifndef _CSCRIPTMAILBOX
#define _CSCRIPTMAILBOX

#include "MetroidPrime/TGameTypes.hpp"
#include "rstl/reserved_vector.hpp"

class CBitStreamReader;
class CBitStreamWriter;
class CStateManager;
class CWorldSaveGameInfo;

class CScriptMailbox {
public:
  CScriptMailbox();
  CScriptMailbox(CBitStreamReader& in, const CWorldSaveGameInfo& saveWorld);
  ~CScriptMailbox();
  void PutTo(CBitStreamWriter& out, const CWorldSaveGameInfo& saveWorld) const;
  void SendMsgs(const TAreaId& areaId, CStateManager& mgr);
  void AddMsg(const TEditorId& id);
  void RemoveMsg(const TEditorId& id);
#ifdef TARGET_PC
  // Port: whether the memory relay's message is kept (the map's pickup dots).
  bool HasMsg(const TEditorId& id) const {
    for (int i = 0; i < mRelays.size(); ++i) {
      if (mRelays[i] == id) {
        return true;
      }
    }
    return false;
  }
#endif

private:
  rstl::reserved_vector< TEditorId, 512 > mRelays;
};
CHECK_SIZEOF(CScriptMailbox, 0x804)

#endif // _CSCRIPTMAILBOX
