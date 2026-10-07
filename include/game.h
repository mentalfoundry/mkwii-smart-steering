// Minimal Mario Kart Wii declarations used by MKWii Smart Steering.
//
// Every address and offset here is PAL (RMCP01). Kamek's -versions mapping
// translates them for RMCE/RMCJ/RMCK at link time. Each value was read from
// the RMCP01 disassembly (split with decomp-toolkit against the riidefi/mkw
// symbol map) and cross-checked against a second source; see
// docs/research.md for the evidence behind each one. Do not change a value
// here without re-verifying it the same way.

#ifndef MKWIISS_GAME_H
#define MKWIISS_GAME_H

#include "kamek/hooks.hpp"

struct Vec3 {
    float x, y, z;
};

// System::KPadRaceInputState (Pulsar: Input::State). Size 0x18.
// stickX is what the kart consumes; for human pads it is already mirrored
// (setStickXMirrored, 0x8051E960). rawStickX is the unmirrored 0..14 value
// that ghosts record.
struct KPadRaceInputState {
    void* vtable;       // 0x00
    u16 buttons;        // 0x04
    u16 rawButtons;     // 0x06
    float stickX;       // 0x08  -1..1, = (raw - 7) / 7, negated in mirror mode
    float stickY;       // 0x0C
    u8 rawStickX;       // 0x10  0..14
    u8 rawStickY;       // 0x11
    u8 trick;           // 0x12
    u8 trickUnmirrored; // 0x13
    u8 unk14[4];        // 0x14
};

// System::KPad (Pulsar: Input::ControllerHolder). Only the current-frame
// race input state is used.
struct KPad {
    u8 unk00[0x28];
    KPadRaceInputState raceState; // 0x28 current frame
};

// System::KPadDirector (Pulsar: Input::Manager).
struct KPadDirector {
    u8 unk0000[0x4155];
    bool isMirror; // 0x4155, read by setStickXMirrored
};

// Kart::KartObjectProxy is the first base of Enemy::AI (Pulsar:
// KartAIController); we only ever pass it back to game functions.
struct KartObjectProxy;

// Enemy AI input accumulator (Pulsar: AI::Inputs).
struct AIInputs {
    KartObjectProxy* ai; // 0x00 the owning Enemy::AI
    u8 unk04[0x08];
    u16 buttons;         // 0x0C
    u8 pad0E[2];
    float steer;         // 0x10 -1..1, written by the CPU driving logic
};

// Route-following driver (Pulsar: AI::CPUDriving). Its vtable pointer lives
// at +0x34 (secondary base); the game always calls through that.
// The AI's position along its route (Pulsar: AI::ENPTController). Point
// indices as read by GetNext/GetCur/GetPreENPTPosition (0x8073CB0C,
// 0x8073CAB8, 0x8073CA64), which look them up with CourseMap::getEnemyPoint.
struct ENPTController {
    u8 unk00[0x09];
    u8 nextPoint;  // 0x09
    u8 curPoint;   // 0x0A
    u8 prevPoint;  // 0x0B
    u8 unk0C[0x14 - 0x0C];
    Vec3 targetPos; // 0x14, the point the AI steers toward (GetCurPosition, 0x8073CB60)
};

// Pulsar: AI::EnemyRouteController. +0x14 per
// EnemyRouteController::IsOutCurENPTBounds (0x8073BD50) and
// AIControlBase::initAfterManager (0x8072A21C).
struct EnemyRouteController {
    u8 unk00[0x14];
    ENPTController* enpt; // 0x14
};

// Enemy route point (KMP ENPT) as returned by CourseMap::getEnemyPoint.
// Width is scaled by 50.0 (rodata lbl_1_data_18474) in
// ENPTController::GetStartingENPTWidth (0x8073CCA8).
struct EnemyPointData {
    Vec3 pos;    // 0x00
    float width; // 0x0C, raw; x50 for course units
};
struct EnemyPoint {
    u8 unk00[0x04];
    const EnemyPointData* data; // 0x04
};
static const float ENEMY_POINT_WIDTH_SCALE = 50.0f;

struct CPUDriving {
    u8 unk00[0x34];
    void** vtable;               // 0x34
    void* inputs;                // 0x38
    EnemyRouteController* route; // 0x3C, see AIControlBase::initAfterManager
};

