#include <string.h>
#include <unity.h>

#include "crc16.h"

void setUp() {}
void tearDown() {}

static void test_check_value() {
    const char* s = "123456789";
    TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16::compute(reinterpret_cast<const uint8_t*>(s), strlen(s)));
}

static void test_empty_is_init() {
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, crc16::compute(nullptr, 0));
    const uint8_t b = 0;
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, crc16::compute(&b, 0));
}

static void test_known_vectors() {
    const uint8_t zero = 0x00;
    TEST_ASSERT_EQUAL_HEX16(0xE1F0, crc16::compute(&zero, 1));
    const uint8_t a = 'A';
    TEST_ASSERT_EQUAL_HEX16(0xB915, crc16::compute(&a, 1));
}

static void test_incremental_matches_single_pass() {
    const uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x00, 0xFF};
    const uint16_t whole = crc16::compute(data, sizeof(data));
    for (size_t split = 0; split <= sizeof(data); ++split) {
        uint16_t c = crc16::update(crc16::INIT, data, split);
        c = crc16::update(c, data + split, sizeof(data) - split);
        TEST_ASSERT_EQUAL_HEX16(whole, c);
    }
}

static void test_single_bit_error_detected() {
    uint8_t data[32];
    for (size_t i = 0; i < sizeof(data); ++i) data[i] = static_cast<uint8_t>(i * 37 + 11);
    const uint16_t good = crc16::compute(data, sizeof(data));
    for (size_t byte = 0; byte < sizeof(data); ++byte) {
        for (uint8_t bit = 0; bit < 8; ++bit) {
            data[byte] ^= static_cast<uint8_t>(1u << bit);
            TEST_ASSERT_NOT_EQUAL(good, crc16::compute(data, sizeof(data)));
            data[byte] ^= static_cast<uint8_t>(1u << bit);
        }
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_check_value);
    RUN_TEST(test_empty_is_init);
    RUN_TEST(test_known_vectors);
    RUN_TEST(test_incremental_matches_single_pass);
    RUN_TEST(test_single_bit_error_detected);
    return UNITY_END();
}
