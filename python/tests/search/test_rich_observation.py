"""The rich half of native observation, from Python.

Transition logs with their disposition, novelty witnesses attached to the transition that produced
them, syntactic effect summaries, realized state deltas, native collection running alongside Python
callbacks, and per-arity IW observation.
"""

from pathlib import Path

import pytest

import pymimir as mm
from pymimir.advanced.search import GoalCountLayerOrderingStrategy

ROOT_DIR = (Path(__file__).parent.parent.parent.parent).absolute()

ALL_CAPTURES = dict(
    collect_statistics=True,
    capture_search_tree=True,
    capture_rejected_transitions=True,
    capture_novel_witnesses=True,
    capture_action_effect_summaries=True,
    capture_realized_effects=True,
)


def _make_problem(domain: str = "gripper", problem: str = "test_problem"):
    """A freshly parsed grounded problem.

    Two problems parsed from the same files do not agree on ground-action indices -- grounding breaks
    match-tree ties on pointer order -- so two runs meant to explore identically share one problem.
    """
    domain_obj = mm.Domain(str(ROOT_DIR / "data" / domain / "domain.pddl"))
    problem_obj = mm.Problem(
        domain_obj, str(ROOT_DIR / "data" / domain / f"{problem}.pddl"), mode="grounded"
    )
    return problem_obj, problem_obj.get_initial_state()


def _brfs_statistics(result, search):
    """The BrFS statistics of the pass the transitions came from."""
    if search is mm.iw:
        return result.observation.by_arity[-1].statistics
    return result.statistics


def _summed_counts(result, search):
    """Counts over every BrFS pass the search ran, which is what a callback observes."""
    passes = [entry.statistics for entry in result.observation.by_arity] if search is mm.iw else [result.statistics]
    return {
        "expanded": sum(s.get_num_expanded() for s in passes),
        "generated": sum(s.get_num_generated() for s in passes),
        "admitted": sum(s.get_num_generated_in_search_tree() for s in passes),
        "rejected": sum(s.get_num_generated_not_in_search_tree() for s in passes),
    }


WIDTH_SEARCHES = [
    (mm.brfs, {}),
    (mm.projective_iw, {}),
    (mm.abstracted_iw, {"width": 2}),
    (mm.iw, {"max_arity": 1}),
    (mm.iw, {"max_arity": 2}),
]


@pytest.mark.parametrize("search, kwargs", WIDTH_SEARCHES)
def test_transition_log_partitions_by_disposition(search, kwargs):
    problem, start_state = _make_problem()
    result = search(problem, start_state, **ALL_CAPTURES, **kwargs)

    transitions = result.transitions
    assert len(transitions) > 0

    admitted = [t for t in transitions if t.is_admitted]
    rejected = [t for t in transitions if not t.is_admitted]
    assert len(admitted) + len(rejected) == len(transitions)

    statistics = _brfs_statistics(result, search)
    assert len(admitted) == statistics.get_num_generated_in_search_tree()
    assert len(rejected) == statistics.get_num_generated_not_in_search_tree()
    assert len(transitions) == statistics.get_num_generated()

    for transition in transitions:
        assert transition.successor_depth == transition.parent_depth + 1
        assert (
            transition.disposition is mm.advanced.search.BrFSTransitionDisposition.ADMITTED
        ) == transition.is_admitted


@pytest.mark.parametrize("search, kwargs", WIDTH_SEARCHES)
def test_admitted_only_capture_keeps_no_rejected_records(search, kwargs):
    problem, start_state = _make_problem()
    result = search(problem, start_state, capture_transitions=True, collect_statistics=True, **kwargs)

    assert len(result.transitions) > 0
    assert all(t.is_admitted for t in result.transitions)
    statistics = _brfs_statistics(result, search)
    assert len(result.transitions) == statistics.get_num_generated_in_search_tree()
    # The rejections were counted even though they were not retained.
    assert statistics.get_num_generated_not_in_search_tree() > 0


