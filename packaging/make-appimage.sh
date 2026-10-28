#!/usr/bin/env bash
#
# Build a self-contained Moch AppImage.
#
#   packaging/make-appimage.sh [VERSION]
#
# The expensive part of a QtWebEngine bundle is not compiling — it is staging
# ~430 MB of Qt, Chromium resources and their dependencies. That staging is
# cached and keyed on the system package versions it copies from, so a
# code-only release costs one incremental C++ build plus a squashfs pack
# (about a minute). Bump a qt6-* package and the next run restages.
#
# Env overrides:
#   APPIMAGE_TOOL   appimagetool binary (default: ~/.local/opt/appimagetool/...)
#   BUILD_DIR       CMake build directory (default: <repo>/build)
#   UPDATE_URL      zsync URL to embed; also emits dist/<name>.zsync
#   SKIP_BUILD=1    package the existing binary
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build}"
DIST="$ROOT/dist"
WORK="$BUILD/appimage"
APPDIR="$WORK/Moch.AppDir"
STAMP_FILE="$WORK/.stage-stamp"
ARCH="${ARCH:-x86_64}"
TOOL="${APPIMAGE_TOOL:-$HOME/.local/opt/appimagetool/usr/bin/appimagetool}"

VERSION="${1:-$(sed -n 's/^project(moch VERSION \([^ ]*\).*/\1/p' "$ROOT/CMakeLists.txt")}"
[[ -n "$VERSION" ]] || { echo "cannot determine version" >&2; exit 1; }

command -v ldd >/dev/null || { echo "ldd is required" >&2; exit 1; }
[[ -x "$TOOL" ]] || { echo "appimagetool not found at $TOOL (set APPIMAGE_TOOL)" >&2; exit 1; }

# ---------------------------------------------------------------- build ----
if [[ "${SKIP_BUILD:-0}" != 1 ]]; then
    # QtWebEngine-heavy translation units peak around 1.2 GB each; on a small
    # machine -j$(nproc) gets cc1plus OOM-killed, so size jobs to free memory.
    if [[ -z "${JOBS:-}" ]]; then
        avail_mb=$(awk '/MemAvailable/{print int($2/1024)}' /proc/meminfo)
        JOBS=$(( avail_mb / 1200 ))
        (( JOBS > $(nproc) )) && JOBS=$(nproc)
        (( JOBS < 1 )) && JOBS=1
    fi
    echo "==> building moch $VERSION (Release, -j$JOBS)"
    cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "$BUILD" --target moch -j"$JOBS"
fi
BIN="$BUILD/src/moch"
[[ -x "$BIN" ]] || { echo "missing binary: $BIN" >&2; exit 1; }

# ------------------------------------------------------------ staging key ----
# Anything that changes the bytes we copy from /usr must invalidate the cache:
# Arch package versions, the binary itself, and this script's logic.
PKGS=(qt6-base qt6-webengine qt6-webchannel qt6-declarative qt6-svg
      qt6-positioning nss icu ffmpeg glib2 libxkbcommon-wayland)
STAMP="$(
    {
        pacman -Q "${PKGS[@]}" 2>/dev/null || true
        sha256sum "$BIN"
        sha256sum "${BASH_SOURCE[0]}"
        uname -m
    } | sha256sum | cut -d' ' -f1
)"

# ------------------------------------------------------- dependency walk ----
# Libraries that must come from the host: the loader/libc family (never bundle
# glibc), the graphics driver stack (bundling it breaks DRI/NVIDIA), and the
# X11/Wayland session libraries the compositor owns.
EXCLUDE_PATTERNS=(
    '^ld-linux'                      # the loader itself
    '^lib(c|m|dl|pthread|rt|util|resolv|nsl|crypt|anl|BrokenLocale)\.so'
    '^lib(GL|EGL|OpenGL|GLX|GLdispatch|glapi|drm|gbm|vulkan|nvidia|cuda)'
    '^lib(X11|Xext|Xrender|Xfixes|Xcursor|Xrandr|Xi|Xinerama|Xcomposite|Xdamage|Xtst|Xxf86vm|xcb|SM|ICE)'
    '^lib(wayland|xkbcommon|input)'
    '^lib(asound|pulse|pipewire|systemd|udev|dbus-1)'
)

is_excluded() {
    local base="${1##*/}" pattern
    for pattern in "${EXCLUDE_PATTERNS[@]}"; do
        [[ "$base" =~ $pattern ]] && return 0
    done
    return 1
}

# Resolves the full dependency closure of the given ELF files to real paths.
deps_of() {
    ldd "$@" 2>/dev/null \
        | awk '/=> \// {print $3} /^[[:space:]]*\/[^[:space:]]+\.so/ {print $1}' \
        | sort -u
}

stage_libs() {
    local so dest
    for so in $(deps_of "$@"); do
        [[ -f "$so" ]] || continue
        is_excluded "$so" && continue
        dest="$APPDIR/usr/lib/${so##*/}"
        [[ -e "$dest" ]] || cp -L "$so" "$dest"
    done
}

if [[ -f "$STAMP_FILE" && "$(cat "$STAMP_FILE")" == "$STAMP" && -x "$APPDIR/usr/bin/moch" ]]; then
    echo "==> staging cache hit ($STAMP) — skipping library copy"
