#!/usr/bin/env bash
# Regression for tools/retail_diff.py's `effect` kind: the ctors #59 fixed (Chykka's spit token
# never locked) must flag on the pre-fix tree, and the ctors that lock through helpers, default
# member initializers and member ctors (DarkSamus, DigitalGuardian, DigitalGuardianHead, Lumite)
# must stay quiet on the working tree (whole-module runs). The old files are read with `git show` into a temp dir.
#   tools/retail_diff_regress.sh         # PASS/FAIL per case, exit 1 on any FAIL
set -u
cd "$(git rev-parse --show-toplevel)"
RD="python3 $PWD/tools/retail_diff.py --kinds effect"
E=src/MetroidPrime/Enemies
TMP=$(mktemp -d "${TMPDIR:-/tmp}/retail-diff-regress.XXXXXX")
trap 'rm -rf "$TMP"' EXIT
fail=0

flags() { # name file pattern: the file's effect findings must match pattern
  if $RD --file "$2" | grep -q -- "$3"; then echo "PASS $1"; else echo "FAIL $1 (no '$3')"; fail=1; fi
}

quiet() { # name module: no effect finding in the module (whole tree: --file alone would not know
  # the callees other files port)
  local out; out=$($RD --module "$2" | grep '^  effect')
  if [ -z "$out" ]; then echo "PASS $1"; else echo "FAIL $1"; echo "$out"; fail=1; fi
}

for s in 1 2; do
  git show "bfdb60dd9~:$E/PortCSwampBossStage$s.cpp" > "$TMP/PortCSwampBossStage$s.cpp"
done
flags "SwampBossStage1 pre-#59 ctor" "$TMP/PortCSwampBossStage1.cpp" "retail locks +0xD7C (xd7c_projectileInfo)"
flags "SwampBossStage2 pre-#59 ctor" "$TMP/PortCSwampBossStage2.cpp" "retail locks +0xCA8 (mSpitProjectile)"
quiet "SwampBossStage1 ctors" SwampBossStage1
quiet "SwampBossStage2 ctors" SwampBossStage2
quiet "DarkSamus ctors" DarkSamus
quiet "DigitalGuardian + Head ctors" DigitalGuardian
quiet "Lumite ctors (SShotAttack member ctor locks)" Lumite
# a planted slip: the DigitalGuardian ctor without its toe-explosion token still flags
sed '/^    x1100_ = TLockedToken< CGenDescription >(/,/;$/d' "$E/PortCDigitalGuardian.cpp" > "$TMP/PortCDigitalGuardian.cpp"
flags "DigitalGuardian planted slip" "$TMP/PortCDigitalGuardian.cpp" "retail locks +0x1100 (x1100_)"

# `call` kind, unported module callees whose every effect leaf the port body names hide by default
# (A9-400): a TLockedToken copy (Lock right after a CToken ctor) and an inlined DeliverCmd copy
calls() { # name args pattern: the call findings must match pattern
  if python3 tools/retail_diff.py --kinds call $2 | grep -q -- "$3"; then echo "PASS $1"; else echo "FAIL $1 (no '$3')"; fail=1; fi
}
nocall() { # name fn callee: default mode hides the callee
  if python3 tools/retail_diff.py --kinds call --fn "$2" | grep -q -- "$3"; then echo "FAIL $1"; fail=1; else echo "PASS $1"; fi
}
nocall "Kralee token copy hidden" fn_37_13C fn_37_840
calls "Kralee token copy --strict" "--strict --fn fn_37_13C" "fn_37_840 (unported) not named"
nocall "SpacePirate DeliverCmd copy hidden" fn_72_5310 fn_72_5418
calls "ElitePirate unnamed leaf (LdrToDamageInfo) shows" "--no-allow --fn fn_15_1A48" "fn_15_1A84 (unported) not named"
# a planted slip: Jump without its DeliverCmd no longer names the copy's leaf
sed '/DeliverCmd($/{N;/CBCJumpCmd(mDestPos, pas::kJT_Normal, pas::kJS_Loop/d}' "$E/PortCSpacePirateStates.cpp" > "$TMP/PortCSpacePirateStates.cpp"
calls "SpacePirate planted slip" "--file $TMP/PortCSpacePirateStates.cpp" "fn_72_5418 (unported) not named"
exit $fail
