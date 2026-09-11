"""`find_solution_iw_nogil`: IW that runs in a Python thread, and can be stopped from another one.

The motivating caller races a landmark-IW worker against a learned autoregressive worker on one
subgoal: whichever achieves it first wins, and the loser has to stop. That needs two things
`find_solution_iw` cannot give it. It keeps the GIL for the whole search, because its event
handlers may be Python objects, so a search started in a `threading.Thread` stalls every other
Python thread until it returns; and it exposes no way to interrupt a search that is already
running.

The variant tested here refuses the Python-backed options on the calling thread first and then
releases the GIL, so the checks below are as much about the refusals as about the search: a guard
that is quietly dropped turns a `ValueError` at the call site into a crash on a thread that holds
no GIL, which is exactly the failure nobody can debug from a traceback.
"""

import threading
import time
from pathlib import Path

import pymimir.advanced.search as search

import pytest

ROOT_DIR = (Path(__file__).parent.parent.parent.parent).absolute()

# An instance no IW ladder finishes in the seconds these tests are willing to wait, which is what
# gives the cancellation and GIL checks a window to observe. Both of them cancel the search rather
# than waiting it out.
HEAVY_DOMAIN, HEAVY_PROBLEM = "spanner", "p03-hard.pddl"


def _context(domain: str = "gripper", problem: str = "test_problem.pddl"):
    domain_filepath = str(ROOT_DIR / "data" / domain / "domain.pddl")
    problem_filepath = str(ROOT_DIR / "data" / domain / problem)
    return search.SearchContext.create(domain_filepath, problem_filepath, search.SearchContextOptions())


def _lifted_context(domain: str = "gripper", problem: str = "test_problem.pddl"):
    domain_filepath = str(ROOT_DIR / "data" / domain / "domain.pddl")
    problem_filepath = str(ROOT_DIR / "data" / domain / problem)
    options = search.SearchContextOptions(search.LiftedOptions(search.LiftedKPKCOptions(search.SymmetryPruning.OFF)))
    return search.SearchContext.create(domain_filepath, problem_filepath, options)


def _heavy_options():
    options = search.IWOptions()
    options.max_arity = 2
    # Far beyond anything a test waits for: a cancellation that silently failed must show up as a
    # hung join, not as a search that happened to run out of budget at about the right moment.
    options.max_time_in_ms = 120_000
    return options


def _plan_length(result):
    return len(result.plan) if result.plan is not None else None


def _landmark_ordering(problem_context):
    landmarks = search.ApproximateFactLandmarkGenerator.create(search.LiftedGrounder(problem_context.get_problem()))
    return search.LandmarkTransitionOrderingStrategy(landmarks)


"""Equivalence."""


def test_nogil_iw_matches_find_solution_iw():
    context = _context()
    options = search.IWOptions()
    options.max_arity = 2

    reference = search.find_solution_iw(context, options)
    released = search.find_solution_iw_nogil(_context(), options)

    assert reference.status == search.SearchStatus.SOLVED
    assert released.status == reference.status
    assert _plan_length(released) == _plan_length(reference)


def test_nogil_iw_matches_find_solution_iw_with_transition_ordering():
    context = _context()
    options = search.IWOptions()
    options.max_arity = 2

    reference = search.find_solution_iw(context, options, _landmark_ordering(context))

    other = _context()
    released = search.find_solution_iw_nogil(other, options, _landmark_ordering(other))

    assert reference.status == search.SearchStatus.SOLVED
    assert released.status == reference.status
    assert _plan_length(released) == _plan_length(reference)


def test_nogil_iw_accepts_an_explicit_none_cancellation():
    context = _context()
    options = search.IWOptions()
    options.max_arity = 2

    result = search.find_solution_iw_nogil(context, options, cancellation=None)

    assert result.status == search.SearchStatus.SOLVED


"""Refusals: everything that could dispatch back into Python."""


class _PythonGoalStrategy(search.IGoalStrategy):
    def test_static_goal(self):
        return True

    def test_dynamic_goal(self, state):
        return False


class _PythonBrFSEventHandler(search.IBrFSEventHandler):
    """Never actually called: the point is that constructing one is possible, so it has to be
    refused before the GIL is released rather than dispatched to from a thread without it."""


