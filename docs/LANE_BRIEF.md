# Lane brief: the standing part

Every decomp lane brief says "read docs/LANE_BRIEF.md first" and then only gives the slice. The rules
behind this are in `docs/DECOMP_WORKFLOW.md` ("Done means verified", "Lane briefs"); this is the checklist.

## Start

1. `git status --short` clean, then `git merge port` (unless the brief names another base). Note the
   slice base (`git rev-parse HEAD`) for the report and the review.
2. Read your module note (`docs/module-analysis/<Module>.md`) before re-deriving anything.

## Work

- Every bit test, mask, compare, store and rodata constant carries its asm offset (`// 0x1234`).
- Mark (`// @ fn_<mod>_<off>`) only a body whose every live arm ran; a dead arm needs proof (the
  only writer, the shipped data scan). Forced arms count, labelled `forced (<cmd>)` in the note's branch table.
- Don't edit shared code (`platform/`, `CGenericFSM2*`, DOL classes, other modules' headers). Report a
  shared bug with file:line and evidence; ask for missing console commands/accessors in the report.
- Probes are temporary: never committed; `git diff <base>` must show none.
- Shared modules: port only the functions your brief names. Add bodies in your own file; never move
  or rewrite a body in another lane's file (a marker line or a one-line call-site swap is the most).
  Before porting a function, grep every branch you merged for its id: if it is ported, use that one.
- A commit that changes a constant, mask, compare, branch direction or call target quotes the
  defining asm lines (offset + instruction) in its message body and shows the decoded value. A
  "fix" without them is rejected unread. The reviewer re-derives each one.

## Decode traps (each was a real wrong "fix"; check before committing)

- `lis rN, X` loads `X << 16`: `lis r4,0x2; addi r0,r4,0x1000` is 0x21000, not 2 or 0x1002.
  `addi` after `lis` is signed: `lis 1; addi -1` is 0xFFFF.
- `li r3,0; li r4,1; lwz r5,<data>; bl __shl2i` is `1ull << data`: r3:r4 is the input, r5 the
  shift count (look up the data word), and the result comes back as r3 (high) : r4 (low). Material
  lists are built this way; the bit number is the enum value (`CMaterialList.hpp`).
- `extrwi.`/`rlwinm.` + `beq skip`: the action needs the bit SET. The bit is the mask in PPC
  numbering (bit N = value bit 31-N): `extrwi r0,r0,1,27` is 0x10.
- `fcmpo` + `cror eq,lt,eq` is `<=`; `cror eq,gt,eq` is `>=`. `mfcr; extrwi r3,r0,1,1` is GT,
  `1,0` LT, `1,2` EQ.
- NaN: `fcmpo` on a NaN sets only the unordered bit, so `ble`/`bge`/`bne` (branch if GT/LT/EQ
  clear) are **taken** and `blt`/`bgt`/`beq` are not. Plain C already matches: `if (a > b)` is
  `fcmpo; ble skip`, `if (a <= b)` is `cror eq,lt,eq; bne skip`. So `!(a <= b)` is NOT `a > b`:
  rewrite a compare only when the asm shape differs (a plain `ble` needs `<`/`>`, a `cror` needs
  `<=`/`>=`). `tools/retail_diff.py --kinds nan` lists suspects. Before committing a select or
  clamp fix, trace NaN through the asm: for `fcmpo 0,f; bge L; …; L: fmr f2,f0`, NaN lands on L
  (gets 0), so `f > 0 ? f : 0` is already right and `0 >= f ? 0 : f` is wrong.
- `cntlzw; li 1; rlwnm` (or `srwi 5`) tests == 0, but `rlwnm` rotates by the count's low 5
  bits only, so check what a negative value gives.
- `mulhw` divides signed; `mulhwu` unsigned. `subfc/adde` and `slwi` pairs are MWCC compare
  idioms, not arithmetic.
- An early `return` in the port must land where retail's branch lands: retail often skips only
  the middle and still runs the tail (resets, sounds).
- The base call belongs on the unhandled path; vtable slots count from the class's own header order
  (cite the slot number); a derived `DeliverCmd` needs its own overload (no slicing).
