/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>
#include <tuple>
#include <utility>
#include <variant>

#include "pw_result/result.h"
#include "pw_status/status.h"

namespace pw::fsm {

/* Forward declaration */
template <typename State>
class Transition;

/** Unique matcher rules for state transitions. */
enum class StateMatcher {
  /** Matches any state, similar to a '*' wildcard */
  kAny,
};

namespace impl {
/** A generic size used to abstract state transition list sizes. */
inline constexpr size_t kGeneric = size_t(-1);

/**
 * Structure used to verify the validity of a state machine. This structure
 * should only be used in a constant evaluation context and never in runtime
 * (except for tests).
 */
template <typename State>
struct TraverseData {
  /** The state represented in the traversal */
  State state;
  /** Whether or not the state has been visited */
  bool visited;
  /** Whether or not the node is valid (we're going to over allocate) */
  bool is_valid;
};

template <typename State, size_t kNumTransitions>
constexpr void VisitTransitionNodes(
    State current_state,
    const std::array<Transition<State>, kNumTransitions>& transitions,
    std::array<TraverseData<State>, kNumTransitions * 2>& visited) {
  // Find the entry corresponding to the current state
  auto visited_node = std::find_if(
      visited.begin(), visited.end(), [current_state](const auto& data) {
        return data.state == current_state && data.is_valid;
      });
  if (visited_node->visited) {
    // State was already visited
    return;
  }
  // Mark the state as visited
  visited_node->visited = true;

  for (const auto& transition : transitions) {
    bool is_from_state = std::holds_alternative<State>(transition.from()) &&
                         std::get<State>(transition.from()) == current_state;
    bool is_from_any =
        std::holds_alternative<StateMatcher>(transition.from()) &&
        std::get<StateMatcher>(transition.from()) == StateMatcher::kAny;
    if (!is_from_state && !is_from_any) {
      // We're not coming from the current state or from '*'
      continue;
    }
    if (!std::holds_alternative<State>(transition.to())) {
      // The target state is not a State type, likely a '*' but we should be
      // explicit about searching for State.
      continue;
    }
    VisitTransitionNodes(
        std::get<State>(transition.to()), transitions, visited);
  }
}
}  // namespace impl

/** A transition node which may be of a custom State or a StateMatcher. */
template <typename State>
using TransitionNode = std::variant<State, StateMatcher>;

/** A single transition in the state machine */
template <typename State>
class alignas(alignof(TransitionNode<State>)) Transition {
 public:
  /** The type nodes for the nodes 'from' and 'to' */
  using node_type = TransitionNode<State>;
  /**
   * Primary constructor building a transition object from 2 nodes. Either node
   * may be of type StateMatcher::kAny which symbolizes that the transition can
   * come from or go to any other node.
   *
   * @param from The originating node of the transition.
   * @param to The target node of the transition.
   */
  constexpr Transition(node_type from, node_type to) : from_(from), to_(to) {}

  /** Copy constructor */
  constexpr Transition(const Transition& other) = default;

  /** Move constructor */
  constexpr Transition(Transition<State>&& other) = default;

  /**
   * Test if a transition is equal to another (the must be of the same State
   * type).
   *
   * 2 transitions are considered the same if the following passes. For
   * example, we'll use a transition (from=StateMatcher::kAny, to=State::a) and
   * test it against (from=State::b, to=State::a).
   *
   * Compare 'from':
   * 1. The 'from' field doesn't match since the left side of the comparison
   *    is using the wildcard matcher and StateMatcher::kAny is not the same
   *    type as State::b.
   * 2. We then check if the 'from' field of either transition has a wildcard
   *    matcher (which it does) which allows us to conclude that the 'from'
   *    fields are equal.
   *
   * Compare 'to':
   * 1. The 'to' field does match since State::a == State::a.
   *
   * @param other The right hand side of the comparison
   * @return true if the two transitions are effectively equal.
   */
  bool operator==(const Transition<State>& other) const {
    return (from_ == other.from_ ||
            std::holds_alternative<StateMatcher>(from_) ||
            std::holds_alternative<StateMatcher>(other.from_)) &&
           (to_ == other.to_ || std::holds_alternative<StateMatcher>(to_) ||
            std::holds_alternative<StateMatcher>(other.to_));
  }

  /**
   * @see operator==
   */
  bool operator!=(const Transition<State>& other) const {
    return !(*this == other);
  }

  /** @return The originating node */
  constexpr node_type from() const { return from_; }
  /** @return The destination node */
  constexpr node_type to() const { return to_; }

