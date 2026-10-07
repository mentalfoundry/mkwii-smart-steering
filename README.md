# MKWii Smart Steering

**Smart Steering for Mario Kart Wii.** Drive the way you like near the
racing line. Start to run wide toward the grass, a wall or a drop, and
Smart Steering smoothly brings you back onto the track, facing the right
way, without slowing you down.

- Works with both Manual and Automatic drift.
- Steer back toward the track yourself and it lets you.
- Active in offline Grand Prix and VS races. Time Trials and online play are
  left untouched.

## Download

Get the latest `MKWiiSmartSteering-x.y.z.zip` from the
[Releases page](https://github.com/mentalfoundry/mkwii-smart-steering/releases).

Supported games: Mario Kart Wii **PAL** (Europe/Australia) and **NTSC-U**
(North America). Japanese and Korean copies aren't supported yet.

## Install on a Wii

You need a Wii with the Homebrew Channel and
[Riivolution](https://wiibrew.org/wiki/Riivolution), plus your Mario Kart Wii
disc.

1. Extract the zip to the root of your SD card. You should end up with a
   `riivolution` folder and a `MKWiiSmartSteering` folder on the card.
2. Insert the SD card and your Mario Kart Wii disc.
3. Open Riivolution from the Homebrew Channel.
4. Under **MKWii Smart Steering**, set **Smart Steering** to **Enabled**.
5. Choose **Launch**.

## Install in Dolphin

You need a recent version of [Dolphin](https://dolphin-emu.org/download/)
and your own copy of Mario Kart Wii.

1. In Dolphin, choose **File → Open User Folder**, then open the `Load`
   folder and create a `Riivolution` folder inside it if there isn't one.
2. Extract the zip into that `Riivolution` folder.
3. Right-click Mario Kart Wii in Dolphin's game list and choose
   **Start with Riivolution Patches**.
4. Set **Smart Steering** to **Enabled** and choose **Start**.

## Turn it on in the game

Smart Steering starts off for every player. Each player turns it on for
themselves on the **Drift Mode** screen, in Grand Prix and VS:

- Press **−** on the Wii Remote (**X** on the Classic Controller, **Z** on
  the GameCube controller) to switch between **On** and **Off**.
- The bottom of the screen shows **Smart Steering: On** or **Off**. In
  split screen it lists every player, so each person can pick their own.

Your choice lasts until you restart the game.

## Good to know

- Smart Steering is meant to help, not race for you. Near the racing line
  you're fully in control.
- Braking or reversing switches it off while you hold the button. Jumps and
  tricks are left alone.

## Credits

Built on [Kamek](https://github.com/Treeki/Kamek) and the loader from
[Pulsar](https://github.com/MelgMKW/Pulsar), with addresses found using the
[Mario Kart Wii decompilation](https://github.com/riidefi/mkw).

Licensed under the [MIT License](LICENSE). Third-party licenses are in
[third_party_licenses](third_party_licenses/).

Want to build it yourself or see how it works? See
[docs/BUILDING.md](docs/BUILDING.md) and [docs/research.md](docs/research.md).

Mario Kart Wii is a trademark of Nintendo. This project isn't affiliated with
or endorsed by Nintendo and doesn't include any game files.
