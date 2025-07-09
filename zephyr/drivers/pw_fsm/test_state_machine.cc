/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <gtest/gtest.h>

#include "pw_fsm/fsm.h"

#define EXPECT_OK(check) EXPECT_EQ(pw::OkStatus(), check);
#define ASSERT_OK(check) ASSERT_EQ(pw::OkStatus(), check);

namespace pw::fsm {

namespace {
enum LedState {
  ON,
  OFF,
};
enum LedColor {
  GREEN,
  BLUE,
};
enum Event {
  BUTTON_PRESS,
  TIMER_EXPIRE,
  TIMER_EXPIRE_RESET,
};

constexpr FsmConfig kLedStateConfig(LedState::OFF,
                                    Transition<LedState>(LedState::OFF,
                                                         LedState::ON),
                                    Transition<LedState>(LedState::ON,
                                                         LedState::OFF));
constexpr FsmConfig kLedOnStateSubConfig(LedColor::GREEN,
                                         Transition<LedColor>(LedColor::GREEN,
                                                              LedColor::BLUE),
                                         Transition<LedColor>(LedColor::BLUE,
                                                              LedColor::GREEN));

static_assert(kLedStateConfig.IsValid(),
              "LED state machine configuration is invalid");
static_assert(kLedOnStateSubConfig.IsValid(),
              "LED ON state subconfiguration is invalid");

class LedColorFsm : public EventStateMachine<LedColor, Event> {
 public:
  LedColorFsm() : EventStateMachine<LedColor, Event>(kLedOnStateSubConfig) {}

 protected:
  LedColor OnEvent(const LedColor& state, const Event& event) override {
    if (event != Event::BUTTON_PRESS) {
      return state;
    }
    switch (state) {
      case LedColor::BLUE:
        return LedColor::GREEN;
      case LedColor::GREEN:
        return LedColor::BLUE;
    }
    PW_UNREACHABLE;
  }
};

class LedFsm : public EventStateMachine<LedState, Event> {
 public:
  LedFsm()
      : EventStateMachine<LedState, Event>(kLedStateConfig), led_color_fsm_() {}

  LedColor GetLedColor() const { return led_color_fsm_.current_state(); }

 protected:
  LedState OnEvent(const LedState& state, const Event& event) override {
    switch (state) {
      case LedState::OFF:
        switch (event) {
          case Event::BUTTON_PRESS:
          case Event::TIMER_EXPIRE_RESET:
            return LedState::OFF;
          case Event::TIMER_EXPIRE:
            return LedState::ON;
        }
        PW_UNREACHABLE;
      case LedState::ON:
        switch (event) {
          case Event::TIMER_EXPIRE_RESET:
            return LedState::OFF;
          case Event::BUTTON_PRESS:
            led_color_fsm_.TriggerEvent(event);
            return LedState::ON;
          case Event::TIMER_EXPIRE:
            return LedState::ON;
        }
        PW_UNREACHABLE;
    }
    PW_UNREACHABLE;
  }

