// Hardware constants. Single source for every hardware number used by the firmware.
// Values come from the firmware skill (rev 4) section 2. Do not add a value that is
// not in the skill; mark unverified values TODO(verify).
#pragma once

#include <stdint.h>

namespace cfg {

// ---- Drive train: TB6600 + 23HS22-2904S + DTGN PX57 5:1 + SFU1204 -------------
constexpr uint32_t MOTOR_FULL_STEPS_PER_REV = 200;
constexpr uint32_t MICROSTEPS = 8;          // Fixed on the TB6600 DIP switches.
constexpr uint32_t GEAR_RATIO = 5;          // DTGN PX57 5:1.
constexpr uint32_t SCREW_LEAD_NM = 4000000; // SFU1204: 4 mm lead.

constexpr uint32_t STEPS_PER_SCREW_REV = MOTOR_FULL_STEPS_PER_REV * MICROSTEPS * GEAR_RATIO;
constexpr uint32_t NM_PER_STEP = SCREW_LEAD_NM / STEPS_PER_SCREW_REV;   // 500 nm
constexpr uint32_t STEPS_PER_MM = 1000000 / NM_PER_STEP;               // 2000

static_assert(SCREW_LEAD_NM % STEPS_PER_SCREW_REV == 0, "step size must be a whole number of nm");
static_assert(NM_PER_STEP == 500, "drive train changed: re-check every derived number in the skill");

// ---- Step signal timing (TB6600 opto via 74AHCT244, active low) -----------------
// TODO(verify): opto part on the actual TB6600 board. 5 us is conservative for
// slow PC817-class optos; tighten only after scoping the opto output.
constexpr uint32_t STEP_PULSE_US = 5;
constexpr uint32_t STEP_MIN_OFF_US = 5;
constexpr uint32_t DIR_SETUP_US = 5;
constexpr uint32_t STEP_RATE_MAX_HZ = 20000;

// ---- Speeds ---------------------------------------------------------------------
constexpr uint32_t SPEED_MAX_MM_PER_MIN = 500;
constexpr uint32_t JOG_LOADED_MAX_MM_PER_MIN = 10;

constexpr uint32_t step_rate_hz_for(uint32_t mm_per_min) {
    return (mm_per_min * STEPS_PER_MM + 59) / 60;  // round up: conservative for the cap check
}
static_assert(step_rate_hz_for(SPEED_MAX_MM_PER_MIN) <= STEP_RATE_MAX_HZ,
              "max speed exceeds the step-rate cap");
static_assert(1000000 / STEP_RATE_MAX_HZ >= STEP_PULSE_US + STEP_MIN_OFF_US,
              "step period too short for pulse + off time");

// ---- HX711 ------------------------------------------------------------------------
constexpr uint32_t HX711_RATE_HZ = 80;                 // RATE pin high. TODO(verify) on the board.
constexpr uint32_t HX711_PERIOD_US = 1000000 / HX711_RATE_HZ;  // 12 500 us
constexpr uint32_t HX711_EFFECTIVE_DELAY_US = HX711_PERIOD_US / 2;  // sample centre
constexpr uint32_t HX711_SCK_HIGH_MAX_US = 60;         // chip powers down beyond this

// ---- Geometry ---------------------------------------------------------------------
// TODO(verify): jaw gap at the home position (LIM_FAR), measured once on the machine.
constexpr uint32_t JAW_GAP_AT_HOME_UM = 0;

// ---- Pins (Teensy 4.1). Status per skill section 2.6 ------------------------------
namespace pin {
constexpr uint8_t STEP = 2;
constexpr uint8_t DIR = 3;
constexpr uint8_t ENA = 4;
constexpr uint8_t HX711_DOUT = 5;
constexpr uint8_t HX711_SCK = 6;
constexpr uint8_t LIM_NEAR = 7;     // NC, external 2.2k pull-up + RC
constexpr uint8_t LIM_FAR = 8;      // NC, external 2.2k pull-up + RC
constexpr uint8_t ESTOP_SENSE = 9;  // opto from the contactor output
constexpr uint8_t I2C_SDA = 18;
constexpr uint8_t I2C_SCL = 19;
constexpr uint8_t STATUS_LED = 13;
// PROVISIONAL
constexpr uint8_t GUARD_SENSE = 28;
constexpr uint8_t SYNC_LED = 29;
constexpr uint8_t BTN_START = 30;
constexpr uint8_t BTN_STOP = 31;
constexpr uint8_t BTN_JOG_POS = 32;
constexpr uint8_t BTN_JOG_NEG = 33;
constexpr uint8_t BTN_HOME = 34;
constexpr uint8_t BTN_TARE = 35;
constexpr uint8_t FAULT_LED = 36;
// RESERVED for the continuity module (phase 2): 16, 17, 20 to 26. Do not use.
}  // namespace pin

}  // namespace cfg
