"""Native observation of the high-level width searches.

The point of these tests is that a callback-free `iw`/`projective_iw`/`abstracted_iw`/`brfs` run
never enters Python during the search, and that the counts it reports natively are the same ones a
callback run would have derived.
"""

from pathlib import Path

import pytest

import pymimir as mm
from pymimir.advanced.search import GoalCountLayerOrderingStrategy

ROOT_DIR = (Path(__file__).parent.parent.parent.parent).absolute()


def _make_problem():
    """A freshly parsed grounded problem.

    Two problems parsed from the same files do not agree on ground-action indices -- grounding breaks
    match-tree ties on pointer order -- so two runs that are meant to explore identically have to
    share one problem. Search nodes and novelty tables are per-run state, so a second search over a
    problem that already holds states behaves like the first.
    """
    domain = mm.Domain(str(ROOT_DIR / "data" / "gripper" / "domain.pddl"))
    problem = mm.Problem(
        domain, str(ROOT_DIR / "data" / "gripper" / "test_problem.pddl"), mode="grounded"
    )
    return problem, problem.get_initial_state()


def _callback_counts(search, problem, start_state, **kwargs):
    """Run `search` with one counting callback per event and return the tallies."""
    counts = {"generated": 0, "in_tree": 0, "not_in_tree": 0, "expanded": 0, "goal": 0}

    def counter(key):
        def callback(*_args):
            counts[key] += 1

        return callback

    result = search(
        problem,
        start_state,
        on_expand_state=counter("expanded"),
        on_expand_goal_state=counter("goal"),
        on_generate_state=counter("generated"),
        on_generate_new_state=counter("in_tree"),
        on_prune_state=counter("not_in_tree"),
        **kwargs,
    )
    return result, counts


def _native_counts(statistics):
    return {
        "generated": statistics.get_num_generated(),
        "in_tree": statistics.get_num_generated_in_search_tree(),
        "not_in_tree": statistics.get_num_generated_not_in_search_tree(),
        "expanded": statistics.get_num_expanded(),
        "goal": statistics.get_num_expanded_goal_states(),
    }


@pytest.mark.parametrize(
    "search, kwargs",
    [
        (mm.brfs, {}),
        (mm.projective_iw, {}),
        (mm.abstracted_iw, {"width": 2}),
        (mm.iw, {"max_arity": 1}),
    ],
)
def test_native_counts_match_callback_counts(search, kwargs):
    problem, start_state = _make_problem()
    callback_result, callback_counts = _callback_counts(search, problem, start_state, **kwargs)
    native_result = search(problem, start_state, collect_statistics=True, **kwargs)

    assert callback_result.status == native_result.status

    statistics = native_result.statistics
    if search is mm.iw:
        # IW keeps one BrFS snapshot per attempted arity; the callbacks saw the last pass.
        statistics = statistics.get_brfs_statistics_by_arity()[-1]

    assert _native_counts(statistics) == callback_counts


@pytest.mark.parametrize(
    "search, kwargs",
    [
        (mm.brfs, {}),
        (mm.projective_iw, {}),
        (mm.abstracted_iw, {"width": 2}),
    ],
)
def test_generated_partitions_into_admitted_and_rejected(search, kwargs):
    problem, start_state = _make_problem()
    result = search(problem, start_state, collect_statistics=True, **kwargs)

    statistics = result.statistics
    assert statistics.get_num_generated() > 0
    assert statistics.get_num_generated() == (
        statistics.get_num_generated_in_search_tree()
        + statistics.get_num_generated_not_in_search_tree()
    )


def test_no_python_event_handler_is_installed_without_callbacks(monkeypatch):
    """A callback-free run must not construct the wrapper's Python event-handler subclass.

    Instantiating it is what puts a Python frame on every expansion and every generated transition,
    so the check is that `IBrFSEventHandler.__init__` is never reached.
    """
    import pymimir.wrapper_search_width as wrapper

    constructed = []

    original_init = wrapper.AdvancedBrFSEventHandler.__init__

    def tracking_init(self, *args, **kwargs):
        constructed.append(type(self).__name__)
        return original_init(self, *args, **kwargs)

    monkeypatch.setattr(wrapper.AdvancedBrFSEventHandler, "__init__", tracking_init)

    problem, start_state = _make_problem()
    mm.iw(problem, start_state, max_arity=1, collect_statistics=True)
    mm.projective_iw(problem, start_state, collect_statistics=True)
    assert constructed == []

    # ... whereas a run that asks for a callback does install one.
    mm.projective_iw(problem, start_state, on_expand_state=lambda _state: None)
    assert constructed != []


