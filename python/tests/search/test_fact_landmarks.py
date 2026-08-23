import pymimir.advanced.formalism as formalism
import pymimir.advanced.search as search

from pathlib import Path

ROOT_DIR = (Path(__file__).parent.parent.parent.parent).absolute()


def _parse(domain_name: str, problem_filename: str = "test_problem.pddl"):
    domain_filepath = str(ROOT_DIR / "data" / domain_name / "domain.pddl")
    problem_filepath = str(ROOT_DIR / "data" / domain_name / problem_filename)
    parser = formalism.Parser(domain_filepath, formalism.ParserOptions())
    parser.get_domain()
    return parser.parse_problem(problem_filepath, formalism.ParserOptions())


def _atom_signature(atom):
    return (atom.get_predicate().get_name(), tuple(obj.get_name() for obj in atom.get_objects()))


def _action_signature(action):
    return (action.get_action().get_name(), tuple(obj.get_name() for obj in action.get_objects()))


def _find_landmark(landmarks, predicate_name: str, object_names):
    signature = (predicate_name, tuple(object_names))
    for atom in landmarks.get_landmark_atoms():
        if _atom_signature(atom) == signature:
            return atom
    return None


def _resolve_atom(landmarks, atom_index: int):
    return landmarks.get_problem().get_repositories().get_fluent_ground_atom(atom_index)


def _atom_signatures(landmarks, atom_indices):
    return {_atom_signature(_resolve_atom(landmarks, index)) for index in atom_indices}


def test_blocks4_goal_landmarks_and_unique_achiever():
    """ Python parity with tests/unit/search/landmarks/fact_landmarks.cpp
        (SearchLandmarksBlocks4GoalCoverageAndUniqueAchieverTest).
    """
    problem = _parse("blocks_4")
    grounder = search.LiftedGrounder(problem)
    landmarks = search.ApproximateFactLandmarkGenerator.create(grounder)

    # All 5 positive fluent goal atoms must be landmarks.
    for predicate_name, object_names in [("clear", ["b2"]),
                                         ("on", ["b2", "b3"]),
                                         ("on-table", ["b3"]),
                                         ("clear", ["b1"]),
                                         ("on-table", ["b1"])]:
        atom = _find_landmark(landmarks, predicate_name, object_names)
        assert atom is not None, f"{predicate_name}{object_names} is not a landmark"
        assert landmarks.is_landmark(atom)
        assert landmarks.is_landmark(atom.get_index())

    # `stack` is the only action schema that ever adds `on`, and only the (b2, b3) grounding adds
    # this specific atom, so it must be a unique achiever.
    on_b2_b3 = _find_landmark(landmarks, "on", ["b2", "b3"])
    achievers = landmarks.get_achievers(on_b2_b3.get_index())
    assert len(achievers) == 1
    assert _action_signature(achievers[0]) == ("stack", ("b2", "b3"))

    unique_achiever = landmarks.get_unique_achiever(on_b2_b3.get_index())
    assert unique_achiever is not None
    assert _action_signature(unique_achiever) == ("stack", ("b2", "b3"))
    assert landmarks.get_unique_achiever_action_index(on_b2_b3.get_index()) == unique_achiever.get_index()
    assert landmarks.is_unique_landmark_achiever(unique_achiever)
    assert landmarks.is_landmark_achiever(unique_achiever)
    assert landmarks.is_first_landmark_achiever(unique_achiever)
    assert on_b2_b3.get_index() in landmarks.get_landmarks_achieved_by_action(unique_achiever)
    assert on_b2_b3.get_index() in landmarks.get_landmarks_uniquely_achieved_by_action(unique_achiever)

    first_achievers = landmarks.get_first_achievers(on_b2_b3.get_index())
    assert len(first_achievers) == 1
    assert _action_signature(first_achievers[0]) == ("stack", ("b2", "b3"))

    # stack(b2, b3)'s only preconditions are (clear b3) and (holding b2): with a single achiever,
    # the necessary-predecessor intersection is exactly its own precondition set.
    predecessors = landmarks.get_predecessors(on_b2_b3.get_index())
    assert _atom_signatures(landmarks, predecessors) == {("clear", ("b3",)), ("holding", ("b2",))}

    # Edge symmetry: on(b2,b3) must appear among clear(b3)'s and holding(b2)'s successors.
    for predicate_name, object_names in [("clear", ["b3"]), ("holding", ["b2"])]:
        atom = _find_landmark(landmarks, predicate_name, object_names)
        assert atom is not None
        assert ("on", ("b2", "b3")) in _atom_signatures(landmarks, landmarks.get_successors(atom.get_index()))


