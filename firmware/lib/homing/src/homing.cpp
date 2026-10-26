#include "homing.h"

namespace homing {

Dir Sequencer::direction() const {
    switch (phase_) {
        case Phase::FAST_APPROACH:
        case Phase::SLOW_REAPPROACH:
            return Dir::TOWARD_LIM_FAR;
        case Phase::BACK_OFF:
            return Dir::TOWARD_LIM_NEAR;
        case Phase::IDLE:
        case Phase::DONE:
            return Dir::TOWARD_LIM_FAR;  // don't-care; caller must not be moving here
    }
    return Dir::TOWARD_LIM_FAR;
}

uint32_t Sequencer::cruise_hz() const {
    switch (phase_) {
        case Phase::FAST_APPROACH:
        case Phase::BACK_OFF:
            return params_.fast_hz;
        case Phase::SLOW_REAPPROACH:
            return params_.slow_hz;
        case Phase::IDLE:
        case Phase::DONE:
            return 0;
    }
    return 0;
}

void Sequencer::start(const Params& params) {
    params_ = params;
    phase_ = Phase::FAST_APPROACH;
}

Phase Sequencer::tick(int32_t position_steps, bool lim_far_active) {
    switch (phase_) {
        case Phase::IDLE:
        case Phase::DONE:
            break;  // no-op until start() is called (again)

        case Phase::FAST_APPROACH:
            if (lim_far_active) {
                phase_ = Phase::BACK_OFF;
                phase_start_position_ = position_steps;
            }
            break;

        case Phase::BACK_OFF: {
            const int32_t moved = position_steps - phase_start_position_;
            const uint32_t moved_abs =
                moved < 0 ? static_cast<uint32_t>(-moved) : static_cast<uint32_t>(moved);
            if (moved_abs >= params_.back_off_steps) {
                phase_ = Phase::SLOW_REAPPROACH;
            }
            break;
        }

        case Phase::SLOW_REAPPROACH:
            if (lim_far_active) {
                phase_ = Phase::DONE;
            }
            break;
    }
    return phase_;
}

}  // namespace homing
