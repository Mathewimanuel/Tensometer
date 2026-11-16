#include "comms.h"

#include <Arduino.h>

#include "config.h"
#include "frame.h"
#include "messages.h"
#include "safety_limits.h"

namespace comms {

namespace {

// Bounds the per-pass RX work so a flood cannot stretch the loop (skill 3.3). One max-size
// frame plus change; not a hardware value.
constexpr uint32_t RX_BYTES_PER_PASS = 320;
// Firmware -> PC status period. Not specified anywhere for this direction; mirrors the
// PC's own 100 ms ping (skill 8), well inside the PC's 1 s loss threshold.
constexpr uint64_t HEARTBEAT_PERIOD_US = 100000;
// TELEMETRY is the force-sample stream (skill 8: 80 Hz = the HX711 rate).
constexpr uint64_t TELEMETRY_PERIOD_US = cfg::HX711_PERIOD_US;

frame::Reader reader;
uint8_t scratch[frame::MAX_PAYLOAD_LEN];
uint8_t tx_seq = 0;
uint32_t dropped = 0;

bool have_ping = false;
uint64_t last_ping_us = 0;
bool was_connected = false;
uint64_t last_heartbeat_us = 0;
uint64_t last_telemetry_us = 0;

// Encodes and writes one frame, or drops it if the TX buffer cannot take it right now.
void send(proto::MsgId id, const uint8_t* body, size_t body_len) {
    if (!Serial) return;
    uint8_t wire[frame::MAX_WIRE_LEN];
    size_t wire_len = 0;
    if (frame::encode(static_cast<uint8_t>(id), tx_seq++, body, body_len, wire, sizeof(wire),
                      &wire_len) != frame::Result::OK ||
        Serial.availableForWrite() < static_cast<int>(wire_len)) {
        ++dropped;
        return;
    }
    Serial.write(wire, wire_len);
}

void reply(uint8_t msg_id, uint8_t seq, const dispatch::Reply& r) {
    uint8_t body[proto::NACK_WIRE_LEN];
    size_t len = 0;
    if (r.ack) {
        messages::encode_ack({msg_id, seq}, body, sizeof(body), &len);
        send(proto::MsgId::ACK, body, len);
    } else {
        messages::encode_nack({msg_id, seq, static_cast<uint8_t>(r.err)}, body, sizeof(body),
                              &len);
        send(proto::MsgId::NACK, body, len);
    }
}

}  // namespace

void begin() {
    Serial.begin(115200);  // USB: baud is ignored on Teensy, kept for monitor tools
}

void poll(uint64_t now_us, dispatch::Dispatcher& dispatcher, const dispatch::Context& ctx,
          core_sm::StateMachine& sm, int32_t position_steps) {
    for (uint32_t i = 0; i < RX_BYTES_PER_PASS && Serial.available() > 0; ++i) {
        if (!reader.push(static_cast<uint8_t>(Serial.read()))) continue;

        frame::Frame f{};
        if (frame::decode(reader.data(), reader.length(), scratch, sizeof(scratch), &f) !=
            frame::Result::OK) {
            // msg_id/seq of a frame that failed CRC are untrustworthy, so no NACK; the PC's
            // own command timeout covers it.
            ++dropped;
            continue;
        }

        if (f.msg_id == static_cast<uint8_t>(proto::MsgId::HEARTBEAT)) {
            if (f.body_len == 0) {
                have_ping = true;
                last_ping_us = now_us;
            } else {
                reply(f.msg_id, f.seq, dispatch::Reply{false, proto::Err::BAD_LENGTH,
                                                       proto::EventType::NONE});
            }
            continue;
        }

        const dispatch::Reply r = dispatcher.handle(f.msg_id, f.body, f.body_len, ctx, sm);
        reply(f.msg_id, f.seq, r);
        // Not gated on ack: a CMD_STOP with a bad body still stopped (decision 1).
        if (r.event != proto::EventType::NONE) send_event(now_us, r.event, position_steps);
        // One command per pass (reviewer #6): the main loop reacts to each state change
        // before the next command is dispatched, and `ctx` is fresh for every command.
        return;
    }
}

bool heartbeat_stale(uint64_t now_us) {
    return !have_ping || now_us - last_ping_us > limits::HEARTBEAT_TIMEOUT_US;
}

void send_event(uint64_t now_us, proto::EventType type, int32_t position_steps) {
    proto::EventBody e{};
    e.timestamp_us = now_us;
    e.type = type;
    e.aux = 0;
    e.position_steps = position_steps;
    // TODO(verify): no force reading exists (#10) and EventBody has no "force unknown"
    // marker, so 0 here means "not measured", not 0 N. Open question, decisions.md 2026-11-16.
    e.force_mN = 0;
    uint8_t body[proto::EVENT_WIRE_LEN];
    size_t len = 0;
    messages::encode_event(e, body, sizeof(body), &len);
    send(proto::MsgId::EVENT, body, len);
}

void service(uint64_t now_us, const Status& s) {
    const bool connected = static_cast<bool>(Serial);
    if (connected && !was_connected) {
        proto::HelloBody h{};
        h.version_major = proto::VERSION_MAJOR;
        h.version_minor = proto::VERSION_MINOR;
        h.build_id = 0;  // no build-id scheme decided yet (opaque to the PC)
        h.capabilities = s.capabilities;
        h.cell_profile = s.cell_profile;
        h.cpu_hz = F_CPU_ACTUAL;
        uint8_t body[proto::HELLO_WIRE_LEN];
        size_t len = 0;
        messages::encode_hello(h, body, sizeof(body), &len);
        send(proto::MsgId::HELLO, body, len);
    }
    was_connected = connected;

    if (now_us - last_heartbeat_us >= HEARTBEAT_PERIOD_US) {
        last_heartbeat_us = now_us;
        const proto::HeartbeatBody hb{s.state, s.lock_owner, s.inhibit_mask,
                                      static_cast<uint8_t>(s.fault_cause)};
        uint8_t body[proto::HEARTBEAT_STATUS_WIRE_LEN];
        size_t len = 0;
        messages::encode_heartbeat_status(hb, body, sizeof(body), &len);
        send(proto::MsgId::HEARTBEAT, body, len);
    }

    // Force fails closed (2026-11-16): TelemetryBody has no "force invalid" flag, so
    // streaming without a real sample would show the PC a fabricated 0 N. Nothing is sent
    // until #10 provides samples. Open question, decisions.md 2026-11-16.
    if (s.force_valid && now_us - last_telemetry_us >= TELEMETRY_PERIOD_US) {
        last_telemetry_us = now_us;
        proto::TelemetryBody t{};
        t.timestamp_us = now_us;
        t.position_steps = s.position_steps;
        t.force_raw_counts = s.force_raw_counts;
        t.force_raw_mN = s.force_mN;
        t.force_filtered_mN = s.force_filtered_mN;
        t.state = s.state;
        t.flags = s.telem_flags;
        t.cycle_index = s.cycle_index;
        uint8_t body[proto::TELEMETRY_WIRE_LEN];
        size_t len = 0;
        messages::encode_telemetry(t, body, sizeof(body), &len);
        send(proto::MsgId::TELEMETRY, body, len);
    }
}

uint32_t dropped_frames() { return dropped; }

}  // namespace comms
