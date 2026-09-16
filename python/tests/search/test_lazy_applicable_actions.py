import pymimir.advanced.formalism as formalism
import pymimir.advanced.search as search

import pytest

from pathlib import Path

ROOT_DIR = (Path(__file__).parent.parent.parent.parent).absolute()

GRIPPER = (ROOT_DIR / "data" / "gripper" / "domain.pddl", ROOT_DIR / "data" / "gripper" / "test_problem.pddl")
DEAD_END = (ROOT_DIR / "data" / "deadend" / "domain.pddl", ROOT_DIR / "data" / "deadend" / "test_problem.pddl")
LIVE = (ROOT_DIR / "data" / "deadend" / "domain.pddl", ROOT_DIR / "data" / "deadend" / "live_problem.pddl")

# Both generators, because they became lazy for different reasons: the lifted one already yielded
# from a coroutine and only the Python binding drained it, while the grounded one drained its match
# tree into a list in C++ before yielding anything.
MODES = [search.LiftedOptions, search.GroundedOptions]


def _context(files, mode):
    domain_filepath, problem_filepath = files
    problem = formalism.Problem.create(str(domain_filepath), str(problem_filepath), formalism.ParserOptions())
    context = search.SearchContext.create(problem, search.SearchContextOptions(mode()))
    state, _ = context.get_state_repository().get_or_create_initial_state()
    return context, state


@pytest.mark.parametrize("mode", MODES)
@pytest.mark.parametrize("files", [GRIPPER, DEAD_END, LIVE])
def test_lazy_iteration_matches_the_eager_list(files, mode):
    """The lazy iterator yields the same actions in the same order as the list."""
    context, state = _context(files, mode)
    generator = context.get_applicable_action_generator()

    eager = [action.get_index() for action in generator.generate_applicable_actions(state)]
    lazy = [action.get_index() for action in generator.create_applicable_action_generator(state)]

    assert lazy == eager


@pytest.mark.parametrize("mode", MODES)
@pytest.mark.parametrize("files, expected", [(GRIPPER, True), (DEAD_END, False), (LIVE, True)])
def test_has_applicable_action_agrees_with_enumeration(files, expected, mode):
    """The one-bit answer is the same bit the full enumeration would give."""
    context, state = _context(files, mode)
    generator = context.get_applicable_action_generator()

    assert generator.has_applicable_action(state) is expected
    assert (len(generator.generate_applicable_actions(state)) > 0) is expected


@pytest.mark.parametrize("mode", MODES)
def test_iterator_can_be_abandoned_after_one_action(mode):
    """Stopping early is the point, and it must not leave the problem in a broken state.

    A `GroundAction` is a raw pointer into the problem's interning table. If the binding let Python
    take ownership of one, this is where that would surface -- not here, but as a corrupt hash set
    when the problem is torn down at interpreter exit.
    """
    context, state = _context(GRIPPER, mode)
    generator = context.get_applicable_action_generator()

    iterator = generator.create_applicable_action_generator(state)
    first = next(iterator)
    del iterator

    assert first.get_index() == generator.generate_applicable_actions(state)[0].get_index()


@pytest.mark.parametrize("mode", MODES)
def test_iterator_is_its_own_iterator_and_stops(mode):
    context, state = _context(GRIPPER, mode)
    generator = context.get_applicable_action_generator()

    iterator = generator.create_applicable_action_generator(state)
    assert iter(iterator) is iterator

    consumed = list(iterator)
    assert len(consumed) == 6

    # Exhausted stays exhausted.
    with pytest.raises(StopIteration):
        next(iterator)


@pytest.mark.parametrize("mode", MODES)
def test_action_satisficing_binding_generator_honours_the_cap(mode):
    """`max_num_groundings` has to actually reach the generator.

    The binding's lambda used to declare its self parameter as
    `ConjunctiveConditionSatisficingBindingGenerator`. The satisficing binding generators are CRTP
    siblings rather than a hierarchy, so nanobind could not cast the instance and refused every
    call -- against a signature whose argument types looked like a match.
    """
    context, state = _context(GRIPPER, mode)
    problem = context.get_problem()

    capped_total = 0
    uncapped_total = 0
    for action in problem.get_domain().get_actions():
        binding_generator = search.ActionSatisficingBindingGenerator(action, problem)
        capped = binding_generator.generate_ground_conjunctions(state, 1)
        uncapped = binding_generator.generate_ground_conjunctions(state, 1_000_000)

        assert len(capped) == min(1, len(uncapped))
        capped_total += len(capped)
        uncapped_total += len(uncapped)

    assert 0 < capped_total < uncapped_total


@pytest.mark.parametrize("mode, supports", [(search.LiftedOptions, False), (search.GroundedOptions, True)])
def test_concurrency_support_is_declared(mode, supports):
    context, _ = _context(GRIPPER, mode)
    generator = context.get_applicable_action_generator()

    assert generator.supports_concurrent_applicable_action_generators() is supports


def test_second_enumeration_under_a_live_lifted_iterator_is_refused():
    """A wrong applicable set is the one failure the caller cannot see, so it is made loud.

    The lifted generators re-initialize their dynamic assignment sets and condition grounders at the
    start of every enumeration. Nothing noticed while enumerations were always drained immediately;
    a suspended lazy iterator is what makes it reachable.
    """
    context, state = _context(GRIPPER, search.LiftedOptions)
    generator = context.get_applicable_action_generator()

    iterator = generator.create_applicable_action_generator(state)
    next(iterator)

    for call in (
        lambda: generator.generate_applicable_actions(state),
        lambda: generator.create_applicable_action_generator(state),
        lambda: generator.has_applicable_action(state),
    ):
        with pytest.raises(RuntimeError, match="still alive"):
            call()

    # Dropping the iterator lifts the refusal; nothing is left registered.
    del iterator
    assert len(generator.generate_applicable_actions(state)) == 6


def test_grounded_generator_allows_concurrent_enumerations():
    context, state = _context(GRIPPER, search.GroundedOptions)
    generator = context.get_applicable_action_generator()

    outer = generator.create_applicable_action_generator(state)
    first = next(outer)

    # Both of these would raise on a lifted generator.
    assert len(generator.generate_applicable_actions(state)) == 6
    assert [a.get_index() for a in generator.create_applicable_action_generator(state)][0] == first.get_index()

    assert [first.get_index()] + [a.get_index() for a in outer] == [
        a.get_index() for a in generator.generate_applicable_actions(state)
    ]