def test_statistics_are_absent_unless_requested():
    problem, start_state = _make_problem()

    result = mm.projective_iw(problem, start_state)
    assert result.statistics is None
    assert result.search_tree is None


def test_per_layer_counts_are_available_without_callbacks():
    problem, start_state = _make_problem()
    result = mm.brfs(problem, start_state, stop_if_goal=False, collect_statistics=True)

    statistics = result.statistics
    g_values = statistics.get_finished_g_values()
    generated = statistics.get_num_generated_until_g_value()
    admitted = statistics.get_num_generated_in_search_tree_until_g_value()
    rejected = statistics.get_num_generated_not_in_search_tree_until_g_value()
    expanded = statistics.get_num_expanded_until_g_value()
    goal_states = statistics.get_num_expanded_goal_states_until_g_value()

    assert len(g_values) > 0
    assert (
        len(generated)
        == len(admitted)
        == len(rejected)
        == len(expanded)
        == len(goal_states)
        == len(g_values)
    )

    for layer, (total, kept, dropped) in enumerate(zip(generated, admitted, rejected)):
        assert total == kept + dropped, f"layer {layer}"

    # Cumulative, so each layer is a prefix total of the next.
    assert generated == sorted(generated)
    assert admitted == sorted(admitted)
    assert rejected == sorted(rejected)


def test_reached_atom_and_repository_counters_are_bound():
    problem, start_state = _make_problem()
    statistics = mm.brfs(problem, start_state, collect_statistics=True).statistics

    assert statistics.get_num_reached_fluent_atoms() > 0
    assert statistics.get_num_reached_derived_atoms() >= 0
    assert statistics.get_num_states() > 0
    assert statistics.get_num_nodes() > 0
    assert statistics.get_num_actions() > 0
    assert statistics.get_num_axioms() >= 0

    incremental = statistics.get_iw1_incremental_first_applicability_statistics()
    assert incremental.get_num_root_actions_fully_enumerated() >= 0
    assert incremental.get_trigger_lookup_time_ms() >= 0.0


@pytest.mark.parametrize(
    "search, kwargs",
    [
        (mm.brfs, {}),
        (mm.projective_iw, {}),
        (mm.abstracted_iw, {"width": 2}),
        (mm.iw, {"max_arity": 1}),
    ],
)
def test_search_tree_is_consistent_and_reconstructs_action_paths(search, kwargs):
    problem, start_state = _make_problem()
    result = search(problem, start_state, capture_search_tree=True, **kwargs)

    tree = result.search_tree
    nodes = tree.nodes
    assert len(nodes) == len(tree)
    assert len(nodes) >= 1

    root = nodes[0]
    assert root.state_index == start_state._advanced_state.get_index()
    assert root.parent_index is None
    assert root.incoming_action_index is None
    assert root.depth == 0

    for index, node in enumerate(nodes[1:], start=1):
        assert node.parent_index is not None
        # A parent is always admitted before its successor, so it is earlier in the list.
        assert node.parent_index < index
        assert node.depth == nodes[node.parent_index].depth + 1
        assert node.incoming_action_index is not None

        action_path = tree.extract_action_path(index)
        assert len(action_path) == node.depth
        assert action_path[-1] == node.incoming_action_index

        state_path = tree.extract_state_path(index)
        assert state_path[0] == root.state_index
        assert state_path[-1] == node.state_index

        assert tree.find_node_by_state(node.state_index) == index

    # Capturing the tree implies collecting statistics, and the two agree on how many states were
    # admitted.
    assert result.statistics is not None
    statistics = result.statistics
    if search is mm.iw:
        statistics = statistics.get_brfs_statistics_by_arity()[-1]
    assert statistics.get_num_generated_in_search_tree() == len(nodes) - 1


def test_search_tree_action_path_matches_the_returned_plan():
    problem, start_state = _make_problem()
    result = mm.brfs(problem, start_state, capture_search_tree=True)

    assert result.status == "solved"
    goal_node = result.search_tree.find_node_by_state(
        result.goal_state._advanced_state.get_index()
    )
    assert goal_node is not None

    action_indices = result.search_tree.extract_action_path(goal_node)
    assert action_indices == [action._advanced_ground_action.get_index() for action in result.solution]


