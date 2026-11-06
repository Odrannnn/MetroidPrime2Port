#include "MetroidPrime/ScriptObjects/CScriptGuiMenu.hpp"

#include "Kyoto/Audio/CSfxManager.hpp"
#include "Kyoto/Input/CFinalInput.hpp"
#include "MetroidPrime/CSaveGameScreen.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/ScriptLoader/SLdrGuiMenu.hpp"
#include "MetroidPrime/TCastTo.hpp"

#include "rstl/math.hpp"
#ifdef TARGET_PC
#include "MetroidPrime/ScriptObjects/CScriptColorModulate.hpp"
#include "MetroidPrime/ScriptObjects/CScriptTextPane.hpp"
#include "port_pointer.h"
#include "port_pointer_menu.h"
#endif

// @ fn_60_8CE0
CScriptGuiMenu::CScriptGuiMenu(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                               bool wrap, bool vertical, int controller, const rstl::string& label,
                               bool locked, ushort selectionChangedSfx)
: CScriptGuiWidget(uid, name, info, controller, label, locked)
, mSelection(0)
, mWrapSelection(wrap)
, mVertical(vertical)
, mSelectionChangedSfx(selectionChangedSfx) {}

// @ fn_60_899C
void CScriptGuiMenu::AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) {
  CScriptGuiWidget::AcceptScriptMsg(mgr, msg);

  switch (msg.GetMessage()) {
  case kSM_AreaLoaded:
    BuildItemList(mgr);
    return;
  case kSM_Activate: {
#ifdef TARGET_PC
    // Port: a menu with no connected items has nothing to activate (retail reads past the empty list).
    if (mSelection < 0 || mSelection >= static_cast< int >(mItems.size())) {
      return;
    }
#endif
    TUniqueId id = mItems[mSelection];
    CEntity* item = mgr.ObjectById(id);
    mgr.SendScriptMsg(item, GetUniqueId(), kSM_Activate);
    return;
  }
  case kSM_Deactivate:
    for (rstl::vector< TUniqueId >::iterator it = mItems.begin(); it != mItems.end(); ++it) {
      CEntity* item = mgr.ObjectById(*it);
      mgr.SendScriptMsg(item, GetUniqueId(), kSM_Deactivate);
    }
    return;
  default:
    break;
  }

  switch (msg.GetMessage()) {
  case kSM_Increment: {
    const int prevSelection = mSelection;
    SelectNext(mgr);
    if (prevSelection != mSelection) {
      SendScriptMsgs(kSS_Footstep, mgr);
      SendScriptMsgs(kSS_Modify, mgr);
      CSfxManager::SfxStart(mSelectionChangedSfx, 127, 63);
    }
    break;
  }
  case kSM_Decrement: {
    const int prevSelection = mSelection;
    SelectPrevious(mgr);
    if (prevSelection != mSelection) {
      SendScriptMsgs(kSS_Footstep, mgr);
      SendScriptMsgs(kSS_Modify, mgr);
      CSfxManager::SfxStart(mSelectionChangedSfx, 127, 63);
    }
    break;
  }
  case kSM_SetToZero:
    SelectFirst(mgr);
    break;
  case kSM_SetToMax:
    SelectLast(mgr);
    break;
  case kSM_InternalMessage0:
  case kSM_InternalMessage1:
  case kSM_InternalMessage2:
    UpdateItemStates(mgr, msg.GetMessage());
    break;
  default:
    break;
  }

  if (!GetActive()) {
    return;
  }
}

#ifdef TARGET_PC
// Pointer: hover selects the item whose text panes are under the pointer, a click
// presses A (the base Think turns it into the menu's A connections), right click B.
// An item reaches its panes directly or through the color modulate its Entered/Exited drive.
static bool PortItemContains(CStateManager& mgr, CEntity* item, float gx, float gy, int depth) {
  for (rstl::vector< SConnection >::const_iterator c = item->GetConnectionList().begin();
       c != item->GetConnectionList().end(); ++c) {
    const TUniqueId tid = mgr.GetIdForScript(c->objId);
    CEntity* target = mgr.ObjectById(tid);
    if (target == nullptr) {
      continue;
    }
    if (TCastToPtr< CScriptTextPane >(target)) {
      float x0, y0, x1, y1;
      if (PortTextPaneBounds(tid, x0, y0, x1, y1) && gx >= x0 && gx <= x1 && gy >= y0 &&
          gy <= y1) {
        return true;
      }
    } else if (depth > 0 && TCastToPtr< CScriptColorModulate >(target) &&
               PortItemContains(mgr, target, gx, gy, depth - 1)) {
      return true;
    }
  }
  return false;
}

