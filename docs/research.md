# Research notes

How the addresses and offsets in `include/game.h`, `externals.txt` and the
hooks in `src/SmartSteering.cpp` were established. All addresses are PAL
(RMCP01) unless noted. Re-run `tools/verify_addresses.py` after changing any
of them.

## Sources

- **Disassembly.** `main.dol` and `StaticR.rel` were extracted from the
  user's PAL disc with decomp-toolkit (`dtk vfs cp`) and split with
  `dtk dol split` using the [riidefi/mkw](https://github.com/riidefi/mkw)
  config. The SHA-1s match the decomp's expected hashes (main.dol
  `ac7d7244…`, StaticR.rel `887bcc07…`), so its symbol names apply exactly.
- **Pulsar headers** ([MelgMKW/Pulsar](https://github.com/MelgMKW/Pulsar),
  MIT) as a second source. They are useful, but not always right:
  `AI::Player` lists two fields at `0x140`, and the `0x2` button bit is
  documented as "brake" when it is also set while drifting.
- **NTSC-U disc** for checking the region mapping (`RMCE01`).

`work/` (game-derived files, not committed) holds the extracted binaries
and the split. `work/tools/fn.py <addr>` prints a function's disassembly by
PAL address.

## Section bases (StaticR.rel, PAL)

| Section | Base | Evidence |
|---|---|---|
| .text | `0x805103B4` | decomp `MapdataFileAccessor` ctor at .text+0x2878 = Pulsar `0x80512C2C` |
| .data | `0x808B2BD0` | `__vt__Q25Enemy2AI` at .data+0x16D08 = Pulsar `0x808C98D8`; KPadPlayer vtable at .data+0x1C0 = Pulsar `0x808B2D90`; matches the REL file layout (load `0x805102E0` + section offset `0x3A28F0`) |
| .bss | `0x809BD6E0` | CourseModel (+0x5864) = Pulsar `0x809C2F44`; RaceConfig (+0x48) = `0x809BD728`; RaceManager (+0x50) = `0x809BD730`; KPadDirector (+0x2C) = `0x809BD70C` |

## How Smart Steering hooks in

**AI side.** Every kart, human ones included, has an `AI::Player`. For
humans, the game switches it to CPU driving after the finish line. The CPU
update (`0x80732A70`, post-race variant `0x80732DE0`) works like this:

1. Clear `inputs->buttons` (+0xC) and `inputs->steer` (+0x10, `0.0f`), and
   reset the state at `+0x150`.
2. Apply the route-resync rule using flags `+0x160/+0x161/+0x162`. That
   rule calls `cpuDriving->vf[0x30](kartPos)`.
3. Call `cpuDriving->vf[0x1C]()` (vtable pointer at `cpuDriving+0x34`),
   plus helpers at `+0x148/+0x14C/+0x154/+0x158`.
4. Encode `stickX = 7 + 7*steer` into the state (`setStickXUnmirrored`).
5. Apply the state through `RaceManager::Player+0x48` → `vf[0x14]` (`0x80535718`).

The mod runs steps 1–3 (only `cpuDriving`'s update, not the other helpers)
from a wrapper around `UpdateRealPlayerDriving` (`0x80732C70`), and never
runs step 5. `+0x158` already runs every frame in `UpdateRealPlayerDriving`,
so calling it again would double-update it. Nothing calls `0x80732C70`
directly: it appears only in three vtables at slot `+0x38`
(`0x808CA73C` Player, `0x808CA7C0` PlayerBike, `0x808CA818` PlayerKart).

**Input side.** `KPadDirector::calc` (`0x805238F0`) calls
`vf[0xC](isPaused)` on the four human pads (`director+0x4`, stride `0xEC`).
`isPaused` is `director+0x4154`. For human pads that call is
`KPadPlayer::calc` (`0x80521768`). It fills the current state at `+0x28`,
then writes the ghost frame from it. The mod wraps that vtable slot
(`0x808B2D9C`) and edits `+0x28` afterwards. Human pads ignore
`setInputState` (`0x805226F4` is a bare `blr`).

**Stick encoding.** `stickX = (raw - 7) / 7`, negated for human pads in
mirror mode (`setStickXMirrored`, `0x8051E960`, reads `KPadDirector+0x4155`).
The constant `7.0` was read from rodata (`@1719`). The AI path uses the
unmirrored setter, so the AI's `steer` and a human's `stickX` share the
kart's frame.

**Buttons** (`KPadRaceInputState::buttons`), from
`KPadGCController::calcInner` (`0x805201B0`): A → `0x1` (accelerate), L →
`0x4` (item), B or R held → `0x2`, and `0x8` (hop/drift) is latched when B/R
is pressed while accelerating. So `0x2` without `0x8` is braking/reversing.

**Game mode.** `RaceConfig+0xB70`: GP = 0 (or 7), VS = 1, TT = 2/5,
online = 7–10 (`isGp`/`isVs`/`isTt`/`isOnline`, `0x8073965C`…).

**Kart flags** (`KartObjectProxy`, word at settings+0x14): CPU `0x1`, local
`0x2`, ghost `0x40`.

**The AI's route.** `cpuDriving` is an `Enemy::AIControlBase`.
`AIControlBase::initAfterManager` (`0x8072A21C`) shows `+0x38` = AI inputs and
`+0x3C` = `EnemyRouteController`. The route controller's `+0x14` is the
`ENPTController` (also used that way by
`EnemyRouteController::IsOutCurENPTBounds`, `0x8073BD50`).
`GetNext/GetCur/GetPreENPTPosition` (`0x8073CB0C`/`CAB8`/`CA64`) read point
indices at `+0x9`/`+0xA`/`+0xB` and look them up with
`CourseMap::getEnemyPoint(u16)` (`0x80514B7C`; `CourseMap::spInstance`
`0x809BD6E8`). The returned object's `+0x4` points at the raw KMP ENPT:
position at `+0x0`, width at `+0xC`. `GetStartingENPTWidth` (`0x8073CCA8`)
scales the width by `50.0` (rodata `lbl_1_data_18474`). On Mushroom Gorge
the scaled half-widths were mostly 750, ranging from about 340 to 920.

**Kart speed.** `KartObjectProxy` → `+0x0` accessor → `+0x28` `KartMove`.
`getBaseSpeed` (`0x805910B0`) reads `+0x14` and `getVehicleSpeed`
(`0x80590CF8`) reads `+0x20`, which matches Pulsar's `Kart::Movement`
(`baseSpeed` `0x14`, `engineSpeed` `0x20`).

**Kart orientation.** `KartObjectProxy::setRot` (`0x80590288`) goes accessor
`+0x8` → `+0x90` → `+0x4` (the physics object; `getPos` reads `+0x68` on the
same path) and writes the same quaternion to `+0xF0` and `+0x100`. Pulsar
names these `mainRot` and `fullRot` (the second includes trick rotation).
Quaternions are `x, y, z, w` with the standard Hamilton product (decomp
`lib/egg/math/eggQuat.hpp`). The turning cheat multiplies both by a small
rotation about the world's vertical axis. Only the orientation is written,
not the velocity; in testing the kart's motion followed its new heading.

**The AI's target point.** `ENPTController+0x14` is the point the AI steers
toward, as returned by `GetCurPosition` (`0x8073CB60`).

**Start boost.** `KartState::ComputeStartBoost` (`0x805959D4`) runs when the
countdown reaches 0 and sets the start-boost index (accessor `+0x4` → `+0xA0`,
the path `KartObjectProxy::setStartBoostIdx` at `0x80590380` writes) to −1, a
burnout, if the charge at `+0x9C` is over a threshold. The countdown is
`240 − RaceManager+0x20` (`RaceInfo_getCountdown`, `0x80533090`). The mod's
per-frame update first runs one frame after GO. A stall at the start line in
testing was a burnout (charge 0.976), not the mod.

## Region mapping

Pulsar's `versions.txt` didn't cover the two vtables we patch for NTSC-U.
Kamek passes unmapped addresses through unchanged, so the hooks would have
hit the wrong memory on RMCE. The ranges added at the end of `[E]` come from
RMCE01's relocation table:

| PAL slot | RMCE slot |
|---|---|
| `0x808CA774`, `0x808CA7F8`, `0x808CA850` | `0x808C596C`, `0x808C59F0`, `0x808C5A48` (−0x4E08) |
| `0x808B2D9C` | `0x808AE6EC` (−0x46B0) |

`tools/verify_addresses.py work/RMCP01 work/RMCE01 E` passes every
function, vtable slot and singleton check. RMCJ/RMCK haven't been checked,
so the Riivolution XML doesn't load the mod on those discs.

## Pulsar loader quirk

Pulsar's `Loader.pul` (disassembled at `0x80004000`) applies `Code.pul` in
two passes:

- **Pass 1, at DOL boot:** copies the code and applies relative commands,
  plus absolute commands below a boundary address.
- **Pass 2, after StaticR.rel loads:** applies absolute commands at or above
  the boundary.

When it skips a command that belongs to the other pass, it doesn't step
over that command's address and argument words. Those words are then read
as commands themselves. That is where each skipped StaticR write logs
"Unknown command: 128" (its `0x80xxxxxx` address word) and "Unknown command:
0" (its small argument word).

This is harmless as long as no leftover word decodes to a real command.
Keep absolute patches inside StaticR to pointer writes (`kmWritePointer`)
and branches. Never `kmWrite32` an arbitrary value into StaticR: a value
whose top byte is 1, 4–6, 10, 32–38, 64 or 65 would be executed as a
command in pass 1.

## Riivolution settings

Riivolution can't patch `Code.pul` (Kamek places it dynamically), so each
setting is a word that a `<memory>` patch writes to a fixed address and the
code reads (`Settings` in `src/SmartSteering.cpp`). The block starts at
`0x80005800`, in padding of the unused debugger (TRK) interrupt table
`0x80004000–0x80005F34`:

- `0x80005734–0x80005C00` is zero in both the RMCP and RMCE `main.dol`.
  The patches use `original="00000000"` to guard against anything else
  being there.
- Pulsar's loader overwrites `0x80004000–0x80004ADC` and keeps its variables
  at `0x80004ADC–0x80004AE7` (from its disassembly). Nothing else touches
  the table in retail.

| Address | Setting | Values |
|---|---|---|
| `0x80005800` | Drift mode | 0 = all players, 1 = Automatic drift only, 2 = Manual drift only |

Automatic drift is read from `KartState+0x14 & 0x10` (flag `0x84`). The
`KartState` constructor (`0x805943B4`) sets it from the player's
controller, and `KartState::reset` (`0x80594594`) clears only `+0x4..+0x10`,
so it holds for the whole race.

## Established by testing in Dolphin

- **The AI's steering works for a human kart mid-race.** The human kart's
  `cpuDriving` is initialised and tracks the route when only its own update
  (`vf[0x1C]`) runs; the `+0x148/+0x14C/+0x154` helpers aren't needed.
  Full AI control drove well.
- **Route data lags.** The AI's route position is stale at race start and
  for about 5 seconds after a Lakitu respawn. It read 6,499 and 59,248 units
  "off route", while real excursions reached at least 3.6× (possibly 6.3×)
  the half-width. Readings beyond 8× are ignored, and so are readings that
  shrink faster than the kart moves (the route catching up: −97 units per
  frame at 55 units per frame of kart speed).
- **Reacting to position alone was too late.** In one run the kart went
  from 282 units off (inside the free zone) to 1,750 off in half a second,
  and two seconds of full AI lock couldn't recover it before it hit the
  water. The deviation is now led by its growth rate (20 frames ahead).
- **Speed.** While correcting, `engineSpeed` holds at base speed (71.8 for
  the test kart), so writing it from the AI update sticks. Easing speed down
  for tight turns kept the kart on track but felt bad to drive, so it was
  dropped in favour of the turning cheat.
- **Turning cheat.** It always rotated the same way the AI was steering
  (AI +1.0 gave a negative step, matching how the rotation is defined), the
  heading error shrank during corrections (for example −0.48 → −0.30 →
  −0.01 radians), and the drive was clean with no falls.
- **Frame order doesn't matter.** The assist computed in the AI update is
  applied at the next pad read, whichever comes first within a frame.

## Design history

Smart Steering first tried to avoid hazards directly. Probes into the course
collision (`CourseModel::checkSpherePartial`, `0x8078F140`) looked for grass,
pits and walls along predicted paths, the assist steered away from them, and
lane keeping centred the kart between the road edges. It detected things
correctly: Mushroom Gorge's fences are course walls (`0x1000`) and its
mushrooms are jump pads (`0x100`). But steering decisions built from
predicted paths were unreliable. A learned turn rate that was too low made
correct turns look dangerous, the assist overrode the player, it zig-zagged
between edges, and it steered players away from optional paths.

Using the AI's route as the happy path replaced all of that, and is much
simpler. Inside half the route's width the player is free. Beyond that the
AI's own steering blends in, reaching full control at the route's edge. The
AI steers toward a point ahead on its route, so it fixes heading as well as
position. Borrowing only the AI's steering (not its braking and drifting)
couldn't make tight bends at full speed, so the assist also turns the kart
directly toward the AI's target point while correcting.

An "Auto Steering" mode (the AI always steers; the stick overrides) was also
built and tested, then dropped in favour of a single Smart Steering option.
The probing version is kept outside the repo at
`work/SmartSteering-collision-probing.cpp`.
