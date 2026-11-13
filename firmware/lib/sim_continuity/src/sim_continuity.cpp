#include "sim_continuity.h"

#include "protocol.h"

namespace sim_continuity {

bool EventQueue::inject_open(uint8_t channel, uint64_t timestamp_us) {
    if (count_ >= QUEUE_CAPACITY) {
        return false;
    }
    const uint8_t tail = static_cast<uint8_t>((head_ + count_) % QUEUE_CAPACITY);
    buf_[tail] = mod::ConductorEvent{channel, timestamp_us};
    ++count_;
    return true;
}

uint8_t EventQueue::drain(mod::ConductorEvent* out, uint8_t cap) {
    uint8_t n = 0;
    while (n < cap && count_ > 0) {
        out[n] = buf_[head_];
        head_ = static_cast<uint8_t>((head_ + 1) % QUEUE_CAPACITY);
        --count_;
        ++n;
    }
    return n;
}

namespace {

EventQueue g_queue;

mod::Status init() { return mod::Status::OK; }

// Software-only: "hardware" is never absent once this build is compiled in.
uint8_t capability() { return proto::CAP_SIM; }

// Nothing timed to do: events only ever appear via inject_open(), called from outside
// the main loop (native tests, or later a bench debug command).
void poll() {}

// No wire config section exists for this module (it is bench/native-test only, skill
// 1.1 -- it never appears in a real test config), so there is nothing to validate.
mod::Status validate_config(const mod::ConfigSection&) { return mod::Status::OK; }

// Software-only: nothing to fault.
uint16_t safety_check() { return 0; }

// Never inhibits motion.
uint32_t inhibit_bits() { return 0; }

void on_state_change(mod::CoreState, mod::CoreState) {}

// No wire event/stream is allocated for this module -- it has nothing to tell the PC.
void emit() {}

uint8_t conductor_events(mod::ConductorEvent* out, uint8_t cap) { return g_queue.drain(out, cap); }

}  // namespace

const mod::Module MODULE = {
    "sim_continuity", init,       capability,       poll, validate_config,
    safety_check,      inhibit_bits, on_state_change, emit, conductor_events,
};

bool inject_open(uint8_t channel, uint64_t timestamp_us) {
    return g_queue.inject_open(channel, timestamp_us);
}

}  // namespace sim_continuity