def test_witnesses_match_the_novelty_witness_event():
    """The native witness of a transition is the one the witness event reports for it.

    Both observers watch the *same* run, composed: comparing two runs would compare two searches.
    """
    import pymimir.advanced.search as advanced

    problem, start_state = _make_problem()
    seen = {}

    class WitnessRecorder(advanced.IBrFSEventHandler):
        def supports_novel_witness_events(self):
            return True

        def on_generate_state_with_novel_witness(self, state, action, cost, successor, novel_atom_indices):
            seen[(state.get_index(), action.get_index(), successor.get_index())] = tuple(novel_atom_indices)

        def on_expand_state(self, state):
            pass

        def on_expand_goal_state(self, state):
            pass

        def on_generate_state(self, state, action, cost, successor):
            pass

        def on_generate_state_in_search_tree(self, state, action, cost, successor):
            pass

        def on_generate_state_not_in_search_tree(self, state, action, cost, successor):
            pass

        def on_finish_g_layer(self, value):
            pass

        def on_start_search(self, state):
            pass

        def on_end_search(self, a0, a1, a2, a3, a4, a5):
            pass

        def on_solved(self, plan):
            pass

        def on_unsolvable(self):
            pass

        def on_exhausted(self):
            pass

        def get_statistics(self):
            return advanced.BrFSStatistics()

    observation_options = advanced.BrFSObservationOptions()
    observation_options.capture_search_tree = True
    observation_options.capture_admitted_transitions = True
    observation_options.capture_rejected_transitions = True
    observation_options.capture_novel_witnesses = True

    native = advanced.ObservationBrFSEventHandler(problem._advanced_problem, observation_options)
    options = advanced.BrFSOptions()
    options.start_state = start_state._advanced_state
    options.event_handler = advanced.CompositeBrFSEventHandler([native, WitnessRecorder()], 0)
    options.pruning_strategy = advanced.AbstractedNoveltyPruningStrategy.create(
        problem._advanced_problem, 1, True, False, False
    )
    advanced.find_solution_brfs(problem._search_context, options)

    checked = 0
    for transition in native.observation.transitions:
        key = (transition.parent_state_index, transition.action_index, transition.successor_state_index)
        expected = seen.get(key)
        if transition.novel_fluent_atom_indices is None:
            assert expected is None
        else:
            assert tuple(transition.novel_fluent_atom_indices) == expected
            checked += 1
    assert checked > 0


@pytest.mark.parametrize("num_threads", [1, 4])
def test_witnesses_stay_with_their_transition_under_beam_search(num_threads):
    """A beam classifies a whole layer long after generating it, so witnesses and classifications
    interleave; each witness still has to end up on its own transition."""
    problem, start_state = _make_problem()
    result = mm.projective_iw(
        problem,
        start_state,
        layer_ordering_strategy=GoalCountLayerOrderingStrategy.create(problem._advanced_problem),
        beam_width=4,
        num_threads=num_threads,
        capture_rejected_transitions=True,
        capture_novel_witnesses=True,
        capture_realized_effects=True,
    )

    checked = 0
    for transition in result.transitions:
        if transition.novel_fluent_atom_indices is None:
            continue
        # A novel atom is one the transition actually brought about, so it must be in the realized
        # additions of that same transition -- which is only true if the witness landed on the right
        # record.
        realized = set(transition.realized_added_fluent_atom_indices)
        assert set(transition.novel_fluent_atom_indices) <= realized, transition
        checked += 1
    assert checked > 0