 private:
  node_type from_;
  node_type to_;
};

template <typename State, size_t kNumTransitions>
constexpr bool validateTransitions(
    State starting_state,
    const std::array<Transition<State>, kNumTransitions>& transitions) {
  std::array<impl::TraverseData<State>, kNumTransitions * 2> visited{};
  size_t num_states = 0;
  for (const auto& transition : transitions) {
    for (const auto& node : {transition.from(), transition.to()}) {
      if (std::holds_alternative<State>(node)) {
        State state = std::get<State>(node);
        // Check if the node's state is already in the visited list.
        if (std::any_of(
                visited.begin(), visited.end(), [state](const auto& data) {
                  return data.state == state && data.is_valid;
                })) {
          // Skip the node, we already have it
          continue;
        }
        // Add the state and increment num_states
        visited[num_states].state = state;
        visited[num_states].visited = false;
        visited[num_states].is_valid = true;
        num_states++;
      }
    }
  }

  // Traverse the tree and update the 'visited' list
  VisitTransitionNodes(starting_state, transitions, visited);

  // Verify that all the nodes are reachable. We over allocated so some might
  // still be invalid, count those as visited.
  return std::all_of(visited.begin(), visited.end(), [](const auto& data) {
    return (data.is_valid && data.visited) || !data.is_valid;
  });
}

/* Forward declaration */
template <typename State, size_t kNumTransitions>
class FsmConfig;

/**
 * The base storage for the FsmConfig object. This object declares the
 * std::array of Transition objects using the kNumTransitions template argument.
 * Since the FsmConfig object will inherit from this, we want to provide generic
 * implementations of all the functions we'll need so we're going to extend the
 * FsmConfig specialization that uses the impl::kGeneric value for the
 * kNumTransitions. This specialization abstracts away the underlying storage so
 * we can pass references to this object without the kNumTransitions template
 * parameter.
 */
template <typename State, size_t kNumTransitions>
struct FsmConfigStorage : public FsmConfig<State, impl::kGeneric> {
 protected:
  /**
   * Construct the underlying storage by copying the transitions.
   *
   * @param transitions The allowed transitions in this config.
   */
  constexpr FsmConfigStorage(
      State starting_state,
      std::array<Transition<State>, kNumTransitions> transitions)
      : FsmConfig<State, impl::kGeneric>(starting_state, kNumTransitions),
        starting_state_(starting_state),
        transitions_(transitions) {}

 private:
  friend class FsmConfig<State, kNumTransitions>;
  friend class FsmConfig<State, impl::kGeneric>;
#ifdef __cpp_lib_launder
  const Transition<State>* transitions() const {
    return std::launder(
        reinterpret_cast<const Transition<State>*>(&transitions_));
  }
#else
  const Transition<State>* transitions() const {
    return reinterpret_cast<const Transition<State>*>(&transitions_);
  }
#endif
  constexpr State starting_state() const { return starting_state_; }

  State starting_state_;
  alignas(Transition<State>)
      std::array<Transition<State>, kNumTransitions> transitions_;
};

/**
 * Base FsmConfig object which leverages the underlying storage of the
 * FsmConfigStorage class.
 */
template <typename State, size_t kNumTransitions = impl::kGeneric>
class FsmConfig : public FsmConfigStorage<State, kNumTransitions> {
 public:
  /**
   * Construct a new FsmConfiguration
   *
   * @param starting_state The initial state for the state machine
   * @param transitions The set of allowed transitions
   */
  template <typename... T>
  constexpr FsmConfig(State starting_state, T... transitions)
      : FsmConfigStorage<State, sizeof...(transitions)>(
            starting_state, std::to_array({transitions...})) {}

  /**
   * Utility to verify if the configuration is valid.
   *
   * A valid configuration is one that has no dangling nodes. For example:
   *
   *   starting_state = state0
   *   state0 -> state2
   *   state1 -> state2
   *
   * The above is an invalid configuration. There is no way to reach state1.
   *
   * @return True if the configuration is valid.
   */
  constexpr bool IsValid() const {
    return validateTransitions(this->starting_state_, this->transitions_);
  }

  /**
   * Helper function to check if a transition is allowed
   *
   * @param from_state The starting state
   * @param to_state The target state
   * @return True if the configuration allows moving from 'from_state' to
   * 'to_state'
   */
  bool IsTransitionAllowed(State from_state, State to_state) const {
    Transition<State> transition(from_state, to_state);
    for (auto* it = this->transitions_cbegin(); it != this->transitions_cend();
         ++it) {
      if (transition == *it) {
        return true;
      }
    }
    return false;
  }
};

/**
 * Deduction guide for FsmConfig.
 *
 * This guide aids the compiler in figuring out the template arguments for the
 * FsmConfig when using a variadic constructor. It allows us to declare the
 * variable via:
 *
 * @code
 * constexpr FsmConfig kMyConfig(...);
 * @endcode
 */
template <typename S, typename... T>
FsmConfig(S starting_state, T... transitions)
    -> FsmConfig<S, sizeof...(transitions)>;

/**
 * A generic specialization of the FsmConfig.
 *
 * This class is not instantiable. It is used strictly to pass FsmConfig<State>&
 * references and pointers around.
 */
template <typename State>
class FsmConfig<State, impl::kGeneric> {
 protected:
  constexpr FsmConfig(State starting_state, size_t num_transitions) noexcept
      : starting_state_(starting_state), num_transitions_(num_transitions) {}

