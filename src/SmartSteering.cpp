// Smart Steering for Mario Kart Wii.
//
// The AI's route is the happy path: drive freely near it; start to stray
// and the assist brings the kart smoothly back onto it, facing down the
// track, without slowing down.
//
// Each race frame, for every local human kart:
//   1. Run the kart's own CPU route-follower (the same one the game uses to
//      drive you after you finish) to get the steering the AI would use,
//      without applying it. It steers toward a point ahead on its route.
//   2. Measure how far the kart is from the route (the polyline through the
//      AI's previous, current and next route points) relative to the
//      route's own width there, leading it by how fast it is growing, and
//      set how much of the AI's steering to blend in.
//   3. While correcting: hold the kart's speed so grass or scrapes don't
//      stall the correction, and turn the kart's heading a little toward
//      the AI's target point so it comes back lined up with the track.
// Then, right after the player's controller is read, the stick is blended
// toward the AI's steering.
//
// Only active in offline Grand Prix and VS. Time Trials are excluded so
// ghosts (which record raw input) stay valid; online is excluded for
// fairness. The "Automatic drift only" Riivolution choice also skips players
// using Manual drift.

#include "game.h"

namespace SmartSteering {

// ---- Tuning (course units, frames at 60 Hz) ----

// Distance from the route as a fraction of the route's half-width there
// (route point width x 50): free below kFreeRatio, full AI control at
// kFullRatio and beyond.
static const float kFreeRatio = 0.5f;
static const float kFullRatio = 1.0f;
static const float kMinHalfWidth = 300.0f; // guard against tiny/zero route widths
// The AI's route position lags at race start and for a few seconds after a
// respawn (seen as 6499 and 59248 units "off route", about 9x and 100x the
// half-width, while real excursions reached at least 3.6x and possibly 6.3x).
// Readings beyond this ratio are treated as route data not ready: no assist.
static const float kStaleRatio = 8.0f;
static const float kWeightRise = 0.2f;     // per frame
static const float kWeightFall = 0.1f;     // per frame

// Lead: the deviation used for the assist is the current distance plus how
// fast it is growing, kLeadFrames ahead. Taking a corner along the route
// keeps the distance steady (no lead); running wide makes it grow, so the
// assist steps in before the kart is far off. In testing the kart went from
// 282 to 1750 units off in half a second while still inside the free zone.
// The rate is smoothed (kRateSmoothing of each new sample) and capped at
// kMaxRate so a jump when the route's points advance can't spike it.
static const float kLeadFrames = 20.0f;
static const float kRateSmoothing = 0.3f;
static const float kMaxRate = 120.0f; // units per frame
static const float kCatchUpFactor = 1.2f; // shrinking faster than this x kart speed = route catching up

// Speed hold: while the assist weight is at least kHoldWeight, the kart's
// engine speed is kept at kHoldFraction of what it was when the hold began
// (never above the kart's base speed), so grass or scrapes don't stall the
// correction. (Slowing down for tight turns was tried and dropped: it kept
// the kart on track but felt bad to drive. The lead above makes the assist
// step in early enough instead.)
static const float kHoldWeight = 0.5f;
static const float kHoldFraction = 0.95f;

// Turning cheat: while correcting (weight at least kTurnWeight), on the
// ground and not braking, the kart's heading is turned toward the AI's target
// point on its route, so it comes back onto the route facing down the track
// at full speed. Each frame it closes kTurnGain of the heading error, at most
// kTurnMaxStep radians scaled by the assist weight.
static const float kTurnWeight = 0.3f;
static const float kTurnGain = 0.08f;
static const float kTurnMaxStep = 0.02f;    // radians per frame (about 1.1 degrees)
static const float kTargetMinDist = 300.0f; // closer than this: aim at the next route point

static const float kMinSpeedSq = 15.0f * 15.0f;       // per-frame motion below this: no assist
static const float kTeleportDistSq = 600.0f * 600.0f; // jump larger than this: respawn / new race

// buttonActions bits, from KPadGCController::calcInner (0x805201B0):
// 0x2 = B/R held, 0x8 = hop/drift (latched when B/R is pressed while
// accelerating). 0x2 without 0x8 is braking/reversing; the AI steers for
// forward motion, so the assist stands down then. Drifting keeps it.
static const u16 kButtonBrakeOrDrift = 0x2;
static const u16 kButtonDrift = 0x8;

static const int kMaxPlayers = 12;

// ---- Riivolution settings ----

// Words written by the XML's <memory> patches, one per setting. They live in
// padding of the unused debugger interrupt table (0x80005734..0x80005C00 is
// zero on the PAL and NTSC-U discs); Pulsar's loader at 0x80004000 only uses
// up to 0x80004AE8. A choice left at its default writes nothing, so every
// setting reads 0 unless chosen.
struct Settings {
    u32 automaticDriftOnly; // 0x80005800: assist only players using Automatic drift
};
static const volatile Settings* const sSettings = (const volatile Settings*)0x80005800;

struct Assist {
    KPad* kpad;        // the player's pad, matched in the input hook
    Vec3 prevPos;
    float aiStick;     // AI steering, -1..1, in the kart's (mirror-applied) frame
    float weight;      // 0..1
    float holdSpeed;   // engine speed held during a hard correction, 0 if none
    float prevDist;    // route distance last frame
    float distRate;    // smoothed growth of the route distance, units per frame
    bool hasPrevPos;
    bool hasPrevDist;
    bool braking;      // player braking/reversing on the last pad read
    bool inRace;       // passed the mode/kart checks this frame
    bool pending;      // computed this frame, not yet applied
};

static Assist sAssist[kMaxPlayers]; // .bss, zeroed by the loader

#ifdef MKWIISS_DEBUG
static float sDebugHeadingError, sDebugTurnStep;
static u32 sDebugFrame;
static float sDebugStickIn, sDebugStickOut;
#endif

typedef void (*CPUDrivingUpdateFn)(CPUDriving* self);
typedef void (*CPUDrivingResyncFn)(CPUDriving* self, const Vec3* pos);

static bool IsModeAllowed() {
    return (isGp() || isVs()) && !isOnline();
}

static float Clamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static float Sqrt(float x);

// atan2 approximation, accurate to about 0.01 radians.
static float Atan2(float y, float x) {
    float ax = x < 0.0f ? -x : x, ay = y < 0.0f ? -y : y;
    if (ax == 0.0f && ay == 0.0f) return 0.0f;
    float a = ax > ay ? ay / ax : ax / ay;
    float s = a * a;
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) r = 1.57079637f - r;
    if (x < 0.0f) r = 3.14159274f - r;
    return y < 0.0f ? -r : r;
}

