// Boot self-test latch (Mathew 2026-11-16, decision 4). Pure logic, no Arduino dependency.
// Runs every module's init() at boot and remembers which returned ERROR (ABSENT is never a
// fault, skill 1.4). A failure latches: the core goes to FAULT, and RESET out of it calls
// retry(), which re-runs init() on the failed modules only. RESET reaches IDLE only if
// retry() returns true, so a STOP+RESET can never bypass a failed self-test.
#pragma once

#include <stdint.h>

#include "module_api.h"

namespace self_test {

class Latch {
public:
    // `table` is the compile-time module table (null entries allowed), up to 32 modules.
    Latch(const mod::Module* const* table, uint8_t count) : table_(table), count_(count) {}

    bool run_all();  // init() on every module; true if none returned ERROR
    bool retry();    // init() again on the latched failures only; true once all pass
    bool ok() const { return failed_ == 0; }

private:
    bool init_ok(uint8_t i) const;

    const mod::Module* const* table_;
    uint8_t count_;
    uint32_t failed_ = 0;  // bit i = MODULES[i] failed init()
};

}  // namespace self_test