- When unsure, keep the existing code and put the question in the report. A wrong fix costs more
  than an unported one.

## Runs

- Foreground with `timeout`; fast mode `MP2_FRAME_LIMIT=0 MP2_TICKS_PER_FRAME=8` (use 1 for
  wall-timed plans) and `tools/drivers/console.py` `@f<n>` scripts; drivers go in `tools/drivers/`.
- Console (`help` lists all): `fsm <id> <state>` (reaches FSM2 sub-machines), `place <id> player [d]`,
  `damage <id> <n> [weapon|powerbomb|annihilator|darkburst|sunburst|sonicboom]`, `send <id> <msg>`,
  `shoot`, `boost` (morphed), `noise <type> [x y z]` (InformListeners; 6 = safe zone), `grapple <id> [d]`, `objs <type>`, `status`. `a; b; c` runs in one tick. `kill` alone kills the
  player; `kill <id>` zeroes a patterned actor's health and runs its Death (actors that reflect every weapon).
- Kill only pids you launched (save `$!`, `kill <pid>`) and any Xvfb you started. Never `pkill`/`killall`
  the game binary, gdb or Xvfb: a dozen lanes run them at once and you kill their runs. Before you report, delete
  only bulky scratch (Xvfb logs, extra gcda trees, intermediate copies) from `/tmp` (a 31 GB tmpfs), as
  `rm -rf /tmp/opencode/<dir>` with the literal absolute path (a variable, `cd` + relative path or a
  loop is denied by the permissions). Never delete evidence: the frozen executed binary, its
  gcda/gcno, the executed fixture with sha256 and the run's jsonl/logs stay in a named evidence dir.
- Never end your turn with a run in flight: nothing wakes you. Wait for it in the foreground
  (`timeout`, or `wait <pids>` in one foreground command); background-shell notifications do not
  reliably wake a lane whose turn has ended. Your last message is always the report. Run independent runs
  concurrently (each cov/smoke run takes its own slot) instead of one after another.
- gdb sites: the game is PIE, so never break at an absolute address (`*0x...` from objdump); use
  `symbol+offset` or a pattern resolved after load. Dry-run every site with static gdb on the exact
  binary (no game) before spending a launch; srp2b and sz2 each lost a launch to this.
- Game slots: before each run do `tools/games_running.sh --wait` in the foreground; it returns
  once fewer than 3 MP2/MP3 games run. Don't count with `pgrep -fc`: it also counts the
  timeout/gdb wrappers, about 4 per game. Never skip a run because slots are busy; wait for one.

## Coverage without probes

`tools/cov/run.sh <spec.py> <out> <world,area> [--con <cmdfile> | --driver '<cmd {port}>'] [--fast]
[--frames n] [--env K=V]` boots the game under gdb with breakpoints that never stop it: no probe, no
rebuild, any `build-port-link` binary. A spec (`tools/cov/specs/<module>.py`) sets `CLASS = 'CSandworm'`
and `SPEC = [(label, where, [exprs], flags)]`: `where` is a demangled-name prefix (`'CSandworm::CanAttack('`)
or `'loc:File.cpp:<line>'` (lines of the *built* source); `exprs` are gdb expressions on the entry registers
(`$this->x8C0_flags & 0x10`, `$rdx` = a state's EStateMsg; the host is x86-64 SysV, not PPC: integer
args in rdi(this) rsi rdx rcx r8 r9, floats in xmm0..; an argument written at entry may already be copied,
so force members through `$this` or break where the value is read; `$al`/`$sp`/`$fp`/`$fs` are registers,
so name gdb scratch variables with a prefix, e.g. `$ds_alive`); flags `ret` (bool result), `retp` (pointer
non-null), `calls` (count every call/jmp site in the body by callee - the way to see arms that end in a
call, tail calls included; `caller=` misses tail calls), `max=<n>` (hits per breakpoint, default 400: raise
it for per-frame functions or later arms never show). `<out>/cov.txt` lists per entry its hits and the
count per key tuple (also `cov.json`; both rewritten every 15 s, so a killed run keeps them); a key that
never shows is an arm that never ran. Label entries with the retail offset; put layer switches and
`fsm`/`place`/`send` steps in the driver (example: `specs/sandworm_temple.con`). Each run takes a free slot (its own
display and console port; set `COV_PORT`/`DISPLAY_NUM` only to override, a busy port fails fast), stop a run by killing `run.sh`, delete `<out>` after.
`tools/cov/force.sh` is the same run with arms forced: the gdb side (`tools/cov/force.py`,
`COV_GDB_SCRIPT`) writes the members the spec's `FORCE` list names at the entry of a body and logs
what it forced, so an arm the fight cannot reach can be taken; the module-specific part (the class
and the forced members) lives in the spec, the script itself has no module in it. To force on a
game the driver has already set up (the console does not answer while gdb boots it), start the
game with `MP2_ALLOW_PTRACE=1` and run `tools/cov/force.sh --attach <pid> <spec.py> <out dir>`;
without that variable the attach fails with "ptrace: Inappropriate ioctl" (yama scope 1).

