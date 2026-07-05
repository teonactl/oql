# Download OQL

OQL è disponibile in due varianti, entrambe aggiornate automaticamente ad ogni commit su `main`:

| Variante | Contenuto | Aggiornamento |
|---|---|---|
| **Base** (`dev-latest`) | Tutti i cue type, effetti built-in, senza scadenza | Automatico ad ogni commit |
| **Beta Full** (`beta-latest`) | Come Base + scadenza temporale, per test interni | Manuale ad ogni rilascio beta |

---

## Download rapido

### Beta Full (versione più recente con tutti i cue type)

| Piattaforma | File |
|---|---|
| **Linux** (x86_64 AppImage) | [oql-beta-linux.AppImage](https://github.com/teonactl/oql/releases/download/beta-latest/oql-beta-linux.AppImage) |
| **macOS Apple Silicon** (arm64 DMG) | [oql-beta-macos-arm64.dmg](https://github.com/teonactl/oql/releases/download/beta-latest/oql-beta-macos-arm64.dmg) |
| **macOS Intel** (x86_64 DMG) | [oql-beta-macos-x64.dmg](https://github.com/teonactl/oql/releases/download/beta-latest/oql-beta-macos-x64.dmg) |
| **Windows** (installer) | [oql-beta-windows.exe](https://github.com/teonactl/oql/releases/download/beta-latest/oql-beta-windows.exe) |

### Base (build di sviluppo, senza scadenza)

| Piattaforma | File |
|---|---|
| **Linux** (x86_64 AppImage) | [oql-linux.AppImage](https://github.com/teonactl/oql/releases/download/dev-latest/oql-linux.AppImage) |
| **macOS Apple Silicon** (arm64 DMG) | [OQL-macOS-arm64.dmg](https://github.com/teonactl/oql/releases/download/dev-latest/OQL-macOS-arm64.dmg) |
| **macOS Intel** (x86_64 DMG) | [OQL-macOS-x86_64.dmg](https://github.com/teonactl/oql/releases/download/dev-latest/OQL-macOS-x86_64.dmg) |
| **Windows** (installer) | [oql-windows-ci.exe](https://github.com/teonactl/oql/releases/download/dev-latest/oql-windows-ci.exe) |

> Pagina release completa: [github.com/teonactl/oql/releases](https://github.com/teonactl/oql/releases)

---

## Istruzioni di installazione

### Linux

1. Scarica il file `.AppImage`.
2. Se il browser chiede **"Keep file?"** clicca **Keep**.
3. Rendilo eseguibile e avvialo:

```bash
chmod +x oql*.AppImage
./oql*.AppImage
```

In alternativa, usa il file manager per renderlo eseguibile (tasto destro → Proprietà → Esegui come programma) e aprilo con un doppio clic.

---

### macOS

> **Il browser potrebbe avvisarti sul download.** Clicca **"Keep"** o **"Mantieni"** per salvare il file — è sicuro.

1. Apri il file `.dmg` scaricato.
2. Trascina **OQL** nella cartella **Applicazioni**.
3. Espelli l'immagine disco.
4. Apri il **Terminale** (Applicazioni → Utility → Terminale) ed esegui:

```bash
xattr -cr /Applications/OQL.app
```

5. Ora puoi aprire OQL normalmente dal Finder, da Applicazioni o dal Launchpad.

**Perché è necessario questo passaggio?**
OQL non è ancora notarizzata da Apple. macOS Gatekeeper blocca le app scaricate da internet non notarizzate, mostrando un messaggio "l'app è danneggiata". Il comando `xattr -cr` rimuove il flag di quarantena aggiunto automaticamente dal browser. Dovrai ripeterlo ad ogni aggiornamento.

---

### Windows

> **Il browser potrebbe avvisarti sul download.** Se vedi un avviso "Il file potrebbe essere pericoloso", clicca **Mantieni** o **Mantieni comunque**.

1. Esegui il file `.exe` scaricato.
2. Se appare **Windows Defender SmartScreen** con "Il PC è protetto":
   - Clicca **"Ulteriori informazioni"**
   - Clicca **"Esegui comunque"**
3. Segui la procedura guidata di installazione.

**Perché appare SmartScreen?**
L'installer di OQL non è ancora firmato digitalmente con un certificato a pagamento. SmartScreen avvisa per qualsiasi installer senza firma di publisher conosciuto. L'applicazione è sicura — il codice sorgente è pubblico su [github.com/teonactl/oql](https://github.com/teonactl/oql).
