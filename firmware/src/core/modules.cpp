// Compile-time module table (skill section 1.2). Adding or removing a module should
// only touch this file, build flags, protocol.h and the module's own files.
#include "module_api.h"

namespace mod {

// Phase 1: no modules yet. sim_continuity, continuity and routing are added here
// behind their FEATURE_* flags when they are written. A zero-length array is not
// legal C++, so the table holds a single null terminator and MODULE_COUNT is 0.
const Module* const MODULES[] = {nullptr};
const uint8_t MODULE_COUNT = 0;

}  // namespace mod
