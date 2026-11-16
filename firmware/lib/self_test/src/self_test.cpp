#include "self_test.h"

namespace self_test {

bool Latch::init_ok(uint8_t i) const {
    const mod::Module* m = table_[i];
    if (m == nullptr || m->init == nullptr) return true;
    return m->init() != mod::Status::ERROR;
}

bool Latch::run_all() {
    if (count_ > 32) {  // cannot track them: fail closed rather than skip any
        failed_ = UINT32_MAX;
        return false;
    }
    failed_ = 0;
    for (uint8_t i = 0; i < count_; ++i) {
        if (!init_ok(i)) failed_ |= (1u << i);
    }
    return ok();
}

bool Latch::retry() {
    if (count_ > 32) return false;
    for (uint8_t i = 0; i < count_; ++i) {
        if ((failed_ & (1u << i)) != 0 && init_ok(i)) failed_ &= ~(1u << i);
    }
    return ok();
}

}  // namespace self_test
