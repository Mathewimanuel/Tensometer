// Break-detection classifier (skill section 7). Pure logic, no Arduino dependency.
// Consumes force samples (raw path, only while extending) plus an optional stream of
// conductor OPEN events, and classifies drops into SUBSTRATE_BREAK, SLIP or neither.
// Identical with or without conductor events: the conductor-coincidence checks simply
// never match when no events are pushed.
//
// Units: force in mN (int32); position in the caller's step-count unit (int32, matches
// core position truth); timestamps in us from the shared monotonic 64-bit counter
// (skill 3.5). Converting position to a physical unit (mm/um) is the caller's job: this
// lib deliberately knows nothing about screw geometry (same decoupling as motion_profile).
// Fractions are permille (parts per 1000), matching safety_limits.h's existing convention, to
// avoid float entirely.
#pragma once

#include <stdint.h>

namespace break_detect {

// NOT wire-compatible with proto::EventType (include/protocol.h) despite the shared
// names -- the two enums are maintained independently (this one has no reason to carry
// phase-2 continuity/routing event types, which would otherwise force this lib to
// depend on protocol.h) and their ordinals already diverge past MAX_FORCE. Whoever wires
// this classifier into comms/SD (issues #12/#13 follow-up) must write an explicit,
// tested mapping function -- never static_cast<proto::EventType>(static_cast<uint8_t>(e)).
enum class EventType : uint8_t {
    NONE = 0,
    SUBSTRATE_BREAK,
    SLIP,
    MAX_FORCE,
    CONDUCTOR_OPEN,
    UNVERIFIED_CLASSIFICATION,
};

struct Event {
    EventType type = EventType::NONE;
    uint64_t timestamp_us = 0;
    int32_t position = 0;
    int32_t force_mN = 0;
};

// Defaults match the skill 7 table.
struct Params {
    int32_t drop_frac_permille = 300;       // sudden-drop threshold, relative to the local
                                             // (drop_window-sample) peak, not the segment peak
    uint32_t drop_window = 3;               // samples a "sudden" drop must happen within
    uint32_t coinc_window_pre = 1;          // samples before the drop sample. Must stay >= 1:
                                             // push_conductor_open() tags events one sample
                                             // early (see its own comment), so 0 would make a
                                             // genuinely-simultaneous conductor event miss
    uint32_t coinc_window_post = 3;         // samples after the drop sample
    int32_t both_frac_permille = 600;       // drop this large + coincident OPEN = both events
    int32_t residual_frac_permille = 70;    // held-low threshold, relative to the segment peak
    uint32_t residual_hold = 8;             // consecutive samples held below residual_frac
    int32_t slip_frac_min_permille = 30;
    int32_t slip_frac_max_permille = 300;
    int32_t slip_recover_position = 2000;   // default ~1 mm in the caller's position unit
                                             // (e.g. 2000 steps at 2000 steps/mm)
    int32_t min_peak_mN = 500;              // below this segment peak, skip drop/residual/slip
                                             // checks entirely (noise floor before real tension)
};

struct ForceSample {
    uint64_t timestamp_us;
    int32_t position;
    int32_t force_mN;
};

// Up to 3 events can fire on one sample: CONDUCTOR_OPEN + SUBSTRATE_BREAK (both_frac
// case) plus UNVERIFIED_CLASSIFICATION (conductors_present but no event source).
constexpr uint8_t MAX_EVENTS_PER_SAMPLE = 3;

class Classifier {
public:
    // Resets everything: test max, energy accumulator, and starts the first segment.
    // `conductors_present`: core test-config flag (skill section 7). `conductor_source_
    // available`: whether continuity/sim is actually enabled and feeding events this
    // test. If present-but-unavailable, every SUBSTRATE_BREAK also carries
    // UNVERIFIED_CLASSIFICATION, since a thick-wire break would look identical to a
    // substrate break with nothing to tell them apart.
    void start_test(bool conductors_present, bool conductor_source_available);

