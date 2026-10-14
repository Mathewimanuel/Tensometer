#include <string.h>
#include <unity.h>

#include "cobs.h"

void setUp() {}
void tearDown() {}

namespace {

void check_encode(const uint8_t* in, size_t in_len, const uint8_t* expect, size_t expect_len) {
    uint8_t out[600];
    size_t out_len = 0;
    TEST_ASSERT_EQUAL(cobs::Result::OK, cobs::encode(in, in_len, out, sizeof(out), &out_len));
    TEST_ASSERT_EQUAL_size_t(expect_len, out_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, expect_len);
}

void round_trip(const uint8_t* in, size_t in_len) {
    uint8_t enc[600];
    uint8_t dec[600];
    size_t enc_len = 0;
    size_t dec_len = 0;
    TEST_ASSERT_EQUAL(cobs::Result::OK, cobs::encode(in, in_len, enc, sizeof(enc), &enc_len));
    TEST_ASSERT_TRUE(enc_len <= cobs::max_encoded_len(in_len));
    for (size_t i = 0; i < enc_len; ++i) TEST_ASSERT_NOT_EQUAL(0, enc[i]);
    TEST_ASSERT_EQUAL(cobs::Result::OK, cobs::decode(enc, enc_len, dec, sizeof(dec), &dec_len));
    TEST_ASSERT_EQUAL_size_t(in_len, dec_len);
    if (in_len > 0) TEST_ASSERT_EQUAL_HEX8_ARRAY(in, dec, in_len);
}

}  // namespace

// Reference vectors from the COBS paper / Wikipedia examples.
static void test_reference_vectors() {
    { const uint8_t in[] = {0x00}; const uint8_t ex[] = {0x01, 0x01}; check_encode(in, 1, ex, 2); }
    { const uint8_t in[] = {0x00, 0x00}; const uint8_t ex[] = {0x01, 0x01, 0x01}; check_encode(in, 2, ex, 3); }
    { const uint8_t in[] = {0x00, 0x11, 0x00}; const uint8_t ex[] = {0x01, 0x02, 0x11, 0x01}; check_encode(in, 3, ex, 4); }
    { const uint8_t in[] = {0x11, 0x22, 0x00, 0x33}; const uint8_t ex[] = {0x03, 0x11, 0x22, 0x02, 0x33}; check_encode(in, 4, ex, 5); }
    { const uint8_t in[] = {0x11, 0x22, 0x33, 0x44}; const uint8_t ex[] = {0x05, 0x11, 0x22, 0x33, 0x44}; check_encode(in, 4, ex, 5); }
    { const uint8_t in[] = {0x11, 0x00, 0x00, 0x00}; const uint8_t ex[] = {0x02, 0x11, 0x01, 0x01, 0x01}; check_encode(in, 4, ex, 5); }
}

static void test_empty_input() {
    uint8_t out[4];
    size_t n = 0;
    TEST_ASSERT_EQUAL(cobs::Result::OK, cobs::encode(nullptr, 0, out, sizeof(out), &n));
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL_HEX8(0x01, out[0]);
    uint8_t dec[4];
    size_t d = 99;
    TEST_ASSERT_EQUAL(cobs::Result::OK, cobs::decode(out, n, dec, sizeof(dec), &d));
    TEST_ASSERT_EQUAL_size_t(0, d);
}

static void test_254_and_255_nonzero_runs() {
    uint8_t in[600];
    for (size_t i = 0; i < sizeof(in); ++i) in[i] = static_cast<uint8_t>(1 + (i % 255));
    const size_t lengths[] = {253, 254, 255, 256, 508, 509, 510};
    for (size_t len : lengths) round_trip(in, len);

    // 254 non-zero bytes: FF block plus a trailing 01 code byte.
    uint8_t enc[300];
    size_t n = 0;
    TEST_ASSERT_EQUAL(cobs::Result::OK, cobs::encode(in, 254, enc, sizeof(enc), &n));
    TEST_ASSERT_EQUAL_size_t(256, n);
    TEST_ASSERT_EQUAL_HEX8(0xFF, enc[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, enc[255]);
}

static void test_round_trip_patterns() {
    uint8_t in[600];
    // all zeros
    memset(in, 0, sizeof(in));
    for (size_t len = 0; len <= 300; ++len) round_trip(in, len);
    // alternating zero / non-zero
    for (size_t i = 0; i < sizeof(in); ++i) in[i] = (i % 2) ? 0x00 : 0xA5;
    for (size_t len = 0; len <= 300; ++len) round_trip(in, len);
    // pseudo-random with ~10% zeros
    uint32_t x = 12345;
    for (size_t i = 0; i < sizeof(in); ++i) {
        x = x * 1103515245u + 12345u;
        const uint8_t b = static_cast<uint8_t>(x >> 16);
        in[i] = (b < 26) ? 0 : b;
    }
    for (size_t len = 0; len <= 600; len += 7) round_trip(in, len);
}

static void test_encode_no_space() {
    const uint8_t in[] = {1, 2, 3, 0, 4, 5};
    uint8_t out[16];
    size_t n = 0;
    const size_t needed = 7;
    for (size_t cap = 0; cap < needed; ++cap) {
        TEST_ASSERT_EQUAL(cobs::Result::NO_SPACE, cobs::encode(in, sizeof(in), out, cap, &n));
    }
    TEST_ASSERT_EQUAL(cobs::Result::OK, cobs::encode(in, sizeof(in), out, needed, &n));
    TEST_ASSERT_EQUAL_size_t(needed, n);
}

static void test_decode_errors() {
    uint8_t dec[16];
    size_t n = 0;
    { const uint8_t bad[] = {0x03, 0x11, 0x00}; TEST_ASSERT_EQUAL(cobs::Result::ZERO_IN_INPUT, cobs::decode(bad, 3, dec, sizeof(dec), &n)); }
    { const uint8_t bad[] = {0x00}; TEST_ASSERT_EQUAL(cobs::Result::ZERO_IN_INPUT, cobs::decode(bad, 1, dec, sizeof(dec), &n)); }
    { const uint8_t bad[] = {0x05, 0x11, 0x22}; TEST_ASSERT_EQUAL(cobs::Result::TRUNCATED, cobs::decode(bad, 3, dec, sizeof(dec), &n)); }
    { const uint8_t ok[] = {0x05, 0x11, 0x22, 0x33, 0x44}; TEST_ASSERT_EQUAL(cobs::Result::NO_SPACE, cobs::decode(ok, 5, dec, 3, &n)); }
    TEST_ASSERT_EQUAL(cobs::Result::BAD_ARG, cobs::decode(dec, 0, dec, sizeof(dec), &n));
    TEST_ASSERT_EQUAL(cobs::Result::BAD_ARG, cobs::encode(nullptr, 3, dec, sizeof(dec), &n));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_reference_vectors);
    RUN_TEST(test_empty_input);
    RUN_TEST(test_254_and_255_nonzero_runs);
    RUN_TEST(test_round_trip_patterns);
    RUN_TEST(test_encode_no_space);
    RUN_TEST(test_decode_errors);
    return UNITY_END();
}