def test_unsupported_and_empty_witnesses_are_distinguishable():
    # Plain BrFS prunes duplicates and cannot answer a witness query at all.
    problem, start_state = _make_problem()
    unsupported = mm.brfs(problem, start_state, capture_rejected_transitions=True, capture_novel_witnesses=True)
    assert len(unsupported.transitions) > 0
    assert all(t.novel_fluent_atom_indices is None for t in unsupported.transitions)
    assert all(t.novelty_witness_count is None for t in unsupported.transitions)

    # Width-1 novelty pruning answers, and some answers are "nothing was novel".
    problem, start_state = _make_problem()
    supported = mm.projective_iw(problem, start_state, capture_rejected_transitions=True, capture_novel_witnesses=True)
    witnesses = [t.novel_fluent_atom_indices for t in supported.transitions if t.novel_fluent_atom_indices is not None]
    assert len(witnesses) > 0
    assert any(len(w) == 0 for w in witnesses), "expected a query that found nothing"
    assert any(len(w) > 0 for w in witnesses), "expected a query that found novel atoms"

    counts = [t.novelty_witness_count for t in supported.transitions if t.novel_fluent_atom_indices is not None]
    assert counts == [len(w) for w in witnesses]


def test_witnesses_are_not_computed_unless_requested():
    problem, start_state = _make_problem()
    without = mm.projective_iw(problem, start_state, capture_rejected_transitions=True)
    assert all(t.novel_fluent_atom_indices is None for t in without.transitions)


def test_effect_summaries_are_syntactic_and_cached():
    problem, start_state = _make_problem()
    result = mm.projective_iw(
        problem, start_state, capture_rejected_transitions=True, capture_action_effect_summaries=True
    )

    advanced_problem = problem._advanced_problem
    seen_actions = set()
    for transition in result.transitions:
        summary = transition.action_effect_summary
        assert summary is not None

        action = advanced_problem.get_ground_action(transition.action_index)
        expected_add = sum(
            len(list(effect.get_conjunctive_effect().get_positive_effects()))
            for effect in action.get_conditional_effects()
        )
        expected_delete = sum(
            len(list(effect.get_conjunctive_effect().get_negative_effects()))
            for effect in action.get_conditional_effects()
        )
        assert summary.num_add_effects == expected_add
        assert summary.num_delete_effects == expected_delete
        seen_actions.add(transition.action_index)

    # One cached summary per distinct action, not one per occurrence.
    observation = result._native_handlers[-1].observation
    assert observation.get_num_action_effect_summaries() == len(seen_actions)


def test_realized_effects_are_the_state_difference():
    problem, start_state = _make_problem()
    result = mm.projective_iw(
        problem,
        start_state,
        capture_rejected_transitions=True,
        capture_realized_effects=True,
        capture_action_effect_summaries=True,
    )

    state_repository = problem._search_context.get_state_repository()

    def fluent_atoms(state_index):
        state = state_repository.get_state(state_repository.get_packed_state(state_index))
        return set(state.get_fluent_atoms())

    checked = 0
    for transition in result.transitions:
        parent = fluent_atoms(transition.parent_state_index)
        successor = fluent_atoms(transition.successor_state_index)

        assert set(transition.realized_added_fluent_atom_indices) == successor - parent
        assert set(transition.realized_deleted_fluent_atom_indices) == parent - successor

        # Realized changes are bounded by what the action syntactically names: an add effect for an
        # atom that already held realizes nothing.
        summary = transition.action_effect_summary
        assert len(transition.realized_added_fluent_atom_indices) <= summary.num_add_effects
        assert len(transition.realized_deleted_fluent_atom_indices) <= summary.num_delete_effects
        checked += 1

    assert checked > 0


def test_realized_effects_are_absent_unless_requested():
    problem, start_state = _make_problem()
    result = mm.projective_iw(problem, start_state, capture_transitions=True)
    assert all(t.realized_added_fluent_atom_indices is None for t in result.transitions)
    assert all(t.action_effect_summary is None for t in result.transitions)