 private:
  const State starting_state_;
  size_t num_transitions_;

 public:
  // Allow generic specialization to pass the request IsTransitionAllowed to the
  // root FsmConfig class.
  bool IsTransitionAllowed(State from_state, State to_state) const {
    return static_cast<const FsmConfig<State, 0>*>(this)->IsTransitionAllowed(
        from_state, to_state);
  }

  // Allow generic specialization to pass the transitions() getter to the
  // root FsmConfig class.
  const Transition<State>* transitions() const {
    return static_cast<const FsmConfig<State, 0>*>(this)->transitions();
  }

  // Allow generic specialization to pass the transitions_cbegin() getter to the
  // root FsmConfig class.
  const Transition<State>* transitions_cbegin() const {
    return &transitions()[0];
  }

  // Allow generic specialization to pass the transitions_cend() getter to the
  // root FsmConfig class.
  const Transition<State>* transitions_cend() const {
    return &transitions()[this->num_transitions()];
  }

  // Allow generic specialization to pass the starting_state() getter to the
  // root FsmConfig class.
  constexpr State starting_state() const { return starting_state_; }

  /**
   * @return The number of transitions contained by this config.
   */
  constexpr size_t num_transitions() const { return num_transitions_; }
};

/**
 * A finite state machine.
 */
template <typename State>
class StateMachine {
 public:
  /** Default virtual destructor */
  virtual ~StateMachine() = default;

 protected:
  /**
   * Base function called when we enter a new state
   *
   * @param state The new state we've entered
   */
  virtual void OnEnter(const State& state) { (void)state; }

  /**
   * Base function called when we exit a state
   *
   * @param state The state we're about to leave
   */
  virtual void OnExit(const State& state) { (void)state; }

 public:
  /**
   * Construct a new state machine binding to the configuration.
   *
   * @param config The configuration containing the starting state and allowed
   * transitions.
   */
  explicit constexpr StateMachine(const FsmConfig<State>& config)
      : config_(config), current_state_(config.starting_state()) {}

  class Transaction {
   public:
    /** Delete the copy constructor, only 1 copy is allowed. */
    Transaction(const Transaction&) = delete;

    /** Delete the assignment operator, only 1 copy is allowed. */
    Transaction& operator=(const Transaction&) = delete;

    /**
     * Move a transaction across memory. Invalidate the old transaction.
     *
     * @param other The old Transaction that will be deleted.
     */
    Transaction(Transaction&& other)
        : state_machine_(other.state_machine_),
          from_(std::move(other.from_)),
          to_(std::move(other.to_)),
          is_done_(other.is_done_),
          status_(other.status_) {
      other.is_done_ = true;
      state_machine_.current_transaction_ = this;
    }

    /**
     * Move assignment constructor.
     *
     * @param other The old Transaction that will be deleted.
     * @return A reference to the new Transaction
     */
    Transaction& operator=(Transaction&& other) noexcept {
      if (this == &other) {
        return *this;
      }
      state_machine_ = std::move(other.state_machine_);
      from_ = std::move(other.from_);
      to_ = std::move(other.to_);
      is_done_ = other.is_done_;
      status_ = other.status_;
      other.is_done_ = true;
      state_machine_.current_transaction_ = this;
      return *this;
    }

    /** Destroy the transaction and commit if valid. */
    ~Transaction() { Commit(); }

    /**
     * Update the status of an in-flight transaction.
     *
     * If the transaction has already been committed, this call will have no
     * effect.
     *
     * @param status The new status of the transaction.
     */
    void SetStatus(pw::Status status) {
      if (is_done_) {
        return;
      }
      status_ = status;
    }

    /**
     * Perform an action and update status.
     *
     * If the current status is ok, run fn and update the status. Otherwise just
     * return the current status.
     *
     * @param fn A function to run
     * @return The current status of the transaction after running the function.
     */
    template <typename Fn, typename... FnArgs>
    pw::Status Do(Fn&& fn, FnArgs&&... args) {
      if (!status_.ok() || is_done_) {
        return status_;
      }
      status_ = std::forward<Fn>(fn)(std::forward<FnArgs>(args)...);
      return status_;
    }

