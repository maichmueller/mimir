import pymimir.advanced.formalism as formalism
import pymimir.advanced.search as search

import pytest

from pathlib import Path

ROOT_DIR = (Path(__file__).parent.parent.parent.parent).absolute()

# Instances on which plain IW(1) is not wide enough but LIW(1) is. Mirrors
# tests/unit/search/algorithms/iw/landmark_novelty.cpp.
WIDTH_GAP_DOMAINS = ["blocks_3", "gripper", "transport", "visitall", "satellite"]


class _Instance:
    """A grounded instance plus its landmark graphs, reused across configurations.

    Reuse is not an optimization: two Problem instances of the same PDDL can enumerate grounded
    actions in different orders, and under novelty pruning generation order decides which candidate
    claims a contested tuple. Expansion counts only compare meaningfully on one shared instance.
    """

    def __init__(self, domain_name: str, problem_filename: str = "test_problem.pddl"):
        domain_filepath = str(ROOT_DIR / "data" / domain_name / "domain.pddl")
        problem_filepath = str(ROOT_DIR / "data" / domain_name / problem_filename)
        self.problem = formalism.Problem.create(domain_filepath, problem_filepath, formalism.ParserOptions())

        grounder = search.LiftedGrounder(self.problem)
        match_tree_options = search.MatchTreeOptions()
        axiom_evaluator = grounder.create_grounded_axiom_evaluator(match_tree_options)
        state_repository = search.StateRepository.create(axiom_evaluator)
        action_generator = grounder.create_grounded_applicable_action_generator(match_tree_options)
        self.context = search.SearchContext.create(self.problem, action_generator, state_repository)

        self.landmarks = search.ApproximateFactLandmarkGenerator.create(grounder)

        # Goal seeding is the only seed source, so switching it off yields an empty landmark set.
        empty_options = search.FactLandmarkGeneratorOptions()
        empty_options.include_positive_goal_facts = False
        self.empty_landmarks = search.ApproximateFactLandmarkGenerator.create(grounder, empty_options)


def _run_iw(instance: _Instance, max_arity: int, landmark_graph=None):
    event_handler = search.DefaultIWEventHandler(instance.problem, True)

    options = search.IWOptions()
    options.max_arity = max_arity
    options.iw_event_handler = event_handler
    options.max_num_states = 500000
    if landmark_graph is not None:
        options.landmark_novelty_graph = landmark_graph

    result = search.find_solution_iw(instance.context, options)
    num_expanded = sum(s.get_num_expanded() for s in event_handler.get_statistics().get_brfs_statistics_by_arity())
    return result, num_expanded


@pytest.mark.parametrize("domain_name", WIDTH_GAP_DOMAINS)
def test_landmark_novelty_closes_the_width_one_gap(domain_name):
    """LIW(1) admits everything IW(1) admits and more, so on a width-2 problem it reaches a goal
    that IW(1) prunes away -- without paying for a full IW(2) pass."""
    instance = _Instance(domain_name)
    assert len(instance.landmarks.get_landmark_atom_indices()) > 0

    plain, _ = _run_iw(instance, 1)
    landmark, _ = _run_iw(instance, 1, instance.landmarks)

    assert plain.status == search.SearchStatus.FAILED
    assert landmark.status == search.SearchStatus.SOLVED
    assert len(landmark.plan.get_actions()) > 0


@pytest.mark.parametrize("domain_name", WIDTH_GAP_DOMAINS)
def test_landmark_novelty_without_landmarks_matches_plain_iw(domain_name):
    """With no landmarks every state falls back to the BOT coordinate, so the search must
    reproduce plain IW expansion for expansion.

    max_arity is 2 rather than 1 on purpose: at max_arity == 1 plain IW enables an extra root
    optimization that only the plain pruning strategy implements, which makes the two
    configurations different searches rather than different feature families.
    """
    instance = _Instance(domain_name)
    assert len(instance.empty_landmarks.get_landmark_atom_indices()) == 0

    plain, plain_expanded = _run_iw(instance, 2)
    degenerate, degenerate_expanded = _run_iw(instance, 2, instance.empty_landmarks)

    assert degenerate.status == plain.status
    assert degenerate_expanded == plain_expanded