def test_aggregates_summarize_the_transition_log():
    problem, start_state = _make_problem()
    result = mm.projective_iw(problem, start_state, **ALL_CAPTURES)

    aggregates = result.compute_transition_aggregates()
    transitions = list(result.transitions)

    assert aggregates.num_transitions == len(transitions)
    assert aggregates.num_admitted + aggregates.num_rejected == aggregates.num_transitions
    assert aggregates.num_admitted == sum(1 for t in transitions if t.is_admitted)

    witnesses = [t.novel_fluent_atom_indices for t in transitions if t.novel_fluent_atom_indices is not None]
    assert aggregates.num_transitions_with_witness == len(witnesses)
    assert aggregates.total_witness_size == sum(len(w) for w in witnesses)
    assert aggregates.max_witness_size == max(len(w) for w in witnesses)
    assert aggregates.average_witness_size == pytest.approx(sum(len(w) for w in witnesses) / len(witnesses))

    assert sum(aggregates.num_admitted_by_depth) == aggregates.num_admitted
    assert sum(aggregates.num_rejected_by_depth) == aggregates.num_rejected

    admitted = [t for t in transitions if t.is_admitted]
    assert aggregates.total_add_effects == sum(t.action_effect_summary.num_add_effects for t in admitted)
    assert aggregates.max_delete_effects == max(t.action_effect_summary.num_delete_effects for t in admitted)
    assert aggregates.total_realized_added == sum(len(t.realized_added_fluent_atom_indices) for t in transitions)
    assert aggregates.total_realized_deleted == sum(len(t.realized_deleted_fluent_atom_indices) for t in transitions)


def test_aggregates_are_None_without_transition_capture():
    problem, start_state = _make_problem()
    assert mm.projective_iw(problem, start_state).compute_transition_aggregates() is None


@pytest.mark.parametrize("search, kwargs", WIDTH_SEARCHES)
def test_native_collection_runs_alongside_python_callbacks(search, kwargs):
    problem, start_state = _make_problem()

    counts = {"expanded": 0, "generated": 0, "admitted": 0, "rejected": 0}

    def counter(key):
        def callback(*_args):
            counts[key] += 1

        return callback

    result = search(
        problem,
        start_state,
        on_expand_state=counter("expanded"),
        on_generate_state=counter("generated"),
        on_generate_new_state=counter("admitted"),
        on_prune_state=counter("rejected"),
        **ALL_CAPTURES,
        **kwargs,
    )

    # The callbacks saw every event of every pass, and the native counters were not doubled by the
    # second observer.
    assert counts == _summed_counts(result, search)

    # ... and the native observation is there too, per pass.
    statistics = _brfs_statistics(result, search)
    assert len(result.transitions) == statistics.get_num_generated()
    assert len(result.search_tree) == statistics.get_num_generated_in_search_tree() + 1


def test_callbacks_alone_still_leave_native_observation_unset():
    problem, start_state = _make_problem()
    expanded = []
    result = mm.projective_iw(problem, start_state, on_expand_state=expanded.append)

    assert len(expanded) > 0
    assert result.statistics is None
    assert result.transitions is None
    assert result.search_tree is None


def test_multi_arity_iw_observes_each_pass_separately():
    problem, start_state = _make_problem()
    result = mm.iw(problem, start_state, max_arity=2, **ALL_CAPTURES)

    by_arity = result.observation.by_arity
    assert len(by_arity) == 3
    assert [entry.arity for entry in by_arity] == [0, 1, 2]

    for entry in by_arity:
        admitted = [t for t in entry.transitions if t.is_admitted]
        assert len(admitted) == entry.statistics.get_num_generated_in_search_tree()
        assert len(entry.transitions) == entry.statistics.get_num_generated()
        # Each pass indexes its own tree; node 0 is that pass's own root.
        if len(entry.search_tree) > 0:
            assert entry.search_tree.nodes[0].parent_index is None
            assert entry.search_tree.nodes[0].state_index == start_state._advanced_state.get_index()

    # The trees are not merged: a wider pass explores more, so it has its own node count.
    assert len(by_arity[2].search_tree) != len(by_arity[1].search_tree)


