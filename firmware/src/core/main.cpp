// Phase 1 firmware entry point. STAGE 1 SKELETON: safe boot only. No motion code
// exists yet, so this build cannot move the motor.
#include <Arduino.h>

#include "config.h"
#include "safety_limits.h"
#include "module_api.h"
#include "protocol.h"

namespace {

// Drive the driver interface to its safe state before anything else runs.
// ENA: LOW at the Teensy -> opto ON -> TB6600 DISABLED (skill 2.1; TODO(verify) polarity
// on the bench). pinMode(OUTPUT) leaves the pin LOW, which is already the safe level;
// the explicit write documents intent.
//
// SAFETY GAP (confirmed 2026-10-26, skill 2.1): there is no 74AHCT244 buffer and no
// pull-down resistor anywhere on ENA (nor pull-ups on STEP/DIR) -- the Teensy pins
// wire straight to the TB6600's common-anode (+5V) opto inputs. Nothing holds ENA
// disabled between power-up and this line running, or during a reset while the 5V/
// TB6600 side stays powered. This code cannot close that gap; it needs a physical
// pull-down added on the ENA line (see docs/decisions.md 2026-10-26, issue #18).
void drive_outputs_safe() {
    pinMode(cfg::pin::ENA, OUTPUT);
    digitalWriteFast(cfg::pin::ENA, LOW);

    // STEP idles HIGH (opto off). pinMode(OUTPUT) briefly drives LOW for well under
    // 1 us before the write below: shorter than any opto's minimum pulse, and the
    // driver is already disabled, so no step can be taken.
    pinMode(cfg::pin::STEP, OUTPUT);
    digitalWriteFast(cfg::pin::STEP, HIGH);
    pinMode(cfg::pin::DIR, OUTPUT);
    digitalWriteFast(cfg::pin::DIR, HIGH);
}

void configure_inputs() {
    // Limits and e-stop use external pull-ups and RC filters (skill 2.6): plain INPUT.
    pinMode(cfg::pin::LIM_NEAR, INPUT);
    pinMode(cfg::pin::LIM_FAR, INPUT);
    pinMode(cfg::pin::ESTOP_SENSE, INPUT);
    pinMode(cfg::pin::HX711_DOUT, INPUT);
    pinMode(cfg::pin::HX711_SCK, OUTPUT);
    digitalWriteFast(cfg::pin::HX711_SCK, LOW);  // SCK high > 60 us powers the HX711 down
}

uint8_t capabilities() {
    uint8_t caps = proto::CAP_CORE;
    for (uint8_t i = 0; i < mod::MODULE_COUNT; ++i) {
        if (mod::MODULES[i] != nullptr && mod::MODULES[i]->capability != nullptr) {
            caps |= mod::MODULES[i]->capability();
        }
    }
    return caps;
}

uint32_t last_blink_ms = 0;

}  // namespace

void setup() {
    drive_outputs_safe();
    configure_inputs();
    pinMode(cfg::pin::STATUS_LED, OUTPUT);
    Serial.begin(115200);  // USB: baud is ignored on Teensy, kept for monitor tools
}

void loop() {
    // Stage 1 placeholder: status blink plus an ASCII banner while no protocol exists.
    const uint32_t now_ms = millis();
    if (now_ms - last_blink_ms >= 500) {
        last_blink_ms = now_ms;
        digitalToggleFast(cfg::pin::STATUS_LED);
        if (Serial) {
            Serial.printf("tensometer skeleton proto %u.%u caps 0x%02X profile %s\n",
                          proto::VERSION_MAJOR, proto::VERSION_MINOR, capabilities(),
                          limits::PROFILES[0].name);
        }
    }
}