def test_landmark_novelty_rejects_atom_level_accelerators():
    """Two of the iw1_* accelerators do not survive the move from atom-level novelty to
    (landmark, free tuple) pairs, for two different reasons.

    Atom-first mode filters actions by ``test_atom_novelty_read_only``, which is handed an atom
    index with no state and no transition attached -- there is nothing to quantify the landmark
    coordinate over, so no exact answer exists even in principle.

    Incremental first-applicability tests every ground action at most once in the whole search.
    That is sound under IW(1), where every atom of every generated state is marked, so a
    re-application can never add an unmarked atom. Under LIW it is not: the same action applied
    at a state with different coordinates can expose a pair nothing has marked.

    The add-effect precheck is deliberately absent from this list -- see the test below.
    """
    instance = _Instance("blocks_3")

    for set_option in [
        lambda o: setattr(o, "iw1_atom_first_mode", True),
        lambda o: setattr(o, "iw1_incremental_first_applicability", True),
        lambda o: setattr(o, "iw1_incremental_first_applicability_debug_crosscheck", True),
    ]:
        options = search.IWOptions()
        options.max_arity = 1
        options.iw_event_handler = search.DefaultIWEventHandler(instance.problem, True)
        options.landmark_novelty_graph = instance.landmarks
        set_option(options)

        with pytest.raises(Exception):
            search.find_solution_iw(instance.context, options)


@pytest.mark.parametrize("domain_name", WIDTH_GAP_DOMAINS)
def test_landmark_novelty_precheck_preserves_the_result(domain_name):
    """The add-effect precheck is exact for LIW, so switching it on may only remove work.

    Expansion counts legitimately drop -- pruning an action before generating its successor is
    the entire point -- but the verdict and the plan length must not move.
    """
    baseline = _run_iw(_Instance(domain_name), 1, _Instance(domain_name).landmarks)

    instance = _Instance(domain_name)
    options = search.IWOptions()
    options.max_arity = 1
    options.iw_event_handler = search.DefaultIWEventHandler(instance.problem, True)
    options.max_num_states = 500000
    options.landmark_novelty_graph = instance.landmarks
    options.iw1_precheck_add_effect_novelty = True
    accelerated = search.find_solution_iw(instance.context, options)

    assert accelerated.status == baseline[0].status
    if accelerated.status == search.SearchStatus.SOLVED:
        assert len(accelerated.plan.get_actions()) == len(baseline[0].plan.get_actions())


def test_landmark_novelty_pruning_strategy_is_bound():
    """`LandmarkNoveltyPruningStrategy` is what a caller driving `find_solution_brfs` with an
    explicit pruning strategy needs; without it LIW novelty is reachable only through the
    `find_solution_iw` ladder."""
    instance = _Instance("blocks_3")

    num_atoms = len(instance.problem.get_repositories().get_fluent_ground_atoms())
    strategy = search.LandmarkNoveltyPruningStrategy.create(instance.landmarks, 1, num_atoms)

    assert strategy.supports_action_add_effect_precheck()
    assert strategy.precheck_requires_delete_effects()
    assert strategy.supports_transition_novel_witness_query()
    # Landmark coordinates are not atom-level features; this one cannot be answered exactly.
    assert not strategy.supports_atom_novelty_query()

    options = search.BrFSOptions()
    options.pruning_strategy = strategy
    options.max_num_states = 500000
    result = search.find_solution_brfs(instance.context, options)
    assert result.status == search.SearchStatus.SOLVED


def test_all_private_landmark_mode_is_bound():
    """The first-class mode is available both on IW options and explicit groupings."""
    instance = _Instance("blocks_3")

    options = search.IWOptions()
    assert not options.landmark_novelty_all_private
    options.landmark_novelty_all_private = True
    assert options.landmark_novelty_all_private

    grouping = search.LandmarkGrouping()
    assert grouping.mode == search.LandmarkGroupingMode.SHARED
    grouping.mode = search.LandmarkGroupingMode.ALL_PRIVATE
    assert grouping.is_all_private()

    # `make_grouping` exposes the same mode without materializing a legacy IndexSet union.
    grouping = search.LandmarkNoveltyPruningStrategy.make_grouping(instance.landmarks, True, all_private=True)
    assert grouping.mode == search.LandmarkGroupingMode.ALL_PRIVATE
    assert grouping.is_all_private()


def test_landmark_novelty_precheck_builds_no_successor_state():
    """The precheck exists to decide whether a successor could survive pruning WITHOUT building
    it. The repository counts every construction entry point, staged ones included, so this stays
    honest through a refactor that reroutes via the staged API."""
    instance = _Instance("blocks_3")

    num_atoms = len(instance.problem.get_repositories().get_fluent_ground_atoms())
    strategy = search.LandmarkNoveltyPruningStrategy.create(instance.landmarks, 1, num_atoms)

    state_repository = instance.context.get_state_repository()
    state, _ = state_repository.get_or_create_initial_state()
    strategy.test_prune_initial_state(state)

    actions = list(instance.context.get_applicable_action_generator().generate_applicable_actions(state))
    assert actions

    before = state_repository.get_num_successor_state_constructions()
    for action in actions:
        add_atoms, del_atoms = state_repository.collect_action_change_effect_fluent_atom_indices(state, action)
        strategy.test_transition_novelty_from_add_effects(state, add_atoms, del_atoms)
    assert state_repository.get_num_successor_state_constructions() == before
