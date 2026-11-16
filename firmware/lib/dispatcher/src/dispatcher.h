// Command dispatcher (skill sections 5 and 8; protocol.h CMD_* NACK contracts; closes the
// "documented but enforced nowhere" gap noted in lib/messages/src/messages.h). Pure logic,
// no Arduino dependency: given one decoded command frame plus a fresh snapshot of the
// facts it needs, enforces that command's NACK contract, applies the matching
// core_sm::Event to the caller's state machine, and says what to reply.
//
// It never starts motion itself. Same "callers react to state changes" pattern as
// lib/state_machine: after an accepted HOME the state is HOMING, after ARM arming() is
// true, and so on -- the main loop's motion stage reacts to that. The parameters a motion
// executor will need (jog direction, preload body, the ARM config frozen at ARM, skill 3.1)
// are kept here and exposed read-only.
//
// FORCE FAILS CLOSED (Mathew, 2026-11-16, docs/decisions.md): until the HX711 driver
// exists (#10), Context::force_valid is false and every command whose guard or contract
// depends on force NACKs Err::BAD_STATE: HOME (|force| < 2 N guard), JOG_START (loaded-jog
// speed cap is force-dependent), TARE, PRELOAD, ARM (force_limit_mN, break detection),
// START and RESUME (cutoff and break detection while moving). Force is never assumed 0.
//
// Decided by Mathew 2026-11-16 (docs/decisions.md):
//   1. Any CRC-valid CMD_STOP stops (SAFETY_TRIP) before its body is even looked at; a bad
//      body then NACKs BAD_LENGTH, but the machine is already in FAULT.
//   2. Jog deadman. 0.7: only JOG action KEEPALIVE refreshes it (in JOG, same direction,
//      force valid; BAD_STATE otherwise, never starts motion); a START while in JOG is
//      BAD_STATE. Expiry -> JOG_STOP + EventType::JOG_DEADMAN via tick().
//   3. Relieving jog: JOG start from FAULT only if LIMIT_STOP is the only cause this
//      FAULT has ever had (round 6: any other cause, any CMD_STOP into FAULT, and any
//      fault or STOP during a relieving jog block relief until RESET reaches IDLE), the
//      self-test latch is clean, exactly one switch is tripped, the direction is away from
//      it, the episode is not already past its travel cap, and core_safety::evaluate()
//      with only that switch suppressed is NONE (homed not required). The switch is ignored
//      only until it has read clear continuously for limits::RELIEF_CLEAR_DEBOUNCE_US, with
//      a cumulative travel cap per FAULT episode (apply_relief()); JOG stop / the deadman
//      return to FAULT.
//   4. RESET re-runs a failed boot self-test (Context::self_test) and only succeeds if it
//      now passes.
//
// Check order per command, so the PC gets the most specific reason first (JOG is the one
// exception: its action/direction fields are range-checked first, reviewer #6/C):
//   wire length (BAD_LENGTH) -> state (BAD_STATE) -> homed/valid (NOT_HOMED) ->
//   inhibit mask (MOTION_INHIBITED, motion-starting events only, decisions.md 2026-10-26
//   item 4) -> body fields (OUT_OF_RANGE etc.) -> force_valid (BAD_STATE) -> apply().
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "core_safety.h"
#include "protocol.h"
#include "safety_limits.h"
#include "self_test.h"
#include "state_machine.h"

namespace dispatch {

// Fresh every call; the dispatcher owns none of these facts.
struct Context {
    bool homed = false;
    bool position_valid = false;
    bool force_valid = false;   // false until #10 lands -- never default this to true
    int32_t force_mN = 0;       // read only when force_valid
    uint64_t now_us = 0;        // jog deadman timestamps
    int32_t position_steps = 0; // relieving-jog travel cap baseline
    // This tick's core-safety snapshot (switches, inhibit mask = OR of every module's
    // inhibit_bits(), ...). RESET's "cause cleared" and the relieving-jog permission are
    // both re-evaluated from it here, with their own overrides, rather than passed in
    // pre-digested (reviewer B).
    core_safety::Inputs safety;
    // Boot self-test latch; RESET re-runs failed module init()s through it (decision 4,
    // 2026-11-16). nullptr = no latch (tests that don't exercise it).
    self_test::Latch* self_test = nullptr;
};

struct Reply {
    bool ack = false;
    proto::Err err = proto::Err::NONE;            // meaningful only when !ack
    proto::EventType event = proto::EventType::NONE;  // EVENT to emit after the reply
};

class Dispatcher {
public:
    // `msg_id`/`body` straight from frame::decode(). HEARTBEAT is the comms layer's, not a
    // command; anything that is not a CMD_* id NACKs UNKNOWN_MSG.
    Reply handle(uint8_t msg_id, const uint8_t* body, size_t body_len, const Context& ctx,
                 core_sm::StateMachine& sm);

