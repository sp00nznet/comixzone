# Comix Zone XBLA - Binary Analysis

## Overview

- **Filename**: ComixZone-Standard-X360-Final.exe
- **Build Date**: March 27, 2009 15:35:37
- **XDK Version**: 2.0.7978.3 (November 2008 XDK)
- **Developer**: Backbone Entertainment
- **Publisher**: Sega
- **Title ID**: 0x584109A1
- **Engine**: SegaVintage XBLA Classic Framework

## XEX2 Format

- **Encryption**: AES-CBC (Normal, retail key)
- **Compression**: LZX (Normal, window_size=32768)
- **PE Data Offset**: 0x3000
- **Compressed Size**: ~1.3 MB
- **Decompressed Size**: 13,893,632 bytes (0xD40000)

## PE Section Layout

| Section | Virtual Address | Virtual Size | Flags | Purpose |
|---------|----------------|--------------|-------|---------|
| .rdata | 0x82000600 | 0x88B24 | R | Constants, strings, vtables |
| .pdata | 0x82089200 | 0x8A30 | R | Exception/unwind tables |
| .text | 0x820A0000 | 0x1D30BC | CODE,X,R | Main executable code |
| .embsec_ | 0x82273200 | 0x17E54 | CODE,X,R | Embedded section 1 |
| .embsec_ | 0x8228B200 | 0x1DF6C | CODE,X,R | Embedded section 2 |
| .embsec_ | 0x822A9200 | 0x19F0 | CODE,X,R | Embedded section 3 |
| .embsec_ | 0x822AAC00 | 0x6CC4 | CODE,X,R | Embedded section 4 |
| .embsec_ | 0x822B1A00 | 0x1637C | CODE,X,R | Embedded section 5 |
| .embsec_ | 0x822C7E00 | 0x1270 | CODE,X,R | Embedded section 6 |
| .embsec_ | 0x822C9200 | 0x240 | CODE,X,R | Embedded section 7 |
| .embsec_ | 0x822C9600 | 0x2CC8 | CODE,X,R | Embedded section 8 |
| .data | 0x822D0000 | 0xA33BD8 | R,W | Data + BSS |
| .XBMOVIE | 0x82D03C00 | 0x8 | R,W | WMV video hooks |
| .idata | 0x82D10000 | 0x3C6 | R,W | Import tables |
| .XBLD | 0x82D20000 | 0xD0 | R | Build info |
| .reloc | 0x82D20200 | 0x15314 | R | Relocations |

**Total code**: ~2.3 MB across .text + 9 .embsec_ sections.

## Architecture: Genesis Emulator Wrapper

The binary is NOT a traditional game port. It's a full **Sega Genesis hardware emulator** wrapped in Xbox 360 infrastructure:

### Emulated Hardware

| Component | Class Name | Genesis Chip |
|-----------|-----------|-------------|
| Main CPU | `generic68` | Motorola 68000 (16-bit, 7.67 MHz) |
| Sound CPU | `jZ80` | Zilog Z80 (8-bit, 3.58 MHz) |
| FM Synth | `YM2612` | Yamaha YM2612 (6-channel FM) |
| Video | VDP (unnamed) | Genesis VDP (64 colors from 512) |
| System | `Genesis_unit` | Full Genesis system coordinator |

### ROM Files

The original Genesis ROMs are embedded as compressed archives:
- `COMIXZON_U.68K.QZ` — US version ROM
- `COMIXZON_E.68K.QZ` — European version ROM
- `COMIXZON_J.68K.QZ` — Japanese version ROM

### Rendering Pipeline

```
Genesis VDP Output (indexed palette, 320x224)
    ↓
Palettized Pixel Shader (ps_palettized.updb)
    ↓
D3D9 Render Target (Xbox 360 Xenos GPU)
    ↓
Display Output (720p with filtering/scaling)
```

Shader source path leaked in binary:
```
c:\dev\SEGAVI~1\bin\lib\rend\Standard-X360-Final\ps_palettized.updb
c:\dev\SEGAVI~1\bin\lib\rend\Standard-X360-Final\ps_filter_palletized.updb
```

## Kernel Imports

### xboxkrnl.exe (250 functions)
Core kernel: threading, synchronization, memory management, file I/O, crypto, GPU.

### xam.xex (162 functions)
System services: Xbox Live, achievements, user profiles, UI overlays, networking, voice chat.

**Total**: 412 kernel imports — significantly heavier than typical XBLA games, reflecting Backbone's deep system integration.

## Key Strings / Engine Identifiers

```
"Version X360 Mar 27 2009 15:35:37"  — Build timestamp
"Sega Genesis"                        — Hardware target
"EmulationRender"                     — Render callback
"0CLASSIC/%s.SR"                      — Classic mode resource path
"0B/SARC_%s.BND"                      — Localization archive path
"IG_XBLA_TitleScreen"                 — Menu system entry point
"IG_PauseMenu"                        — In-game pause
"DAMPMenu"                            — Menu framework
"ShadowRenderThread"                  — Rendering thread name
"USERSETTINGS"                        — Profile/settings system
"XAudio2Create"                       — Audio initialization
"Framework::HandleCriticalReadError"  — Error handler
```

## Backbone Entertainment Engine (SegaVintage)

### Shared with The Simpsons Arcade XBLA (2012)

| Component | Comix Zone | Simpsons | Shared? |
|-----------|-----------|----------|---------|
| UnitBase class | Yes | Yes | Yes |
| Framework lifecycle | Yes | Yes | Yes |
| Livework (Xbox Live) | Yes | Yes | Yes |
| DFILE (File I/O) | Yes | Yes | Yes |
| RamVault (Memory) | Yes | Yes | Yes |
| SARC_*.BND (Strings) | Yes | Yes | Yes |
| .X360.FNQ (Fonts) | Yes | Yes | Yes |
| IG_XBLA_* (Menus) | Yes | Yes | Yes |
| DAMPMenu | Yes | Yes | Yes |
| .SR (Resources) | Yes | Yes | Yes |
| VD* kernel APIs | Yes | Yes | Yes |
| 68K emulation | Yes | No (CPS arcade) | No |
| Z80 emulation | Yes | No | No |
| YM2612 FM synth | Yes | No | No |

The Simpsons used a later evolution (2012) of this framework, wrapping a Konami CPS arcade emulator instead of a Genesis emulator. Core infrastructure is identical.