def test_nogil_iw_rejects_a_python_goal_strategy():
    options = search.IWOptions()
    options.goal_strategy = _PythonGoalStrategy()

    with pytest.raises(ValueError) as error:  # nanobind maps std::invalid_argument to ValueError
        search.find_solution_iw_nogil(_context(), options)

    assert "IWOptions.goal_strategy" in str(error.value)


def test_nogil_iw_rejects_a_python_brfs_event_handler():
    options = search.IWOptions()
    options.brfs_event_handler = _PythonBrFSEventHandler()

    with pytest.raises(ValueError) as error:
        search.find_solution_iw_nogil(_context(), options)

    assert "IWOptions.brfs_event_handler" in str(error.value)


def test_nogil_iw_rejects_a_python_brfs_event_handler_inside_a_composite():
    """A composite is only as safe as its children, so it is admitted by recursion, not by type."""
    context = _context()
    native = search.DefaultBrFSEventHandler(context.get_problem(), True)
    composite = search.CompositeBrFSEventHandler.create([native, _PythonBrFSEventHandler()], 0)

    options = search.IWOptions()
    options.brfs_event_handler = composite

    with pytest.raises(ValueError) as error:
        search.find_solution_iw_nogil(context, options)

    assert "IWOptions.brfs_event_handler" in str(error.value)


def test_nogil_iw_rejects_a_python_layer_ordering_strategy():
    """`ILayerOrderingStrategy` has a trampoline too, and an ordered layer scores every state."""

    class PythonLayerOrdering(search.ILayerOrderingStrategy):
        pass

    options = search.IWOptions()
    options.layer_ordering_strategy = PythonLayerOrdering()

    with pytest.raises(ValueError) as error:
        search.find_solution_iw_nogil(_context(), options)

    assert "IWOptions.layer_ordering_strategy" in str(error.value)


def test_nogil_iw_accepts_a_composite_of_native_brfs_event_handlers():
    """The recursion must admit a composite, not reject every composite."""
    context = _context()
    problem = context.get_problem()
    composite = search.CompositeBrFSEventHandler.create(
        [search.DefaultBrFSEventHandler(problem, True), search.DebugBrFSEventHandler(problem, True)], 0
    )

    options = search.IWOptions()
    options.max_arity = 2
    options.brfs_event_handler = composite

    assert search.find_solution_iw_nogil(context, options).status == search.SearchStatus.SOLVED


def test_nogil_iw_accepts_the_native_strategies_and_handlers():
    """The allowlist has to let the objects the motivating caller actually passes through."""
    context = _context()
    problem = context.get_problem()

    options = search.IWOptions()
    options.max_arity = 2
    options.goal_strategy = search.ProblemGoalStrategy.create(problem)
    options.iw_event_handler = search.DefaultIWEventHandler(problem, True)
    options.brfs_event_handler = search.DefaultBrFSEventHandler(problem, True)

    assert search.find_solution_iw_nogil(context, options).status == search.SearchStatus.SOLVED


"""Cancellation."""


def test_search_cancellation_handle_round_trips():
    cancellation = search.SearchCancellation()
    assert not cancellation.is_canceled()
    cancellation.request_cancel()
    assert cancellation.is_canceled()


def test_nogil_iw_stops_when_canceled_from_another_thread():
    context = _context(HEAVY_DOMAIN, HEAVY_PROBLEM)
    options = _heavy_options()
    cancellation = search.SearchCancellation()

    holder = {}

    def run():
        holder["result"] = search.find_solution_iw_nogil(context, options, cancellation)

    worker = threading.Thread(target=run, daemon=True)
    started = time.perf_counter()
    worker.start()

    # Long enough for the ladder to be inside an expansion loop, where the flag is polled.
    time.sleep(1.0)
    assert worker.is_alive(), "the instance finished too fast to say anything about cancellation"
    assert not cancellation.is_canceled()

    cancellation.request_cancel()
    assert cancellation.is_canceled()

    worker.join(timeout=60)
    elapsed = time.perf_counter() - started

    assert not worker.is_alive()
    assert holder["result"].status == search.SearchStatus.CANCELED
    # The search's own budget is 120 s, so finishing here is the cancellation and not the clock.
    assert elapsed < 60.0


