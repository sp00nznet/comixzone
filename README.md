# Comix Zone XBLA - Static Recompilation

**Ripping a beat-em-up out of a dead console and into the modern age.**

Comix Zone was a 1995 Sega Genesis classic where you play as Sketch Turner — a comic book artist sucked into his own creation. The 2009 Xbox Live Arcade version by Backbone Entertainment wrapped the original game in a full Xbox 360 engine with widescreen support, HD rendering, achievements, and online leaderboards.

Then Microsoft delisted it. Now it's trapped on dying hardware with no legal way to purchase it.

This project is a **static recompilation** of the Xbox 360 XBLA binary — translating the PowerPC machine code directly into native x86-64 C code that runs on modern PCs. No emulation. No interpretation. Direct, compiled, full-speed native code.

---

## Current Status: Binary Analysis Complete

| Phase | Status |
|-------|--------|
| STFS Container Extraction | Done |
| XEX2 Decryption + LZX Decompression | Done |
| PE Section Mapping | Done |
| ABI Helper Location | Done (8/8 found) |
| Switch Table Extraction | Done (76 tables) |
| Kernel Import Analysis | Done (412 imports) |
| Engine Architecture Analysis | Done |
| XenonRecomp Code Generation | Next |
| Runtime Implementation | Pending |
| Playable Build | The dream |

---

## What We Found Inside

The binary (`ComixZone-Standard-X360-Final.exe`, built March 27 2009) reveals a fascinating architecture:

### It's a Genesis Emulator in a Trenchcoat

Backbone Entertainment didn't port Comix Zone — they built a **full Sega Genesis emulator** and wrapped it in an Xbox 360 shell:

- **Motorola 68000 CPU** — Complete 68K instruction set emulator (`generic68` class). The original Genesis processor, running instruction-by-instruction inside PowerPC code
- **Zilog Z80 Sound CPU** — `jZ80` class handling the Genesis sound subsystem
- **Yamaha YM2612** — FM synthesis chip emulation for that iconic Genesis sound
- **Genesis VDP** — Video Display Processor emulation, outputting through palettized pixel shaders (`ps_palettized.updb`)
- **Original ROMs embedded** — `COMIXZON_U.68K.QZ`, `COMIXZON_E.68K.QZ`, `COMIXZON_J.68K.QZ` (US/EU/JP compressed 68K ROMs)

### Two Modes, One Binary

```
Classic Mode  → 0CLASSIC/COMIXZON.SR  (4.8MB) — Original Genesis game, pixel-perfect
Enhanced Mode → 0B/COMIXZON_FW.SR     (7.9MB) — Full Widescreen with HD assets
```

### The Backbone Entertainment Connection

