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
ICON_DIR="${HOME}/.local/share/icons/hicolor/scalable/apps"
DESKTOP_DIR="${HOME}/.local/share/applications"

mkdir -p "$BIN_DIR" "$ICON_DIR" "$DESKTOP_DIR"

# ── Copia il binario ──────────────────────────────────────────────────────────
cp -f "$BINARY" "$BIN_DIR/openqlab"
chmod +x "$BIN_DIR/openqlab"
echo "Binario installato in $BIN_DIR/openqlab"

# ── Copia l'icona SVG ─────────────────────────────────────────────────────────
cp -f "$SCRIPT_DIR/resources/openqlab.svg" "$ICON_DIR/openqlab.svg"
echo "Icona installata in $ICON_DIR/openqlab.svg"

# ── Crea il file .desktop con il percorso assoluto del binario ────────────────
sed "s|Exec=openqlab|Exec=$BIN_DIR/openqlab|" \
    "$SCRIPT_DIR/resources/openqlab.desktop" \
    > "$DESKTOP_DIR/openqlab.desktop"
chmod 644 "$DESKTOP_DIR/openqlab.desktop"
echo "Launcher installato in $DESKTOP_DIR/openqlab.desktop"

# ── Aggiorna le cache desktop/icone ──────────────────────────────────────────
if command -v update-desktop-database &>/dev/null; then
    update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true
fi
if command -v gtk-update-icon-cache &>/dev/null; then
    gtk-update-icon-cache -f -t "${HOME}/.local/share/icons/hicolor" 2>/dev/null || true
fi
if command -v xdg-desktop-menu &>/dev/null; then
    xdg-desktop-menu forceupdate 2>/dev/null || true
fi

echo ""
echo "Installazione completata."

# ── Avvisa se ~/.local/bin non è nel PATH ─────────────────────────────────────
if [[ ":$PATH:" != *":$BIN_DIR:"* ]]; then
    echo ""
    echo "NOTA: $BIN_DIR non è nel tuo PATH."
    echo "Aggiungi questa riga al tuo ~/.bashrc o ~/.zshrc:"
    echo "  export PATH=\"\$HOME/.local/bin:\$PATH\""
fi
