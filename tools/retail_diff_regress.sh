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
exit $fail