def test_nogil_iw_releases_the_gil_for_the_whole_search():
    """A pure-Python loop in this thread must keep running while the search runs in another.

    Holding the GIL for the native call would leave the main thread unable to execute a single
    bytecode until the search returned, so the counter would read 0. The threshold is orders of
    magnitude below what an unblocked interpreter reaches in the observation window, which is what
    keeps this a test of the GIL rather than of machine speed.
    """
    observation_window_in_s = 2.0

    context = _context(HEAVY_DOMAIN, HEAVY_PROBLEM)
    options = _heavy_options()
    cancellation = search.SearchCancellation()

    finished = threading.Event()

    def run():
        try:
            search.find_solution_iw_nogil(context, options, cancellation)
        finally:
            finished.set()

    worker = threading.Thread(target=run, daemon=True)
    started = time.perf_counter()
    worker.start()

    ticks = 0
    while not finished.is_set() and (time.perf_counter() - started) < observation_window_in_s:
        ticks += 1
    observed_in_s = time.perf_counter() - started

    cancellation.request_cancel()
    worker.join(timeout=60)

    assert not finished.is_set() or observed_in_s >= observation_window_in_s, (
        "the search finished inside the observation window, so it says nothing about the GIL"
    )
    assert observed_in_s >= observation_window_in_s * 0.9
    assert ticks > 100_000, f"only {ticks} Python iterations while the search ran"


"""Cancellation the search could not honor is refused at the call site."""


def test_cancellation_with_a_layer_ordering_strategy_is_refused():
    context = _context()
    options = search.IWOptions()
    options.max_arity = 2
    options.layer_ordering_strategy = search.InOrderLayerOrderingStrategy.create()

    with pytest.raises(ValueError) as error:
        search.find_solution_iw_nogil(context, options, search.SearchCancellation())
    assert "IWOptions.layer_ordering_strategy" in str(error.value)

    # Without the handle the very same search is accepted: the refusal is about what the native
    # search can prove, not about the option being unsupported here.
    assert search.find_solution_iw_nogil(_context(), options).status == search.SearchStatus.SOLVED


def _beam_capable_options(context):
    """BrFS refuses a beam or a next-layer cap without an eager-scoring layer ordering, so both of
    those cases have to carry one to reach the cancellation check at all."""
    options = search.IWOptions()
    options.max_arity = 2
    options.layer_ordering_strategy = search.GoalCountLayerOrderingStrategy.create(context.get_problem())
    return options


def test_cancellation_with_a_beam_width_is_refused():
    context = _context()
    options = _beam_capable_options(context)
    options.beam_width = 8

    with pytest.raises(ValueError) as error:
        search.find_solution_iw_nogil(context, options, search.SearchCancellation())
    assert "IWOptions.beam_width" in str(error.value)

    assert search.find_solution_iw_nogil(_context(), options).status == search.SearchStatus.SOLVED


def test_cancellation_with_a_next_layer_cap_is_refused():
    context = _context()
    options = _beam_capable_options(context)
    options.max_next_layer_states = 8

    with pytest.raises(ValueError) as error:
        search.find_solution_iw_nogil(context, options, search.SearchCancellation())
    assert "IWOptions.max_next_layer_states" in str(error.value)

    assert search.find_solution_iw_nogil(_context(), options).status == search.SearchStatus.SOLVED


def test_cancellation_with_the_transition_ordering_overload_is_refused():
    context = _context()
    options = search.IWOptions()
    options.max_arity = 2

    with pytest.raises(ValueError) as error:
        search.find_solution_iw_nogil(context, options, _landmark_ordering(context), search.SearchCancellation())
    assert "transition_ordering_strategy" in str(error.value)

    other = _context()
    assert search.find_solution_iw_nogil(other, options, _landmark_ordering(other)).status == search.SearchStatus.SOLVED


def test_cancellation_is_accepted_with_the_iw1_accelerators():
    """Both IW(1) accelerators stay on the plain queued search path, so they do work with a shared
    control. Listing them next to the beam would refuse a combination the search supports, and they
    are exactly the two the motivating caller runs with."""
    grounded = _context()
    precheck = search.IWOptions()
    precheck.max_arity = 2
    precheck.iw1_precheck_add_effect_novelty = True
    assert search.find_solution_iw_nogil(grounded, precheck, search.SearchCancellation()).status == search.SearchStatus.SOLVED

    # `iw1_incremental_first_applicability` needs partial-binding completion, which only the lifted
    # applicable-action generator offers.
    lifted = _lifted_context()
    incremental = search.IWOptions()
    incremental.max_arity = 2
    incremental.iw1_incremental_first_applicability = True
    assert search.find_solution_iw_nogil(lifted, incremental, search.SearchCancellation()).status == search.SearchStatus.SOLVED