    // Resets the segment peak and per-segment state. Call at START, every RESUME, every
    // cycle reload, and after a conductor-only classified drop (the classifier itself
    // does the latter automatically -- see process_sample).
    void start_segment();

    // Records a conductor OPEN event observed around the current sample. Call this from
    // wherever conductor events are drained (continuity or sim module), interleaved with
    // process_sample() calls in the normal main-loop order (skill 3.3).
    void push_conductor_open(uint64_t timestamp_us);

    // Processes one force sample. Only meaningful while extending (skill 7); samples fed
    // with extending == false are ignored entirely (no state changes, no events). Writes
    // up to `out_cap` events into `out` and returns how many were written. Some drop
    // classifications are deferred by up to Params::coinc_window_post samples while
    // waiting to see whether a conductor event arrives, so a call can emit 0 events now
    // and the resulting event(s) on a later call, still carrying the original drop
    // sample's timestamp/position/force.
    uint8_t process_sample(const ForceSample& sample, bool extending, const Params& params,
                            Event* out, uint8_t out_cap);

    int32_t test_max_mN() const { return test_max_mN_; }
    int64_t energy_raw() const { return energy_raw_; }  // mN * position-unit, trapezoid sum
    bool broken() const { return broken_; }

private:
    static constexpr uint8_t RING_LEN = 16;
    static constexpr uint8_t COND_RING_LEN = 8;

    struct RingEntry {
        bool valid = false;
        uint32_t sample_index = 0;
        int32_t position = 0;
        int32_t force_mN = 0;
        uint64_t timestamp_us = 0;
    };
    struct CondEntry {
        bool valid = false;
        uint32_t sample_index = 0;
    };
    struct PendingDrop {
        bool active = false;
        uint32_t drop_sample_index = 0;
        uint64_t timestamp_us = 0;
        int32_t position = 0;
        int32_t force_mN = 0;      // force at the drop sample
        int32_t local_peak_mN = 0; // pre-drop local peak (failure-point force)
    };

    int32_t local_peak_within(uint32_t current_idx, uint32_t window) const;
    bool conductor_open_in_window(uint32_t current_idx, uint32_t center_index, uint32_t pre,
                                   uint32_t post) const;
    uint8_t resolve_pending_drop(const Params& params, uint32_t current_idx, Event* out,
                                  uint8_t out_cap);
    uint8_t emit_substrate_break(uint64_t timestamp_us, int32_t position, int32_t failure_force_mN,
                                  bool with_conductor_open, Event* out, uint8_t out_cap);
    uint8_t update_slip(const ForceSample& sample, uint32_t current_idx, const Params& params,
                         Event* out, uint8_t out_cap);
    void reset_segment_state();

    bool conductors_present_ = false;
    bool conductor_source_available_ = false;
    bool unverified_ = false;

    int32_t test_max_mN_ = INT32_MIN;
    int64_t energy_raw_ = 0;
    bool have_prev_for_energy_ = false;
    int32_t prev_position_ = 0;
    int32_t prev_force_mN_ = 0;

    int32_t segment_peak_mN_ = INT32_MIN;
    bool broken_ = false;

    uint32_t sample_index_ = 0;
    uint32_t last_sample_index_ = 0;  // for tagging push_conductor_open() arrivals
    RingEntry ring_[RING_LEN] = {};
    CondEntry cond_ring_[COND_RING_LEN] = {};
    uint8_t cond_ring_pos_ = 0;

    PendingDrop pending_;
    uint32_t residual_run_ = 0;

    // Slip candidate: a dip into the slip band that might recover.
    bool slip_candidate_active_ = false;
    int32_t slip_pre_peak_mN_ = 0;
    int32_t slip_dip_min_mN_ = 0;
    int32_t slip_dip_position_ = 0;
};

}  // namespace break_detect
