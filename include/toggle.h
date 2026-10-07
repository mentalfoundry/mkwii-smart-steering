// Per-player Smart Steering on/off toggle set on the drift select screens
// (src/DriftSelectToggle.cpp).

#ifndef MKWIISS_TOGGLE_H
#define MKWIISS_TOGGLE_H

#include "kamek/types.hpp"

// True if the player in this local (HUD) slot turned Smart Steering on (it
// starts off).
bool SmartSteeringToggle_isOn(s32 hudSlot);

#endif
