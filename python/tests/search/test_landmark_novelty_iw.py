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
    """The iw1_* accelerators all query atom-level novelty, which LIW does not expose."""
    instance = _Instance("blocks_3")

    for set_option in [
        lambda o: setattr(o, "iw1_precheck_add_effect_novelty", True),
        lambda o: setattr(o, "iw1_atom_first_mode", True),
        lambda o: setattr(o, "iw1_incremental_first_applicability", True),
    ]:
        options = search.IWOptions()
        options.max_arity = 1
        options.iw_event_handler = search.DefaultIWEventHandler(instance.problem, True)
        options.landmark_novelty_graph = instance.landmarks
        set_option(options)

        with pytest.raises(Exception):
            search.find_solution_iw(instance.context, options)
