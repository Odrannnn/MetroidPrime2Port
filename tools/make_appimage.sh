#!/usr/bin/env bash
# Packages the port as a distributable AppImage: the executable with a
# launcher and icon. The disc image is not included (it is the user's own), so
# the port asks for it on first launch and remembers the answer. No game data
# of any kind is packaged.
#
# Usage: tools/make_appimage.sh [build-dir] [output-dir]
#
# appimagetool is fetched into the output directory on first use; it needs
# network access to pull the AppImage runtime unless --runtime-file is used.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${1:-"$ROOT/build/port-gcc"}
OUT=${2:-"$ROOT/build/appimage"}
BIN="$BUILD/metroid_prime2_port"
APP_ID=io.github.odrannnn.metroidprime2port
ICON="$ROOT/packaging/metroid_prime2_port.png"
DESKTOP="$ROOT/packaging/$APP_ID.desktop"
# The package is for the build host's architecture; appimagetool runs natively.
ARCH=${ARCH:-$(uname -m)}
# A fixed release, checked by hash: the tool runs on the release runner.
TOOL_VERSION=1.9.1
case "$ARCH" in
    x86_64) TOOL_SHA256=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0 ;;
    aarch64) TOOL_SHA256=f0837e7448a0c1e4e650a93bb3e85802546e60654ef287576f46c71c126a9158 ;;
    *) echo "no pinned appimagetool for $ARCH" >&2; exit 1 ;;
esac
TOOL="$OUT/appimagetool-$ARCH.AppImage"
TOOL_URL="https://github.com/AppImage/appimagetool/releases/download/$TOOL_VERSION/appimagetool-$ARCH.AppImage"

if [[ ! -x "$BIN" ]]; then
    echo "no executable at $BIN - build first" >&2
    exit 1
fi
for f in "$ICON" "$DESKTOP"; do
    [[ -f "$f" ]] || { echo "missing $f" >&2; exit 1; }
done
APPDIR="$OUT/MetroidPrime2.AppDir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin"

install -m755 "$BIN" "$APPDIR/usr/bin/metroid_prime2_port"
python3 "$ROOT/tools/write_recomplauncher.py" --platform linux --appimage-arch "$ARCH" \
    "$APPDIR/recomplauncher.json"
# A build with MP_EMBED_RESOURCES carries the textures inside the executable; otherwise the port
# looks for them next to it. Tracked files only in a git checkout, so nothing dropped into textures/
# locally (a dump, a pack made from game data) is handed out with the package.
if ! grep -q '^MP_EMBED_RESOURCES:BOOL=ON' "$BUILD/CMakeCache.txt" 2>/dev/null; then
    if git -C "$ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        (cd "$ROOT" && git ls-files -z textures | xargs -0 cp --parents -t "$APPDIR/usr/bin")
    else
        cp -r "$ROOT/textures" "$APPDIR/usr/bin/textures"
    fi
fi
install -m644 "$ICON" "$APPDIR/$APP_ID.png"
# File managers and AppImage launchers read the icon from here.
ln -s "$APP_ID.png" "$APPDIR/.DirIcon"

# No shared libraries are bundled. The release binary is built on an old base
# (.github/workflows/linux-release.yml), so the system's own freetype and
# libpng are at least as new as the ones it was linked against, while
# a bundled copy drags in its build host's sonames (AlmaLinux's freetype wants
# libbz2.so.1 and harfbuzz, which Debian doesn't provide under those names).
# An AppImage made from a desktop build only runs on distributions at least as
# new as that desktop.

# Third-party notices have to travel with anything that is handed out. The
# shared collector covers the vendored and fetched components, the prebuilt
# packages that ship no licence, and the generated data tables.
"$ROOT/tools/collect_licenses.sh" "$BUILD" "$APPDIR/usr/share/licenses/metroid-prime2-port"

cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/sh
HERE=$(dirname "$(readlink -f "$0")")
exec "$HERE/usr/bin/metroid_prime2_port" "$@"
EOF
chmod 755 "$APPDIR/AppRun"

# The packaged desktop entry is the AppImage's too; its Icon is the app id.
install -m644 "$DESKTOP" "$APPDIR/$APP_ID.desktop"

# A cached copy is reused only if it is the pinned one.
if [[ ! -x "$TOOL" ]] || ! echo "$TOOL_SHA256  $TOOL" | sha256sum -c --quiet - 2>/dev/null; then
    echo "fetching appimagetool $TOOL_VERSION"
    mkdir -p "$OUT"
    curl -fsSL -o "$TOOL.part" "$TOOL_URL"
    echo "$TOOL_SHA256  $TOOL.part" | sha256sum -c --quiet - ||
        { echo "appimagetool download does not match its pinned SHA-256" >&2; rm -f "$TOOL.part"; exit 1; }
    mv "$TOOL.part" "$TOOL"
    chmod +x "$TOOL"
fi

# FUSE is not available everywhere; the tool can unpack itself instead.
RUN=()
if ! "$TOOL" --version >/dev/null 2>&1; then
    RUN=(--appimage-extract-and-run)
fi

# Omit build-host xattrs so symlinks use the basic inode type launcher readers support.
ARCH=$ARCH "$TOOL" "${RUN[@]}" --mksquashfs-opt=-no-xattrs "$APPDIR" "$OUT/MetroidPrime2-$ARCH.AppImage"
echo "wrote $OUT/MetroidPrime2-$ARCH.AppImage"

# Keep the manifest beside the AppImage for launchers without embedded profile support.
PACKAGE="metroid-prime2-port-linux-$ARCH-appimage"
mkdir -p "$OUT/$PACKAGE"
install -m755 "$OUT/MetroidPrime2-$ARCH.AppImage" "$OUT/$PACKAGE/"
install -m644 "$APPDIR/recomplauncher.json" "$OUT/$PACKAGE/recomplauncher.json"
tar -C "$OUT" -czf "$OUT/$PACKAGE.tar.gz" \
    "$PACKAGE/MetroidPrime2-$ARCH.AppImage" "$PACKAGE/recomplauncher.json"
echo "wrote $OUT/$PACKAGE.tar.gz"
