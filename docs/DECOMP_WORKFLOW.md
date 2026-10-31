# Module decomp workflow (parallel sessions)

How a module session and its lanes work so they don't redo each other's work. Lanes get a
pointer to this file plus their slice, not a restatement of it.

## Ownership

- One session per module family, one branch `decomp/<family>`, one worktree. It owns its
  `src/MetroidPrime/Enemies/Port<Class>*.cpp`, its headers' `TARGET_PC` blocks, and
  `docs/module-analysis/<Module>.md`. Nothing else.
- Shared files (`tools/modules_port.txt`, `src/MetroidPrime/PortModuleStubs.cpp`, `files.cmake`,
  `docs/modules.tsv`, any upstream file another family also includes): change only the lines
  for your own module, and say so in the report. The orchestrator resolves the merge.
- Shared tooling lives on `port` and is not rebuilt per session. Need a tool change? Ask the
  orchestrator; don't fork a copy. Current tools:
  - `tools/rel_pseudo/` - REL plf -> ET_EXEC for REA/Ghidra, split per function, `relsim.py`
    REL similarity. (Supersedes `tools/rel_plf2exec.py`; don't add another linker.)
  - m2c pseudo-C over the whole REL; REA `batch_decompile`.
  - `tools/upstream_hollow.py [--dol]` - upstream bodies that are skeletons of retail or reach stubs (needs the matching build; `docs/module-analysis/upstream_hollow.md`).
- One orchestrator (the port session) runs every family and reviews every lane patch; there are
  no per-family orchestrator sessions. Lane models: up to three `sonnet` lanes (reviews included)
  plus one DeepSeek worker (`opencode-go/deepseek-v4.1-flash`). (Superseded 2026-10-29: this
  bullet said MiniMax free only, at most three Opus lanes, never DeepSeek/`haiku`/`sonnet`; MiniMax
  M3.1 Flash is deprecated and the lane set changed 2026-10-24. The "MiniMax"/"Opus" wording
  further down this file is likewise historical.) A lane that fails twice gets a narrower brief, or
  the orchestrator does the piece itself.
- An accessor or field a lane needs in a shared header (one other families include) is asked for
  in the report, not added on the branch: the orchestrator commits it to `port` once and the lane
  merges `port`. (Two lanes adding the same accessor is the usual batch-merge conflict.)
- Lanes also write their final report to `/tmp/opencode/reports/<lane>.md` (overwrite), so an
  orchestrator after a handoff reads one directory instead of each lane session. The orchestrator
  updates `docs/STATUS.md` as lanes report but commits it once per batch or at a handoff.
- Each lane gets an exclusive list of functions (by `fn_<id>_<off>`) and files. Two lanes never
  hold the same file at once; run them in sequence, or split the file.

## Baseline

- A lane starts from a named commit on the session branch and commits on top of it. No
  rebases: if the branch moved, the session merges. A patch with unrelated or reversed hunks
  is rejected and redone, not repaired by hand.
- Sessions merge `port` only when the orchestrator announces a merge point, not ad hoc. After each
  batch lands, the orchestrator merges `port` into every live lane branch (or tells the lane to as
  the first step of its next slice), so lanes build on each other's code and the next batch
  merges cleanly.

## Don't re-investigate

- Decompile a module once: write the pseudo-C to `/tmp/opencode/pseudo/<Module>/` (one `.c`
  per function) and have lanes `grep`/read ranges there. Do not re-run REA per lane.
- Every verified fact (layout offsets, vtable slots, state/trigger tables, constants, anim
  ids, spawn area + layer that works) goes into `docs/module-analysis/<Module>.md` in a
  terse table the moment it is verified. A lane reads that note first and does not
  re-derive what is there. Wrong entries are deleted, not annotated.
- Briefs give paths and line ranges, never file contents.

## Progress

`python3 tools/port_progress.py` counts the port's markers per REL module. Its last column, `B`,
is the behaviour progress: `docs/module-analysis/<Module>_functions.tsv` says per function whether
it is behaviour the port has to reproduce (`B`), a host copy the host's base class already provides
(`H`), or dead or table-only code (`T`: a vtable slot, unreferenced code, the REL runtime). Only
the `B` rows are work, so the column is `B marked / B total`; a module with no table shows `-` and
the `TOTAL` line sums the tables that exist. A table's `class` column carries `B`/`H`/`T` directly;
a table with no class column falls back to its `kind` column (`behaviour` -> B, `host-copy` -> H,
`vtable`/`unreferenced`/`rel-runtime`/`rel-entry` -> T, anything else -> B).

Matched bodies need no coverage run: a body objdiff reports 100% against retail is retail's code
(user's call, 2026-10-24). After each batch merge, refresh `build/G2ME01/report.json`
(`python3 configure.py && $T/ninja`), then run `tools/bulk_mark_matched.py --write`. It marks every
unmarked REL function that is at 100%, has one definition in its unit's source, has no TARGET_PC in
or around the body, and that symbols.txt places at the same offset. The script is idempotent, and
its dry run lists the skips. `port_progress` also prints the H/T rows at 100% as `copies`, plus a
`behaviour + copies` total. Lane slices go to the functions still unmarked after that; smoke-only
slices are not lane work.

## Done means verified

A lane reports `STATUS: DONE` only with all of these pasted (one line each):
host build `0 FAILED`; `gen_files_cmake.py --check` rc 0; for upstream-file edits, the
TARGET_PC strip-diff CLEAN and the matching build's `fuzzy/matched` percentages; the runtime
area, frame count and `grep -c 'port: crashed:\|reach-stub'` = 0 (`[port] caught` is only Android's crash line). Anything skipped makes it
`STATUS: INCOMPLETE - <what>`.

Run game runs in the foreground (with `timeout`). A lane that ends its turn waiting on a
background job is never woken; it must report, not wait. Find and kill only the pids its own
script launched (bracketed `pgrep -f` names); other lanes run the game at the same time.

Fast runs: `MP2_FRAME_LIMIT=0 MP2_TICKS_PER_FRAME=8` runs the game ~3-4x faster than real time
(same fixed 1/60 s ticks); time console scripts in game updates with `@f<n>` in
`tools/drivers/console.py`, not wall seconds. Reach a state instead of playing to it: `fsm <id> <state>`
(force an AI state), `place <id> player [d]` (move an actor), `damage <id> <n> [weapon]`, `send`,
`aim`, `frame` (`help` lists all). Gameplay drivers go in `tools/drivers/` (committed with the
slice), not only `/tmp`. A lane that would save runs with a console command it lacks says so in its
report; the orchestrator adds it on `port`.

An arm reached only through a forced setup (`fsm`, `place`, `send`, `damage`) counts toward a marker
when the retail code of that arm then ran, and the module note's branch table says `forced (<cmd>)`
for that arm. Forcing is meant for arms gated by shipped data or by hours of play; an arm normal
play reaches in a short run is reached that way.

Lanes mark only bodies a run exercised fully (not just an early return), unless their brief says the
orchestrator marks that module. A marker needs a run that exercised the function and a pass of
the asm-vs-C++ structure check, `tools/asmcheck.py check <Module> <cpp> [fn...]` with all tags (calls,
compares, conditional branches, stores, bits, float constants (`FCONST`); the orchestrator reads only what it flags; tags and
tolerances in its docstring). `tools/review_decomp_diff.sh` runs its correctness tags on every
changed `Port*.cpp` (`ASM` lines); any hit means a fresh `sonnet` review before merge. Bit/compare reading
for a brief: `asmcheck.py annotate <Module> fn_<id>_<off> [--header H --class C]`. Changing the
tool: `tools/asmcheck_regress.sh` must stay all PASS.

## Lane briefs

- The standing part (start, rules, console, gates, report) is `docs/LANE_BRIEF.md`; a brief says
  "read docs/LANE_BRIEF.md first" and gives only the slice.
- Every bit test, mask, flag set, compare and rodata constant a lane writes carries the asm offset
  it came from (`// 0xCE2C`); copy the annotator's comment (`asmcheck.py annotate`) where there
  is one. Code with no offset
  is rejected.
- Bit-heavy functions (message handlers, ctor flag unpacking, trigger tables, CR extractions) go
  one or two per lane. Long briefs are where worker lanes guess.
- A fresh `sonnet` review only for diffs the checks flag or that are bit-heavy, not per merge.

## Review every lane patch

Lanes pass the gate first time but slip in the same places. Before committing a lane's
patch, run `tools/review_decomp_diff.sh <lane-base>` and answer every hit:

- **NULL** - null guards retail doesn't have. Keep one only if the asm has the test or a named
  port limit requires it (say which in the comment); otherwise delete it.
- **MASK** - re-derive each bit test from the asm `rlwinm`/`andi.` yourself (`rlwinm rD,rS,0,21,21`
  is `& 0x400`, not `& 0x20`: PowerPC bit N is `1u << (31-N)`, never `1u << N`). A selftest written from the same reading proves nothing.
- **ADDR / STUB** - no thunk that compares against a retail address and otherwise calls a stub;
  no stand-in left behind a `// @`.
- **MARK** - a marker means the body is retail's and ran. A function that mostly calls unported
  functions is not ported: unmark it or port the callees. A body another lane already ported is
  called, not copied (`grep -rn 'fn_<id>_<off>' src/`).
- **retail_diff** - `tools/retail_diff.py --diff <lane-base>` (or `--module M`, `--fn fn_X`; no
  selector = whole port, ~5 s, no build) lists per marked body: retail `bl` targets the port never
  names, rodata floats missing from the file, branch-count/size shape, and `nan` (fcmpo + plain
  `blt/ble/bge/bgt` needs a C `<`/`>`; `cror` needs `<=`/`>=`; negation flips the NaN arm with the
  sense, so `!(a <= b)` is wrong for a plain `ble`). Leads only: answer each like the hits above.
- **mathdiff** - after any Kyoto/Math or CMath.hpp change, run `tools/mathdiff.py` (~5 s with build;
  `[ADDR|#idx ...] [--seed N] [--n N] [--strict] [--verbose] [--rebuild]`). It runs retail
  (`tools/ppc_run.py`, unicorn on `orig/G2ME01/sys/main.dol`) and the port's units
  (`tools/mathdiff/harness.cpp`, one case line per function) on random inputs incl. NaN/inf, tol
  2e-6 rel. Only MISMATCH fails; `inf-edge`/`rounding`/`known` are soft (`--strict` fails them too).
- Lanes never run `git reset --hard`, `git clean`, `git stash`, `git checkout -- <path>` or `rm -rf`
  on a path held in a variable; reject a patch made that way and re-brief.

## Context

- Sessions hand off (AGENTS.md "Session length and handoffs") at a committed slice rather than
  ride through compactions; the module note is what makes the handoff cheap.
- Bulky output (builds, logs, pseudo-C dumps) stays in files; read the lines that matter.