    /**
     * Commit the transaction.
     *
     * This operation will only execute if the transaction is currently in
     * flight. Once it was called, any future calls will be ignored.
     *
     * @return status() if this is the first call to Commit()
     * @return pw::Status::ResourceExhausted() otherwise
     */
    pw::Status Commit() {
      if (is_done_) {
        return pw::Status::ResourceExhausted();
      }
      is_done_ = true;
      state_machine_.CommitTransaction(*this);
      return status_;
    }

    /**
     * @return The current status of the transaction
     */
    pw::Status status() const { return status_; }

    /**
     * @return True if the transaction has been committed or canceled, false if
     * it is still in flight
     */
    bool done() const { return is_done_; }

   private:
    friend class StateMachine;

    Transaction(StateMachine& state_machine, State from, State to)
        : state_machine_(state_machine), from_(from), to_(to) {
      state_machine.current_transaction_ = this;
    }

    StateMachine& state_machine_;
    State from_;
    State to_;
    bool is_done_ = false;
    pw::Status status_ = pw::OkStatus();
  };

  /**
   * Begin a transaction.
   *
   * One a single transaction can be in flight at a time.
   *
   * When initiated, the StateMachine verifies that the current state can
   * transition to the provided state. If so, a Transaction object will be
   * returned wrapped in a pw::Result. A transaction by default is valid. If
   * anything should happen to invalidate the transaction, the caller must
   * simply call Transaction::SetStatus() with a non-ok status before committing
   * the transaction. A transaction can be completed by either a call to
   * Transaction::Commit() or letting the object go out of scope. Example:
   *
   * @code
   * auto transaction = fsm.BeginTransaction(MyEnum::WAIT_FOR_INPUT);
   * // Do a few things...
   * transaction.Commit();
   * // The state should now be WAIT_FOR_INPUT
   * @endcode
   *
   * @param state The new desired state
   * @return A pw::Result wrapping a valid Transaction if the transition is
   * possible
   * @return A pw::Result with an InvalidArgument status if the transition is
   * not possible
   */
  pw::Result<Transaction> BeginTransaction(const State& state) {
    bool is_valid = config_.IsTransitionAllowed(current_state_, state);
    if (!is_valid) {
      return pw::Result<Transaction>(pw::Status::InvalidArgument());
    }
    if (current_transaction_ != nullptr) {
      return pw::Result<Transaction>(pw::Status::ResourceExhausted());
    }

    return pw::Result(Transaction(*this, current_state_, state));
  }

  /** @return The current state */
  const State& current_state() const { return current_state_; }

  /**
   * Check if the current state matches any of the given states.
   *
   * @param states A list of possible states to check against
   * @return true if current_state() matches any of the states in the list
   */
  bool StateAnyOf(std::initializer_list<State> states) const {
    return std::any_of(
        states.begin(), states.end(), [this](const State& state) {
          return state == current_state_;
        });
  }

 private:
  void CommitTransaction(Transaction& transaction) {
    if (current_transaction_ != &transaction) {
      return;
    }
    current_transaction_ = nullptr;
    if (!transaction.status().ok()) {
      return;
    }
    PW_ASSERT(current_state_ == transaction.from_);
    OnExit(current_state_);
    current_state_ = transaction.to_;
    OnEnter(current_state_);
  }
  const FsmConfig<State>& config_;
  State current_state_;
  Transaction* current_transaction_ = nullptr;
};

template <typename State, typename Event>
class EventStateMachine : public StateMachine<State> {
 public:
  /**
   * Construct a new event based StateMachine
   *
   * @param config The configuration for the state machine.
   */
  constexpr EventStateMachine(const FsmConfig<State>& config)
      : StateMachine<State>(config) {}

  /**
   * Notify the state machine of an event.
   *
   * The current OnEvent handler will be called and possibly trigger a state
   * change.
   *
   * @param event The event that was triggered
   * @return pw::OkStatus() on success
   * @return pw::Status::InvalidArgument() if the event handler triggers an
   * invalid state transition
   */
  pw::Status TriggerEvent(const Event& event) {
    State current_state = this->current_state();
    State new_state = OnEvent(current_state, event);
    if (new_state == current_state) {
      return pw::OkStatus();
    }
    auto transaction = this->BeginTransaction(new_state);
    if (!transaction.ok()) {
      return transaction.status();
    }
    return transaction->Commit();
  }

 protected:
  /**
   * Base function called when an event is posted to the state machine.
   *
   * @param state The current state
   * @param event The event that was triggered
   * @return The new state (must be a valid transition)
   */
  virtual State OnEvent(const State& state, const Event& event) = 0;
};

}  // namespace pw::fsm
