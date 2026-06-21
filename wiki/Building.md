# Building from Source

## Requirements

| Dependency | Minimum version | Notes |
|---|---|---|
| CMake | 3.20 | Build system |
| Qt | 6.4 | Core, Widgets, Multimedia, MultimediaWidgets, Svg |
| C++ compiler | GCC 11 / Clang 14 / MSVC 2022 | C++20 required |
| lilv | any | LV2 host library (optional, enables LV2 plugins) |
| libX11 | any | Linux only — for VST2 native editor embedding |
| FFmpeg / GStreamer | — | Pulled in transitively by Qt Multimedia |

---

## Linux

### Arch Linux

```bash
sudo pacman -S cmake qt6-base qt6-multimedia qt6-svg \
               lilv libx11 gcc
```

### Ubuntu 24.04 / Debian 12

```bash
sudo apt install cmake \
    qt6-base-dev qt6-multimedia-dev libqt6svg6-dev \
    liblilv-dev libx11-dev \
    build-essential
```

### Fedora 40+

```bash
sudo dnf install cmake \
    qt6-qtbase-devel qt6-qtmultimedia-devel qt6-qtsvg-devel \
    lilv-devel libX11-devel gcc-c++
```

### Build (all distros)

```bash
git clone https://github.com/teonactl/oql.git
cd oql

# Release build (recommended for production use)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Debug build with AddressSanitizer (for development)
cmake -B build_asan \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -g"
cmake --build build_asan -j$(nproc)
```

### Install launcher and icon

```bash
./install.sh
```

Installs to `~/.local/` (no root required). Works on KDE, GNOME, XFCE, MATE, Cinnamon, LXQt, Budgie.

---

## macOS

```bash
brew install cmake qt@6 lilv

export PATH="$(brew --prefix qt@6)/bin:$PATH"
export CMAKE_PREFIX_PATH="$(brew --prefix qt@6)"

git clone https://github.com/teonactl/oql.git
cd oql
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(sysctl -n hw.logicalcpu)
open build/oql.app   # or ./build/oql
```

> Native VST2 editor embedding is currently Linux-only (requires X11). VST2 audio processing works on macOS; only the GUI editor window is disabled.

---

## Windows

1. Install Qt 6 via the [Qt online installer](https://www.qt.io/download-open-source) — select the MSVC 2022 kit.
2. Install [CMake](https://cmake.org/download/) and [Visual Studio 2022](https://visualstudio.microsoft.com/).
3. Open a **Developer PowerShell for VS 2022** window:

```powershell
git clone https://github.com/teonactl/oql.git
cd oql
cmake -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.x.x\msvc2022_64"
cmake --build build --config Release
.\build\Release\oql.exe
```

> LV2 support on Windows requires building lilv from source or disabling it via `-DWITH_LV2=OFF` (cmake option not yet wired — comment out the `pkg_check_modules(LILV …)` line in `CMakeLists.txt`).

---

## CMake options

| Option | Default | Description |
|---|---|---|
| `CMAKE_BUILD_TYPE` | `Debug` | `Release` for production, `Debug` for development |
| `CMAKE_PREFIX_PATH` | — | Path to Qt installation (needed on macOS/Windows) |

---

## Folder structure after build

```
build/
└── oql          # the executable
```

All resources are compiled into the binary via Qt's resource system (`resources.qrc`).

---

## Optional: VST2 plugins for testing

OQL ships no plugins. For testing, install ZynFusion's standalone effects:

```bash
# Arch
sudo pacman -S zyn-fusion

# Ubuntu / Debian
sudo apt install zynaddsubfx
```

ZynReverb and ZynAlienWah will appear in `/usr/lib/vst/` or `/usr/lib/lxvst/`.
