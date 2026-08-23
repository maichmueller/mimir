"""Python gate for the Rollout IW(1) and atomic-goal portfolio bindings.

Both entry points release the GIL for the whole native call, so the checks here are as much about
the binding contract -- options round-trip, no Python callbacks reachable from a worker thread,
other Python threads keep running -- as about the search itself.
"""

import threading
import time
from pathlib import Path

import pymimir.advanced.formalism as formalism
import pymimir.advanced.search as search

ROOT_DIR = (Path(__file__).parent.parent.parent.parent).absolute()


def _paths(domain: str, problem: str):
    return str(ROOT_DIR / "data" / domain / "domain.pddl"), str(ROOT_DIR / "data" / domain / problem)


def _grounded_context(domain="gripper", problem="test_problem.pddl"):
    domain_filepath, problem_filepath = _paths(domain, problem)
    return search.SearchContext.create(domain_filepath, problem_filepath, search.SearchContextOptions())


def _lifted_context(domain="gripper", problem="test_problem.pddl"):
    domain_filepath, problem_filepath = _paths(domain, problem)
    options = search.SearchContextOptions(search.LiftedOptions(search.LiftedKPKCOptions(search.SymmetryPruning.OFF)))
    return search.SearchContext.create(domain_filepath, problem_filepath, options)


def test_rollout_iw_options_round_trip():
    options = search.RolloutIWOptions()
    options.seed = 17
    options.max_depth = 12
    options.max_rollouts = 100
    options.max_num_states = 1000
    options.max_time_in_ms = 5000
    options.incumbent_bound = 7
    options.action_ordering_configuration = search.RolloutIWActionOrderingConfiguration(
        search.RolloutIWActionOrderingKind.MIXED_REGRESSION_RANDOM, 99
    )

    assert options.seed == 17
    assert options.max_depth == 12
    assert options.max_rollouts == 100
    assert options.max_num_states == 1000
    assert options.max_time_in_ms == 5000
    assert options.incumbent_bound == 7
    assert options.action_ordering_configuration.kind == search.RolloutIWActionOrderingKind.MIXED_REGRESSION_RANDOM
    assert options.action_ordering_configuration.seed == 99


def test_rollout_iw_grounded_and_lifted():
    for context in (_grounded_context(), _lifted_context()):
        result = search.find_solution_rollout_iw(context, search.RolloutIWOptions())

        # Rollout IW(1) is not complete on gripper's width-2 conjunctive goal, so a plan is not
        # guaranteed -- but the four rollout cases must have been exercised either way.
        statistics = result.statistics
        assert statistics.num_rollouts > 0
        assert statistics.num_generated_states > 0
        assert statistics.num_case_1 + statistics.num_case_2 + statistics.num_case_3 + statistics.num_case_4 > 0

        if result.search_result.status == search.SearchStatus.SOLVED:
            assert result.plan_length == len(result.plan_steps)
            assert result.plan_length == len(result.search_result.plan)
            for step in result.plan_steps:
                assert step.schema is not None
                assert len(step.binding) == step.schema.get_arity()
        else:
            assert result.stop_reason


def test_rollout_iw_all_orderings():
    for kind in (
        search.RolloutIWActionOrderingKind.IN_ORDER,
        search.RolloutIWActionOrderingKind.RANDOMIZED,
        search.RolloutIWActionOrderingKind.DIRECT_GOAL_ACHIEVER_FIRST,
        search.RolloutIWActionOrderingKind.GOAL_REGRESSION_RELEVANCE,
        search.RolloutIWActionOrderingKind.MIXED_REGRESSION_RANDOM,
    ):
        options = search.RolloutIWOptions()
        options.action_ordering_configuration = search.RolloutIWActionOrderingConfiguration(kind, 5)
        result = search.find_solution_rollout_iw(_grounded_context(), options)
        assert result.statistics.num_rollouts > 0


def test_rollout_iw_rejects_python_goal_strategy():
    """A Python-subclassed strategy would be called from a GIL-released search. Refuse it up front."""

    class PythonGoalStrategy(search.IGoalStrategy):
        def test_static_goal(self):
            return True

        def test_dynamic_goal(self, state):
            return False

    options = search.RolloutIWOptions()
    options.goal_strategy = PythonGoalStrategy()

    try:
        search.find_solution_rollout_iw(_grounded_context(), options)
    except Exception as error:  # nanobind maps std::invalid_argument to ValueError
        assert "native goal strategy" in str(error)
    else:
        raise AssertionError("expected a Python goal strategy to be rejected")


