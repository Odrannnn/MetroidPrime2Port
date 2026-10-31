# Session status (detail)

The Status section of AGENTS.md links here: this is the per-session detail, rewritten rather than
appended, dated as in the heading there.

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
