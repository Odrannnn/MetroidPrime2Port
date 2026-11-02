# DigitalGuardian (module 14), dg30 run C: the knee armour (fn_14_D4D0..fn_14_D7BC, fn_14_CE1C,
# fn_14_CE94) and the body's knee loops (fn_14_3A44, fn_14_3DA8).
# - The third and fourth knees with no echo emitter (data-dead: mKneeEchoParameters sets isEchoEmitter;
#   forced in fn_14_4080, as the knee ctor's lines have no code in the coverage line table), so Think's 0xD27C and AcceptScriptMsg's 0xD440 tests take their null arm.
# - fn_14_CE94's state changes: knee 0's with no sound handle (0xCEEC's play arm, 0xCF4C's skip
#   arm) and knee 2's with the handle set, so
#   SetArmored keeps it (0xCEEC) or removes it (0xCF4C), Think updates it (0xD19C) and the Delete
#   case removes it (0xD3FC). The index 0xFFF names no sound: the sfx manager ignores it.
# Run: BUILD=$PWD/build-port-cov tools/cov/force.sh tools/cov/specs/digitalguardian_dg30_knees.py \
#        <out> 1baa96c2,09_cliff_temple_dark --driver 'python3 tools/drivers/dg30_knees.py {port}' \
#        --fast --frames 900
CLASS = 'CDigitalGuardianKneeArmor'
SPEC = []
FORCE = [
    ('D4D0 no echo emitter', 'loc:PortCDigitalGuardian.cpp:2116', [],
     ['echo.mIsEchoEmitter = 0'], 2, 'knees 3 and 4: isEchoEmitter cleared'),
    # every SetArmored call stops here (the early return included): only a state change writes,
    # knee 2 (uid 0x57A in this area) gets the dummy handle, the others none
    ('CE94 handle', 'CDigitalGuardianKneeArmor::SetArmored(',
     ['$this->mUniqueId.value', '$rdx & 1', '$this->x19C_24_armored_', '$this->x198_.mID'],
     ['$this->x198_.mID = ($this->x19C_24_armored_ != ($rdx & 1)) ? '
      '((($this->mUniqueId.value & 0x7ff) == 0x17a) ? 0xfffff : 0) : $this->x198_.mID'],
     0, 'x198 per knee'),
]