def test_optimized_iw1_keeps_the_width_zero_placeholder():
    problem, start_state = _make_problem()
    result = mm.iw(problem, start_state, max_arity=1, **ALL_CAPTURES)

    by_arity = result.observation.by_arity
    assert [entry.arity for entry in by_arity] == [0, 1]

    # The width-0 pass runs no search at all, and says so with empty data rather than by vanishing.
    assert len(by_arity[0].search_tree) == 0
    assert len(by_arity[0].transitions) == 0
    assert by_arity[0].statistics.get_num_generated() == 0

    assert len(by_arity[1].search_tree) > 0
    assert len(by_arity[1].transitions) > 0

    # The convenience views point at the pass that actually ran.
    assert len(result.search_tree) == len(by_arity[1].search_tree)
    assert len(result.transitions) == len(by_arity[1].transitions)


def test_partial_observation_survives_out_of_states():
    problem, start_state = _make_problem()
    result = mm.iw(problem, start_state, max_arity=2, max_num_states=3, **ALL_CAPTURES)

    assert result.status == "out_of_states"
    assert result.observation is not None
    assert len(result.observation.by_arity) > 0
    for entry in result.observation.by_arity:
        assert len(entry.transitions) <= entry.statistics.get_num_generated()


def test_path_reconstruction_materializes_only_on_call():
    problem, start_state = _make_problem()
    result = mm.brfs(problem, start_state, capture_search_tree=True)

    tree = result.search_tree
    assert tree.get_action_indices(0) == []
    assert tree.get_state_indices(0) == [start_state._advanced_state.get_index()]
    assert tree.get_actions(0) == []
    assert len(tree.get_states(0)) == 1

    for node_index in range(len(tree)):
        action_indices = tree.get_action_indices(node_index)
        state_indices = tree.get_state_indices(node_index)
        assert len(state_indices) == len(action_indices) + 1
        assert state_indices[0] == start_state._advanced_state.get_index()
        assert state_indices[-1] == tree.nodes[node_index].state_index
        assert len(action_indices) == tree.nodes[node_index].depth

    node_index = len(tree) - 1
    actions = tree.get_actions(node_index)
    states = tree.get_states(node_index)
    assert all(isinstance(action, mm.GroundAction) for action in actions)
    assert all(isinstance(state, mm.State) for state in states)
    assert [action._advanced_ground_action.get_index() for action in actions] == tree.get_action_indices(node_index)
    assert [state._advanced_state.get_index() for state in states] == tree.get_state_indices(node_index)


def test_path_reconstruction_rejects_unknown_nodes():
    problem, start_state = _make_problem()
    tree = mm.brfs(problem, start_state, capture_search_tree=True).search_tree

    with pytest.raises(IndexError):
        tree.get_action_indices(len(tree))
    with pytest.raises(IndexError):
        tree.get_state_indices(len(tree))


@pytest.mark.parametrize("num_threads", [1, 4])
def test_beam_and_parallel_beam_capture_transitions(num_threads):
    problem, start_state = _make_problem()
    result = mm.projective_iw(
        problem,
        start_state,
        layer_ordering_strategy=GoalCountLayerOrderingStrategy.create(problem._advanced_problem),
        beam_width=4,
        num_threads=num_threads,
        **ALL_CAPTURES,
    )

    statistics = result.statistics
    assert len(result.transitions) == statistics.get_num_generated()
    assert sum(1 for t in result.transitions if t.is_admitted) == statistics.get_num_generated_in_search_tree()
    assert len(result.search_tree) == statistics.get_num_generated_in_search_tree() + 1

    # Admitted records arrive in the order the beam finally admitted them, so a parent is always
    # recorded before its successors.
    admitted_states = {start_state._advanced_state.get_index()}
    for transition in result.transitions:
        if transition.is_admitted:
            assert transition.parent_state_index in admitted_states
            admitted_states.add(transition.successor_state_index)