    limits::ProfileId profile() const { return profile_; }
    const limits::CellProfile& profile_limits() const {
        return limits::PROFILES[static_cast<uint8_t>(profile_)];
    }
    bool coupler_confirmed() const { return coupler_confirmed_; }
    proto::JogDir jog_dir() const { return jog_dir_; }
    const proto::PreloadBody& preload() const { return preload_; }
    // Frozen at the last accepted ARM (skill 3.1). Meaningful from arming() onward.
    const proto::ArmBody& armed_config() const { return arm_; }

    // Jog deadman (decision 2, 2026-11-16): true once more than
    // limits::JOG_KEEPALIVE_TIMEOUT_US has passed since the last accepted JOG start or
    // keep-alive. The main loop applies JOG_STOP when this is true while in JOG.
    bool jog_deadman_expired(uint64_t now_us) const {
        return now_us - last_jog_us_ > limits::JOG_KEEPALIVE_TIMEOUT_US;
    }

    // ---- FAULT cause: owned here (skill 3.1), reported in the heartbeat ----------------
    // NONE outside FAULT/a relieving jog. Set by trip()/safety_tick(), by CMD_STOP only if
    // no cause is recorded yet, cleared when RESET reaches IDLE.
    proto::EventType fault_cause() const { return cause_; }

    // Main-loop stage 1, once per pass, with this pass's core_safety::evaluate() verdict
    // (decision 2, Mathew 2026-11-16). Not in FAULT: trip(). Already in FAULT: a DIFFERENT
    // cause overwrites fault_cause and is reported; the same cause again reports nothing
    // (no event spam while a condition persists). A non-LIMIT_STOP cause arising in FAULT
    // blocks relief until RESET; a later LIMIT_STOP overwrite never unblocks it. Returns
    // the EventType to emit, NONE if none.
    proto::EventType safety_tick(core_sm::StateMachine& sm, core_safety::FaultReason reason);

    // A new fault (from safety_tick(), the boot self-test, the main loop's no-executor
    // guard): SAFETY_TRIP and cause recorded. If it arises during a relieving jog, relief is
    // blocked until RESET (round 5: ANY fault ends relief). Returns `cause` for the EVENT.
    proto::EventType trip(core_sm::StateMachine& sm, proto::EventType cause);

    bool relief_jog_active() const { return relief_.active(); }

    // Main-loop stage 5, once per pass: applies JOG_STOP if the jog deadman has expired and
    // returns EventType::JOG_DEADMAN to emit (decision 3, 0.7); NONE otherwise.
    proto::EventType tick(uint64_t now_us, core_sm::StateMachine& sm);

    // Main-loop stage 1, before core_safety::evaluate(): while a relieving jog out of FAULT
    // is running, sets in.relieving_switch (only until the switch has read clear
    // continuously for limits::RELIEF_CLEAR_DEBOUNCE_US, timed with `now_us`, 2a) and
    // in.relief_overtravel (2b, cumulative over the FAULT episode). Outside one, leaves both
    // unset and ends the press (the episode itself is kept until RESET reaches IDLE).
    void apply_relief(core_safety::Inputs& in, const core_sm::StateMachine& sm,
                      int32_t position_steps, uint64_t now_us);

private:
    bool arm_fields_valid(const proto::ArmBody& b) const;

    limits::ProfileId profile_ = limits::ProfileId::CELL_100KG;  // skill 2.2 default
    bool coupler_confirmed_ = false;  // never assumed: only an accepted SELECT_PROFILE sets it
    proto::JogDir jog_dir_ = proto::JogDir::TOWARD_LIM_FAR;
    proto::PreloadBody preload_{};
    proto::ArmBody arm_{};
    uint64_t last_jog_us_ = 0;
    core_safety::ReliefMonitor relief_;
    proto::EventType cause_ = proto::EventType::NONE;
    // Relief refused until RESET reaches IDLE (rounds 4-6): any fault (or STOP) during a
    // relieving jog, or any cause other than LIMIT_STOP at any point in this FAULT
    // (including a CMD_STOP that entered it).
    bool relief_blocked_ = false;
};

// The limit switch a jog in `dir` moves away from (relieving jog, decision 3): the switch
// whose trip rule core_safety suppresses while that jog runs out of FAULT.
core_safety::Switch relieved_switch(proto::JogDir dir);

// core_safety's cause -> wire EventType. Only ESTOP, LIMIT_STOP and HEARTBEAT_LOST have a
// dedicated EventType; every other reason reports EventType::FAULT (open question for
// Mathew, docs/decisions.md 2026-11-16). NONE -> NONE.
proto::EventType fault_event(core_safety::FaultReason reason);

}  // namespace dispatch
