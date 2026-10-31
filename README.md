# Echoes Unbound - Metroid Prime 2 native port

> **Disclaimer:** this is an independent project, not affiliated with or endorsed
> by [PrimeDecomp](https://github.com/PrimeDecomp). It builds on their
> decompilation, but they don't maintain or support this port: please report
> problems here, not to them.

**[Latest release: 0.6.2](https://github.com/Odrannnn/MetroidPrime2Port/releases)**

**Looking for the first game? See the [Metroid Prime Unbound](https://github.com/Odrannnn/MetroidPrimePort).**

**You can find all new features and enhancements in the F1 menu (or start + back on controller)**
**I recommend checking out Portal Transition Skip in F1 > Game!**

[![Linux build](https://github.com/Odrannnn/MetroidPrime2Port/actions/workflows/linux-build.yml/badge.svg?branch=port)](https://github.com/Odrannnn/MetroidPrime2Port/actions/workflows/linux-build.yml)
[![FOSSA Status](https://app.fossa.com/api/projects/git%2Bgithub.com%2FOdrannnn%2FMetroidPrime2Port.svg?type=shield)](https://app.fossa.com/projects/git%2Bgithub.com%2FOdrannnn%2FMetroidPrime2Port?ref=badge_shield)

A native build of **Metroid Prime 2: Echoes** for Linux, Windows, macOS, Android and (experimentally) iOS,
rendered through Aurora (SDL3/WebGPU). There's no emulator involved. Supported
discs: USA GameCube (`G2ME01`), PAL (`G2MP01`) and
Trilogy (`R3ME01` USA, `R3MP01` PAL).
This is a pre-release: please report what issues you find.

**No game content is included.** You need your own copy of the game: the port
reads it from a disc image and never ships any of its assets. This repository
ships no disc image, and neither does any package or release built from it.

## What it adds over the console release

- **Cutscene skip** Switching between the dark and light world has never been so fast! Also try the warp cheat in Debug to swap world without portal!
- **Widescreen** with a widescreen-aware HUD, **render scale**, vsync and high fps support with **frame interpolation**
- **Graphics:** first-person FOV, MSAA, anisotropic filtering, dynamic resolution,
  HUD scale, arm cannon offset, toggles to hide the helmet and visor effects
- **Controls:** mouse and twin-stick aim, gyro aim, lock-on free aim, pointer aim, everything rebindable
- **Optional tweaks:** Fast Morph, Spring Ball, Toggle Lock-On, Sticky Charge,
  walk speed, scan speed, hint delay, elevator ride; an **Original experience**
  switch puts the retail behaviour back
- **Skippable cutscenes**
- **Language** switching on PAL and Trilogy discs
- **Save states** and **quick resume** after quitting
- **Saves:** import and export memory card files, with Dolphin and PrimeHack
  card folders
- **Metroid Prime Trilogy:** Trilogy's HUD frames,
  reticles and, opt-in, its radial beam and visor menus (mouse, pad and touch support)
- **Suit colours** Change colours of Varia, Dark and Light suits
- **Mods:** drop-in mod folders that replace disc data or single resources, and
  an HD texture pack ([docs/MODS.md](docs/MODS.md))
- **Photo mode** with a free camera and high-resolution capture
- **In-game time display**
- **Discord Rich Presence**
- **Mobile touch overlay**
- **F1 overlay** (Or Start + Back combo) This is the menu housing every setting!

## Downloads

Releases are published on the
[GitHub releases page](https://github.com/Odrannnn/MetroidPrime2Port/releases).
**Every build needs your own disc image; none of them contains one.**

| Platform | Asset |
|---|---|
| Linux x86_64 / aarch64 | `metroid-prime2-port-X-linux-{x86_64,aarch64}.tar.gz`, or the AppImage |
| Linux x86_64 (Flatpak) | `MetroidPrime2Port.flatpak` (`flatpak install --user MetroidPrime2Port.flatpak`) |
| Windows x86_64 / arm64 | `metroid-prime2-port-X-windows-{x86_64,arm64}.zip` |
| macOS (Apple silicon) | `metroid-prime2-port-X-macos-arm64.zip` |
| Android (arm64) | `metroid-prime2-port-X-android-arm64.apk` |
| iPhone / iPad (experimental) | `MetroidPrime2Port.ipa`, unsigned: sign it yourself and sideload it ([docs/IOS.md](docs/IOS.md)) |

**Quick start:** download the build for your platform, start it, and pick your disc
image when it asks (Android shows a file picker and copies the image into the
app). Supported discs are the GameCube USA (`G2ME01`) and PAL (`G2MP01`)
releases and the Wii Metroid Prime Trilogy (USA `R3ME01`, PAL `R3MP01`), as
`.iso`, `.gcm`, `.rvz`, `.wia`, `.gcz`, `.ciso` or `.wbfs`. See [docs/PAL.md](docs/PAL.md)
and [docs/TRILOGY.md](docs/TRILOGY.md) for what is verified on each. Press **F1**
for every setting.

On Linux the AppImage bundles no libraries: FreeType, libpng and a Vulkan
driver, X11 or Wayland come from the system.

## Running

```sh
./metroid_prime2_port "/path/to/Metroid Prime 2 - Echoes.iso"
```

The `MP2_DISC` environment variable works too. Headless and scripted runs,
the gameplay harness and the area sweep are in [docs/RUNNING.md](docs/RUNNING.md).

## Building

CMake 3.25+ and Ninja with a C++20 toolchain. A host build needs your disc only at run
time, never to compile. The port configuration is `-DMP_SDK_HEADERS_ONLY=OFF`,
which adds `extern/aurora` (it fetches its own SDL3 and Dawn) and builds the
`metroid_prime2_port` executable:

```sh
cmake -S . -B build-port-link -G Ninja -DMP_SDK_HEADERS_ONLY=OFF
cmake --build build-port-link --target metroid_prime2_port
```

[docs/RUNNING.md](docs/RUNNING.md) has the run, test and coverage commands,
[docs/RELEASING.md](docs/RELEASING.md) what CI builds, [docs/ANDROID.md](docs/ANDROID.md) the
Android build, and [PORT_NOTES.md](PORT_NOTES.md) how the port works (the REL
runtime, `TARGET_PC` rules, SDK shims). The upstream matching build
(`python3 configure.py && ninja`, see [docs/DECOMP_README.md](docs/DECOMP_README.md)) is kept
intact; every port change to an upstream file sits in a `#ifdef TARGET_PC` block.

## Troubleshooting

- **Where is my data?** In a `user/` folder beside the executable (settings, controls, memory
  cards, log, caches), so a copied build carries it with it; an AppImage keeps it in the folder the
  `.AppImage` file is in. Where that folder cannot be written to (a Flatpak, a system package, a
  macOS app bundle) it is the per-user folder (`%APPDATA%\Metroid Prime 2 Echoes`,
  `~/.local/share/Metroid Prime 2: Echoes`, `~/Library/Application Support/Metroid Prime 2: Echoes`).
  An older install whose `settings.cfg` is still in the per-user folder keeps using it until you
  move its contents into `user/`. `MP2_USER_PATH` overrides all of this.
- **Where are the logs?** `metroid_prime2_port.log` in the user folder (F1 > System
  > Log); the previous run's is `metroid_prime2_port.old.log`. Attach it to a bug report.
- **"unsupported disc"**: the image is not one of the releases above. Re-dump it,
  or check you picked the right disc.
- **Settings in a bad state**: F1 > System > Settings > **Reset configuration...**
  backs up and resets every setting and mapping; saves are not touched.

## Credits

This port would not exist without the work of these projects. Thank you.

**Foundation**

- [PrimeDecomp](https://github.com/PrimeDecomp): the Echoes decompilation the
  game code is built from, expanded here into a fully playable port, and the
  decompiled Dolphin SDK.
- [Aurora](https://github.com/encounter/aurora): the GameCube graphics, input and
  platform layer over SDL3 and WebGPU.
- [MusyX](https://github.com/AxioDL/musyx): the audio engine.
- [nod](https://github.com/encounter/nod): the disc image reader.

**Platform and input**

- [libadrenotools](https://github.com/bylaws/libadrenotools): custom Vulkan drivers on Android.
- [GamepadMotionHelpers](https://github.com/JibbSmart/GamepadMotionHelpers): gyro
  calibration and aiming spaces.
- [Kenney Input Prompts](https://kenney.nl/assets/input-prompts) (CC0): the
  controller button icons.
- [Dolphin](https://dolphin-emu.org) and [PrimeHack](https://github.com/shiiion/dolphin):
  the memory card folders the port imports from and exports to.

**Libraries**

[SDL3](https://libsdl.org), [Dawn](https://dawn.googlesource.com/dawn),
[Dear ImGui](https://github.com/ocornut/imgui), [fmt](https://github.com/fmtlib/fmt),
[Abseil](https://abseil.io), [FreeType](https://freetype.org), libpng,
[zstd](https://github.com/facebook/zstd), [xxHash](https://github.com/Cyan4973/xxHash),
[SQLite](https://sqlite.org) and [Tracy](https://github.com/wolfpld/tracy).

## Licensing

Every third-party component keeps its own licence; the full list, with where each
licence text lives, is in [`NOTICE`](NOTICE), and the texts are copied into every
release package.

`LICENSE` (MIT) covers this project's own work — the platform layer, the build and
packaging scripts, and the Android integration. It does **not** cover the game code
under `src/` and `include/`, which comes from the decompilation and remains
Nintendo's. See `NOTICE` for the exact scope and the third-party terms.
`LICENSE-decomp` is upstream PrimeDecomp's CC0 dedication, kept as it was.

Metroid Prime 2: Echoes is a trademark of Nintendo. This project is unaffiliated
with Nintendo and Retro Studios, and ships no game data.


[![FOSSA Status](https://app.fossa.com/api/projects/git%2Bgithub.com%2FOdrannnn%2FMetroidPrime2Port.svg?type=large)](https://app.fossa.com/projects/git%2Bgithub.com%2FOdrannnn%2FMetroidPrime2Port?ref=badge_large)

## Documentation

| Document | What it covers |
|---|---|
| `docs/SETTINGS.md` | Every control, setting and environment variable |
| `docs/QOL.md` | Quality-of-life features: what is in, what is next |
| `docs/RUNNING.md` | Running the port: harness, console scripts, sweeps, coverage |
| `docs/PAL.md` | The PAL disc: what is verified |
| `docs/TRILOGY.md` | Metroid Prime Trilogy: mount, HUD, radial menus |
| `docs/MODS.md` | The mods folder, suit colours, texture pack |
| `docs/CONSOLE.md` | The debug console (`MP2_CONSOLE`) |
| `docs/ANDROID.md` | The Android build, signing, install and crash reading |
| `docs/IOS.md` | iPhone and iPad (experimental): sideloading, build |
| `docs/RANDOMIZER.md` | The item randomizer |
| `docs/RELEASING.md` | What a release contains and must carry |
| `docs/DECOMP_README.md` | PrimeDecomp/echoes README (decomp build) |
| `PORT_NOTES.md` | How the port works and non-obvious facts |

## AI assistance

Parts of this port were written with AI coding assistants, under my oversight.