def test_atomic_goal_portfolio_grounded_and_lifted():
    for mode_context in (_grounded_context(), _lifted_context()):
        options = search.AtomicGoalIWPortfolioOptions()
        options.num_rollout_workers = 3
        options.num_threads = 4
        options.base_seed = 4711
        options.max_time_in_ms = 20000

        result = search.find_solution_atomic_goal_iw_portfolio(mode_context, options)

        assert result.executed_mode in (
            search.AtomicGoalPortfolioSearchMode.GROUNDED,
            search.AtomicGoalPortfolioSearchMode.LIFTED_KPKC,
        )
        assert result.stop_reason
        assert len(result.rollout_statistics) == 3
        assert len(result.rollout_statuses) == 3

        if result.status == search.SearchStatus.SOLVED:
            assert result.plan is not None
            assert result.plan_length == len(result.plan)
            assert result.plan_length == len(result.plan_steps)
        else:
            assert not result.certified_optimal


def test_atomic_goal_portfolio_atomic_goal_and_certificate():
    """A single goal atom is the intended use, and is where the certificate is meaningful."""
    context = _grounded_context()
    problem = context.get_problem()

    goal_atoms = [literal.get_atom() for literal in problem.get_fluent_goal_literals() if literal.get_polarity()]
    assert goal_atoms, "gripper's goal is a conjunction of positive fluent atoms"

    options = search.AtomicGoalIWPortfolioOptions()
    options.num_rollout_workers = 2
    options.num_threads = 2
    options.atomic_goal_atoms = formalism.FluentGroundAtomList([goal_atoms[0]])

    result = search.find_solution_atomic_goal_iw_portfolio(context, options)

    if result.status == search.SearchStatus.SOLVED:
        assert result.plan is not None
        if result.certified_optimal:
            assert result.iw_lower_bound >= result.plan_length


def test_atomic_goal_portfolio_mode_validation():
    grounded = _grounded_context()
    options = search.AtomicGoalIWPortfolioOptions()
    options.num_rollout_workers = 1
    options.search_mode = search.AtomicGoalPortfolioSearchMode.LIFTED_KPKC

    try:
        search.find_solution_atomic_goal_iw_portfolio(grounded, options)
    except Exception as error:
        assert "LIFTED_KPKC" in str(error)
    else:
        raise AssertionError("expected a grounded context to be rejected in LIFTED_KPKC mode")


def test_atomic_goal_portfolio_releases_the_gil():
    """The whole native call must run without the GIL, or a plain Python thread would stall.

    Needs an instance the portfolio cannot finish instantly, or there is no window in which to
    observe anything; a time budget then bounds the test rather than the instance doing so.
    """
    budget_in_ms = 400
    context = _grounded_context("spanner", "p01-hard.pddl")

    ticks = []
    stop = threading.Event()

    def tick():
        while not stop.is_set():
            ticks.append(time.perf_counter())
            time.sleep(0.001)

    ticker = threading.Thread(target=tick, daemon=True)
    ticker.start()
    try:
        options = search.AtomicGoalIWPortfolioOptions()
        options.num_rollout_workers = 7
        options.num_threads = 8
        options.max_time_in_ms = budget_in_ms

        started = time.perf_counter()
        search.find_solution_atomic_goal_iw_portfolio(context, options)
        elapsed_in_ms = (time.perf_counter() - started) * 1000.0
    finally:
        stop.set()
        ticker.join(timeout=5)

    assert elapsed_in_ms > 50.0, "the instance finished too fast to say anything about the GIL"

    # Ticks recorded while the native call was running. Holding the GIL would leave this at zero.
    ticks_during_call = [t for t in ticks if started <= t <= started + elapsed_in_ms / 1000.0]
    assert len(ticks_during_call) > 5


def test_search_status_enum_covers_every_native_value():
    """Every C++ ``SearchStatus`` value must have a Python counterpart.

    nanobind enums raise ``ValueError`` when a native value has no binding, and the failure lands
    on whoever reads ``result.status`` -- far from the enum declaration that caused it. ``CANCELED``
    was missing, so any portfolio run in which a worker got canceled (routine: the certifier is
    canceled as soon as a rollout incumbent is certified) blew up in Python instead of reporting.
    """
    for name in ("IN_PROGRESS", "OUT_OF_TIME", "OUT_OF_MEMORY", "OUT_OF_STATES", "FAILED", "EXHAUSTED", "SOLVED", "UNSOLVABLE", "CANCELED"):
        assert hasattr(search.SearchStatus, name), f"SearchStatus.{name} is not bound"


def test_portfolio_statuses_are_readable_when_workers_are_canceled():
    """Reading the statuses of a run that cancels workers must not raise.

    This is the end the missing ``CANCELED`` binding actually broke: the enum conversion happens on
    attribute access, so the error surfaces here rather than at import time.
    """
    context = _grounded_context("blocks_4", "test_problem.pddl")

    options = search.AtomicGoalIWPortfolioOptions()
    options.num_rollout_workers = 4
    options.num_threads = 4

    result = search.find_solution_atomic_goal_iw_portfolio(context, options)

    assert isinstance(result.status, search.SearchStatus)
    for status in result.rollout_statuses:
        assert isinstance(status, search.SearchStatus)
