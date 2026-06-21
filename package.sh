#!/usr/bin/env bash
# package.sh — crea un AppImage distribuibile di OQL
# Uso: ./package.sh [edition]
#   edition: full (default) oppure base
# Output: OQL-<version>-<edition>-x86_64.AppImage

set -euo pipefail
cd "$(dirname "$0")"

EDITION="${1:-full}"
ARCH="x86_64"
VERSION="$(git describe --tags --always --dirty 2>/dev/null || echo "dev")"
OUTNAME="OQL-${VERSION}-${EDITION}-${ARCH}.AppImage"

echo "==> OQL AppImage builder"
echo "    Edition : $EDITION"
echo "    Version : $VERSION"
echo "    Output  : $OUTNAME"
echo ""

# ── 1. Scarica linuxdeploy e il plugin Qt ────────────────────────────────────
TOOLS_DIR="$PWD/tools"
mkdir -p "$TOOLS_DIR"

LD="$TOOLS_DIR/linuxdeploy-${ARCH}.AppImage"
LDQ="$TOOLS_DIR/linuxdeploy-plugin-qt-${ARCH}.AppImage"
AIT="$TOOLS_DIR/appimagetool-${ARCH}.AppImage"

_dl() {
    local url="$1" dst="$2"
    if [[ ! -f "$dst" ]]; then
        echo "  Scarico $(basename "$dst") ..."
        curl -fsSL --retry 3 -o "$dst" "$url"
        chmod +x "$dst"
        echo "  OK"
    else
        echo "  Trovato in cache: $(basename "$dst")"
    fi
}

_dl "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage" "$LD"
_dl "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${ARCH}.AppImage" "$LDQ"
_dl "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-${ARCH}.AppImage" "$AIT"

# ── 2. Build Release ──────────────────────────────────────────────────────────
BUILD_DIR="build-release-${EDITION}"
echo ""
echo "==> Configuro cmake ($BUILD_DIR) ..."
cmake -B "$BUILD_DIR" \
      -DCMAKE_BUILD_TYPE=Release \
      -DOQL_EDITION="${EDITION}" \
      -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
      -DCMAKE_EXE_LINKER_FLAGS="-Wl,--as-needed" \
      2>&1 | grep -E "^--|error:|warning:|-- Found|-- Build" | head -30

echo "==> Compilo con $(nproc) core ..."
cmake --build "$BUILD_DIR" --parallel "$(nproc)"

BINARY="$BUILD_DIR/oql"
[[ -x "$BINARY" ]] || { echo "ERRORE: binario non trovato: $BINARY"; exit 1; }
echo "    Binario: $BINARY ($(du -sh "$BINARY" | cut -f1))"

# ── 3. Prepara AppDir ─────────────────────────────────────────────────────────
APPDIR="$PWD/AppDir-${EDITION}"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/share/applications"
mkdir -p "$APPDIR/usr/share/icons/hicolor/scalable/apps"

cp "$BINARY" "$APPDIR/usr/bin/oql"

cat > "$APPDIR/usr/share/applications/oql.desktop" <<DESKTOP
[Desktop Entry]
Name=OQL
GenericName=Show Control
Comment=Cue-based show control per audio, video e lighting
Exec=oql
Icon=oql
Terminal=false
Type=Application
Categories=AudioVideo;Audio;
Keywords=cue;show;audio;theater;
X-AppImage-Version=${VERSION}
DESKTOP

cp resources/oql.svg "$APPDIR/usr/share/icons/hicolor/scalable/apps/oql.svg"

# ── 4. Lancia linuxdeploy con il plugin Qt ───────────────────────────────────
echo ""
echo "==> Bundling Qt e dipendenze ..."

export QMAKE
QMAKE="$(command -v qmake6 2>/dev/null || command -v qmake)"
export QMAKE

# Passo 1: linuxdeploy bundla Qt e le dipendenze nell'AppDir
APPIMAGE_EXTRACT_AND_RUN=1 \
LINUXDEPLOY_DISABLE_STRIP=1 \
"$LD" \
    --appimage-extract-and-run \
    --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/oql" \
    --desktop-file "$APPDIR/usr/share/applications/oql.desktop" \
    --icon-file "resources/oql.svg" \
    --plugin qt \
    || true   # gli errori di strip sul bundled-strip vecchio sono non fatali

# Passo 2: AppRun e symlink richiesti da appimagetool nella root dell'AppDir
cat > "$APPDIR/AppRun" <<'APPRUN'
#!/bin/bash
HERE="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="${HERE}/usr/plugins"
export QML_IMPORT_PATH="${HERE}/usr/qml"
export XDG_DATA_DIRS="${HERE}/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
exec "${HERE}/usr/bin/oql" "$@"
APPRUN
chmod +x "$APPDIR/AppRun"

# Symlink desktop file e icona nella root (appimagetool li richiede qui)
ln -sf "usr/share/applications/oql.desktop" "$APPDIR/oql.desktop"
ln -sf "usr/share/icons/hicolor/scalable/apps/oql.svg" "$APPDIR/oql.svg"

echo ""
echo "==> Creazione AppImage ..."
APPIMAGE_EXTRACT_AND_RUN=1 \
"$AIT" \
    --appimage-extract-and-run \
    "$APPDIR" \
    "$OUTNAME"

echo ""
echo "================================================================"
echo "  DONE: $OUTNAME"
SIZE="$(du -sh "$OUTNAME" 2>/dev/null | cut -f1)"
echo "  Dimensione: $SIZE"
echo "  Condividi questo file — gira su qualsiasi Linux x86_64."
echo "================================================================"
