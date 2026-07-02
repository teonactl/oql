# Beta Testing

Beta builds are time-limited test versions distributed for internal testing before a stable release. They are built from the same codebase as the stable version but may include unfinished features or known issues.

The latest beta release is always available at:
**[github.com/teonactl/oql/releases/tag/beta-latest](https://github.com/teonactl/oql/releases/tag/beta-latest)**

---

## Installation instructions

### Linux

1. Download the `.AppImage` file from the beta release page.
2. In your browser, if prompted **"Keep file?"** click **Keep**.
3. Make it executable and run it:

```bash
chmod +x oql-beta-linux.AppImage
./oql-beta-linux.AppImage
```

---

### macOS

> **Your browser may warn you about the download.** Click **"Keep"** or **"Allow"** to save the file — it is safe.

1. Download the `.dmg` file for your Mac:
   - **Apple Silicon (M1/M2/M3/M4)** → `oql-beta-macos-arm64.dmg`
   - **Intel** → `oql-beta-macos-x64.dmg`
2. Open the `.dmg` file.
3. Drag **OQL** into the **Applications** folder.
4. Eject the disk image.
5. Open **Terminal** (Applications → Utilities → Terminal) and run:

```bash
xattr -cr /Applications/OQL.app
```

6. You can now open OQL normally from the Applications folder or Launchpad.

**Why is this needed?**
OQL is not yet notarized by Apple. macOS Gatekeeper blocks any app downloaded from the internet that is not notarized, showing a "damaged and can't be opened" message. The `xattr -cr` command removes the quarantine flag that the browser added automatically. You will need to repeat this step every time you install a new beta version.

---

### Windows

> **Your browser may warn you about the download.** If you see a "This file may be dangerous" warning, click **Keep** or **Keep anyway** to save the installer.

1. Download `oql-beta-windows.exe` from the beta release page.
2. Run the installer.
3. If **Windows Defender SmartScreen** appears with "Windows protected your PC":
   - Click **"More info"**
   - Click **"Run anyway"**
4. Follow the installation wizard.

**Why does SmartScreen appear?**
OQL's installer is not yet signed with a code-signing certificate. SmartScreen warns about any unsigned installer. The application is safe — the source code is public at [github.com/teonactl/oql](https://github.com/teonactl/oql).

---

## Beta expiry

Beta builds include an expiry date. After that date the application will refuse to start and prompt you to download a newer version. Check the [beta release page](https://github.com/teonactl/oql/releases/tag/beta-latest) for the current expiry date.

---

## Reporting issues

Please report any bugs or unexpected behaviour found in beta builds by opening an issue at [github.com/teonactl/oql/issues](https://github.com/teonactl/oql/issues). Include your platform, OS version, and steps to reproduce.
