#include <unity.h>

#include "messages.h"

void setUp() {}
void tearDown() {}

using messages::Result;

static void test_hello_round_trip() {
    proto::HelloBody in{proto::VERSION_MAJOR, proto::VERSION_MINOR, 0xDEADBEEFu,
                         proto::CAP_CORE | proto::CAP_SIM, 1, 600000000u};
    uint8_t buf[proto::HELLO_WIRE_LEN];
    size_t len = 0;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Result::OK),
                           static_cast<int>(messages::encode_hello(in, buf, sizeof(buf), &len)));
    TEST_ASSERT_EQUAL_UINT32(proto::HELLO_WIRE_LEN, len);

    proto::HelloBody out{};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Result::OK),
                           static_cast<int>(messages::decode_hello(buf, len, &out)));
    TEST_ASSERT_EQUAL_UINT8(in.version_major, out.version_major);
    TEST_ASSERT_EQUAL_UINT8(in.version_minor, out.version_minor);
    TEST_ASSERT_EQUAL_UINT32(in.build_id, out.build_id);
    TEST_ASSERT_EQUAL_UINT8(in.capabilities, out.capabilities);
    TEST_ASSERT_EQUAL_UINT8(in.cell_profile, out.cell_profile);
    TEST_ASSERT_EQUAL_UINT32(in.cpu_hz, out.cpu_hz);
}

static void test_ack_round_trip() {
    proto::AckBody in{static_cast<uint8_t>(proto::MsgId::CMD_ARM), 42};
    uint8_t buf[proto::ACK_WIRE_LEN];
    size_t len = 0;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Result::OK),
                           static_cast<int>(messages::encode_ack(in, buf, sizeof(buf), &len)));
    TEST_ASSERT_EQUAL_UINT32(proto::ACK_WIRE_LEN, len);

    proto::AckBody out{};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Result::OK),
                           static_cast<int>(messages::decode_ack(buf, len, &out)));
    TEST_ASSERT_EQUAL_UINT8(in.acked_msg_id, out.acked_msg_id);
    TEST_ASSERT_EQUAL_UINT8(in.acked_seq, out.acked_seq);
}

static void test_nack_round_trip() {
    proto::NackBody in{static_cast<uint8_t>(proto::MsgId::CMD_START), 7,
                        static_cast<uint8_t>(proto::Err::NOT_HOMED)};
    uint8_t buf[proto::NACK_WIRE_LEN];
    size_t len = 0;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Result::OK),
                           static_cast<int>(messages::encode_nack(in, buf, sizeof(buf), &len)));
    TEST_ASSERT_EQUAL_UINT32(proto::NACK_WIRE_LEN, len);

    proto::NackBody out{};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Result::OK),
                           static_cast<int>(messages::decode_nack(buf, len, &out)));
    TEST_ASSERT_EQUAL_UINT8(in.acked_msg_id, out.acked_msg_id);
    TEST_ASSERT_EQUAL_UINT8(in.acked_seq, out.acked_seq);
    TEST_ASSERT_EQUAL_UINT8(in.error_code, out.error_code);
}

static void test_heartbeat_status_round_trip() {
    proto::HeartbeatBody in{mod::CoreState::RUN, proto::LockOwner::PC, 0x00000005u,
                             static_cast<uint8_t>(proto::EventType::NONE)};
    uint8_t buf[proto::HEARTBEAT_STATUS_WIRE_LEN];
    size_t len = 0;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(Result::OK),
        static_cast<int>(messages::encode_heartbeat_status(in, buf, sizeof(buf), &len)));
    TEST_ASSERT_EQUAL_UINT32(proto::HEARTBEAT_STATUS_WIRE_LEN, len);

    proto::HeartbeatBody out{};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Result::OK),
                           static_cast<int>(messages::decode_heartbeat_status(buf, len, &out)));
    TEST_ASSERT_TRUE(in.state == out.state);
    TEST_ASSERT_TRUE(in.lock_owner == out.lock_owner);
    TEST_ASSERT_EQUAL_UINT32(in.inhibit_mask, out.inhibit_mask);
    TEST_ASSERT_EQUAL_UINT8(in.fault_cause, out.fault_cause);
}

static void test_telemetry_round_trip() {
    proto::TelemetryBody in{123456789012345ull, -1000, 8388608, -980000, -975500,
                             mod::CoreState::RUN,
                             static_cast<uint8_t>(proto::TELEM_POSITION_VALID | proto::TELEM_HOMED)};
    uint8_t buf[proto::TELEMETRY_WIRE_LEN];
    size_t len = 0;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(Result::OK),
        static_cast<int>(messages::encode_telemetry(in, buf, sizeof(buf), &len)));
    TEST_ASSERT_EQUAL_UINT32(proto::TELEMETRY_WIRE_LEN, len);

    proto::TelemetryBody out{};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Result::OK),
                           static_cast<int>(messages::decode_telemetry(buf, len, &out)));
    TEST_ASSERT_TRUE(in.timestamp_us == out.timestamp_us);
    TEST_ASSERT_EQUAL_INT32(in.position_steps, out.position_steps);
    TEST_ASSERT_EQUAL_INT32(in.force_raw_counts, out.force_raw_counts);
    TEST_ASSERT_EQUAL_INT32(in.force_raw_mN, out.force_raw_mN);
    TEST_ASSERT_EQUAL_INT32(in.force_filtered_mN, out.force_filtered_mN);
    TEST_ASSERT_TRUE(in.state == out.state);
    TEST_ASSERT_EQUAL_UINT8(in.flags, out.flags);
}

