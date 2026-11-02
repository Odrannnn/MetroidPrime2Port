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

# Sweep A9 const/nan/shape rules (A9-520..): the cases each rule cleared stay quiet in one
# whole-tree run without the allowlist, and planted slips still flag
RK="python3 $PWD/tools/retail_diff.py --kinds const,nan,shape"
ALL=$($RK --no-allow --top 2000)
absent() { # name id kind: no `kind` finding under id
  if printf '%s\n' "$ALL" | awk -v id="$2" '/^fn_/{on=($1==id)} on' | grep -q "^  $3 "; then
    echo "FAIL $1 ($2 $3)"; fail=1; else echo "PASS $1"; fi
}
absent "const: close_enough/IsEpsilon epsilons" fn_5_F28 const
absent "const: named M_PIF / Deg2Rad, const float defs" fn_1_1F28 const
absent "const: named Deg2Rad" fn_1_2AA8 const
absent "const: CColor byte fold x/255" fn_3_5F5C const
absent "const: literal product folds" fn_6_56DC const
absent "nan: mfcr extrwi EQ bit is ==" fn_28_5BE8 nan
absent "nan: MWCC unrolls a literal-trip loop" fn_14_37FC nan
absent "nan: shadowed header inline counted per call" fn_2_1514 nan
absent "nan: shadowed header inline counted per call (2)" fn_7_787C nan
absent "nan: negated forms from the body's own code" fn_29_B294 nan
absent "shape: loader switch in the generated .inc" fn_6_A0 shape
absent "shape: auto_ptr assign inlines" fn_10_58C0 shape
absent "shape: jumptable switch" fn_18_AA4 shape
absent "shape: new null check" fn_13_4770 shape
absent "shape: Min/Max selects" fn_29_3D74 shape
absent "shape: string_l temporaries" fn_56_5060 shape
absent "shape: CTransform4f plumbing" fn_14_1570 shape
absent "shape: inlined helper lines" fn_56_9660 shape
kflags() { # name kind file pattern
  if $RK --kinds "$2" --file "$3" | grep -q -- "$4"; then echo "PASS $1"; else echo "FAIL $1 (no '$4')"; fail=1; fi
}
# planted slips: UpdateHeadTurn's pre-A9-500 Rad2Deg/25 misses retail's 0.43633232, and a
# ShouldFire whose `<=` turned `<` misses retail's cror compare
git show "19e3ba672:$E/PortCSwampBossStage1.cpp" > "$TMP/PortCSwampBossStage1.cpp"
kflags "SwampBossStage1 pre-A9-500 constant" const "$TMP/PortCSwampBossStage1.cpp" "0.43633232"
sed 's/!(mTimer <= 0\.f)/!(mTimer < 0.f)/' "$E/PortCElitePirate.cpp" > "$TMP/PortCElitePirate.cpp"
kflags "ElitePirate planted < for <=" nan "$TMP/PortCElitePirate.cpp" "cror float compares"
exit $fail
