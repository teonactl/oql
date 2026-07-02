# Download

Pre-built packages are produced automatically by GitHub Actions on every commit to `main`.

| Platform | Download |
|---|---|
| **Linux** (x86_64 AppImage) | [GitHub Actions → Build Linux](https://github.com/teonactl/oql/actions/workflows/build-linux.yml) |
| **macOS Apple Silicon** (arm64 DMG) | [GitHub Actions → Build macOS](https://github.com/teonactl/oql/actions/workflows/build-macos.yml) |
| **macOS Intel** (x86_64 DMG) | [GitHub Actions → Build macOS](https://github.com/teonactl/oql/actions/workflows/build-macos.yml) |
| **Windows** (installer) | — |

Open the latest successful run and download the artifact for your platform.

---

## Installation instructions

### Linux

1. Download the `.AppImage` file.
2. In your browser, if prompted **"Keep file?"** click **Keep**.
3. Make it executable and run it:

```bash
chmod +x OQL-*.AppImage
./OQL-*.AppImage
```

Or use your file manager to mark it as executable (right-click → Properties → Allow executing as program) and double-click it.

---

### macOS

> **Your browser may warn you about the download.** Click **"Keep"** or **"Allow"** to save the file — it is safe.

1. Open the downloaded `.dmg` file.
2. Drag **OQL** into the **Applications** folder.
3. Eject the disk image.
4. Open **Terminal** (Applications → Utilities → Terminal) and run:

```bash
xattr -cr /Applications/OQL.app
```

5. You can now open OQL normally from the Applications folder or Launchpad.

**Why is this needed?**
OQL is not yet notarized by Apple. macOS Gatekeeper blocks any app downloaded from the internet that is not notarized, showing a "damaged and can't be opened" message. The `xattr -cr` command removes the quarantine flag that the browser added automatically. You will need to repeat this step every time you install a new version.

---

### Windows

> **Your browser may warn you about the download.** If you see a "This file may be dangerous" warning, click **Keep** or **Keep anyway** to save the installer.

1. Run the downloaded installer (`OQL-*-setup.exe`).
2. If **Windows Defender SmartScreen** appears with "Windows protected your PC":
   - Click **"More info"**
   - Click **"Run anyway"**
3. Follow the installation wizard.

**Why does SmartScreen appear?**
OQL's installer is not yet digitally signed with a paid code-signing certificate. SmartScreen warns about any installer that is not signed by a known publisher. The application is safe — you can inspect the source code at [github.com/teonactl/oql](https://github.com/teonactl/oql).
