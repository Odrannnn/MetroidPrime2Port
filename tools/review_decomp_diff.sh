#!/bin/bash
# Usage: tools/review_decomp_diff.sh <base> [head]   (default head: HEAD)
# Flags the slips lanes make in decomp patches, on added lines of src/ and include/ C++:
#   NULL   null guards (retail rarely has them; each needs a reason or goes)
#   ADDR   retail address literals (0x80xxxxxx) - thunks that compare against retail addresses
#   STUB   stub/stand-in/TODO/forwarding text added next to ported code
#   MASK   bit tests (& 0x.., rlwinm-derived) - re-derive each mask from the asm, not the selftest
#   MARK   new `// @` markers - check the body really is the function and its callees are ported
#   DEL    code lines removed per file - each removal needs a reason
#   DOCS   a docs/note/tsv commit that changes C++ code lines
# Prints "<tag> <file>:<line>: <text>" and a count per tag. A hit is a question, not a verdict.
set -u
base=${1:?usage: review_decomp_diff.sh <base> [head]}
head=${2:-HEAD}
git diff -U0 "$base" "$head" -- 'src/*.cpp' 'src/*.hpp' 'include/*.hpp' 'platform/*.cpp' |
awk '
  /^\+\+\+ b\// { f = substr($0, 7); next }
  /^@@/ { split($3, a, ","); ln = substr(a[1], 2) + 0; next }
  /^\+/ {
    t = substr($0, 2)
    if (t ~ /(==|!=)[ \t]*(nullptr|NULL)|(nullptr|NULL)[ \t]*(==|!=)|if[ \t]*\([ \t]*![ \t]*[A-Za-z_][A-Za-z_0-9>.-]*[ \t]*\)/) hit("NULL")
    if (t ~ /0x8[0-1][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]/) hit("ADDR")
    if (t ~ /[Ss]tub|[Ss]tand-in|TODO|FIXME|not ported|unported/) hit("STUB")
    if (t ~ /&[ \t]*0x[0-9A-Fa-f]+/ && t !~ /^[ \t]*\/\//) hit("MASK")
    if (t ~ /^[ \t]*\/\/ @ fn_/) hit("MARK")
    ln++
  }
  function hit(tag) { printf "%s %s:%d: %s\n", tag, f, ln, t; n[tag]++ }
  END { for (k in n) printf "== %s %d\n", k, n[k] }
'
# DEL: code lines (not comments/blank) removed per file - a rewrite removes lines too, but a removal
# nobody mentions is how 8a5800003 lost seven SpacePirate lines.
git diff -U0 "$base" "$head" -- 'src/*.cpp' 'src/*.hpp' 'include/*.hpp' 'platform/*.cpp' |
awk '/^--- a\// { f = substr($0, 7); next }
     /^-[^-]/ { t = substr($0, 2); if (t !~ /^[ \t]*(\/\/|$)/) d[f]++ }
     END { for (k in d) printf "DEL %s: %d code lines removed\n", k, d[k] }'
# DOCS: a commit whose subject says docs but whose diff changes C++ code lines.
for c in $(git rev-list --no-merges "$base..$head"); do
  s=$(git log -1 --format=%s "$c")
  case $s in docs*|*" docs"*|*note*|*tsv*) ;; *) continue ;; esac
  k=$(git show --format= -U0 "$c" -- 'src/*.cpp' 'src/*.hpp' 'include/*.hpp' 'platform/*.cpp' |
      grep -E '^[-+][^-+]' | grep -c -v -E '^[-+][ \t]*(//|$)')
  [ "$k" -gt 0 ] && echo "DOCS $(git log -1 --format='%h %s' "$c" | cut -c1-90): $k C++ code lines changed"
done
# Structure check (tools/asmcheck.py) on every changed Port*.cpp at <head>, correctness tags only.
# The module is the one whose fn ids the file cites most (docs/modules.tsv id column). A hit here
# means an Opus audit before merge; run `asmcheck.py check` with all tags before any `// @`.
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
for f in $(git diff --name-only "$base" "$head" -- 'src/*Port*.cpp'); do
  git cat-file -e "$head:$f" 2>/dev/null || continue
  id=$(git show "$head:$f" | grep -o 'fn_[0-9]\+_' | sort | uniq -c | sort -rn | awk 'NR==1{print $2}' | tr -dc '0-9')
  [ -n "$id" ] || continue
  mod=$(awk -F'\t' -v id="$id" '$2==id{print $1}' "$here/../docs/modules.tsv")
  [ -n "$mod" ] || continue
  if [ "$head" = HEAD ]; then src=$f; else mkdir -p "$tmp/$(dirname "$f")"; git show "$head:$f" >"$tmp/$f"; src=$tmp/$f; fi
  out=$(python3 "$here/asmcheck.py" check "$mod" "$src" --root "$(git rev-parse --show-toplevel)" --only MASK,XBIT,CROP,XCMP,XSTORE,OFFS,FCONST 2>&1)
  rc=$?
  [ $rc -eq 0 ] && continue
  printf '%s\n' "$out" | sed "s|^|ASM $f ($mod): |"
  printf '== ASM %s %d\n' "$f" "$(printf '%s\n' "$out" | grep -c '^[A-Z]')"
done
rm -rf "$tmp"
