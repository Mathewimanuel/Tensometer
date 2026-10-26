#include "state_machine.h"

namespace core_sm {

using mod::CoreState;
using proto::LockOwner;

namespace {
bool homed_and_valid(const Guards& g) { return g.homed && g.position_valid; }
}  // namespace

bool StateMachine::apply(Event event, const Guards& guards) {
    // Wins from any state, including FAULT itself (idempotent).
    if (event == Event::SAFETY_TRIP) {
        state_ = CoreState::FAULT;
        lock_owner_ = LockOwner::NONE;
        arming_ = false;
        return true;
    }

    switch (state_) {
        case CoreState::BOOT:
            if (event == Event::SELF_TEST_PASS) {
                state_ = CoreState::IDLE;
                return true;
            }
            return false;

        case CoreState::IDLE:
        case CoreState::READY: {
            // While arming (ARM accepted, move-to-L0 in flight), nothing else may run:
            // otherwise a later stray L0_REACHED from the now-irrelevant move would still
            // be accepted and jump to READY from wherever JOG/TARE/etc. left the carriage,
            // silently breaking the "READY implies at L0" contract. Only L0_REACHED (or
            // SAFETY_TRIP, handled above) may act while arming_ is true.
            if (state_ == CoreState::IDLE && arming_ && event != Event::L0_REACHED) {
                return false;
            }

            // JOG/TARE/PRELOAD share one precondition and one "return here" pattern
            // from both IDLE and READY (skill section 5: PRELOAD "same precondition as
            // JOG/TARE").
            CoreState target = CoreState::IDLE;  // overwritten below; satisfies -Wall
            bool is_substate_entry = true;
            switch (event) {
                case Event::JOG_START: target = CoreState::JOG; break;
                case Event::TARE_START: target = CoreState::TARE; break;
                case Event::PRELOAD_START: target = CoreState::PRELOAD; break;
                default: is_substate_entry = false; break;
            }
            if (is_substate_entry) {
                if (!homed_and_valid(guards)) return false;
                return_state_ = state_;
                state_ = target;
                return true;
            }

            if (state_ == CoreState::IDLE) {
                if (event == Event::HOME) {
                    // Caller checks the |force| < 2 N homing guard (skill 3.4) before
                    // sending this -- the state machine knows nothing about force.
                    state_ = CoreState::HOMING;
                    return true;
                }
                if (event == Event::ARM) {
                    if (!(homed_and_valid(guards) && guards.config_valid)) return false;
                    arming_ = true;  // state stays IDLE; caller starts the move to L0
                    return true;
                }
                if (event == Event::L0_REACHED) {
                    if (!arming_) return false;
                    arming_ = false;
                    state_ = CoreState::READY;
                    return true;
                }
            } else {  // READY
                if (event == Event::START) {
                    state_ = guards.cycle_mode ? CoreState::CYCLE : CoreState::RUN;
                    return true;
                }
            }
            return false;
        }

        case CoreState::HOMING:
            if (event == Event::HOMING_DONE) {
                state_ = CoreState::IDLE;
                return true;
            }
            return false;

        case CoreState::JOG:
            if (event == Event::JOG_STOP) {
                state_ = return_state_;
                return true;
            }
            return false;

        case CoreState::TARE:
            if (event == Event::TARE_DONE) {
                state_ = return_state_;
                return true;
            }
            return false;

        case CoreState::PRELOAD:
            if (event == Event::PRELOAD_DONE) {
                state_ = return_state_;
                return true;
            }
            return false;

        case CoreState::RUN:
        case CoreState::CYCLE:
            if (event == Event::PAUSE) {
                return_state_ = state_;
                state_ = CoreState::HOLD;
                lock_owner_ = LockOwner::PC;
                return true;
            }
            if (event == Event::SUBSTRATE_BREAK) {
                state_ = CoreState::BREAK_HOLD;
                return true;
            }
            if (event == Event::SEGMENT_DONE) {
                // Same HOLD as PAUSE, but RESUME should land in READY, not back in
                // RUN/CYCLE -- reuses return_state_ for that purpose.
                return_state_ = CoreState::READY;
                state_ = CoreState::HOLD;
                lock_owner_ = LockOwner::PC;
                return true;
            }
            return false;

        case CoreState::HOLD:
            if (event == Event::MODULE_LOCK_REQUEST) {
                lock_owner_ = LockOwner::MODULE;
                return true;
            }
            if (event == Event::MODULE_LOCK_RELEASE) {
                lock_owner_ = LockOwner::PC;
                return true;
            }
            if (event == Event::RESUME) {
                if (lock_owner_ != LockOwner::PC || guards.module_inhibit_present) {
                    return false;
                }
                state_ = return_state_;  // RUN or CYCLE, whichever it was paused from
                lock_owner_ = LockOwner::NONE;
                return true;
            }
            return false;

        case CoreState::BREAK_HOLD:
            // Own RESET-like exit back to IDLE (Mathew, 2026-10-26), same cause_cleared
            // guard as FAULT's RESET -- besides SAFETY_TRIP (handled above), this is the
            // only way out.
            if (event == Event::RESET) {
                if (!guards.cause_cleared) return false;
                state_ = CoreState::IDLE;
                lock_owner_ = LockOwner::NONE;
                return true;
            }
            return false;

        case CoreState::FAULT:
            if (event == Event::RESET) {
                if (!guards.cause_cleared) return false;
                state_ = CoreState::IDLE;
                lock_owner_ = LockOwner::NONE;
                arming_ = false;
                return true;
            }
            return false;
    }
    return false;
}

}  // namespace core_sm
