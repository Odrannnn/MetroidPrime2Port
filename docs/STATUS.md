# Session status (detail)

The Status section of AGENTS.md links here: this is the per-session detail, rewritten rather than
appended, dated as in the heading there.

## 2026-10-31: save-state sweep, sessions 2-3

Merged into port (unpushed):
- sc/restorehold: `src/MetroidPrime/main.cpp` fixed-tick loop counts ticks actually run and stops on
  `PortOverlay::TickHold()` (Rezbit timer / ai_time drift after a restore).
- kbres: knockback resistance saved after hp (optional trailing field; Dark Trooper).
- ds2doors: an hp <= 0 restore marks actors/platforms dead (`PortRoomState::MarkDead`).
- sc/quadraxis: FSM2 machines saved with sub-machine path + leaf node index; Quadraxis 3/3, 0 strict.
- sc/dg-collider: `tools/statecheck.py` `step_ticks()` (console `step N` only queues; polls `frame`),
  used by A1, C (`--post-wait`) and `--wait`. Quadraxis x10CC_ `cls:X -> none` in B is a lazy rebuild
  in `x61DC_` on the first Think: harmless; use `--post-wait ~30`.
- Ending slots 53→56, dg.kneecol/killknee route, `docs/SAVESTATE_SCHEMA.md` "what a save keeps",
  RUNNING.md names `MP_SAVESTATE_FIELDS=0`.
- Docs audits: GOTCHAS, TRILOGY, DECOMP_README, LANE_BRIEF, DECOMP_WORKFLOW, MODULES, SETTINGS,
  env claims (settings.cpp header, QOL idle cursor, console `(env X set; wins if the key reads it)`),
  CONSOLE.md + console help strings (state hold, crosshair gamecube, hint radius, place angles), MODS.md.

Lanes running at handoff (review the full diff, merge, release the slot):
- sonnet `ses_e6cd854abffeETfhrA5kCCHUtY` sc/ing3stage1 (lane2) → /tmp/opencode/reports/sc-ing3stage1.md.
- sonnet `ses_e6cb09371ffeccjj7P9el6yQl6` sc/dg-pickups (lane1) → sc-dg-pickups.md.
- sonnet `ses_e6caf0fcfffeO5GjyT4GwY4g0k` sc/statecheck-setup (lane3; `--setup` `;` desync, dead
  Puffer round trip) → sc-statecheck-setup.md.
- worker `ses_e6ca17b9effeNnn05Q9aNlyfQH` docs/mods-followup (settings index texture rows filed
  under Game, stale header comments) → mods-followup.md.

Open: CMinorIng Ing3 ai Idle→PathFind; knee-kill pickups lost on round trip; retail Puffer has no
engaged state. `platform/debug_ui.cpp:296` has the old env claim but is unbuilt. Statecheck runs:
copy the userdir, own --port/--display, `--build <tree>/build-port-link`.

## 2026-10-31: behaviour audit 2 (A8) complete

- Ledger `docs/AUDIT.md` A8-1..A8-28 all closed. Fixes merged: A8-17 morph-ball Render PC arm
  (fix/a8-ballrender, reviewed vs asm 800C4404-800C4F40, review rework d00eab8b5), A8-24 snow forces,
  A8-25 rain splash generator, docs RUNNING/GOTCHAS staleness.
- User decisions: A8-16 keep the #30 multiplayer gate; A8-22 keep the once-per-system screen copy
  (perf); A8-26 keep the confirming START press; A8-28 keep Remastered crosshair default.
- Not seen on screen: morph-ball rain splash and spider glass (no raining area / magnet track reached).

## 2026-10-29: 0.6.3 released

- Bumped `versionCode` 9 / `versionName` "0.6.3", metainfo 0.6.3 entry, notes
  `docs/release-notes/0.6.3.md`; release commit `a158c2851` (fixes #10-#15 and a save-state layer fix since 0.6.2).
- CI on `a158c2851`, all `success`: linux-release 40930262879, windows 40930267321, macos
  40930272480, android 40930276804, ios 40930281275. Flatpak built locally from `a158c2851` (12,250,288 B).
- Crash soak (`build-port-link` at `a158c2851`): `build/s-release-2026-10-29c/soak/summary.tsv`,
  **96/96 rows `ok`**, exit 0. Log `/tmp/opencode/soak-063.log`.
- Artefact checks from `build/release-0.6.3/out` (12 assets): the version string 0.6.3 is in
  the Windows, macOS, Linux (tarballs and both AppImages) and APK binaries; IPA `Info.plist` 0.6.3; no game data in any zip, APK, IPA,
  tarball or AppImage; no `user/` folder in the Windows zips; macOS executable chmod +x before zipping.
- Tag `v0.6.3` = `a158c2851`; GitHub release
  https://github.com/Odrannnn/MetroidPrime2Port/releases/tag/v0.6.3, normal release
  (`isPrerelease` false, 12 assets); `releases/latest` answers `v0.6.3`. Not installed on a device.

## Open items carried from older sessions

Sessions older than 0.6.2 moved to `docs/STATUS-history.md`; what is still open from them:

- **Android verification pending** (2026-10-26 widescreen/ball/suit batch): touch layouts at 16:9/21:9, the pause Inventory recolor and the ball shadow have not been run on the phone.
- **Shared spare copy buffer** (2026-10-20 scan-visor session): camera filters, `CPlayerGun` and the particles still copy into the shared spare buffer without a `Destination` and could take each other's resolved texture; fold them into `Destination` when one shows the symptom.
- **Echoes' prompt-glyph `kKeys`** (2026-10-19 twenty-fourth session): the game draws prompts from GUI models (`model_abutton`), not 32x32 textures; building `kKeys` needs a dump taken while a prompt is up plus the model's own texture size/hash.

Older sessions: `docs/STATUS-history.md`.


## Smoke rules (2026-10-24)

Poke your own family's actor; no `=> err` proofs; strictly increasing @f; objs checked >=40f after a kill; editor ids, not uids; no forced Dead. Beam-free lanes = `worker` + model `opencode-go/beam-free`; their reports are unreliable, so check con_*.out.