static void test_event_round_trip() {
    proto::EventBody in{987654321098765ull, proto::EventType::SUBSTRATE_BREAK, 0, 50000, 1234567};
    uint8_t buf[proto::EVENT_WIRE_LEN];
    size_t len = 0;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Result::OK),
                           static_cast<int>(messages::encode_event(in, buf, sizeof(buf), &len)));
    TEST_ASSERT_EQUAL_UINT32(proto::EVENT_WIRE_LEN, len);

    proto::EventBody out{};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Result::OK),
                           static_cast<int>(messages::decode_event(buf, len, &out)));
    TEST_ASSERT_TRUE(in.timestamp_us == out.timestamp_us);
    TEST_ASSERT_TRUE(in.type == out.type);
    TEST_ASSERT_EQUAL_UINT8(in.aux, out.aux);
    TEST_ASSERT_EQUAL_INT32(in.position_steps, out.position_steps);
    TEST_ASSERT_EQUAL_INT32(in.force_mN, out.force_mN);
}

static void test_event_negative_force_round_trips() {
    // A compressive/noise reading can be negative; the i32 path must sign-extend cleanly.
    proto::EventBody in{0, proto::EventType::MAX_FORCE, 0, -500, -12345};
    uint8_t buf[proto::EVENT_WIRE_LEN];
    size_t len = 0;
    messages::encode_event(in, buf, sizeof(buf), &len);
    proto::EventBody out{};
    messages::decode_event(buf, len, &out);
    TEST_ASSERT_EQUAL_INT32(-500, out.position_steps);
    TEST_ASSERT_EQUAL_INT32(-12345, out.force_mN);
}

static void test_encode_no_space_when_buffer_too_small() {
    proto::EventBody in{0, proto::EventType::SLIP, 0, 0, 0};
    uint8_t buf[proto::EVENT_WIRE_LEN - 1];  // one byte short
    size_t len = 0;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Result::NO_SPACE),
                           static_cast<int>(messages::encode_event(in, buf, sizeof(buf), &len)));
}

static void test_decode_too_short_when_buffer_truncated() {
    proto::EventBody in{0, proto::EventType::SLIP, 0, 0, 0};
    uint8_t buf[proto::EVENT_WIRE_LEN];
    size_t len = 0;
    messages::encode_event(in, buf, sizeof(buf), &len);

    proto::EventBody out{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(Result::TOO_SHORT),
        static_cast<int>(messages::decode_event(buf, len - 1, &out)));
}

static void test_decode_rejects_oversized_buffer() {
    // A version-mismatched sender's longer body must be rejected, not silently truncated.
    proto::EventBody in{0, proto::EventType::SLIP, 0, 0, 0};
    uint8_t buf[proto::EVENT_WIRE_LEN + 4] = {};
    size_t len = 0;
    messages::encode_event(in, buf, proto::EVENT_WIRE_LEN, &len);

    proto::EventBody out{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(Result::TOO_SHORT),
        static_cast<int>(messages::decode_event(buf, len + 4, &out)));
}

static void test_decode_rejects_null_pointers() {
    proto::EventBody out{};
    uint8_t buf[proto::EVENT_WIRE_LEN] = {};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Result::BAD_ARG),
                           static_cast<int>(messages::decode_event(nullptr, 0, &out)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(Result::BAD_ARG),
        static_cast<int>(messages::decode_event(buf, sizeof(buf), nullptr)));
}

static void test_encode_rejects_null_pointers() {
    proto::EventBody in{};
    size_t len = 0;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(Result::BAD_ARG),
        static_cast<int>(messages::encode_event(in, nullptr, 16, &len)));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_hello_round_trip);
    RUN_TEST(test_ack_round_trip);
    RUN_TEST(test_nack_round_trip);
    RUN_TEST(test_heartbeat_status_round_trip);
    RUN_TEST(test_telemetry_round_trip);
    RUN_TEST(test_event_round_trip);
    RUN_TEST(test_event_negative_force_round_trips);
    RUN_TEST(test_encode_no_space_when_buffer_too_small);
    RUN_TEST(test_decode_too_short_when_buffer_truncated);
    RUN_TEST(test_decode_rejects_oversized_buffer);
    RUN_TEST(test_decode_rejects_null_pointers);
    RUN_TEST(test_encode_rejects_null_pointers);
    return UNITY_END();
}
