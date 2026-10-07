# Building MKWii Smart Steering

This is for people who want to build the mod or work on it. Players should
use the zip from the Releases page; see the [README](../README.md).

## How it works

Every kart in Mario Kart Wii has a CPU driver attached; for a human kart the
game only uses it after the finish line. Each race frame, the mod runs that
driver's route-follower to get the steering it would use, without applying
it, and measures how far the kart is from the CPU's route compared with the
route's width there (both from the course's own data). Near the route the
player is free. Further out, the stick is blended toward the CPU's steering,
and the kart's heading is turned a little toward the CPU's target point so it
comes back facing down the track. The kart's speed is held while it
corrects.

`docs/research.md` has the reverse-engineering behind every address and
offset, plus what testing established.

## Requirements

- **Git Bash** (or another bash on Windows).
- **CodeWarrior** PowerPC command-line tools. Install the free
  [NXP CodeWarrior Special Edition for MPC55xx/MPC56xx v2.10](http://cache.nxp.com/lgfiles/devsuites/PowerPC/CW55xx_v2_10_SE.exe),
  then copy `license.dat` from the install folder, and the `.exe` and `.dll`
  files from `PowerPC_EABI_Tools/Command_Line_Tools/`, into `tools/cw/`.
  CodeWarrior is proprietary, so it isn't included in this repository.
- **.NET SDK** 8 or newer. The first build compiles the Kamek linker from
  `tools/kamek-src/` into `tools/kamek/`.
- **Python 3**, for `package.sh` and `tools/verify_addresses.py`.

## Build

```bash
./build.sh          # release build
./build.sh debug    # with OSReport logging
```

The output is `dist/MKWiiSmartSteering/Binaries/Code.pul`. `dist/` is laid
out like an SD card root: copy its contents to a card, or to Dolphin's
`Load/Riivolution` folder.

## Make a release

```bash
./package.sh 1.0.0
```

This builds a release and writes `release/MKWiiSmartSteering-1.0.0.zip`,
ready to attach to a GitHub release. GitHub can't build it automatically
because CodeWarrior can't be redistributed.

## Debugging in Dolphin

The debug build prints `[SmartSteering]` lines through `OSReport` about twice
a second: distance from the route, assist strength, the AI's and player's
steering, speed and the turning correction. To see them:

1. In Dolphin's `Config/Logger.ini`, set `OSREPORT_HLE = True` and
   `WriteToFile = True`. The log goes to `Logs/dolphin.log` in Dolphin's
   user folder.
2. Dolphin only hooks `OSReport` if it knows where it is. Create
   `Maps/RMCP01.map` in Dolphin's user folder containing:

   ```
   .text section layout
   801a25d0 0000008c 801a25d0 0 OSReport
   ```

3. Start Dolphin with debugging enabled (`Dolphin.exe -d`). A Dolphin game
   mod descriptor (`.json` with `riivolution` patches and an option choice)
   can launch straight into the game with the mod enabled.

## Supporting another region

Addresses are written for PAL (RMCP01). Kamek translates them for other
regions using `versions.txt`, but leaves any address outside its ranges
unchanged without warning. `tools/verify_addresses.py` compares the real game
files to catch that:

```bash
python tools/verify_addresses.py <PAL dir> <other region dir> E
```

Each directory needs `main.dol` and `StaticR.rel` extracted from that
region's disc (for example with
[decomp-toolkit](https://github.com/encounter/decomp-toolkit):
`dtk vfs cp "game.rvz:sys/main.dol" .`). Only PAL and NTSC-U pass today, so
`dist/riivolution/MKWiiSmartSteering.xml` only loads the mod on those
discs. To add Japanese or Korean support, verify that region, add any missing
`versions.txt` ranges, and add its loader hook lines to the XML.

Never commit game files. `.gitignore` keeps `work/` (extracted binaries and
disassembly) and `ref/` (reference clones) out of the repository.

## Project layout

| Path | What |
|---|---|
| `src/SmartSteering.cpp` | The mod |
| `src/DriftSelectToggle.cpp` | Per-player on/off switch on the drift select screens |
| `include/toggle.h` | Shared between the two |
| `include/game.h` | Game structures and functions it uses, with where each was verified |
| `externals.txt` | Game symbol addresses (PAL) |
| `versions.txt` | Address translation for other regions (Pulsar's, plus our additions) |
| `include/kamek/` | Kamek hook macros (from Pulsar/Kamek) |
| `tools/kamek-src/` | Kamek linker source (Pulsar's fork) |
| `tools/verify_addresses.py` | Region address checker |
| `dist/` | SD card layout: Riivolution XML, Pulsar's `Loader.pul`, built `Code.pul` |
| `docs/research.md` | Reverse-engineering notes |