 private:
  LedColorFsm led_color_fsm_;
};

TEST(StateMachine, TransitionConstruction) {
  using VariantType = std::variant<LedState, StateMatcher>;
  constexpr Transition<LedState> transition(LedState::ON, LedState::OFF);
  constexpr Transition<LedState> transition_copy(transition);
  constexpr Transition<LedState> transition_wildcard(LedState::ON,
                                                     StateMatcher::kAny);
  constexpr Transition<LedState> transition_wildcard_destination(
      std::move(transition_wildcard));

  ASSERT_EQ(VariantType(LedState::ON), transition.from());
  ASSERT_EQ(VariantType(LedState::OFF), transition.to());
  ASSERT_EQ(transition, transition_copy);
  ASSERT_EQ(VariantType(LedState::ON), transition_wildcard_destination.from());
  ASSERT_EQ(VariantType(StateMatcher::kAny),
            transition_wildcard_destination.to());
}

TEST(StateMachine, StaticAssertConfig) {
  static_assert(kLedStateConfig.IsValid());
  static_assert(kLedOnStateSubConfig.IsValid());

  constexpr FsmConfig kInvalidLedStateConfig(
      LedState::OFF, Transition<LedState>(LedState::ON, LedState::OFF));
  static_assert(!kInvalidLedStateConfig.IsValid());
}

TEST(StateMachine, VerifyAllowedTransitionsInConfig) {
  enum TriState {
    STATE0,
    STATE1,
    STATE2,
  };
  constexpr FsmConfig kConfig(
      TriState::STATE0,
      Transition<TriState>(TriState::STATE0, TriState::STATE1),
      Transition<TriState>(TriState::STATE1, TriState::STATE2),
      Transition<TriState>(TriState::STATE2, StateMatcher::kAny));
  ASSERT_TRUE(kConfig.IsTransitionAllowed(TriState::STATE0, TriState::STATE1));
  ASSERT_TRUE(kConfig.IsTransitionAllowed(TriState::STATE2, TriState::STATE0));
  ASSERT_FALSE(kConfig.IsTransitionAllowed(TriState::STATE1, TriState::STATE0));
}

TEST(StateMachine, Transaction) {
  StateMachine<LedState> sm(kLedStateConfig);

  auto transaction = sm.BeginTransaction(LedState::ON);
  ASSERT_EQ(pw::OkStatus(), transaction.status());
  ASSERT_OK(transaction->Do(
      [](int x, int y) -> pw::Status {
        if (x == 10 && y == 5) {
          return pw::OkStatus();
        }
        return pw::Status::InvalidArgument();
      },
      10,
      5));
  ASSERT_OK(transaction->Commit());
  ASSERT_EQ(LedState::ON, sm.current_state());
}

TEST(StateMachine, FailedTransactionDo) {
  StateMachine<LedState> sm(kLedStateConfig);

  auto transaction = sm.BeginTransaction(LedState::ON);
  ASSERT_EQ(pw::OkStatus(), transaction.status());
  ASSERT_EQ(pw::Status::InvalidArgument(), transaction->Do([]() -> pw::Status {
    return pw::Status::InvalidArgument();
  }));
  ASSERT_EQ(pw::Status::InvalidArgument(), transaction->Do([]() -> pw::Status {
    PW_ASSERT(false);
    PW_UNREACHABLE;
  }));
  ASSERT_EQ(pw::Status::InvalidArgument(), transaction->Commit());
  ASSERT_EQ(LedState::OFF, sm.current_state());
}

TEST(StateMachine, ConcurrentTransactions) {
  StateMachine<LedState> sm(kLedStateConfig);

  auto transaction = sm.BeginTransaction(LedState::ON);
  ASSERT_EQ(pw::OkStatus(), transaction.status());
  ASSERT_EQ(pw::Status::ResourceExhausted(),
            sm.BeginTransaction(LedState::ON).status());
}

TEST(StateMachine, InvalidTransaction) {
  enum TriState {
    STATE0,
    STATE1,
    STATE2,
  };
  constexpr FsmConfig kConfig(
      TriState::STATE0,
      Transition<TriState>(TriState::STATE0, TriState::STATE1),
      Transition<TriState>(TriState::STATE1, TriState::STATE2),
      Transition<TriState>(TriState::STATE2, StateMatcher::kAny));
  StateMachine<TriState> sm(kConfig);

  auto transaction = sm.BeginTransaction(TriState::STATE2);
  ASSERT_EQ(pw::Status::InvalidArgument(), transaction.status());
}

TEST(StateMachine, IsStateAnyOf) {
  LedFsm sm;

  ASSERT_TRUE(sm.StateAnyOf({LedState::OFF, LedState::ON}));
  ASSERT_TRUE(sm.StateAnyOf({LedState::OFF}));
  ASSERT_FALSE(sm.StateAnyOf({LedState::ON}));
}

TEST(StateMachine, ConcurrentTransactionAndEvent) {
  LedFsm sm;

  auto transaction = sm.BeginTransaction(LedState::ON);
  ASSERT_EQ(pw::OkStatus(), transaction.status());
  ASSERT_EQ(pw::Status::ResourceExhausted(),
            sm.TriggerEvent(Event::TIMER_EXPIRE));
}

TEST(StateMachine, TransitionsOnEvent) {
  LedFsm sm;

  ASSERT_EQ(sm.current_state(), LedState::OFF);
  ASSERT_EQ(sm.GetLedColor(), LedColor::GREEN);

  ASSERT_OK(sm.TriggerEvent(Event::TIMER_EXPIRE));
  ASSERT_EQ(sm.current_state(), LedState::ON);
  ASSERT_EQ(sm.GetLedColor(), LedColor::GREEN);

  ASSERT_OK(sm.TriggerEvent(Event::BUTTON_PRESS));
  ASSERT_EQ(sm.current_state(), LedState::ON);
  ASSERT_EQ(sm.GetLedColor(), LedColor::BLUE);

  ASSERT_OK(sm.TriggerEvent(Event::BUTTON_PRESS));
  ASSERT_EQ(sm.current_state(), LedState::ON);
  ASSERT_EQ(sm.GetLedColor(), LedColor::GREEN);

  ASSERT_OK(sm.TriggerEvent(Event::TIMER_EXPIRE_RESET));
  ASSERT_EQ(sm.current_state(), LedState::OFF);
  ASSERT_EQ(sm.GetLedColor(), LedColor::GREEN);

  ASSERT_OK(sm.TriggerEvent(Event::BUTTON_PRESS));
  ASSERT_EQ(sm.current_state(), LedState::OFF);
  ASSERT_EQ(sm.GetLedColor(), LedColor::GREEN);
}
}  // namespace
}  // namespace pw::fsm
