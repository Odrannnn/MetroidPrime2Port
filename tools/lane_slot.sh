#!/bin/bash
# tools/lane_slot.sh — reusable lane worktrees (slots) that keep their build-port-link, so a new
# task rebuilds only what differs from the slot's last checkout instead of a whole fresh tree.
# Run from wt-mp2-port. Slots are ../wt-mp2-lane<N> (this tree's depth, so ccache hits too).
#
#   tools/lane_slot.sh take <branch> [start=port]  claim a free slot, switch it to a new <branch>
#                                                  from <start> (or to <branch> if it exists),
#                                                  build metroid_prime2_port, print the slot path
#   tools/lane_slot.sh release <slot-dir> <branch> free a slot once its branch is committed; refused
#                                                  unless <branch> still holds the claim (a stale
#                                                  owner must not free a slot someone re-took)
#   tools/lane_slot.sh list                        slots, their branch and owner
#   tools/lane_slot.sh add                         create one more slot (configured and built)
#
# A slot is free when it has no claim file and a clean tree. The claim lives in the slot's
# per-worktree git dir, so it never shows in `git status`. Release keeps the branch (merge it
# from any tree) and parks the slot on a detached port; its build stays for the next taker.
set -euo pipefail
SRC=$(git rev-parse --show-toplevel)
ROOT=$(cd "$SRC/.." && pwd)
T=$SRC/../MetroidPrimePort/build/review-tools/bin

claim_file() { echo "$(git -C "$1" rev-parse --absolute-git-dir)/lane-slot-claim"; }
slots() { ls -d "$ROOT"/wt-mp2-lane[0-9]* 2>/dev/null | sort -V || true; }
is_clean() { [ -z "$(git -C "$1" status --porcelain --untracked-files=no)" ]; }
build() { "$T/ninja" -C "$1/build-port-link" metroid_prime2_port > "$1/build-port-link.slot.log" 2>&1 ||
            { echo "build failed: $1/build-port-link.slot.log" >&2; return 1; }; }

case ${1:-} in
take)
  BRANCH=${2:?branch}; START=${3:-port}
  for s in $(slots); do
    [ -e "$(claim_file "$s")" ] && continue
    is_clean "$s" || continue
    if git show-ref -q --verify "refs/heads/$BRANCH"; then
      git -C "$s" switch -q "$BRANCH"
    else
      git -C "$s" switch -q -c "$BRANCH" "$START"
    fi
    git -C "$s" submodule update -q --init extern/musyx
    echo "$BRANCH $(date -Iseconds)" > "$(claim_file "$s")"
    build "$s"
    echo "$s ready on $BRANCH: \$T/ninja -C $s/build-port-link metroid_prime2_port"
    exit 0
  done
  echo "no free slot: tools/lane_slot.sh add, or release one" >&2; exit 1 ;;
release)
  S=$(cd "${2:?slot dir}" && pwd); BRANCH=${3:?branch the slot was claimed for}
  [ -e "$(claim_file "$S")" ] || { echo "$S is not claimed" >&2; exit 1; }
  read -r OWNER _ < "$(claim_file "$S")"
  [ "$OWNER" = "$BRANCH" ] || { echo "$S is claimed by $OWNER, not $BRANCH; left alone" >&2; exit 1; }
  is_clean "$S" || { echo "$S has uncommitted changes; commit or discard them first" >&2; exit 1; }
  git -C "$S" switch -q --detach port
  rm -f "$(claim_file "$S")"
  echo "$S free (detached at port)" ;;
list)
  for s in $(slots); do
    c=$(claim_file "$s")
    printf '%-18s %-40s %s\n' "$(basename "$s")" "$(git -C "$s" branch --show-current || true)" \
      "$([ -e "$c" ] && echo "claimed: $(cat "$c")" || echo free)"
  done ;;
add)
  n=1; while [ -e "$ROOT/wt-mp2-lane$n" ]; do n=$((n + 1)); done
  S=$ROOT/wt-mp2-lane$n
  "$SRC/tools/new_worktree.sh" "$S" "slot/lane$n" port > /dev/null
  git -C "$S" switch -q --detach port
  git branch -q -D "slot/lane$n"
  build "$S"
  echo "$S added (detached at port, built)" ;;
*) sed -n '2,17p' "$0" >&2; exit 2 ;;
esac
