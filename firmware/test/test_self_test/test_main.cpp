#include <unity.h>

#include "self_test.h"

void setUp() {}
void tearDown() {}

namespace {
mod::Status a_status = mod::Status::OK;
mod::Status b_status = mod::Status::OK;
int a_calls = 0;
int b_calls = 0;
mod::Status init_a() { ++a_calls; return a_status; }
mod::Status init_b() { ++b_calls; return b_status; }

mod::Module module_a{"a", init_a, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
mod::Module module_b{"b", init_b, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
const mod::Module* const TABLE[] = {&module_a, nullptr, &module_b};

void reset_fakes() {
    a_status = b_status = mod::Status::OK;
    a_calls = b_calls = 0;
}
}  // namespace

static void test_all_pass_and_absent_is_not_a_failure() {
    reset_fakes();
    b_status = mod::Status::ABSENT;  // skill 1.4: absence is never a fault
    self_test::Latch latch(TABLE, 3);
    TEST_ASSERT_TRUE(latch.run_all());
    TEST_ASSERT_TRUE(latch.ok());
}

static void test_failure_latches_and_retry_reinits_only_failed_modules() {
    reset_fakes();
    a_status = mod::Status::ERROR;
    self_test::Latch latch(TABLE, 3);
    TEST_ASSERT_FALSE(latch.run_all());
    TEST_ASSERT_EQUAL_INT(1, b_calls);

    TEST_ASSERT_FALSE(latch.retry());  // still failing: stays latched
    TEST_ASSERT_FALSE(latch.ok());
    TEST_ASSERT_EQUAL_INT(2, a_calls);
    TEST_ASSERT_EQUAL_INT(1, b_calls);  // the healthy module is not re-initialised

    a_status = mod::Status::OK;
    TEST_ASSERT_TRUE(latch.retry());
    TEST_ASSERT_TRUE(latch.ok());
    TEST_ASSERT_EQUAL_INT(3, a_calls);
    TEST_ASSERT_TRUE(latch.retry());  // nothing left to retry
    TEST_ASSERT_EQUAL_INT(3, a_calls);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_all_pass_and_absent_is_not_a_failure);
    RUN_TEST(test_failure_latches_and_retry_reinits_only_failed_modules);
    return UNITY_END();
}
