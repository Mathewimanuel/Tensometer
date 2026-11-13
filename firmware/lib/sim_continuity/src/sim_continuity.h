// Pure-logic synthetic conductor-event queue and mod::Module driver (skill 1.1/1.2,
// issue #14). No Arduino dependency -- natively tested, unlike continuity/routing, which
// will need real src/ hardware drivers once they exist. Lets a caller (native test, or
// later a bench debug command) inject synthetic conductor-OPEN events and drain them
// FIFO through the exact mod::Module::conductor_events() hook signature, proving the
// seam into break detection works before any phase-2 hardware exists.
//
// MODULE itself lives here rather than under src/modules/ because, unlike continuity or
// routing, it has nothing hardware-dependent in it -- there is no reason to exempt it
// from native testing the way a real driver file would be.
#pragma once

#include <stdint.h>

#include "module_api.h"

namespace sim_continuity {

// Matches break_detect::Classifier's own COND_RING_LEN: no point holding more pending
// synthetic events than the classifier itself would ever track.
constexpr uint8_t QUEUE_CAPACITY = 8;

// Not thread/ISR-safe by design: events are injected and drained from the same
// main-loop context (skill 3.3), same as every other module seam in this codebase. See
// module_api.h's conductor_events doc for why a real continuity module cannot copy this.
class EventQueue {
public:
    // Returns false if the queue is already full -- the caller's problem, not a silent
    // drop, mirroring the out/out_cap style used elsewhere (e.g. break_detect).
    bool inject_open(uint8_t channel, uint64_t timestamp_us);

    // Drains up to `cap` pending events into `out`, oldest first. Matches the
    // mod::Module::conductor_events() hook signature exactly (module_api.h).
    uint8_t drain(mod::ConductorEvent* out, uint8_t cap);

    uint8_t pending() const { return count_; }

private:
    mod::ConductorEvent buf_[QUEUE_CAPACITY] = {};
    uint8_t head_ = 0;
    uint8_t count_ = 0;
};

// The mod::Module descriptor registered in modules.cpp, behind FEATURE_SIM_CONTINUITY.
extern const mod::Module MODULE;

// Bench/test-only injection point: appends a synthetic conductor-OPEN event to the
// singleton queue MODULE.conductor_events() drains. Not part of mod::Module -- nothing
// in the main loop calls this. It exists so native tests (and later, a bench debug
// command, if one is ever decided) can drive this module the same way real hardware
// eventually will.
bool inject_open(uint8_t channel, uint64_t timestamp_us);

}  // namespace sim_continuity