This is the **same engine** used for The Simpsons Arcade XBLA (2012). We call it the **SegaVintage framework** (from the shader path `c:\dev\SEGAVI~1\`). Shared components:

- `UnitBase` — Base emulation unit class
- `Framework` — Application lifecycle manager
- `Livework` — Xbox Live integration layer
- `DFILE` — Custom file I/O abstraction
- `RamVault` — Memory management
- `SARC_*.BND` — Localized string archives
- `.X360.FNQ` — Font archive format
- `IG_XBLA_*` — Menu system hierarchy
- `DAMPMenu` — Dynamic Application Menu Platform

The Simpsons used a later (2012) evolution of this framework. Cross-pollinating knowledge between the two recomp projects accelerates both.

---

## Binary Layout

```
Address Range        Section     Purpose
─────────────────────────────────────────────────────────
0x82000600-0x82089124  .rdata     Constants, strings, error messages
0x82089200-0x82091A30  .pdata     Exception handling tables
0x820A0000-0x822730BC  .text      Main executable code (1.9MB)
0x82273200-0x822CC2C8  .embsec_*  9 embedded sections (emulator cores)
0x822D0000-0x82D03BD8  .data      Writable data + BSS (10.7MB)
0x82D03C00-0x82D03C08  .XBMOVIE   WMV video codec hooks
0x82D10000-0x82D103C6  .idata     Import address tables
0x82D20000-0x82D200D0  .XBLD      Build metadata
0x82D20200-0x82D35514  .reloc     Relocation fixups

Entry Point: 0x8212CD98
Image Base:  0x82000000
Image Size:  13,893,632 bytes (0xD40000)
```

### ABI Helpers (PowerPC calling convention)

```toml
savegprlr_14  = 0x82310F00    # Save general-purpose registers + link register
restgprlr_14  = 0x82310F50    # Restore GPR + LR
savefpr_14    = 0x82311720    # Save floating-point registers
restfpr_14    = 0x8231176C    # Restore FPR
savevmx_14    = 0x823117C0    # Save VMX/Altivec vector registers (14-31)
restvmx_14    = 0x82311A58    # Restore VMX (14-31)
savevmx_64    = 0x82311854    # Save VMX (64-127)
restvmx_64    = 0x82311AEC    # Restore VMX (64-127)
```

### Kernel Imports

The game imports from two Xbox 360 system libraries:
- **xboxkrnl.exe** — 250 kernel functions (threading, memory, I/O, crypto)
- **xam.xex** — 162 system functions (UI, achievements, networking, profiles)

This is a heavy import footprint — Backbone's engine uses deep system integration for Live features, save data, user profiles, and multiplayer infrastructure.

---

## Game Data Files

| File | Size | Purpose |
|------|------|---------|
| `default.xex` | 1.36 MB | Xbox 360 executable (XEX2, LZX compressed) |
| `COMIXZON_FW.SR` | 7.97 MB | Enhanced widescreen game resources |
| `COMIXZON.SR` | 4.86 MB | Classic mode resources (original Genesis data) |
| `SARC_US_ENGLISH.BND` | 38 KB | English string archive |
| `SARC_FRENCH.BND` | 44 KB | French localization |
| `SARC_GERMAN.BND` | 42 KB | German localization |
| `SARC_ITALIAN.BND` | 42 KB | Italian localization |
| `SARC_SPANISH.BND` | 42 KB | Spanish localization |
| `SARC_JAPANESE.BND` | 51 KB | Japanese localization |
| `HELVETICA_21.X360.FNQ` | 345 KB | Helvetica font |
| `TRAJAN_*.X360.FNQ` | ~2.6 MB | Trajan fonts (24pt, 37pt, regular/bold) |

---

## Recompilation Pipeline

```
 ┌──────────────┐     ┌──────────────┐     ┌───────────────┐
 │  XBLA RAR    │────▶│  STFS/LIVE   │────▶│  default.xex  │
 │  (encrypted) │     │  (container) │     │  (XEX2 + AES) │
 └──────────────┘     └──────────────┘     └───────┬───────┘
                                                    │
                                    ┌───────────────▼───────────────┐
                                    │     AES Decrypt + LZX         │
                                    │     Decompress = PE Image     │
                                    │     (13.8 MB PowerPC binary)  │
                                    └───────────────┬───────────────┘
                                                    │
                      ┌─────────────────────────────┼──────────────────┐
                      │                             │                  │
              ┌───────▼───────┐            ┌────────▼───────┐  ┌──────▼──────┐
              │  find_abi     │            │  extract       │  │  parse_xex  │
              │  _addrs.py    │            │  _switch       │  │  _imports   │
              │  (8 helpers)  │            │  _tables.py    │  │             │
              └───────┬───────┘            │  (76 tables)   │  └──────┬──────┘
                      │                    └────────┬───────┘         │
                      └─────────────────────────────┼─────────────────┘
                                                    │
                                    ┌───────────────▼───────────────┐
                                    │        XenonRecomp            │
                                    │    PowerPC → C++ translation  │
                                    │   (static recompilation)      │
                                    └───────────────┬───────────────┘
                                                    │
                                    ┌───────────────▼───────────────┐
                                    │        ReXGlue SDK            │
                                    │   D3D12 GPU · Audio · Input   │
                                    │   Xbox 360 kernel stubs       │
                                    └───────────────┬───────────────┘
                                                    │
                                    ┌───────────────▼───────────────┐
                                    │     Native x86-64 Windows     │
                                    │        Executable             │
                                    │   ┌─────────────────────┐     │
                                    │   │  Sketch Turner is   │     │
                                    │   │  FREE from the      │     │
                                    │   │  comic book...      │     │
                                    │   │  and the Xbox 360.  │     │
                                    │   └─────────────────────┘     │
                                    └───────────────────────────────┘
```

---

## Building (WIP)

### Prerequisites
- Python 3.8+ with `pycryptodome`
- CMake 3.20+, Ninja, Clang 18+
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) (with Altivec/VMX patches)
- [ReXGlue SDK](https://github.com/hedge-dev/ReXGlue)

### Steps
```bash
# 1. Extract game files
python extract_comixzone.py <STFS_package> game_files/

# 2. Extract PE from XEX2 (with LZX decompression)
python extract_xex.py game_files/default.xex game_files/pe_image.bin

# 3. Generate recompiled code
XenonRecomp config/comixzone.toml

# 4. Build native executable
cd project && cmake --preset win-amd64 && cmake --build out --config Release
```

---

## Technical Notes

### Challenges Specific to This Binary

1. **Genesis Emulation Layer**: The game contains a full 68K CPU emulator, Z80 emulator, YM2612 FM synth, and VDP — all running inside PowerPC code that we're translating to x86-64. This is "emulation inside recompilation" — the recompiled code will be running a Genesis emulator natively on x86-64.

2. **9 Embedded Code Sections**: Unlike simpler XBLA games with one `.text` section, Comix Zone has 9 `.embsec_` sections alongside `.text`, likely separating emulator cores into distinct compilation units.

3. **Heavy Kernel Usage**: 412 total kernel/XAM imports means extensive stub work for Xbox Live features, user profiles, save data, and the achievement system.

4. **WMV Video Codec**: The `.XBMOVIE` section and embedded WMV decoder shaders (`E:\xenon\nov08\...`) indicate video playback support that needs D3D12 translation.

5. **Palettized Rendering**: The Genesis VDP outputs indexed color through custom pixel shaders (`ps_palettized.updb`). The recomp needs to handle this GPU-side rendering path.

### Cross-Reference: Simpsons Arcade XBLA

Both games share Backbone's SegaVintage engine framework. Key areas where Simpsons recomp work directly transfers:
- `Livework` Xbox Live integration stubs
- `DFILE` file I/O abstraction
- `UnitBase` / `Framework` lifecycle
- Menu system (`IG_XBLA_*`, `DAMPMenu`)
- String archive (`SARC_*.BND`) loading
- Font rendering (`.X360.FNQ`)
- `RamVault` memory management

---

## Why Static Recompilation?

Static recompilation is **not emulation**. Instead of interpreting PowerPC instructions one at a time at runtime, we translate the entire binary ahead of time into equivalent x86-64 C code. The result compiles to a native Windows executable that runs at full speed with no emulation overhead.

This approach has already been proven on several XBLA titles:
- **The Simpsons Arcade** — Fully playable, 4-player support
- **Vigilante 8 Arcade** — 90 FPS, split-screen working
- **Crazy Taxi** — In progress, keyboard input working

The toolchain ([XenonRecomp](https://github.com/hedge-dev/XenonRecomp) + [ReXGlue](https://github.com/hedge-dev/ReXGlue)) handles the heavy lifting. Our job is the game-specific analysis, configuration, and runtime integration.

---

## License

This project contains no copyrighted game code or assets. It is a clean-room recompilation tooling project. You need a legitimate copy of the game to use any outputs.
