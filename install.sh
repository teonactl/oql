#!/usr/bin/env bash
# install.sh — installa OpenQLab per l'utente corrente (nessun sudo richiesto)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ── Trova il binario compilato ────────────────────────────────────────────────
BINARY=""
for candidate in \
    "$SCRIPT_DIR/build/openqlab" \
    "$SCRIPT_DIR/build_asan/openqlab"
do
    if [[ -x "$candidate" ]]; then
        BINARY="$candidate"
        break
    fi
done

if [[ -z "$BINARY" ]]; then
    echo "Errore: nessun binario trovato. Compila prima con:"
    echo "  cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)"
    exit 1
fi

echo "Binario trovato: $BINARY"

# ── Destinazioni (XDG user-local, nessun sudo) ────────────────────────────────
BIN_DIR="${HOME}/.local/bin"
ICON_SVG_DIR="${HOME}/.local/share/icons/hicolor/scalable/apps"
ICON_256_DIR="${HOME}/.local/share/icons/hicolor/256x256/apps"
ICON_128_DIR="${HOME}/.local/share/icons/hicolor/128x128/apps"
DESKTOP_DIR="${HOME}/.local/share/applications"

mkdir -p "$BIN_DIR" "$ICON_SVG_DIR" "$ICON_256_DIR" "$ICON_128_DIR" "$DESKTOP_DIR"

# ── Copia il binario ──────────────────────────────────────────────────────────
cp -f "$BINARY" "$BIN_DIR/openqlab"
chmod +x "$BIN_DIR/openqlab"
echo "Binario installato in $BIN_DIR/openqlab"

# ── Installa icona SVG ────────────────────────────────────────────────────────
cp -f "$SCRIPT_DIR/resources/openqlab.svg" "$ICON_SVG_DIR/openqlab.svg"
echo "Icona SVG installata"

# ── Genera icone PNG (fallback per KDE e altri ambienti) ─────────────────────
_svg="$ICON_SVG_DIR/openqlab.svg"
_converted=0
for size in 256 128; do
    _outdir="${HOME}/.local/share/icons/hicolor/${size}x${size}/apps"
    mkdir -p "$_outdir"
    _out="$_outdir/openqlab.png"
    if command -v rsvg-convert &>/dev/null; then
        rsvg-convert -w "$size" -h "$size" "$_svg" -o "$_out" && _converted=1
    elif command -v inkscape &>/dev/null; then
        inkscape --export-type=png --export-width="$size" \
                 --export-filename="$_out" "$_svg" 2>/dev/null && _converted=1
    elif command -v convert &>/dev/null; then
        convert -background none -resize "${size}x${size}" "$_svg" "$_out" && _converted=1
    fi
done
[[ $_converted -eq 1 ]] && echo "Icone PNG generate (128×128, 256×256)" || \
    echo "Nessun convertitore SVG→PNG trovato (rsvg-convert/inkscape/imagemagick), uso solo SVG"

# ── Crea il file .desktop con il percorso assoluto del binario ────────────────
sed "s|Exec=openqlab|Exec=$BIN_DIR/openqlab|" \
    "$SCRIPT_DIR/resources/openqlab.desktop" \
    > "$DESKTOP_DIR/openqlab.desktop"
chmod 644 "$DESKTOP_DIR/openqlab.desktop"
echo "Launcher installato in $DESKTOP_DIR/openqlab.desktop"

# ── Aggiorna le cache desktop/icone ──────────────────────────────────────────
update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true

# KDE Plasma: ricostruisce l'indice dei servizi (necessario per il menu start)
if command -v kbuildsycoca6 &>/dev/null; then
    kbuildsycoca6 --noincremental 2>/dev/null || true
elif command -v kbuildsycoca5 &>/dev/null; then
    kbuildsycoca5 --noincremental 2>/dev/null || true
fi

# GNOME/GTK: aggiorna cache icone hicolor
if command -v gtk-update-icon-cache &>/dev/null; then
    gtk-update-icon-cache -f -t "${HOME}/.local/share/icons/hicolor" 2>/dev/null || true
fi

echo ""
echo "Installazione completata."
echo "Se l'icona non appare nel menu, fai logout e login dalla sessione KDE/GNOME."

# ── Avvisa se ~/.local/bin non è nel PATH ─────────────────────────────────────
if [[ ":$PATH:" != *":$BIN_DIR:"* ]]; then
    echo ""
    echo "NOTA: $BIN_DIR non è nel tuo PATH."
    echo "Aggiungi questa riga al tuo ~/.bashrc o ~/.zshrc:"
    echo "  export PATH=\"\$HOME/.local/bin:\$PATH\""
fi
