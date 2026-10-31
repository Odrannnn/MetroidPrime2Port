#ifndef _CHEALTHINFO
#define _CHEALTHINFO

#include "types.h"

#include "MetroidPrime/TGameTypes.hpp"
#include "MetroidPrime/Weapons/WeaponTypes.hpp"

class CHealthInfo {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
  CHealthInfo(float hp, float resist);

  void SetHP(float hp) { mHealth = hp; }

  void SetKnockbackResistance(float resist) { mKnockbackResistance = resist; }

  float GetKnockBackResistance() const { return mKnockbackResistance; }

  float GetHP() const { return mHealth; }

  float GetInitialHP() const { return mInitialHealth; } // Guessed name.

  TUniqueId GetDamageId1() const { return mDeathDamageOwner; } // Guessed name.

  TUniqueId GetDamageId2() const { return mDeathDamageSource; } // Guessed name.

  TUniqueId GetLastDamageSource() const { return mLastDamageSource; } // Guessed name.
#ifdef TARGET_PC
  // The guest's CHealthInfo + 0x18 (the archive's uidC): the owner of the last damage. The
  // ScriptPlayerTurret's damage message reads it (fn_63_1BE8 at 0x00002338).
  TUniqueId GetLastDamageOwner() const { return mLastDamageOwner; }
  // The guest's CHealthInfo + 0x14: the MediumIng's kSM_Damage arm reads its weapon type
  // (fn_41_79C8).
  const CWeaponMode& GetLastDamageWeaponMode() const { return mLastDamageWeaponMode; }
  // The guest's CHealthInfo + 0x14 (the last damage's weapon mode, a word) read as a float by the
  // ScriptPlayerTurret's kSM_Stop case (fn_63_1BE8 at 0x000020B0: `lfs f1, 0x14(r5)`).  The guest's
  // CWeaponMode packs its bit fields from the top (PowerPC is big-endian), so the word is rebuilt
  // here: the weapon type in the high half, the three flags below it.
  float GetLastDamageWeaponAsFloat() const {
    const uint bits = (uint(mLastDamageWeaponMode.GetType()) << 16) |
                      (mLastDamageWeaponMode.IsCharged() ? 0x8000u : 0u) |
                      (mLastDamageWeaponMode.IsComboed() ? 0x4000u : 0u) |
                      (mLastDamageWeaponMode.IsInstantKill() ? 0x2000u : 0u);
    union {
      uint bits;
      float value;
    } punned = {bits};
    return punned.value;
  }
#endif

  const CWeaponMode& GetCauseOfDeathWeapon() const { return mDeathWeaponMode; } // Guessed name.

  bool GetDamageFlag() const { return mFrozenAtDeath; } // Guessed name; victim was frozen.

  // SetCauseOfDeathWeapon is an original Wii export; parameter names are guessed.
  void SetCauseOfDeathWeapon(CWeaponMode mode, TUniqueId owner, TUniqueId source, const bool frozen,
                             bool radiusDamage);
  // Guessed name; native damage attribution distinguishes weapon owner and source.
  void SetLastDamageWeapon(CWeaponMode mode, TUniqueId owner, TUniqueId source, bool radiusDamage);

private:
  // Guessed names, corroborated by construction and native damage/death consumers.
  float mInitialHealth;
  float mHealth;
  float mKnockbackResistance;
  CWeaponMode mDeathWeaponMode;
  TUniqueId mDeathDamageOwner;
  TUniqueId mDeathDamageSource;
  CWeaponMode mLastDamageWeaponMode;
  TUniqueId mLastDamageOwner;
  TUniqueId mLastDamageSource;
  bool mFrozenAtDeath : 1;
  bool mLastDamageWasRadius : 1;
};
CHECK_SIZEOF(CHealthInfo, 0x20)

#endif // _CHEALTHINFO