Compiler coverage replaces the per-function gdb spec when the question is "which arms of *every*
body ran": a coverage build (`-DMP_PORT_COVERAGE=ON`, its own build dir `build-port-cov`)
compiles the port's REL module bodies - the sources `tools/modules_port.txt` lists - with gcov, so
one ordinary run reports the branch coverage of all of them at once. `tools/cov/gcov_report.py
<build dir> <gcov prefix dir> [--module <Name>] [--fn fn_10_1D794 ...]` prints one row per body:
the retail id, the C++ name, the lines hit, the branches taken, the lines whose branch was never
taken, and `ALL-ARMS` when every branch was taken. The run scripts set `GCOV_PREFIX` and
`GCOV_PREFIX_STRIP` when the binary is a coverage build, so concurrent runs write into their own
directory, and the game's flush hook (`platform/port_coverage.cpp`) writes the counters on a clean
quit, on SIGTERM and on SIGINT. Several prefix dirs may be given: their counters add up, so the
arms of a natural run and of a forced one merge into one profile (pass the dirs to `gcov_report.py`;
if you merge with `gcov-tool merge` instead, put the forced profiles first: it keeps the first
profile's non-zero counters, so a natural run merged first hides the forced arms). gdb stays the tool for the arms
no run reaches (`tools/cov/force.sh`): coverage says which arm is missing, not how to reach it.

## Gates (one line each in the report)

`$T` is the review-tools bin dir (`T=$PWD/../MetroidPrimePort/build/review-tools/bin`, AGENTS.md
"Commands"). Build `$T/ninja -C build-port-link metroid_prime2_port` 0 FAILED; `python3 tools/gen_files_cmake.py
--check` rc 0; `python3 tools/gen_entity_schema.py --check` rc 0 when the lane touches an entity header
(a `class ... : public <entity>` in `include/`): it re-derives the schema from those headers, so a field
added, removed or retyped fails it, naming `platform/generated/port_entity_schema.inc` and
`docs/SAVESTATE_SCHEMA.md` (rc 77 is clang-19 missing, not staleness; fix with `python3 tools/gen_entity_schema.py`);
a lane that changes the savestate field code (`platform/port_savestate_fields.cpp`) instead keeps the
generated `## What a save keeps` section true by hand - no checker sees that prose - and says so in its report;
`tools/review_decomp_diff.sh <base>` output (every MARK/ASM line explained); the run:
area, frames, `port: crashed:` (`[port] caught` is Android's crash-buffer line) / `reach-stub` count 0;
upstream-file edits: TARGET_PC strip-diff clean
and `python3 configure.py && $T/ninja` "87 files OK".

Test only what materially changed (user, 2026-10-24): run the gates and runs that touch the code
your diff changed, the bodies you marked and their callers' area, nothing wider. Don't re-run
unchanged families, whole-module sweeps or smoke; say in the report what you skipped. The orchestrator
reviews every diff itself.

## Report

Write `/tmp/opencode/reports/<lane>.md` (overwrite) and reply with it, under ~400 tokens in the reply:
first line `STATUS: DONE` or `STATUS: INCOMPLETE — <reason>`; commits with base; each function: marked
or not and why (arm table in the note, not the reply); files with line numbers; gates; asks; caveats.