def test_search_tree_indices_materialize_into_states_and_actions():
    """Indices are resolvable back into real objects after the search, not only during it."""
    problem, start_state = _make_problem()
    result = mm.brfs(problem, start_state, capture_search_tree=True)

    state_repository = problem._search_context.get_state_repository()
    advanced_problem = problem._advanced_problem

    node = result.search_tree.nodes[1]
    state = state_repository.get_state(state_repository.get_packed_state(node.state_index))
    assert state.get_index() == node.state_index

    action = advanced_problem.get_ground_action(node.incoming_action_index)
    assert action.get_index() == node.incoming_action_index


@pytest.mark.parametrize("num_threads", [1, 4])
def test_beam_and_parallel_beam_report_native_counts(num_threads):
    problem, start_state = _make_problem()
    result = mm.projective_iw(
        problem,
        start_state,
        layer_ordering_strategy=GoalCountLayerOrderingStrategy.create(problem._advanced_problem),
        beam_width=3,
        num_threads=num_threads,
        collect_statistics=True,
        capture_search_tree=True,
    )

    statistics = result.statistics
    assert statistics.get_num_generated() > 0
    assert statistics.get_num_generated() == (
        statistics.get_num_generated_in_search_tree()
        + statistics.get_num_generated_not_in_search_tree()
    )
    assert statistics.get_num_generated_in_search_tree() == len(result.search_tree) - 1


@pytest.mark.parametrize("num_threads", [1, 4])
def test_survivors_only_beam_counts_partition(num_threads):
    """SURVIVORS_ONLY defers admission to a replay, so rejections arrive from two places.

    Only the partition is asserted: with several threads the beam's tie-breaking is not reproducible
    across runs, so absolute counts are not either.
    """
    problem, start_state = _make_problem()
    result = mm.projective_iw(
        problem,
        start_state,
        layer_ordering_strategy=GoalCountLayerOrderingStrategy.create(problem._advanced_problem),
        beam_width=3,
        beam_novelty_mode="survivors_only",
        num_threads=num_threads,
        collect_statistics=True,
    )

    statistics = result.statistics
    assert statistics.get_num_generated() > 0
    assert statistics.get_num_generated_not_in_search_tree() > 0
    assert statistics.get_num_generated() == (
        statistics.get_num_generated_in_search_tree()
        + statistics.get_num_generated_not_in_search_tree()
    )


def test_native_observation_is_rejected_together_with_callbacks():
    problem, start_state = _make_problem()

    with pytest.raises(ValueError, match="cannot be combined with per-event Python callbacks"):
        mm.iw(
            problem,
            start_state,
            max_arity=1,
            collect_statistics=True,
            on_expand_state=lambda _state: None,
        )

    with pytest.raises(ValueError, match="cannot be combined with per-event Python callbacks"):
        mm.projective_iw(
            problem,
            start_state,
            capture_search_tree=True,
            on_prune_state=lambda *_args: None,
        )

    with pytest.raises(ValueError, match="cannot be combined with per-event Python callbacks"):
        mm.brfs(
            problem,
            start_state,
            collect_statistics=True,
            on_finish_g_layer=lambda _value: None,
        )


def test_multi_arity_tree_capture_is_rejected_rather_than_truncated():
    problem, start_state = _make_problem()

    with pytest.raises(ValueError, match="capture_search_tree requires max_arity=1"):
        mm.iw(problem, start_state, max_arity=2, capture_search_tree=True)


def test_multi_arity_statistics_keep_one_snapshot_per_arity():
    problem, start_state = _make_problem()
    result = mm.iw(problem, start_state, max_arity=2, collect_statistics=True)

    per_arity = result.statistics.get_brfs_statistics_by_arity()
    # One entry per attempted width, including the width-0 placeholder.
    assert len(per_arity) >= 2
    assert result.statistics.get_effective_width() == len(per_arity) - 1


def test_callbacks_still_work_and_leave_observation_unset():
    """The callback API is unchanged for callers that were already using it."""
    problem, start_state = _make_problem()
    expanded = []

    result = mm.projective_iw(
        problem, start_state, on_expand_state=lambda state: expanded.append(state)
    )

    assert len(expanded) > 0
    assert all(isinstance(state, mm.State) for state in expanded)
    assert result.statistics is None
    assert result.search_tree is None
