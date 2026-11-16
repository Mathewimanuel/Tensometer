// PC link driver (skill section 8, main-loop step 6). Thin: reads USB Serial bytes into
// lib/frame's Reader, hands decoded commands to lib/dispatcher, and encodes outgoing
// HELLO/ACK/NACK/HEARTBEAT/TELEMETRY/EVENT with lib/messages. No protocol decisions live
// here; every NACK contract is lib/dispatcher's (natively tested).
//
// Never blocks (skill 3.3): reads at most RX_BYTES_PER_PASS per call, and an outgoing
// frame that does not fit in the USB TX buffer right now is dropped and counted, not
// waited on.
#pragma once

#include <stdint.h>

#include "dispatcher.h"
#include "protocol.h"
#include "state_machine.h"

namespace comms {

// Everything the outgoing status frames report, snapshotted by the main loop.
struct Status {
    mod::CoreState state;
    proto::LockOwner lock_owner;
    uint32_t inhibit_mask;
    proto::EventType fault_cause;
    int32_t position_steps;
    bool force_valid;      // TELEMETRY is only sent with a real force sample (see .cpp)
    int32_t force_raw_counts;
    int32_t force_mN;
    int32_t force_filtered_mN;
    uint8_t telem_flags;   // proto::TELEM_*
    uint32_t cycle_index;
    uint8_t capabilities;  // proto::CAP_*
    uint8_t cell_profile;  // limits::ProfileId
};

void begin();

// Reads received bytes and dispatches at most ONE command per call (PC HEARTBEAT pings are
// consumed here and don't count; everything else goes to `dispatcher` and gets an
// ACK/NACK), so the main loop reacts to each state change before the next command. Emits
// the EVENT the dispatcher asks for (the dispatcher itself owns the FAULT cause).
void poll(uint64_t now_us, dispatch::Dispatcher& dispatcher,
          const dispatch::Context& ctx, core_sm::StateMachine& sm, int32_t position_steps);

// True if no PC heartbeat ping arrived within limits::HEARTBEAT_TIMEOUT_US, or none ever
// has. core_safety only acts on it while motion is active.
bool heartbeat_stale(uint64_t now_us);

void send_event(uint64_t now_us, proto::EventType type, int32_t position_steps);

// Periodic output: HELLO on every new USB connection, HEARTBEAT status, TELEMETRY.
void service(uint64_t now_us, const Status& status);

uint32_t dropped_frames();  // TX-full drops plus undecodable RX frames, since boot

}  // namespace comms