else
    echo "==> staging AppDir from the system ($STAMP)"
    rm -rf "$APPDIR"
    mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib/qt6/plugins" \
             "$APPDIR/usr/share/qt6/resources" \
             "$APPDIR/usr/share/qt6/translations/qtwebengine_locales" \
             "$APPDIR/usr/share/applications" \
             "$APPDIR/usr/share/icons/hicolor"

    # Chromium's helper process, normally looked up in /usr/lib/qt6.
    cp -L /usr/lib/qt6/QtWebEngineProcess "$APPDIR/usr/lib/qt6/QtWebEngineProcess"

    # Chromium's data files: without these QtWebEngine starts and paints blank.
    cp -L /usr/share/qt6/resources/*.pak "$APPDIR/usr/share/qt6/resources/"
    cp -L /usr/share/qt6/resources/v8_context_snapshot.bin "$APPDIR/usr/share/qt6/resources/"
    cp -L /usr/share/qt6/translations/qtwebengine_locales/*.pak \
          "$APPDIR/usr/share/qt6/translations/qtwebengine_locales/"

    # Only the plugin families Moch actually loads: QPA, images, SQLite
    # (history/bookmarks), TLS, icons, and the Wayland/X11 integration —
    # wayland-graphics-integration-client carries the Wayland EGL buffer
    # integration; without it Qt has no GL context and no window opens.
    for family in platforms imageformats iconengines sqldrivers tls \
                  wayland-shell-integration wayland-decoration-client \
                  wayland-graphics-integration-client \
                  xcbglintegrations networkinformation; do
        [[ -d "/usr/lib/qt6/plugins/$family" ]] || continue
        cp -a "/usr/lib/qt6/plugins/$family" "$APPDIR/usr/lib/qt6/plugins/"
    done

    write_stamp=1
fi

# Always refresh the parts that change with the source tree.
install -m 0755 "$BIN" "$APPDIR/usr/bin/moch"
install -m 0755 "$ROOT/packaging/AppRun" "$APPDIR/AppRun"

sed "s/@VERSION@/$VERSION/" "$ROOT/packaging/moch.desktop" > "$APPDIR/moch.desktop"
install -m 0644 "$APPDIR/moch.desktop" "$APPDIR/usr/share/applications/moch.desktop"

for size in 16 24 32 48 64 128 256 512; do
    dir="$APPDIR/usr/share/icons/hicolor/${size}x${size}/apps"
    mkdir -p "$dir"
    rsvg-convert -w "$size" -h "$size" "$ROOT/packaging/moch.svg" -o "$dir/moch.png"
done
cp "$APPDIR/usr/share/icons/hicolor/256x256/apps/moch.png" "$APPDIR/moch.png"
ln -sf moch.png "$APPDIR/.DirIcon"

# Library staging depends on the plugin set, so do it after both exist.
stage_libs "$APPDIR/usr/bin/moch" \
           "$APPDIR/usr/lib/qt6/QtWebEngineProcess" \
           $(find "$APPDIR/usr/lib/qt6/plugins" -name '*.so' -type f)

# Pull in anything the freshly copied libraries themselves need that the
# first sweep could not see (their own new DEPENDENCIES).
for _ in 1 2; do
    stage_libs $(find "$APPDIR/usr/lib" -maxdepth 1 -name '*.so*' -type f) \
               $(find "$APPDIR/usr/lib/qt6/plugins" -name '*.so' -type f)
done

# Re-stamp only when the system payload was actually rebuilt.
if [[ "${write_stamp:-0}" == 1 ]]; then
    echo "$STAMP" > "$STAMP_FILE"
fi

# ------------------------------------------------------------- package ----
mkdir -p "$DIST"
NAME="Moch-$VERSION-$ARCH.AppImage"
echo "==> packing $NAME"

TOOL_ARGS=(--no-appstream)
[[ -n "${UPDATE_URL:-}" ]] && TOOL_ARGS+=(-u "$UPDATE_URL")
rm -f "$DIST/$NAME" "$DIST/$NAME.zsync"

cd "$WORK"
ARCH="$ARCH" VERSION="$VERSION" SOURCE_DATE_EPOCH=1 \
    "$TOOL" "${TOOL_ARGS[@]}" "$APPDIR" "$DIST/$NAME"

(
    cd "$DIST"
    sha256sum "$NAME" > "$NAME.sha256"
)

# Guard against an image that packs to nothing (seen when the runtime and
# mksquashfs race on a cold cache): unpack one file back out of the image and
# require it to match the binary we just built.
VERIFY_DIR="$(mktemp -d)"
trap 'rm -rf "$VERIFY_DIR"' EXIT
if ! ( cd "$VERIFY_DIR" && "$DIST/$NAME" --appimage-extract 'usr/bin/moch' >/dev/null 2>&1 ); then
    echo "verification failed: image is not readable" >&2
    exit 1
fi
if [[ ! -s "$VERIFY_DIR/squashfs-root/usr/bin/moch" ]]; then
    echo "verification failed: usr/bin/moch missing from the image" >&2
    exit 1
fi
BIN_SIZE=$(stat -c %s "$BIN")
IMG_SIZE=$(stat -c %s "$VERIFY_DIR/squashfs-root/usr/bin/moch")
if [[ "$BIN_SIZE" != "$IMG_SIZE" ]]; then
    echo "verification failed: packaged binary is $IMG_SIZE bytes, built one is $BIN_SIZE" >&2
    exit 1
fi
echo "    verified:        usr/bin/moch present ($IMG_SIZE bytes) and readable from the image"

echo
echo "==> $DIST/$NAME  ($(du -h "$DIST/$NAME" | cut -f1))"
echo "    libraries staged: $(find "$APPDIR/usr/lib" -name '*.so*' | wc -l)"
[[ -f "$DIST/$NAME.zsync" ]] && echo "    delta file:       $DIST/$NAME.zsync"
echo "    sha256:           $(cut -d' ' -f1 "$DIST/$NAME.sha256")"