def test_gripper_non_unique_achiever():
    """ Python parity with tests/unit/search/landmarks/fact_landmarks.cpp
        (SearchLandmarksGripperNonUniqueAchieverTest).
    """
    problem = _parse("gripper")
    grounder = search.LiftedGrounder(problem)
    landmarks = search.ApproximateFactLandmarkGenerator.create(grounder)

    at_ball2_roomb = _find_landmark(landmarks, "at", ["ball2", "roomb"])
    assert at_ball2_roomb is not None
    assert landmarks.is_landmark(at_ball2_roomb)

    # Two grippers can each carry ball2 into roomb: exactly two, non-unique, first achievers.
    achievers = landmarks.get_achievers(at_ball2_roomb.get_index())
    assert len(achievers) == 2
    assert {_action_signature(action) for action in achievers} == {("drop", ("ball2", "roomb", "left")),
                                                                   ("drop", ("ball2", "roomb", "right"))}

    assert len(landmarks.get_first_achievers(at_ball2_roomb.get_index())) == 2

    assert landmarks.get_unique_achiever_action_index(at_ball2_roomb.get_index()) is None
    assert landmarks.get_unique_achiever(at_ball2_roomb.get_index()) is None
    for action in achievers:
        assert not landmarks.is_unique_landmark_achiever(action)
        assert landmarks.is_landmark_achiever(action)

    # The necessary-precondition intersection must drop gripper identity: only the shared
    # (at-robby roomb) precondition survives, not either gripper's (carry ball2 ?).
    predecessors = landmarks.get_predecessors(at_ball2_roomb.get_index())
    assert _atom_signatures(landmarks, predecessors) == {("at-robby", ("roomb",))}


def test_greedy_necessary_orderings_disabled():
    """ Python parity with SearchLandmarksGreedyNecessaryOrderingsDisabledTest: discovery still runs
        to a fixpoint, only the ordering edges are suppressed.
    """
    problem = _parse("blocks_4")
    grounder = search.LiftedGrounder(problem)

    options = search.FactLandmarkGeneratorOptions()
    options.compute_greedy_necessary_orderings = False
    landmarks = search.ApproximateFactLandmarkGenerator.create(grounder, options)

    assert len(landmarks.get_landmark_atom_indices()) > 5
    for atom_index in landmarks.get_landmark_atom_indices():
        assert len(landmarks.get_predecessors(atom_index)) == 0
        assert len(landmarks.get_successors(atom_index)) == 0


def test_achieved_and_unachieved_landmarks_in_initial_state():
    """ Python parity with SearchLandmarksAchievedUnachievedInitialStateTest.
    """
    problem = _parse("blocks_4")
    grounder = search.LiftedGrounder(problem)
    landmarks = search.ApproximateFactLandmarkGenerator.create(grounder)

    search_context = search.SearchContext.create(problem, search.SearchContextOptions())
    initial_state = search_context.get_state_repository().get_or_create_initial_state()[0]

    achieved = landmarks.get_achieved_landmark_atom_indices(initial_state)
    unachieved = landmarks.get_unachieved_landmark_atom_indices(initial_state)

    assert len(achieved) + len(unachieved) == len(landmarks.get_landmark_atom_indices())

    achieved_signatures = _atom_signatures(landmarks, achieved)
    unachieved_signatures = _atom_signatures(landmarks, unachieved)
    assert ("clear", ("b2",)) in achieved_signatures
    assert ("clear", ("b1",)) in achieved_signatures
    assert ("on", ("b2", "b3")) in unachieved_signatures
    assert ("on-table", ("b1",)) in unachieved_signatures


def test_landmark_transition_ordering_options():
    problem = _parse("gripper")
    landmarks = search.ApproximateFactLandmarkGenerator.create(search.LiftedGrounder(problem))

    options = search.LandmarkTransitionOrderingOptions()
    assert options.prefer_new_landmarks
    assert options.prefer_unique_achievers
    assert options.prefer_landmark_actions_when_deleting
    assert options.prefer_fewer_deleted_landmarks
    options.prefer_fewer_deleted_landmarks = False

    ordering = search.LandmarkTransitionOrderingStrategy(landmarks, options)
    assert not ordering.get_options().prefer_fewer_deleted_landmarks
    assert len(ordering.get_landmarks().get_landmark_atom_indices()) == len(landmarks.get_landmark_atom_indices())


def test_iw_with_landmark_transition_ordering():
    """ End-to-end: IW with a landmark transition ordering. gripper's test_problem is not solvable at
        width 1, so max_arity=2 is used here (the ordering only applies to the width-1 pass).
    """
    problem = _parse("gripper")
    landmarks = search.ApproximateFactLandmarkGenerator.create(search.LiftedGrounder(problem))
    search_context = search.SearchContext.create(problem, search.SearchContextOptions())
    ordering = search.LandmarkTransitionOrderingStrategy(landmarks)

    iw_options = search.IWOptions()
    iw_options.max_arity = 2

    result = search.find_solution_iw(search_context, iw_options, ordering)

    assert result.status == search.SearchStatus.SOLVED
    assert len(result.plan) == 3


def test_brfs_with_landmark_transition_ordering():
    """ End-to-end: BrFS with a landmark transition ordering finds the same optimal plan length as
        the default (queued) transition order.
    """
    problem = _parse("delivery")
    landmarks = search.ApproximateFactLandmarkGenerator.create(search.LiftedGrounder(problem))
    search_context = search.SearchContext.create(problem, search.SearchContextOptions())
    ordering = search.LandmarkTransitionOrderingStrategy(landmarks)

    ordered_result = search.find_solution_brfs(search_context, search.BrFSOptions(), ordering)
    assert ordered_result.status == search.SearchStatus.SOLVED
    assert len(ordered_result.plan) == 4

    default_result = search.find_solution_brfs(search_context, search.BrFSOptions())
    assert default_result.status == search.SearchStatus.SOLVED
    assert len(default_result.plan) == 4
