#include <string.h>
#include <unity.h>

#include "crc16.h"
#include "frame.h"

void setUp() {}
void tearDown() {}

namespace {

size_t build(uint8_t id, uint8_t seq, const uint8_t* body, size_t len, uint8_t* wire, size_t cap) {
    size_t n = 0;
    TEST_ASSERT_EQUAL(frame::Result::OK, frame::encode(id, seq, body, len, wire, cap, &n));
    return n;
}

}  // namespace

static void test_round_trip_all_lengths() {
    uint8_t body[frame::MAX_BODY_LEN];
    for (size_t i = 0; i < sizeof(body); ++i) body[i] = static_cast<uint8_t>((i * 7) % 5 == 0 ? 0 : i);

    for (size_t len = 0; len <= frame::MAX_BODY_LEN; ++len) {
        uint8_t wire[frame::MAX_WIRE_LEN];
        const size_t n = build(0x10, static_cast<uint8_t>(len), body, len, wire, sizeof(wire));
        TEST_ASSERT_TRUE(n <= frame::MAX_WIRE_LEN);
        TEST_ASSERT_EQUAL_HEX8(0x00, wire[n - 1]);              // delimiter last
        for (size_t i = 0; i + 1 < n; ++i) TEST_ASSERT_NOT_EQUAL(0, wire[i]);  // no zeros inside

        uint8_t scratch[frame::MAX_PAYLOAD_LEN];
        frame::Frame f{};
        TEST_ASSERT_EQUAL(frame::Result::OK, frame::decode(wire, n - 1, scratch, sizeof(scratch), &f));
        TEST_ASSERT_EQUAL_HEX8(0x10, f.msg_id);
        TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(len), f.seq);
        TEST_ASSERT_EQUAL_size_t(len, f.body_len);
        if (len > 0) TEST_ASSERT_EQUAL_HEX8_ARRAY(body, f.body, len);
    }
}

static void test_crc_is_little_endian_over_header_and_body() {
    const uint8_t body[] = {0xAA, 0xBB};
    uint8_t wire[32];
    const size_t n = build(0x21, 0x05, body, sizeof(body), wire, sizeof(wire));
    uint8_t payload[16];
    size_t plen = 0;
    TEST_ASSERT_EQUAL(cobs::Result::OK, cobs::decode(wire, n - 1, payload, sizeof(payload), &plen));
    TEST_ASSERT_EQUAL_size_t(6, plen);
    const uint8_t covered[] = {0x21, 0x05, 0xAA, 0xBB};
    const uint16_t crc = crc16::compute(covered, sizeof(covered));
    TEST_ASSERT_EQUAL_HEX8(crc & 0xFF, payload[4]);
    TEST_ASSERT_EQUAL_HEX8(crc >> 8, payload[5]);
}

static void test_corruption_rejected() {
    const uint8_t body[] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t wire[32];
    const size_t n = build(0x11, 0x42, body, sizeof(body), wire, sizeof(wire));
    // Flip each bit of each encoded byte (except the delimiter). Every corruption must
    // be rejected: by COBS structure, length, or CRC. Never accepted as a valid frame.
    for (size_t i = 0; i + 1 < n; ++i) {
        for (uint8_t bit = 0; bit < 8; ++bit) {
            uint8_t bad[32];
            memcpy(bad, wire, n);
            bad[i] ^= static_cast<uint8_t>(1u << bit);
            uint8_t scratch[frame::MAX_PAYLOAD_LEN];
            frame::Frame f{};
            TEST_ASSERT_NOT_EQUAL(frame::Result::OK,
                                  frame::decode(bad, n - 1, scratch, sizeof(scratch), &f));
        }
    }
}

static void test_body_too_long_and_no_space() {
    uint8_t body[frame::MAX_BODY_LEN + 1] = {};
    uint8_t wire[frame::MAX_WIRE_LEN + 8];
    size_t n = 0;
    TEST_ASSERT_EQUAL(frame::Result::BODY_TOO_LONG,
                      frame::encode(1, 1, body, sizeof(body), wire, sizeof(wire), &n));
    TEST_ASSERT_EQUAL(frame::Result::NO_SPACE, frame::encode(1, 1, body, 10, wire, 5, &n));
}

static void test_too_short_rejected() {
    // A valid COBS encoding of 3 bytes: too short for header + CRC.
    const uint8_t enc[] = {0x04, 0x01, 0x02, 0x03};
    uint8_t scratch[frame::MAX_PAYLOAD_LEN];
    frame::Frame f{};
    TEST_ASSERT_EQUAL(frame::Result::TOO_SHORT, frame::decode(enc, sizeof(enc), scratch, sizeof(scratch), &f));
}

static void test_reader_splits_stream() {
    uint8_t stream[200];
    size_t total = 0;
    const uint8_t b1[] = {0x00, 0x01};
    const uint8_t b2[] = {0x09};
    total += build(0x04, 1, b1, sizeof(b1), stream + total, sizeof(stream) - total);
    total += build(0x04, 2, b2, sizeof(b2), stream + total, sizeof(stream) - total);
    stream[total++] = 0x00;  // stray delimiter: must not produce a frame

    frame::Reader r;
    int frames = 0;
    for (size_t i = 0; i < total; ++i) {
        if (r.push(stream[i])) {
            uint8_t scratch[frame::MAX_PAYLOAD_LEN];
            frame::Frame f{};
            TEST_ASSERT_EQUAL(frame::Result::OK, frame::decode(r.data(), r.length(), scratch, sizeof(scratch), &f));
            ++frames;
            TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(frames), f.seq);
        }
    }
    TEST_ASSERT_EQUAL_INT(2, frames);
}

static void test_reader_discards_overlong_then_recovers() {
    frame::Reader r;
    for (size_t i = 0; i < frame::MAX_WIRE_LEN + 10; ++i) TEST_ASSERT_FALSE(r.push(0x55));
    TEST_ASSERT_FALSE(r.push(0x00));  // end of the overlong frame: discarded
    TEST_ASSERT_EQUAL_UINT32(1, r.overflow_count());

    uint8_t wire[32];
    const uint8_t body[] = {7};
    const size_t n = build(0x02, 9, body, sizeof(body), wire, sizeof(wire));
    bool got = false;
    for (size_t i = 0; i < n; ++i) got = r.push(wire[i]);
    TEST_ASSERT_TRUE(got);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_round_trip_all_lengths);
    RUN_TEST(test_crc_is_little_endian_over_header_and_body);
    RUN_TEST(test_corruption_rejected);
    RUN_TEST(test_body_too_long_and_no_space);
    RUN_TEST(test_too_short_rejected);
    RUN_TEST(test_reader_splits_stream);
    RUN_TEST(test_reader_discards_overlong_then_recovers);
    return UNITY_END();
}
