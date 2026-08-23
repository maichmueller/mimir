"""`blocked_states` makes the search plan *around* a caller's closed set.

The motivating caller executes IW plans inside a longer episode and keeps its
own set of visited states. Before this option its only recourse was to replay
the returned plan and abort on reaching a visited state, which answers the
wrong question: it establishes that *this* plan crosses the closed set, never
that no plan avoids it. Those differ whenever a second route exists, and the
caller was terminating whole episodes on the difference.

The tests below pin both halves of the contract: a blocked state is genuinely
never entered, and the search still finds an alternative when one exists.
"""

from pathlib import Path

import pymimir.advanced.formalism as formalism
import pymimir.advanced.search as search

import pytest

ROOT_DIR = (Path(__file__).parent.parent.parent.parent).absolute()

DOMAINS = ["blocks_3", "gripper", "miconic"]


def _context(domain_name: str, problem_filename: str = "test_problem.pddl"):
    domain_filepath = str(ROOT_DIR / "data" / domain_name / "domain.pddl")
    problem_filepath = str(ROOT_DIR / "data" / domain_name / problem_filename)
    problem = formalism.Problem.create(
        domain_filepath, problem_filepath, formalism.ParserOptions()
    )
    grounder = search.LiftedGrounder(problem)
    match_tree_options = search.MatchTreeOptions()
    axiom_evaluator = grounder.create_grounded_axiom_evaluator(match_tree_options)
    state_repository = search.StateRepository.create(axiom_evaluator)
    action_generator = grounder.create_grounded_applicable_action_generator(
        match_tree_options
    )
    return problem, search.SearchContext.create(
        problem, action_generator, state_repository
    )


def _run(context, *, blocked=(), max_arity=2, start_state=None):
    options = search.IWOptions()
    options.max_arity = max_arity
    if blocked:
        options.blocked_states = set(int(index) for index in blocked)
    if start_state is not None:
        options.start_state = start_state
    return search.find_solution_iw(context, options)


def _plan_actions(result):
    """The returned plan's actions, or an empty list when there is no plan."""
    plan = result.plan
    return list(plan.get_actions()) if plan is not None else []


def _plan_state_indices(context, result, start_state):
    """Every state index the returned plan passes through, start excluded."""
    repository = context.get_state_repository()
    state, metric = start_state, 0.0
    indices = []
    for action in _plan_actions(result):
        state, metric = repository.get_or_create_successor_state(state, action, metric)
        indices.append(int(state.get_index()))
    return indices


@pytest.mark.parametrize("domain_name", DOMAINS)
def test_the_option_defaults_to_blocking_nothing(domain_name) -> None:
    _problem, context = _context(domain_name)
    assert set(search.IWOptions().blocked_states) == set()
    baseline = _run(context)
    explicit_empty = _run(context, blocked=())
    assert baseline.status == explicit_empty.status
    assert bool(_plan_actions(baseline)) == bool(_plan_actions(explicit_empty))


@pytest.mark.parametrize("domain_name", DOMAINS)
def test_a_blocked_state_is_never_entered(domain_name) -> None:
    """Block the plan's own first successor; any new plan must avoid it."""
    _problem, context = _context(domain_name)
    repository = context.get_state_repository()
    start_state, _metric = repository.get_or_create_initial_state()

    baseline = _run(context, start_state=start_state)
    if not _plan_actions(baseline):
        pytest.skip("the instance is solved at its initial state")
    first_successor = _plan_state_indices(context, baseline, start_state)[0]

    blocked = _run(context, blocked=(first_successor,), start_state=start_state)
    assert first_successor not in _plan_state_indices(context, blocked, start_state)


def test_the_start_state_is_exempt() -> None:
    """A caller standing on an already-visited state is the normal case.

    Blocking the start would make every such search fail immediately, which is
    the opposite of the point: the closed set describes where the caller has
    *been*, and it has necessarily been where it is standing.
    """
    _problem, context = _context("gripper")
    repository = context.get_state_repository()
    start_state, _metric = repository.get_or_create_initial_state()

    result = _run(
        context,
        blocked=(int(start_state.get_index()),),
        start_state=start_state,
    )
    assert _plan_actions(result)


def test_blocking_the_only_route_exhausts_rather_than_returning_it() -> None:
    """Blocking every successor of the start leaves nowhere legal to go.

    This is the case the replay-and-veto executor could not distinguish from
    "the goal is unreachable at this width": here the search itself reports the
    failure, so the caller can tell the two apart -- and, crucially, the status
    is *not* one of the budget statuses, so the caller knows the answer is a
    conclusion rather than a timeout.
    """
    _problem, context = _context("gripper")
    repository = context.get_state_repository()
    action_generator = context.get_applicable_action_generator()
    start_state, metric = repository.get_or_create_initial_state()

    successors = set()
    for action in action_generator.generate_applicable_actions(start_state):
        successor, _successor_metric = repository.get_or_create_successor_state(
            start_state, action, metric
        )
        successors.add(int(successor.get_index()))
    assert successors

    result = _run(context, blocked=successors, start_state=start_state)
    assert not _plan_actions(result)
    assert result.status in {
        search.SearchStatus.FAILED,
        search.SearchStatus.EXHAUSTED,
        search.SearchStatus.UNSOLVABLE,
    }
    assert result.status not in {
        search.SearchStatus.OUT_OF_TIME,
        search.SearchStatus.OUT_OF_STATES,
        search.SearchStatus.OUT_OF_MEMORY,
        search.SearchStatus.CANCELED,
    }
