#!/usr/bin/env bash
#
# Register an AppImage with the desktop so it appears in the launcher.
#
#   packaging/install.sh [path/to/Moch-*.AppImage]
#
# An AppImage does not install itself: the .desktop file inside the image is
# only read by integration daemons such as appimaged, which most systems do
# not run. Without an entry in ~/.local/share/applications the launcher has
# nothing to list, so this copies the image to ~/Applications and writes a
# desktop entry pointing at it.
#
# The entry deliberately uses the name "Moch Browser" and the icon
# "moch-browser": "Moch" and "moch.png" already belong to an unrelated
# application on machines that have one, and icon names are global.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_DIR="${APP_DIR:-$HOME/Applications}"
DATA_DIR="${XDG_DATA_HOME:-$HOME/.local/share}"
APPS_DIR="$DATA_DIR/applications"
ICON_ROOT="$DATA_DIR/icons/hicolor"
DESKTOP_FILE="$APPS_DIR/moch-browser.desktop"
VERSION="$(sed -n 's/^project(moch VERSION \([^ ]*\).*/\1/p' "$ROOT/CMakeLists.txt")"

SRC="${1:-$ROOT/dist/Moch-$VERSION-x86_64.AppImage}"
[[ -f "$SRC" ]] || { echo "no AppImage at $SRC" >&2; exit 1; }
SRC="$(readlink -f "$SRC")"

mkdir -p "$APP_DIR" "$APPS_DIR" "$ICON_ROOT"
DEST="$APP_DIR/$(basename "$SRC")"
if [[ "$SRC" != "$DEST" ]]; then
    echo "==> installing $(basename "$SRC") to $APP_DIR"
    install -m 0755 "$SRC" "$DEST"
else
    chmod 0755 "$DEST"
fi

for size in 16 24 32 48 64 128 256 512; do
    dir="$ICON_ROOT/${size}x${size}/apps"
    mkdir -p "$dir"
    rsvg-convert -w "$size" -h "$size" "$ROOT/packaging/moch.svg" -o "$dir/moch-browser.png"
done

cat > "$DESKTOP_FILE" <<EOF
[Desktop Entry]
Type=Application
Name=Moch Browser
GenericName=Web Browser
Comment=AI-native web browser with a built-in browsing agent
Exec=$DEST %U
Icon=moch-browser
Terminal=false
Categories=Network;WebBrowser;
Keywords=browser;web;ai;agent;
MimeType=x-scheme-handler/http;x-scheme-handler/https;text/html;application/xhtml+xml;
StartupNotify=true
StartupWMClass=moch
X-AppImage-Version=$VERSION
EOF

chmod 0644 "$DESKTOP_FILE"

if command -v update-desktop-database >/dev/null; then
    update-desktop-database "$APPS_DIR" >/dev/null 2>&1 || true
fi
if command -v gtk-update-icon-cache >/dev/null; then
    gtk-update-icon-cache -f -t "$ICON_ROOT" >/dev/null 2>&1 || true
fi

echo "==> desktop entry: $DESKTOP_FILE"
echo "    launches:      $DEST"
echo "    icon:          moch-browser (16-512 px)"
echo
echo "Moch Browser should now appear in the launcher. If it does not, the"
echo "launcher is caching its index: reload it, or log out and back in."
