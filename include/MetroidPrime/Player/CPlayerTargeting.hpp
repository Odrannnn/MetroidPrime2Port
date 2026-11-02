#ifndef _CPLAYERTARGETING
#define _CPLAYERTARGETING

#include "Kyoto/Graphics/CColor.hpp"
#include "MetroidPrime/TGameTypes.hpp"
#include "rstl/vector.hpp"

class CAABox;
class CActor;
class CEntity;
class CFrustumPlanes;
class CInGameGuiManagerSet;
class CStateManager;

// Guessed names, recovered from scan-visor consumers and the native object-list behavior.
class CPlayerTargeting {
public:
  struct SScanObject {
    SScanObject(TUniqueId id, const CColor& previousColor, float fadeTime)
    : mId(id), mPreviousColor(previousColor), mFadeTime(fadeTime) {}

    TUniqueId mId;
    CColor mPreviousColor;
    float mFadeTime;
  };

  explicit CPlayerTargeting(TUniqueId playerId);
  TUniqueId GetResolvedTargetId() const { return mResolvedTargetId; }
  TUniqueId GetScanTargetId(const CStateManager& mgr, int paletteIndex) const;
  TUniqueId ResolveScanTarget(const CStateManager& mgr, TUniqueId id) const;
  void Draw(CStateManager& mgr, const CInGameGuiManagerSet& gui) const;
  void PrepareStaticGeometry(const CStateManager& mgr, const TAreaId& areaId) const;
  int GetScanTargetIndex(const CStateManager& mgr, const TUniqueId& id) const;
  void Update(float dt, CStateManager& mgr);

private:
  // Guessed names; indices select the normal and highlighted scan palettes.
  enum EScanState {
    kSS_Invalid,
    kSS_Unlisted,
    kSS_Unscanned,
    kSS_CriticalUnscanned,
    kSS_Scanned,
    kSS_CriticalScanned,
    kSS_Hacked
  };

  EScanState GetScanState(CStateManager& mgr, const TUniqueId& id) const;
  CColor GetHighlightColor(CStateManager& mgr, const TUniqueId& id) const;
  CColor GetScanObjectColor(CStateManager& mgr, int index) const;
  void UpdateScanObjects(float dt, CStateManager& mgr);
  bool AddScanObject(const CActor& actor, const CStateManager& mgr);
  static CAABox GetTargetBounds(const CStateManager& mgr, TUniqueId id);
  static bool HasStaticGeometry(const CStateManager& mgr, TUniqueId id);
  bool IsInVisibleArea(const CStateManager& mgr, const CEntity* entity) const;
  CFrustumPlanes GetScanFrustum(const CStateManager& mgr) const;

  TUniqueId mPlayerId;
  TUniqueId mTargetId;
  TUniqueId mResolvedTargetId;
  int mReserved; // Guessed name: initialized to -1; no use found in this TU or identified
                 // consumers.
  float mTargetTime;
  float mScanTime;
  float mRefreshTimer;
  rstl::vector< SScanObject > mScanObjects;
#ifdef TARGET_PC
  // One bit per object slot: the port has 2048 (TGameTypes.hpp), so retail's 128 bytes would be
  // read and written past their end for ids >= 1024 (stray bits there kept scannables out of
  // mScanObjects: they pulsed red/yellow and could not be scanned, #51).
  uchar mScanObjectMembership[kMaxObjects / 8];
#else
  uchar mScanObjectMembership[128];
#endif
};
CHECK_SIZEOF(CPlayerTargeting, 0xa8)
typedef CPlayerTargeting::SScanObject CPlayerTargetingScanObject;
CHECK_SIZEOF(CPlayerTargetingScanObject, 0xc)

#endif // _CPLAYERTARGETING