// Kart::KartMove (Pulsar: Kart::Movement), reached from a KartObjectProxy
// as proxy->accessor->move. Offsets agree between Pulsar's header and the
// getters getBaseSpeed (0x805910B0, +0x14) and getVehicleSpeed
// (0x80590CF8, +0x20).
struct KartMove {
    u8 unk00[0x14];
    float baseSpeed;      // 0x14
    float softSpeedLimit; // 0x18
    float unk1c;          // 0x1C
    float engineSpeed;    // 0x20
};
// Kart::KartState (Pulsar: Kart::Status). Start boost result, set when the
// countdown ends by KartState::ComputeStartBoost (0x805959D4): -1 means the
// player held accelerate too long and burns out. Path confirmed by
// KartObjectProxy::setStartBoostIdx (0x80590380, accessor+0x4 -> +0xA0).
struct KartState {
    u8 unk00[0x9C];
    float startBoostCharge; // 0x9C
    s32 startBoostIdx;      // 0xA0, -1 = burnout
};
// EGG::Quatf: x, y, z, w; Hamilton product (decomp lib/egg/math/eggQuat.hpp).
struct Quat {
    float x, y, z, w;
};

// Kart physics state (Pulsar: Kart::Physics), reached as
// accessor->body(+0x8)->holder(+0x90)->physics(+0x4), the path used by
// KartObjectProxy::getPos (0x8059020C, +0x68) and setRot (0x80590288,
// which writes the same quaternion to +0xF0 and +0x100).
struct KartPhysics {
    u8 unk00[0x68];
    Vec3 position;  // 0x68
    u8 unk74[0xF0 - 0x74];
    Quat mainRot;   // 0xF0
    Quat fullRot;   // 0x100, mainRot plus special rotation (tricks)
};
struct KartPhysicsHolder {
    u8 unk00[0x04];
    KartPhysics* physics; // 0x04
};
struct KartBody {
    u8 unk00[0x90];
    KartPhysicsHolder* holder; // 0x90
};

struct KartAccessor {
    u8 unk00[0x04];
    KartState* state; // 0x04
    KartBody* body;   // 0x08
    u8 unk0C[0x28 - 0x0C];
    KartMove* move;   // 0x28
};
struct KartObjectProxyLayout {
    KartAccessor* accessor; // 0x00
};
static const u32 CPUDRIVING_VF_UPDATE = 0x1C;      // per-frame route/steer update
static const u32 CPUDRIVING_VF_RESYNC_ROUTE = 0x30; // (const Vec3* pos) re-anchor route to position

// Per-kart AI controller (Pulsar: AI::Player). Exists for human karts too;
// the game uses it to drive the kart after it finishes the race.
// Pulsar's header lists both 'controller' and 'inputs' at 0x140; the
// disassembly of 0x80732A70 / 0x80732DE0 shows the layout below.
struct AIPlayer {
    u8 unk000[0x140];
    AIInputs* inputs;            // 0x140
    CPUDriving* cpuDriving;      // 0x144
    u8 unk148[0x150 - 0x148];
    KPadRaceInputState* state;   // 0x150
    u8 unk154[0x160 - 0x154];
    u8 routeResyncRequested;     // 0x160
    u8 routeResynced;            // 0x161
    u8 routeResyncOnlyAirborne;  // 0x162
    bool hasRaceEnded;           // 0x163
};

// System::RaceManager (Pulsar: Raceinfo).
struct RaceManagerPlayer {
    u8 unk00[0x48];
    KPad* kpad; // 0x48, target of the CPU-input apply call 0x80535718
};
struct RaceManager {
    u8 unk00[0x0C];
    RaceManagerPlayer** players; // 0x0C
};

// System::RaceConfig (Pulsar: Racedata).
struct RaceConfig {
    u8 unk00[0x24];
    u8 playerCount; // 0x24 (racesScenario + 0x4)
};

extern "C" {
// main.dol
void OSReport(const char* fmt, ...);

// StaticR.rel, game-mode predicates (read RaceConfig+0xB70)
bool isGp();     // mode 0 or 7
bool isVs();     // mode 1
bool isOnline(); // mode 7..10

// Kart::KartObjectProxy
const Vec3* KartObjectProxy_getPos(const KartObjectProxy* self);
u8 KartObjectProxy_getPlayerIdx(const KartObjectProxy* self);
bool KartObjectProxy_isLocal(KartObjectProxy* self);
bool KartObjectProxy_isCpu(const KartObjectProxy* self);
bool KartObjectProxy_isGhost(KartObjectProxy* self);

// Enemy::AI::isOnGround, used by the game's route-resync rule
bool EnemyAI_isOnGround(KartObjectProxy* self);

// System::CourseMap::getEnemyPoint(u16) const
struct CourseMap;
const EnemyPoint* CourseMap_getEnemyPoint(const CourseMap* self, u16 idx);
extern CourseMap* CourseMap_spInstance;

// Originals of the functions we wrap through vtables
void AIPlayer_updateRealPlayerDriving(AIPlayer* self);
// isPaused is KPadDirector+0x4154 (see KPadDirector::calc, 0x805238F0)
void KPadPlayer_calc(KPad* self, bool isPaused);

// Singletons
extern RaceManager* RaceManager_spInstance;
extern RaceConfig* RaceConfig_spInstance;
extern KPadDirector* KPadDirector_spInstance;
}

#endif