// Rotate q about the world's vertical axis by a small angle: (0, sin a/2,
// 0, cos a/2) * q, renormalised. Rotating by +a takes (x, z) toward
// (x cos a + z sin a, -x sin a + z cos a).
static void YawQuat(Quat& q, float angle) {
    float s = 0.5f * angle;
    float c = 1.0f - 0.5f * s * s;
    Quat r;
    r.x = c * q.x + s * q.z;
    r.y = c * q.y + s * q.w;
    r.z = c * q.z - s * q.x;
    r.w = c * q.w - s * q.y;
    float n = Sqrt(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
    if (n > 0.0f) {
        q.x = r.x / n;
        q.y = r.y / n;
        q.z = r.z / n;
        q.w = r.w / n;
    }
}

static float Sqrt(float x) {
    if (x <= 0.0f) return 0.0f;
    float g = x > 1.0f ? x * 0.5f : 1.0f;
    for (int i = 0; i < 12; ++i) g = 0.5f * (g + x / g);
    return g;
}

// ---- Route ----

static const EnemyPointData* RoutePoint(u8 idx) {
    const EnemyPoint* p = CourseMap_getEnemyPoint(CourseMap_spInstance, idx);
    if (p == nullptr) return nullptr;
    return p->data;
}

// Squared distance in the ground plane from (px, pz) to segment a-b.
static float DistSqToSegment(float px, float pz, const Vec3& a, const Vec3& b) {
    float abx = b.x - a.x, abz = b.z - a.z;
    float len2 = abx * abx + abz * abz;
    float t = len2 > 0.0f ? Clamp(((px - a.x) * abx + (pz - a.z) * abz) / len2, 0.0f, 1.0f) : 0.0f;
    float dx = a.x + abx * t - px, dz = a.z + abz * t - pz;
    return dx * dx + dz * dz;
}

struct RouteDeviation {
    bool valid;
    float dist;      // from the route, course units
    float halfWidth; // route half-width at the nearest point
};

static RouteDeviation MeasureRoute(const AIPlayer* self, const Vec3* pos) {
    RouteDeviation r = {false, 0.0f, 0.0f};
    if (CourseMap_spInstance == nullptr) return r;
    EnemyRouteController* route = self->cpuDriving->route;
    if (route == nullptr || route->enpt == nullptr) return r;
    const ENPTController* e = route->enpt;
    const EnemyPointData* prev = RoutePoint(e->prevPoint);
    const EnemyPointData* cur = RoutePoint(e->curPoint);
    const EnemyPointData* next = RoutePoint(e->nextPoint);
    if (prev == nullptr || cur == nullptr || next == nullptr) return r;
    float d1 = DistSqToSegment(pos->x, pos->z, prev->pos, cur->pos);
    float d2 = DistSqToSegment(pos->x, pos->z, cur->pos, next->pos);
    r.valid = true;
    r.dist = Sqrt(d1 < d2 ? d1 : d2);
    r.halfWidth = cur->width * ENEMY_POINT_WIDTH_SCALE;
    if (r.halfWidth < kMinHalfWidth) r.halfWidth = kMinHalfWidth;
    return r;
}

// ---- AI route-follower ----

// The same route resync rule the game applies before CPU driving
// (0x80732A70 / 0x80732DE0).
static void ResyncRouteIfRequested(AIPlayer* self, const Vec3* pos) {
    if (self->routeResyncRequested) {
        if (!self->routeResynced) {
            if (!self->routeResyncOnlyAirborne || !EnemyAI_isOnGround(self->inputs->ai)) {
                CPUDriving* cpu = self->cpuDriving;
                ((CPUDrivingResyncFn)cpu->vtable[CPUDRIVING_VF_RESYNC_ROUTE / 4])(cpu, pos);
                self->routeResynced = 1;
            }
        }
    } else {
        self->routeResynced = 0;
    }
    self->routeResyncRequested = 0;
}

// Steering the CPU driver would apply this frame, without applying it.
static float ComputeAIStick(AIPlayer* self, const Vec3* pos) {
    AIInputs* in = self->inputs;
    in->buttons = 0;
    in->steer = 0.0f;
    ResyncRouteIfRequested(self, pos);
    CPUDriving* cpu = self->cpuDriving;
    ((CPUDrivingUpdateFn)cpu->vtable[CPUDRIVING_VF_UPDATE / 4])(cpu);
    return Clamp(in->steer, -1.0f, 1.0f);
}

// ---- Speed hold ----

static void HoldSpeed(Assist& a, KartObjectProxy* kart) {
    KartMove* move = ((KartObjectProxyLayout*)kart)->accessor->move;
    if (a.weight < kHoldWeight || a.braking) {
        a.holdSpeed = 0.0f;
        return;
    }
    if (a.holdSpeed == 0.0f) {
        a.holdSpeed = move->engineSpeed < move->baseSpeed ? move->engineSpeed : move->baseSpeed;
    }
    float floor = a.holdSpeed * kHoldFraction;
    if (move->engineSpeed < floor) move->engineSpeed = floor;
}

// ---- Turning cheat ----

static void TurnTowardRoute(Assist& a, AIPlayer* self, KartObjectProxy* kart, const Vec3* pos, const Vec3& vel,
                            float speedXZ) {
#ifdef MKWIISS_DEBUG
    sDebugHeadingError = 0.0f;
    sDebugTurnStep = 0.0f;
#endif
    if (a.weight < kTurnWeight || a.braking || speedXZ <= 0.0f) return;
    if (!EnemyAI_isOnGround(kart)) return; // leave jumps and tricks alone
    EnemyRouteController* route = self->cpuDriving->route;
    if (route == nullptr || route->enpt == nullptr) return;
    const ENPTController* e = route->enpt;
    Vec3 target = e->targetPos;
    float dx = target.x - pos->x, dz = target.z - pos->z;
    if (dx * dx + dz * dz < kTargetMinDist * kTargetMinDist) {
        const EnemyPointData* next = RoutePoint(e->nextPoint);
        if (next == nullptr) return;
        dx = next->pos.x - pos->x;
        dz = next->pos.z - pos->z;
    }
    float hx = vel.x / speedXZ, hz = vel.z / speedXZ;
    // Signed angle from the heading to the target; positive is the +a
    // rotation of YawQuat.
    float angle = Atan2(hz * dx - hx * dz, hx * dx + hz * dz);
    float maxStep = kTurnMaxStep * a.weight;
    float step = Clamp(angle * kTurnGain, -maxStep, maxStep);
    KartPhysics* phys = ((KartObjectProxyLayout*)kart)->accessor->body->holder->physics;
    YawQuat(phys->mainRot, step);
    YawQuat(phys->fullRot, step);
#ifdef MKWIISS_DEBUG
    sDebugHeadingError = angle;
    sDebugTurnStep = step;
#endif
}

// ---- Per-frame update ----

static void Update(AIPlayer* self) {
    KartObjectProxy* kart = self->inputs->ai;
    u8 idx = KartObjectProxy_getPlayerIdx(kart);
    if (idx >= kMaxPlayers) return;
    Assist& a = sAssist[idx];
    a.pending = false;
    a.inRace = false;

    if (self->hasRaceEnded || !IsModeAllowed()) return;
    if (!KartObjectProxy_isLocal(kart) || KartObjectProxy_isCpu(kart) || KartObjectProxy_isGhost(kart)) return;
    if (RaceManager_spInstance == nullptr) return;
    if (idx >= RaceConfig_spInstance->playerCount) return;
    if (sSettings->automaticDriftOnly &&
        !KartState_on(((KartObjectProxyLayout*)kart)->accessor->state, KART_FLAG_AUTOMATIC_DRIFT)) {
        return;
    }
    a.inRace = true;

    const Vec3* pos = KartObjectProxy_getPos(kart);
    Vec3 vel = {0.0f, 0.0f, 0.0f};
    if (a.hasPrevPos) {
        vel.x = pos->x - a.prevPos.x;
        vel.y = pos->y - a.prevPos.y;
        vel.z = pos->z - a.prevPos.z;
    }
    float distSq = vel.x * vel.x + vel.y * vel.y + vel.z * vel.z;
    bool teleported = !a.hasPrevPos || distSq > kTeleportDistSq;
    a.prevPos = *pos;
    a.hasPrevPos = true;
    if (teleported) {
#ifdef MKWIISS_DEBUG
        KartState* state = ((KartObjectProxyLayout*)kart)->accessor->state;
        OSReport("[SmartSteering] start/respawn p%d startBoostIdx=%d (-1 = burnout) charge=%.3f\n", idx,
                 state->startBoostIdx, state->startBoostCharge);
#endif
        // New race or respawn: re-anchor the route-follower to where we are.
        self->routeResyncRequested = 1;
        self->routeResynced = 0;
        a.weight = 0.0f;
        a.holdSpeed = 0.0f;
        a.hasPrevDist = false;
        vel.x = vel.y = vel.z = 0.0f;
    }

    a.kpad = RaceManager_spInstance->players[idx]->kpad;
    a.aiStick = ComputeAIStick(self, pos);

    float target = 0.0f;
    RouteDeviation dev = {false, 0.0f, 0.0f};
    float speedSqXZ = vel.x * vel.x + vel.z * vel.z;
    if (speedSqXZ >= kMinSpeedSq) {
        dev = MeasureRoute(self, pos);
        if (dev.valid && dev.dist / dev.halfWidth <= kStaleRatio) {
            if (a.hasPrevDist) {
                float rate = Clamp(dev.dist - a.prevDist, -kMaxRate, kMaxRate);
                a.distRate += (rate - a.distRate) * kRateSmoothing;
            } else {
                a.distRate = 0.0f;
            }
            a.prevDist = dev.dist;
            a.hasPrevDist = true;
            // The distance can't shrink faster than the kart moves; when it
            // does, the route position is still catching up (seen after the
            // race start: -97/frame at 55/frame), so treat it as not ready.
            bool catchingUp = -a.distRate > Sqrt(speedSqXZ) * kCatchUpFactor;
            if (!catchingUp) {
                float lead = a.distRate > 0.0f ? a.distRate * kLeadFrames : 0.0f;
                float ratio = (dev.dist + lead) / dev.halfWidth;
                target = Clamp((ratio - kFreeRatio) / (kFullRatio - kFreeRatio), 0.0f, 1.0f);
            }
        } else {
            a.hasPrevDist = false;
        }
    } else {
        a.hasPrevDist = false;
    }

    if (target > a.weight) {
        a.weight = target - a.weight > kWeightRise ? a.weight + kWeightRise : target;
    } else {
        a.weight = a.weight - target > kWeightFall ? a.weight - kWeightFall : target;
    }
    a.pending = a.weight > 0.0f;
    HoldSpeed(a, kart);
    TurnTowardRoute(a, self, kart, pos, vel, Sqrt(speedSqXZ));

#ifdef MKWIISS_DEBUG
    if ((sDebugFrame++ % 30) == 0) {
        KartMove* move = ((KartObjectProxyLayout*)kart)->accessor->move;
        OSReport("[SmartSteering] p%d speed2=%.0f route=%.0f/%.0f rate=%.1f weight=%.2f ai=%.2f stick=%.2f->%.2f "
                 "engine=%.1f base=%.1f hold=%.1f headErr=%.2f turn=%.3f\n",
                 idx, speedSqXZ, dev.dist, dev.halfWidth, a.distRate, a.weight, a.aiStick, sDebugStickIn,
                 sDebugStickOut, move->engineSpeed, move->baseSpeed, a.holdSpeed, sDebugHeadingError,
                 sDebugTurnStep);
    }
#endif
}

static void Apply(KPadRaceInputState& st, float steerTo, float weight) {
    float player = st.stickX;
    float out;
    if ((steerTo > 0.0f && player >= steerTo) || (steerTo < 0.0f && player <= steerTo)) {
        out = player; // already turning harder the same way
    } else {
        out = player + (steerTo - player) * weight;
    }
    // Back to the 0..14 grid a real stick produces: stickX = (raw - 7) / 7,
    // with raw mirrored in mirror mode (setStickXMirrored, 0x8051E960).
    int q = (int)(Clamp(out, -1.0f, 1.0f) * 7.0f + 7.5f);
    if (q > 14) q = 14;
    st.stickX = (float)(q - 7) / 7.0f;
    st.rawStickX = (u8)(KPadDirector_spInstance->isMirror ? 14 - q : q);
#ifdef MKWIISS_DEBUG
    sDebugStickIn = player;
    sDebugStickOut = st.stickX;
#endif
}

// ---- Hooks ----

static void UpdateRealPlayerDrivingHook(AIPlayer* self) {
    AIPlayer_updateRealPlayerDriving(self);
    Update(self);
}
// AI::Player vtable slot 0x38 (UpdateRealPlayerDriving) in the Player,
// PlayerBike and PlayerKart vtables; nothing calls 0x80732C70 directly.
kmWritePointer(0x808CA774, UpdateRealPlayerDrivingHook);
kmWritePointer(0x808CA7F8, UpdateRealPlayerDrivingHook);
kmWritePointer(0x808CA850, UpdateRealPlayerDrivingHook);

static void KPadPlayerCalcHook(KPad* self, bool paused) {
    KPadPlayer_calc(self, paused);
    if (paused) return;
    for (int i = 0; i < kMaxPlayers; ++i) {
        Assist& a = sAssist[i];
        if (a.kpad != self || !a.inRace) continue;
        u16 buttons = self->raceState.buttons;
        a.braking = (buttons & kButtonBrakeOrDrift) && !(buttons & kButtonDrift);
        if (a.pending && !a.braking) Apply(self->raceState, a.aiStick, a.weight);
        a.pending = false;
    }
}
// System::KPadPlayer vtable (0x808B2D90) slot 0xC = calc(bool).
kmWritePointer(0x808B2D9C, KPadPlayerCalcHook);

} // namespace SmartSteering