void CScriptGuiMenu::PortPointerInput(CStateManager& mgr) {
  if (!GetActive() || mItems.size() == 0 || !PortPointerMenu::Begin()) {
    return;
  }
  CSaveGameScreen* saveScreen = mgr.mSaveGameScreen.get();
  if (saveScreen != nullptr && saveScreen->GetUIType() != CSaveGameScreen::kUIT_SaveReady) {
    return;
  }
  CFinalInput& input = mgr.mFinalInputs[mControllers.size() == 0 ? mControllerNumber
                                                                : mControllers[mActiveController]];
  if (PortPointerMenu::TakeBack()) {
    PortPointerMenu::PressB(input);
    return;
  }
  float gx, gy;
  int count;
  // Hover is the mouse's, so a finger sliding across the items selects nothing; a tap selects and confirms.
  bool clicked = PortPointerMenu::PeekClickPoint(gx, gy);
  if (!clicked && (!PortPointer::MouseIsLast() || !PortPointer::Position(gx, gy))) {
    return;
  }
  for (int i = 0; i < mItems.size(); ++i) {
    CEntity* item = mgr.ObjectById(mItems[i]);
    if (item == nullptr || IsItemLocked(i, mgr) || !PortItemContains(mgr, item, gx, gy, 1)) {
      continue;
    }
    SetSelection(i, mgr);
    if (clicked && PortPointerMenu::TakeClickPoint(gx, gy, count)) {
      PortPointerMenu::PressA(input);
    }
    return;
  }
}
#endif
// @ fn_60_8878
void CScriptGuiMenu::Think(float dt, CStateManager& mgr) {
#ifdef TARGET_PC
  PortPointerInput(mgr);
#endif
  CScriptGuiWidget::Think(dt, mgr);
  if (!GetActive()) {
    return;
  }

  CSaveGameScreen* saveScreen = mgr.mSaveGameScreen.get();
  if (saveScreen != nullptr && saveScreen->GetUIType() != CSaveGameScreen::kUIT_SaveReady) {
    return;
  }

  if (mControllers.size() == 0) {
    ProcessMenuInput(mgr.mFinalInputs[mControllerNumber], mgr);
  } else if (!ProcessMenuInput(mgr.mFinalInputs[mControllers[mActiveController]], mgr)) {
    for (int i = 0; i < mControllers.size(); ++i) {
      if (i != mActiveController && ProcessMenuInput(mgr.mFinalInputs[mControllers[i]], mgr)) {
        mActiveController = i;
        return;
      }
    }
  }
}

// @ fn_60_8770
void CScriptGuiMenu::BuildItemList(CStateManager& mgr) {
  if (mItems.size() != 0) {
    return;
  }

  mItems.reserve(GetConnectionList().size());
  TUniqueId selectedId = kInvalidUniqueId;
  for (rstl::vector< SConnection >::const_iterator it = GetConnectionList().begin();
       it != GetConnectionList().end(); ++it) {
    const EScriptObjectState state = it->state;
    const TUniqueId id = mgr.GetIdForScript(it->objId);
    if (state == kSS_Connect) {
      mItems.push_back_unsafe(id);
    } else if (state == kSS_Play) {
      selectedId = id;
    }
  }

  for (int i = 0; i < mItems.size(); ++i) {
    if (mItems[i] == selectedId) {
      mSelection = i;
      return;
    }
  }
}

// @ fn_60_85D8
void CScriptGuiMenu::SetSelection(int idx, CStateManager& mgr) {
  if (idx == mSelection) {
    return;
  }

#ifdef TARGET_PC
  // Port: the multiplayer return sets the setup menus before their own AreaLoaded built the item
  // list (retail reads the empty list's null data); build it now, and skip an index it lacks.
  BuildItemList(mgr);
  if (idx == mSelection || idx < 0 || idx >= static_cast< int >(mItems.size())) {
    return;
  }
  if (mSelection < 0 || mSelection >= static_cast< int >(mItems.size())) {
    mSelection = idx;
    return;
  }
#endif
  TUniqueId oldId = mItems[mSelection];
  CEntity* oldItem = mgr.ObjectById(oldId);
  mgr.SendScriptMsg(oldItem, GetUniqueId(), kSM_Close);
  if (GetActive()) {
    mgr.SendScriptMsg(oldItem, GetUniqueId(), kSM_Deactivate);
  }

  mSelection = idx;
  TUniqueId newId = mItems[mSelection];
  CEntity* newItem = mgr.ObjectById(newId);
  if (GetActive()) {
    mgr.SendScriptMsg(newItem, GetUniqueId(), kSM_Activate);
  }
  mgr.SendScriptMsg(newItem, GetUniqueId(), kSM_Open);
}

