// Optional-module interface (skill section 1.2). The core only ever talks to modules
// through this table; it never includes or calls a module's own code.
#pragma once

#include <stdint.h>

namespace mod {

enum class Status : uint8_t { OK = 0, ABSENT, ERROR };

enum class CoreState : uint8_t {
    BOOT, IDLE, HOMING, JOG, TARE, PRELOAD, READY, RUN, HOLD, CYCLE, BREAK_HOLD, FAULT
};

struct ConfigSection {
    uint8_t id;
    uint8_t len;
    const uint8_t* data;
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
};

// Defined in src/core/modules.cpp. Empty in the phase 1 build.
extern const Module* const MODULES[];
extern const uint8_t MODULE_COUNT;

}  // namespace mod
