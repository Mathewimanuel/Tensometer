#include "cycle_sequencer.h"

namespace cycle {

int32_t Sequencer::target_steps() const {
    switch (phase_) {
        case Phase::TO_FLOOR:
        case Phase::TO_MIN:
            return params_.target_min_steps;
        case Phase::TO_MAX:
            return params_.target_max_steps;
        case Phase::IDLE:
        case Phase::DONE:
            return params_.target_min_steps;  // don't-care; caller must not be moving here
    }
    return params_.target_min_steps;
}

uint32_t Sequencer::cruise_hz() const {
    return (phase_ == Phase::IDLE || phase_ == Phase::DONE) ? 0 : params_.cruise_hz;
}

uint32_t Sequencer::accel_hz_per_s() const {
    return (phase_ == Phase::IDLE || phase_ == Phase::DONE) ? 0 : params_.accel_hz_per_s;
}

uint32_t Sequencer::steps_remaining(int32_t position_steps) const {
    if (phase_ == Phase::IDLE || phase_ == Phase::DONE) return 0;
    const int64_t diff =
        static_cast<int64_t>(target_steps()) - static_cast<int64_t>(position_steps);
    return diff < 0 ? static_cast<uint32_t>(-diff) : static_cast<uint32_t>(diff);
}

void Sequencer::start(const Params& params) {
    params_ = params;
    cycles_completed_ = 0;
    phase_ = Phase::TO_FLOOR;
}

void Sequencer::abort() { phase_ = Phase::IDLE; }

Phase Sequencer::tick(int32_t position_steps) {
    switch (phase_) {
        case Phase::IDLE:
        case Phase::DONE:
            break;  // no-op until start() is called (again)

        case Phase::TO_FLOOR:
            if (position_steps >= params_.target_min_steps) {
                phase_ = Phase::TO_MAX;
            }
            break;

        case Phase::TO_MAX:
            if (position_steps >= params_.target_max_steps) {
                phase_ = Phase::TO_MIN;
            }
            break;

        case Phase::TO_MIN:
            if (position_steps <= params_.target_min_steps) {
                ++cycles_completed_;
                phase_ = (cycles_completed_ >= params_.cycle_count) ? Phase::DONE : Phase::TO_MAX;
            }
            break;
    }
    return phase_;
}

}  // namespace cycle