// @ fn_60_84DC
void CScriptGuiMenu::SelectPrevious(CStateManager& mgr) {
  const int count = mItems.size();
#ifdef TARGET_PC
  // Port: `% count` with no items is a SIGFPE on x86 (silent on PPC); nothing to select.
  if (count == 0) {
    return;
  }
#endif
  if (mWrapSelection) {
    for (int i = (mSelection + count - 1) % count; i != mSelection; i = (i + count - 1) % count) {
      if (!IsItemLocked(i, mgr)) {
        SetSelection(i, mgr);
        return;
      }
    }
  } else {
    for (int i = rstl::max_val(-1, mSelection - 1); i >= 0; --i) {
      if (!IsItemLocked(i, mgr)) {
        SetSelection(i, mgr);
        return;
      }
    }
  }
}

// @ fn_60_83D0
void CScriptGuiMenu::SelectNext(CStateManager& mgr) {
  const int count = mItems.size();
#ifdef TARGET_PC
  if (count == 0) {
    return;
  }
#endif
  if (mWrapSelection) {
    for (int i = (mSelection + 1) % count; i != mSelection; i = (i + 1) % count) {
      if (!IsItemLocked(i, mgr)) {
        SetSelection(i, mgr);
        return;
      }
    }
  } else {
    for (int i = rstl::min_val(mSelection + 1, count); i < count; ++i) {
      if (!IsItemLocked(i, mgr)) {
        SetSelection(i, mgr);
        return;
      }
    }
  }
}

// @ fn_60_8348
void CScriptGuiMenu::SelectFirst(CStateManager& mgr) {
  const int count = mItems.size();
  for (int i = 0; i < count; ++i) {
    if (!IsItemLocked(i, mgr)) {
      SetSelection(i, mgr);
      return;
    }
  }
}

// @ fn_60_82C8
void CScriptGuiMenu::SelectLast(CStateManager& mgr) {
  for (int i = mItems.size() - 1; i >= 0; --i) {
    if (!IsItemLocked(i, mgr)) {
      SetSelection(i, mgr);
      return;
    }
  }
}

// @ fn_60_817C
void CScriptGuiMenu::UpdateItemStates(CStateManager& mgr, EScriptObjectMessage msg) {
  for (int i = 0; i < mItems.size(); ++i) {
    TUniqueId id = mItems[i];
    CEntity* item = mgr.ObjectById(id);
    switch (msg) {
    case kSM_InternalMessage0:
      item->SendScriptMsgs(i == mSelection ? kSS_InternalState0 : kSS_InternalState10, mgr);
      break;
    case kSM_InternalMessage1:
      item->SendScriptMsgs(i == mSelection ? kSS_InternalState1 : kSS_InternalState11, mgr);
      break;
    case kSM_InternalMessage2:
      item->SendScriptMsgs(i == mSelection ? kSS_InternalState2 : kSS_InternalState12, mgr);
      break;
    default:
      break;
    }
  }
}

// @ fn_60_811C
bool CScriptGuiMenu::IsItemLocked(int idx, CStateManager& mgr) const {
  if (CEntity* item = mgr.ObjectById(mItems[idx])) {
    if (const CScriptGuiWidget* widget = TCastToConstPtr< CScriptGuiWidget >(item)) {
      return widget->IsLocked();
    }
    return false;
  }
  return true;
}

// @ fn_60_7F54
bool CScriptGuiMenu::ProcessMenuInput(const CFinalInput& input, CStateManager& mgr) {
  bool prev = mVertical ? input.DLAUp() || input.DDPUp() : input.DLALeft() || input.DDPLeft();
  bool next = mVertical ? input.DLADown() || input.DDPDown() : input.DLARight() || input.DDPRight();

  if (mPrevRepeat.Update(input.DeltaTime(), prev) && prev) {
    mgr.SendScriptMsg(this, GetUniqueId(), kSM_Decrement);
  } else if (!prev && mNextRepeat.Update(input.DeltaTime(), next) && next) {
    mgr.SendScriptMsg(this, GetUniqueId(), kSM_Increment);
  }

  return prev || next;
}

// @ fn_60_7D20
CEntity* LoadGuiMenu(CStateManager& mgr, CInputStream& input, CEntityInfo& info) {
  SLdrGuiMenu sldrThis;
#include "MetroidPrime/ScriptLoader/SLdrGuiMenu.inc"

  bool vertical = sldrThis.controlDirection == 0;
  const int controller = sldrThis.widgetProperties.controllerNumber - 1;
  return rs_new CScriptGuiMenu(mgr.AllocateUniqueId(), sldrThis.editorProperties.name,
                               LdrToEntityInfo(info, sldrThis.editorProperties),
                               sldrThis.wrapSelection, vertical, controller,
                               sldrThis.widgetProperties.guiLabel,
                               sldrThis.widgetProperties.isLocked, sldrThis.selectionChangedSound);
}

// @ fn_60_7CB0
CScriptGuiMenu::~CScriptGuiMenu() {}
