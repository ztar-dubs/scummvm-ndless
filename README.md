# ScummVM for TI-Nspire CX CAS (Ndless)

A port of [ScummVM](https://www.scummvm.org/) to the TI-Nspire CX CAS calculator using the [Ndless](https://ndless.me/) SDK.

Play classic point-and-click adventure games and RPGs on your calculator!

## Supported Games

| Engine | Games |
|--------|-------|
| **SCUMM** (v1-v6) | Monkey Island 1 & 2, Indiana Jones, Day of the Tentacle, Sam & Max, Loom, Maniac Mansion |
| **Kyra** | Legend of Kyrandia 1-3 |
| **EOB** | Eye of the Beholder 1 & 2 |
| **LOL** | Lands of Lore: The Throne of Chaos |
| **AGI** | King's Quest 1-4, Space Quest 1-2, Leisure Suit Larry 1, Police Quest 1 |
| **SCI** (v0-v1) | King's Quest 5-6, Space Quest 3-5, Quest for Glory 1-3, Leisure Suit Larry 2-6 |
| **GOB** | Gobliiins 1-3, Bargon Attack, Woodruff |
| **Queen** | Flight of the Amazon Queen (freeware) |
| **Sky** | Beneath a Steel Sky (freeware) |
| **Lure** | Lure of the Temptress (freeware) |

## Building

### Prerequisites

- [Ndless SDK](https://ndless.me/) installed and configured
- A POSIX environment (Cygwin on Windows, or Linux/macOS)

### Configure

```bash
export PATH="/path/to/ndless-sdk/bin:/path/to/ndless-sdk/toolchain/install/bin:$PATH"
export NDLESS_SDK="/path/to/ndless-sdk"
export CXX="nspire-g++"

./configure --host=nspire --disable-all-engines \
  --enable-engine=scumm \
  --enable-engine=kyra --enable-engine=eob --enable-engine=lol \
  --enable-engine=agi --enable-engine=sci --enable-engine=gob \
  --enable-engine=queen --enable-engine=sky --enable-engine=lure \
  --disable-mt32emu --disable-hq-scalers --disable-translation \
  --disable-eventrecorder --disable-tts
```

### Build

```bash
make -j4
make scummvm.tns
```

The output `scummvm.tns` is the executable for the calculator. It is also automatically copied to `prod/`.

## Installation on Calculator

Copy the following structure to your calculator (e.g. `/documents/scummvm/`):

```
scummvm/
  scummvm.tns              <-- The executable
  data/
    encoding.dat.tns        <-- Required (character encoding)
    kyra.dat.tns            <-- Required for Kyra/EOB/LOL games
    lure.dat.tns            <-- Required for Lure of the Temptress
    scummremastered.zip.tns <-- Recommended (GUI theme)
  roms/
    mygame/                 <-- One folder per game
      DATAFILE1.tns
      ...
  saves/                    <-- Created automatically
```

> **Important:** All files on the Nspire must have the `.tns` extension. Append `.tns` to your game data files (e.g. `RESOURCE.MAP` becomes `RESOURCE.MAP.tns`).

Data files are located in `dists/engine-data/` (for engine .dat files) and `gui/themes/` (for GUI themes) within this source tree.

## Nspire-Specific Source Files

The files created specifically for this port:

```
backends/platform/nspire/
  osystem-nspire.h/cpp     # OSystem backend (events, touchpad, VKB, key mapping)
  nspire-graphics.h/cpp    # Graphics manager (320x240 RGB565, dirty rects, scaling)
  main.cpp                 # Entry point
  module.mk                # Build module
  nspire.mk                # .tns generation rules
  build.sh                 # Build helper script

backends/fs/nspire/
  nspire-fs.h/cpp          # Filesystem node (handles .tns extension transparently)
  nspire-fs-factory.h/cpp  # Filesystem factory
```

Files modified from upstream ScummVM:

```
configure                           # Nspire host detection, toolchain, build flags
backends/module.mk                  # Added Nspire filesystem objects
backends/keymapper/input-watcher.*  # 500ms grace period for key remapping
backends/keymapper/remap-widget.cpp # ConfMan-based VKB communication
gui/browser.cpp                     # Fixed array out-of-bounds bug
```

## Default Controls

| Key | Action |
|-----|--------|
| Touchpad / Arrows | Move cursor |
| Ctrl | Left click |
| Shift | Middle click |
| Var | Right click |
| Enter | Confirm / Return |
| Space | Skip / Pause |
| Menu | Game menu (F5) |
| ON | Quit |
| ON + Esc | Force quit |
| 8 / 2 / 4 / 6 | Cursor Up / Down / Left / Right |
| A-Z, 0-9 | Keyboard input |

All keys can be remapped via Options > Keymaps using the built-in virtual keyboard.

## Documentation

Detailed documentation (installation, game data files, controls, data file reference) is available in:
- `prod/doc/readme.html` (English)
- `prod/doc/lisezmoi.html` (French)

## Credits

- [ScummVM](https://www.scummvm.org/) - The multi-engine game emulator (GPL-3.0)
- [Ndless](https://ndless.me/) - Native development SDK for TI-Nspire
- Based on ScummVM version 2026.1.1git

## License

This project is licensed under the GNU General Public License v3.0 - see the [LICENSE](LICENSE) file for details.
