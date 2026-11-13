// Optional-module interface (skill section 1.2). The core only ever talks to modules
// through this table; it never includes or calls a module's own code.
#pragma once

#include <stdint.h>

namespace mod {

enum class Status : uint8_t { OK = 0, ABSENT, ERROR };

// Appended, not inserted: nothing serializes CoreState onto the wire yet, but a future
// encoding may do static_cast<uint8_t>(state), and appending keeps every existing value
// stable instead of silently shifting states added after it.
enum class CoreState : uint8_t {
    BOOT, IDLE, HOMING, JOG, TARE, READY, RUN, HOLD, CYCLE, BREAK_HOLD, FAULT, PRELOAD
};

struct ConfigSection {
    uint8_t id;
    uint8_t len;
    const uint8_t* data;
};

// A conductor-OPEN event drained from a module's conductor_events() hook (skill 1.2/1.3,
// decided 2026-11-13 for issue #14). `channel` is module-defined (e.g. continuity's
// 4-channel index, 0-3); feed `timestamp_us` into break_detect::Classifier::
// push_conductor_open() for coincidence matching -- break detection only cares about
// timing, never which channel. Per-channel OPEN/DEGRADED/RECLOSE/SHORT reporting for the
// PC/SD (skill 2.4) is a separate concern, handled by each module's own emit() hook.
struct ConductorEvent {
    uint8_t channel;
    uint64_t timestamp_us;
};

struct Module {
    const char* name;
    Status (*init)();
    uint8_t (*capability)();                        // proto::CAP_* bits, 0 if hardware absent
    void (*poll)();                                 // once per loop pass, must not block
    Status (*validate_config)(const ConfigSection&);
    uint16_t (*safety_check)();                     // 0 = ok, else fault code
    uint32_t (*inhibit_bits)();                     // motion-inhibit reasons asserted
    void (*on_state_change)(CoreState from, CoreState to);
    void (*emit)();                                 // push frames to comms and SD queues
    // Drains up to `cap` pending ConductorEvents into `out`, returns the count (skill
    // 1.2/1.3: "drain events into the break-detection seam"). May be nullptr for any
    // module that never produces conductor events (e.g. routing) -- callers must
    // null-check before calling. Appended last, not inserted, matching this file's
    // existing CoreState convention above. Added for issue #14 (sim_continuity):
    // documented in the skill's hook table since module_api.h was first written, but
    // never implemented because no module existed yet to need it.
    //
    // This stand-in's own queue is filled and drained from the same main-loop context
    // (skill 3.3) and is NOT thread/ISR-safe. The real phase-2 continuity module will
    // fill its queue from ADC-DMA-complete ISR context (skill 2.4/3.2) and needs genuine
    // SPSC/volatile protection -- do not copy sim_continuity's single-context queue
    // implementation verbatim.
    uint8_t (*conductor_events)(ConductorEvent* out, uint8_t cap);
};

// Defined in src/core/modules.cpp. Empty in the phase 1 build.
extern const Module* const MODULES[];
extern const uint8_t MODULE_COUNT;

}  // namespace mod
