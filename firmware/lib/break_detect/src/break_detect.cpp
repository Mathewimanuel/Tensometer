#include "break_detect.h"

#include <algorithm>
#include <iterator>

namespace break_detect {

void Classifier::start_test(bool conductors_present, bool conductor_source_available) {
    conductors_present_ = conductors_present;
    conductor_source_available_ = conductor_source_available;
    unverified_ = conductors_present_ && !conductor_source_available_;

    test_max_mN_ = INT32_MIN;
    energy_raw_ = 0;
    have_prev_for_energy_ = false;
    broken_ = false;

    sample_index_ = 0;
    last_sample_index_ = 0;
    cond_ring_pos_ = 0;

    reset_segment_state();  // also clears ring_, cond_ring_ and pending_
}

void Classifier::reset_segment_state() {
    segment_peak_mN_ = INT32_MIN;
    residual_run_ = 0;
    slip_candidate_active_ = false;
    // Clear the local-peak lookback so a stale pre-reset sample (e.g. the force level
    // just before a HOLD) can never be read as "the recent peak" for the first sample(s)
    // of the new segment -- sample_index_ does not advance while extending == false, so
    // without this a resume could otherwise compare against pre-hold ring entries that
    // are still "in window" by index alone.
    std::fill(std::begin(ring_), std::end(ring_), RingEntry{});
    std::fill(std::begin(cond_ring_), std::end(cond_ring_), CondEntry{});
    pending_ = PendingDrop{};
}

void Classifier::start_segment() { reset_segment_state(); }

void Classifier::push_conductor_open(uint64_t /*timestamp_us*/) {
    // Ignored when the source is unavailable: with no genuine conductor events possible,
    // every SUBSTRATE_BREAK already carries UNVERIFIED_CLASSIFICATION (see start_test),
    // and MAX_EVENTS_PER_SAMPLE's budget assumes CONDUCTOR_OPEN and
    // UNVERIFIED_CLASSIFICATION never both apply to the same break -- a stray call here
    // (e.g. a module still draining a stale queue after being told it's unavailable)
    // must not silently break that budget.
    if (!conductor_source_available_) return;

    // Tagged with last_sample_index_ (the most recently COMPLETED sample), not the
    // sample about to be processed next: this call lands between two process_sample()
    // calls, and the main loop drains module events (3) before break detection (4) in
    // the same pass (skill 3.3), so an event observed "at" the drop sample is actually
    // tagged one index early. Params::coinc_window_pre's default of 1 absorbs exactly
    // this one-sample lag; a caller that sets coinc_window_pre = 0 would make a
    // genuinely-simultaneous conductor event fail to match.

    cond_ring_[cond_ring_pos_] = CondEntry{true, last_sample_index_};
    cond_ring_pos_ = static_cast<uint8_t>((cond_ring_pos_ + 1) % COND_RING_LEN);
}

int32_t Classifier::local_peak_within(uint32_t current_idx, uint32_t window) const {
    int32_t peak = INT32_MIN;
    for (const auto& e : ring_) {
        if (!e.valid) continue;
        if (e.sample_index + window < current_idx) continue;  // too old
        if (e.sample_index > current_idx) continue;           // not yet reached (defensive)
        if (e.force_mN > peak) peak = e.force_mN;
    }
    return peak;
}

bool Classifier::conductor_open_in_window(uint32_t current_idx, uint32_t center_index,
                                           uint32_t pre, uint32_t post) const {
    const uint32_t lo = (center_index > pre) ? (center_index - pre) : 0;
    const uint32_t hi = center_index + post;
    return std::any_of(std::begin(cond_ring_), std::end(cond_ring_), [&](const CondEntry& e) {
        return e.valid && e.sample_index >= lo && e.sample_index <= hi &&
               e.sample_index <= current_idx;  // <= current_idx: not observable yet otherwise
    });
}

uint8_t Classifier::emit_substrate_break(uint64_t timestamp_us, int32_t position,
                                          int32_t failure_force_mN, bool with_conductor_open,
                                          Event* out, uint8_t out_cap) {
    uint8_t n = 0;
    if (with_conductor_open && n < out_cap) {
        out[n++] = Event{EventType::CONDUCTOR_OPEN, timestamp_us, position, failure_force_mN};
    }
    if (n < out_cap) {
        out[n++] = Event{EventType::SUBSTRATE_BREAK, timestamp_us, position, failure_force_mN};
    }
    if (unverified_ && n < out_cap) {
        out[n++] = Event{EventType::UNVERIFIED_CLASSIFICATION, timestamp_us, position,
                          failure_force_mN};
    }
    broken_ = true;
    return n;
}

uint8_t Classifier::resolve_pending_drop(const Params& params, uint32_t current_idx, Event* out,
                                          uint8_t out_cap) {
    const bool matched = conductor_open_in_window(current_idx, pending_.drop_sample_index,
                                                    params.coinc_window_pre,
                                                    params.coinc_window_post);
    const bool timed_out =
        current_idx > pending_.drop_sample_index + params.coinc_window_post;

    if (!matched && !timed_out) {
        return 0;  // still waiting
    }

    uint8_t n = 0;
    if (matched) {
        const int64_t drop_permille =
            (pending_.local_peak_mN > 0)
                ? static_cast<int64_t>(pending_.local_peak_mN - pending_.force_mN) * 1000 /
                      pending_.local_peak_mN
                : 0;
        if (drop_permille < params.both_frac_permille) {
            // Conductor-only: log CONDUCTOR_OPEN, reset the segment, no stop. A single
            // physical drop can only ever resolve once (pending_ gates new detection
            // while active), so no separate per-segment dedup is needed here.
            if (n < out_cap) {
                out[n++] = Event{EventType::CONDUCTOR_OPEN, pending_.timestamp_us,
                                  pending_.position, pending_.local_peak_mN};
            }
            reset_segment_state();
        } else {
            n = emit_substrate_break(pending_.timestamp_us, pending_.position,
                                      pending_.local_peak_mN, /*with_conductor_open=*/true,
                                      out, out_cap);
        }
    } else {
        // Timed out with no coincident conductor event: plain substrate break.
        n = emit_substrate_break(pending_.timestamp_us, pending_.position, pending_.local_peak_mN,
                                  /*with_conductor_open=*/false, out, out_cap);
    }

    pending_ = PendingDrop{};
    return n;
}

uint8_t Classifier::update_slip(const ForceSample& sample, uint32_t current_idx,
                                 const Params& params, Event* out, uint8_t out_cap) {
    if (!slip_candidate_active_) {
        const int32_t local_peak = local_peak_within(current_idx, params.drop_window);
        if (local_peak <= 0) return 0;
        const int64_t drop_permille =
            static_cast<int64_t>(local_peak - sample.force_mN) * 1000 / local_peak;
        if (drop_permille >= params.slip_frac_min_permille &&
            drop_permille < params.slip_frac_max_permille) {
            slip_candidate_active_ = true;
            slip_pre_peak_mN_ = local_peak;
            slip_dip_min_mN_ = sample.force_mN;
            slip_dip_position_ = sample.position;
        }
        return 0;
    }

    if (sample.force_mN < slip_dip_min_mN_) {
        slip_dip_min_mN_ = sample.force_mN;
        slip_dip_position_ = sample.position;
    }

    if (sample.force_mN >= slip_pre_peak_mN_) {
        const int32_t traveled = sample.position - slip_dip_position_;
        slip_candidate_active_ = false;
        if (traveled <= params.slip_recover_position) {
            if (out_cap > 0) {
                out[0] = Event{EventType::SLIP, sample.timestamp_us, sample.position,
                                sample.force_mN};
                return 1;
            }
        }
        return 0;
    }

    if (sample.position - slip_dip_position_ > params.slip_recover_position) {
        // Never recovered within the allowed travel: give up the candidate, not a slip.
        slip_candidate_active_ = false;
    }
    return 0;
}

uint8_t Classifier::process_sample(const ForceSample& sample, bool extending,
                                    const Params& params, Event* out, uint8_t out_cap) {
    if (!extending || broken_) return 0;

    const uint32_t idx = sample_index_;
    uint8_t n = 0;

    // Energy: trapezoid sum of force over position, continuous across the whole test.
    if (have_prev_for_energy_) {
        const int64_t avg2 = static_cast<int64_t>(sample.force_mN) + prev_force_mN_;
        const int64_t dpos = static_cast<int64_t>(sample.position) - prev_position_;
        energy_raw_ += avg2 * dpos / 2;
    }
    prev_position_ = sample.position;
    prev_force_mN_ = sample.force_mN;
    have_prev_for_energy_ = true;

    // Test maximum (persists across segments within a test).
    if (sample.force_mN > test_max_mN_) {
        test_max_mN_ = sample.force_mN;
        if (n < out_cap) {
            out[n++] = Event{EventType::MAX_FORCE, sample.timestamp_us, sample.position,
                              sample.force_mN};
        }
    }

    // Segment peak (resets at start_segment()/conductor-only drops).
    if (sample.force_mN > segment_peak_mN_) segment_peak_mN_ = sample.force_mN;

    ring_[idx % RING_LEN] =
        RingEntry{true, idx, sample.position, sample.force_mN, sample.timestamp_us};

    if (pending_.active) {
        // This sample belongs to an earlier drop's coincidence window, whether it
        // resolves right now or is still waiting: either way, it has already been
        // spent on that classification and must not also be evaluated fresh below
        // (broken_ may have just gone true inside resolve_pending_drop, too).
        n += resolve_pending_drop(params, idx, out + n, out_cap - n);
        last_sample_index_ = idx;
        sample_index_ = idx + 1;
        return n;
    }

    last_sample_index_ = idx;
    sample_index_ = idx + 1;

    if (segment_peak_mN_ < params.min_peak_mN) {
        return n;  // below the noise-floor gate: no real tension yet
    }

    // Sudden drop, relative to the local (drop_window-sample) peak.
    const int32_t local_peak = local_peak_within(idx, params.drop_window);
    if (local_peak > 0) {
        const int64_t drop_permille =
            static_cast<int64_t>(local_peak - sample.force_mN) * 1000 / local_peak;
        if (drop_permille >= params.drop_frac_permille) {
            pending_.active = true;
            pending_.drop_sample_index = idx;
            pending_.timestamp_us = sample.timestamp_us;
            pending_.position = sample.position;
            pending_.force_mN = sample.force_mN;
            pending_.local_peak_mN = local_peak;
            n += resolve_pending_drop(params, idx, out + n, out_cap - n);
            return n;
        }
    }

    // Held-low (gradual tear / slow failure), relative to the segment peak.
    const int64_t resid_permille =
        static_cast<int64_t>(sample.force_mN) * 1000 / segment_peak_mN_;
    if (resid_permille <= params.residual_frac_permille) {
        ++residual_run_;
        if (residual_run_ >= params.residual_hold) {
            n += emit_substrate_break(sample.timestamp_us, sample.position, segment_peak_mN_,
                                       /*with_conductor_open=*/false, out + n, out_cap - n);
            residual_run_ = 0;
            return n;
        }
    } else {
        residual_run_ = 0;
    }

    n += update_slip(sample, idx, params, out + n, out_cap - n);
    return n;
}

}  // namespace break_detect
