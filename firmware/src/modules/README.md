# Optional modules

continuity/ and routing/ go here once built (skill section 1): each implements mod::Module from include/module_api.h, with hardware access in this thin driver layer and pure logic in its own lib/, registered only in src/core/modules.cpp behind its FEATURE_* flag.

sim_continuity is the exception: it has no hardware to drive, so its whole mod::Module implementation lives in lib/sim_continuity instead and gets native tests like any other pure-logic library.
