// Safety limits and load-cell profiles (skill sections 2.2 and 4).
// Every force limit used anywhere must come from the active profile in this file.
#pragma once

#include <stdint.h>

namespace limits {

struct CellProfile {
    const char* name;
    int32_t rated_mN;
    int32_t cutoff_mN;          // firmware hard stop
    int32_t safe_overload_mN;   // 0 = not known
    int32_t coupler_slip_mN;    // mechanical coupler setting required; 0 = TBD (profile unusable)
};

enum class ProfileId : uint8_t { CELL_100KG = 0, CELL_500KG = 1, CELL_10KG = 2, COUNT };

constexpr CellProfile PROFILES[] = {
    {"DYLY-103 100 kg", 980665, 880000, 1471000, 1100000},
    {"PSD-S1 500 kg", 4903325, 3000000, 0, 0},  // TODO(verify): coupler setting (> ~3.3 kN)
    {"10 kg", 98067, 88000, 0, 1100000},         // coupler cannot protect this cell
};
static_assert(sizeof(PROFILES) / sizeof(PROFILES[0]) == static_cast<uint8_t>(ProfileId::COUNT),
              "profile table size");

// A profile is selectable only if its coupler setting is known and slips above the cutoff.
constexpr bool profile_selectable(const CellProfile& p) {
    return p.coupler_slip_mN > 0 && p.cutoff_mN < p.coupler_slip_mN;
}
static_assert(profile_selectable(PROFILES[0]), "default profile must be selectable");

// ---- Timing limits (skill section 4) -----------------------------------------------
constexpr uint32_t HEARTBEAT_TIMEOUT_US = 1000000;   // PC heartbeat loss while moving -> FAULT
// Jog deadman (Mathew 2026-11-16): a jog stops if no JOG KEEPALIVE (action 2, same
// direction, protocol 0.7) arrives within this window; the PC sends one every ~100 ms
// while the button is held.
// TODO(verify): ~250 ms chosen for USB/PC jitter vs. overrun distance; tune on the bench.
constexpr uint32_t JOG_KEEPALIVE_TIMEOUT_US = 250000;
// Relieving jog out of a limit FAULT (decision 2b): FAULT if the tripped switch has not read
// clear within this many steps of the FAULT episode's first relieving-jog start (cumulative
// over presses, decided 2026-11-16 round 4). TODO(verify): awaiting Mathew's measurement of
// the switch release travel. No value is invented here: 0 means the jog faults within one
// loop pass of the first step unless the switch has already released, i.e. relieving jog
// is effectively disabled until the measured number is entered.
constexpr uint32_t RELIEF_MAX_TRAVEL_STEPS = 0;
// Relieving jog (decision 2, rounds 5-6): the tripped switch must read clear continuously
// for this long, timed with the main loop's 64-bit microsecond clock, before it counts as
// cleared (cap lifted, switch re-armed); any tripped read restarts the timing.
// TODO(verify): placeholder, not derived from any measurement -- Mathew will measure the
// switch's bounce/chatter on the bench.
constexpr uint32_t RELIEF_CLEAR_DEBOUNCE_US = 20000;
constexpr uint32_t HX711_STALE_US = 50000;          // force sample stale while moving or loaded
constexpr uint32_t LOOP_WORST_CASE_US = 2000;

// ---- Homing and jog ----------------------------------------------------------------
constexpr int32_t HOMING_ALLOWED_MAX_mN = 2000;      // only home with |F| < 2 N
constexpr int32_t HOMING_ABORT_mN = 5000;            // abort homing if |F| > 5 N
constexpr int32_t JOG_LOADED_THRESHOLD_PERMILLE = 50;  // 5% of cutoff -> slow jog

// ---- Slip suspect (no encoder) -------------------------------------------------------
constexpr int32_t SLIP_FORCE_PERMILLE_OF_COUPLER = 800;  // force > 80% of coupler slip
constexpr int32_t SLIP_FLAT_PERMILLE = 10;               // and < 1% change
constexpr uint32_t SLIP_TRAVEL_UM = 2000;                // over >= 2 mm commanded travel

}  // namespace limits
