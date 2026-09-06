import pytest

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


def _find_lifted(landmarks, rendered: str):
    for record in landmarks.get_lifted_landmarks():
        if str(record) == rendered:
            return record
    return None


def _sets_over_predicate(landmarks, predicate_name: str):
    return [members for members in landmarks.get_disjunctive_landmarks()
            if members and _resolve_atom(landmarks, members[0]).get_predicate().get_name() == predicate_name]


def test_blocks4_chain_matches_the_grounded_generator():
    """ Python parity with tests/unit/search/landmarks/lifted_fact_landmarks.cpp
        (SearchLandmarksLiftedBlocks4Test).
    """
    problem = _parse("blocks_4")
    landmarks = search.LiftedFactLandmarkGenerator.create(problem)

    for predicate_name, object_names in [("clear", ["b2"]),
                                         ("on", ["b2", "b3"]),
                                         ("on-table", ["b3"]),
                                         ("clear", ["b1"]),
                                         ("on-table", ["b1"])]:
        assert _find_landmark(landmarks, predicate_name, object_names) is not None

    # `stack` is the only schema adding `on`, so on(b2,b3)'s whole precondition set is necessary.
    on_b2_b3 = _find_landmark(landmarks, "on", ["b2", "b3"])
    predecessors = landmarks.get_predecessors(on_b2_b3.get_index())
    assert _atom_signatures(landmarks, predecessors) == {("clear", ("b3",)), ("holding", ("b2",))}

    # This used to pin the opposite: on-table(b2) and on(b1,b3) were the grounded generator's
    # h_max surplus. Since the §2.7 self-dependent-precondition rule they are found soundly, and
    # the two generators agree exactly on this instance.
    assert _find_landmark(landmarks, "on-table", ["b2"]) is not None
    assert _find_landmark(landmarks, "on", ["b1", "b3"]) is not None

    grounded = search.ApproximateFactLandmarkGenerator.create(search.LiftedGrounder(problem))
    assert {_atom_signature(a) for a in landmarks.get_landmark_atoms()} == {
        _atom_signature(a) for a in grounded.get_landmark_atoms()
    }


def test_gripper_disjunctive_carry_matches_the_grounded_set():
    """ Python parity with SearchLandmarksLiftedGripperTest.
    """
    problem = _parse("gripper")
    landmarks = search.LiftedFactLandmarkGenerator.create(problem)

    at_ball2_roomb = _find_landmark(landmarks, "at", ["ball2", "roomb"])
    assert at_ball2_roomb is not None
    assert _atom_signatures(landmarks, landmarks.get_predecessors(at_ball2_roomb.get_index())) == {("at-robby", ("roomb",))}

    carry_sets = _sets_over_predicate(landmarks, "carry")
    assert len(carry_sets) == 1
    assert _atom_signatures(landmarks, carry_sets[0]) == {("carry", ("ball2", "left")), ("carry", ("ball2", "right"))}

    grounded_options = search.FactLandmarkGeneratorOptions()
    grounded_options.max_disjunctive_landmark_size = 4
    grounded = search.ApproximateFactLandmarkGenerator.create(search.LiftedGrounder(problem), grounded_options)
    grounded_carry_sets = _sets_over_predicate(grounded, "carry")
    assert len(grounded_carry_sets) == 1
    assert list(grounded_carry_sets[0]) == list(carry_sets[0])


def test_childsnack_static_disambiguation():
    """ Python parity with SearchLandmarksLiftedChildsnackTest.
    """
    problem = _parse("childsnack")
    landmarks = search.LiftedFactLandmarkGenerator.create(problem)

    # child1 is not allergic: `serve_sandwich_no_gluten` is statically impossible here.
    assert _sets_over_predicate(landmarks, "no_gluten_sandwich") == []
    assert _find_landmark(landmarks, "served", ["child1"]) is not None
    assert _find_landmark(landmarks, "ontray", ["sandw1", "tray1"]) is not None
    assert _find_lifted(landmarks, "at(tray1, kitchen)").is_initially_true()

    ipc = _parse("landmark_ipc_smallest/childsnack-ipc", "p69.pddl")
    ipc_landmarks = search.LiftedFactLandmarkGenerator.create(ipc)

    # Eight children, four allergic, two trays, three tables: the fact landmarks are exactly the
    # goal atoms and everything below them stays partial.
    assert len(ipc_landmarks.get_landmark_atom_indices()) == 8

    no_gluten = _find_lifted(ipc_landmarks, "no_gluten_sandwich(?)")
    assert no_gluten is not None
    records = ipc_landmarks.get_lifted_landmarks()
    parents = {str(records[position]) for position in no_gluten.get_parent_positions()}
    assert "served(child2)" in parents      # allergic
    assert "served(child1)" not in parents  # not allergic

    at_table1 = _find_lifted(ipc_landmarks, "at(?, table1)")
    assert _atom_signatures(ipc_landmarks, at_table1.get_member_atom_indices()) == {("at", ("tray1", "table1")),
                                                                                    ("at", ("tray2", "table1"))}

    ontray = _find_lifted(ipc_landmarks, "ontray(?, ?)")
    assert len(ontray.get_member_atom_indices()) == 22  # 11 sandwiches x 2 trays
    assert len(_sets_over_predicate(ipc_landmarks, "ontray")) == 1

    at_kitchen_sandwich = _find_lifted(ipc_landmarks, "at_kitchen_sandwich(?)")
    assert [str(records[p]) for p in at_kitchen_sandwich.get_parent_positions()] == ["ontray(?, ?)"]

    # at(?, kitchen) is recorded, initially true, and nothing is derived through it.
    at_kitchen = _find_lifted(ipc_landmarks, "at(?, kitchen)")
    assert at_kitchen.is_initially_true()
    position = [str(record) for record in records].index("at(?, kitchen)")
    for record in records:
        assert position not in record.get_parent_positions()


def test_graph_contract_without_an_achiever_index():
    """ Python parity with SearchLandmarksLiftedGraphContractTest.
    """
    problem = _parse("gripper")
    landmarks = search.LiftedFactLandmarkGenerator.create(problem)

    assert not landmarks.has_achiever_index()

    some_landmark = landmarks.get_landmark_atom_indices()[0]
    for call in [lambda: landmarks.get_achiever_action_indices(some_landmark),
                 lambda: landmarks.get_achievers(some_landmark),
                 lambda: landmarks.get_first_achiever_action_indices(some_landmark),
                 lambda: landmarks.get_first_achievers(some_landmark),
                 lambda: landmarks.get_unique_achiever_action_index(some_landmark),
                 lambda: landmarks.get_unique_achiever(some_landmark),
                 lambda: search.LandmarkTransitionOrderingStrategy(landmarks)]:
        with pytest.raises(Exception):
            call()

    grounded = search.ApproximateFactLandmarkGenerator.create(search.LiftedGrounder(problem))
    assert grounded.has_achiever_index()
    assert grounded.get_lifted_landmarks() == []
    grounded.get_achiever_action_indices(grounded.get_landmark_atom_indices()[0])
    search.LandmarkTransitionOrderingStrategy(grounded)

    union_of_members = set()
    seen = set()
    for members in landmarks.get_disjunctive_landmarks():
        assert members
        assert list(members) == sorted(set(members))
        assert tuple(members) not in seen
        seen.add(tuple(members))
        for member in members:
            assert not landmarks.is_landmark(member)
        union_of_members.update(members)
    assert set(landmarks.get_disjunctive_landmark_atom_indices()) == union_of_members

    for atom_index in landmarks.get_landmark_atom_indices():
        for predecessor in landmarks.get_predecessors(atom_index):
            assert landmarks.is_landmark(predecessor)
            assert atom_index in landmarks.get_successors(predecessor)

    # A lifted problem interns atoms during search, so an index past the graph's range is a
    # legitimate question with the answer "no edges".
    beyond = max(landmarks.get_landmark_atom_indices()) + 100000
    assert landmarks.get_predecessors(beyond) == []
    assert landmarks.get_successors(beyond) == []
    assert not landmarks.is_landmark(beyond)


def test_extraction_is_deterministic():
    """ Python parity with SearchLandmarksLiftedDeterministicOrderTest: subsumption makes the rule
        order-sensitive, so the derived-predicate order has to be defined rather than hashed.
    """
    def render(landmarks):
        lines = []
        for record in landmarks.get_lifted_landmarks():
            members = sorted(_atom_signature(_resolve_atom(landmarks, i)) for i in record.get_member_atom_indices())
            lines.append(f"{record} {record.is_initially_true()} {members} {list(record.get_parent_positions())}")
        return "\n".join(lines)

    for domain, instance in [("childsnack", "test_problem.pddl"),
                             ("landmark_ipc_smallest/childsnack-ipc", "p69.pddl"),
                             ("landmark_ipc_smallest/rovers-ipc", "p69.pddl")]:
        first = render(search.LiftedFactLandmarkGenerator.create(_parse(domain, instance)))
        second = render(search.LiftedFactLandmarkGenerator.create(_parse(domain, instance)))
        assert first == second, domain


def test_lifted_landmark_rendering_and_options():
    problem = _parse("gripper")
    landmarks = search.LiftedFactLandmarkGenerator.create(problem)

    carry = _find_lifted(landmarks, "carry(ball2, ?)")
    assert carry is not None
    assert repr(carry) == "carry(ball2, ?)"
    assert carry.get_predicate().get_name() == "carry"
    binding = carry.get_binding()
    assert len(binding) == 2
    assert binding[0].get_name() == "ball2"
    assert binding[1] is None  # a free position is None, not a sentinel object
    assert carry.get_fact_atom_index() is None
    assert len(carry.get_member_atom_indices()) == 2

    at_ball2_roomb = _find_lifted(landmarks, "at(ball2, roomb)")
    assert at_ball2_roomb.get_fact_atom_index() == _find_landmark(landmarks, "at", ["ball2", "roomb"]).get_index()

    options = search.LiftedFactLandmarkGeneratorOptions()
    assert options.include_positive_goal_facts
    assert options.compute_greedy_necessary_orderings
    assert options.use_static_filter
    assert options.max_occurrence_combinations == 64
    assert options.max_disjunctive_members == 0  # 0 is UNCAPPED on this generator
    assert not hasattr(options, "promote_singleton_disjunctions")  # promotion is a theorem, not an option

    # A set over the cap is dropped, never truncated.
    options.max_disjunctive_members = 1
    capped = search.LiftedFactLandmarkGenerator.create(problem, options)
    assert _sets_over_predicate(capped, "carry") == []

    # Goal seeding is the only seed source.
    options = search.LiftedFactLandmarkGeneratorOptions()
    options.include_positive_goal_facts = False
    assert search.LiftedFactLandmarkGenerator.create(problem, options).get_landmark_atom_indices() == []


def test_a_predicate_no_schema_adds_keeps_only_its_initial_atoms():
    """ Python parity with SearchLandmarksLiftedUnaddablePredicateTest.

        miconic's `origin` is fluent because `board` deletes it, and nothing adds it, so every
        instance beyond the initial ones is unreachable by construction.
    """
    problem = _parse("landmark_ipc_smallest/miconic-ipc-hard", "p30-hard.pddl")
    initial_origin = {
        str(atom)
        for atom in problem.get_fluent_initial_atoms()
        if atom.get_predicate().get_name() == "origin"
    }
    assert initial_origin

    grounder = search.LiftedGrounder(problem)
    assert grounder.create_ground_actions()
    repositories = problem.get_repositories()
    atoms_after_grounding = len(repositories.get_fluent_ground_atoms())

    landmarks = search.LiftedFactLandmarkGenerator.create(problem)

    assert _sets_over_predicate(landmarks, "origin") == []
    for record in landmarks.get_lifted_landmarks():
        if record.get_predicate().get_name() != "origin":
            continue
        members = {
            str(_resolve_atom(landmarks, index))
            for index in record.get_member_atom_indices()
        }
        assert members <= initial_origin, f"{record} keeps unreachable members"
        assert record.is_initially_true()

    # Before the filter this instance interned 96,226 atoms against the grounder's 1,651.
    atoms_after_extraction = len(repositories.get_fluent_ground_atoms())
    assert atoms_after_extraction <= atoms_after_grounding + 16


def test_a_type_gated_adder_excludes_the_wrong_objects():
    """ Python parity with SearchLandmarksLiftedTypeGatedAdderTest.

        spanner's `at` is added only by `walk`, which binds a `?m - man`, so no spanner and no nut
        can ever be `at` anywhere it did not start.
    """
    problem = _parse("spanner", "p30-hard.pddl")
    landmarks = search.LiftedFactLandmarkGenerator.create(problem)

    def type_names(obj):
        return {base.get_name() for base in obj.get_bases()}

    # Asked of every `at` atom the graph names, fact or member: §2.7 turned spanner's `at` sets into
    # fact landmarks, so a members-only check would now pass vacuously. An `at` naming a spanner or
    # a nut is admissible only where the instance put it, since nothing can move one.
    initial = {str(atom) for atom in problem.get_fluent_initial_atoms()}
    atom_indices = list(landmarks.get_landmark_atom_indices())
    for members in landmarks.get_disjunctive_landmarks():
        atom_indices.extend(members)

    seen_at_atoms = 0
    for index in atom_indices:
        atom = _resolve_atom(landmarks, index)
        if atom.get_predicate().get_name() != "at":
            continue
        seen_at_atoms += 1
        located = atom.get_objects()[0]
        if type_names(located) & {"spanner", "nut"}:
            assert str(atom) in initial, str(atom)
    assert seen_at_atoms > 0, "no `at` atom at all: the test would pass vacuously"


def test_a_self_dependent_precondition_recovers_the_blocks_chain():
    """ Python parity with SearchLandmarksLiftedSelfDependentPrecondition* (blocks half).

        `clear(b3)`'s `putdown`/`stack` achievers need `holding(b3)`, every adder of which needs
        `clear(b3)` itself, so only `unstack(?x, b3)` can be first -- and `on(?x, b3)` has to be an
        initial atom, which binds ?x to the block actually on b3.
    """
    problem = _parse("blocks_4")
    landmarks = search.LiftedFactLandmarkGenerator.create(problem)

    clear_b3 = _find_landmark(landmarks, "clear", ["b3"])
    assert clear_b3 is not None
    predecessors = _atom_signatures(landmarks, landmarks.get_predecessors(clear_b3.get_index()))
    assert ("on", ("b1", "b3")) in predecessors
    assert ("clear", ("b1",)) in predecessors
    assert ("arm-empty", ()) in predecessors

    # `holding(b2)`: `unstack(b2, ?y)` is dropped because b2 is on the table in I, leaving `pickup`
    # as the only possible first achiever and its whole precondition set necessary.
    holding_b2 = _find_landmark(landmarks, "holding", ["b2"])
    assert holding_b2 is not None
    assert ("on-table", ("b2",)) in _atom_signatures(
        landmarks, landmarks.get_predecessors(holding_b2.get_index())
    )

    # The two generators now agree exactly on this instance.
    grounded = search.ApproximateFactLandmarkGenerator.create(search.LiftedGrounder(problem))
    assert {_atom_signature(a) for a in landmarks.get_landmark_atoms()} == {
        _atom_signature(a) for a in grounded.get_landmark_atoms()
    }


def test_a_self_dependent_precondition_fixes_the_ferry_location():
    """ Python parity with SearchLandmarksLiftedSelfDependentPreconditionFerryTest. """
    problem = _parse("ferry")
    landmarks = search.LiftedFactLandmarkGenerator.create(problem)

    on_car1 = _find_landmark(landmarks, "on", ["car1"])
    assert on_car1 is not None
    predecessors = _atom_signatures(landmarks, landmarks.get_predecessors(on_car1.get_index()))
    assert ("at-ferry", ("loc3",)) in predecessors  # car1's initial location
    assert ("empty-ferry", ()) in predecessors
    assert ("at", ("car1", "loc3")) in predecessors
    assert _sets_over_predicate(landmarks, "at-ferry") == []


def test_the_reachability_options_are_on_the_python_surface():
    """ §9.7: every option reachable from Python, with the generator's own defaults. """
    options = search.LiftedFactLandmarkGeneratorOptions()
    assert options.reachability_filter_members is True
    assert options.reachability_disambiguation == search.ReachabilityDisambiguation.PER_LITERAL
    assert options.first_achievers_restricted is True
    assert options.verify_pi_plus is True
    assert options.complete_fact_landmarks == search.CompleteFactLandmarks.MEMBERS

    # JOINT is on the enum and refused by the generator until the engine can project a query, so a
    # caller that asks for it is told rather than quietly given the weaker rule.
    options.reachability_disambiguation = search.ReachabilityDisambiguation.JOINT
    with pytest.raises(Exception):
        search.LiftedFactLandmarkGenerator.create(_parse("blocks_4"), options)


def test_first_achievers_restricted_closes_the_cross_city_chain():
    """ Python parity with SearchLandmarksLiftedFirstAchieversRestrictedTest. """
    problem = _parse("landmark_lifted_first_achiever_chain")
    default = search.LiftedFactLandmarkGenerator.create(problem)

    off = search.LiftedFactLandmarkGeneratorOptions()
    off.reachability_filter_members = False
    off.reachability_disambiguation = search.ReachabilityDisambiguation.OFF
    off.first_achievers_restricted = False
    off.verify_pi_plus = False
    off.complete_fact_landmarks = search.CompleteFactLandmarks.OFF
    without = search.LiftedFactLandmarkGenerator.create(_parse("landmark_lifted_first_achiever_chain"), off)

    assert _find_landmark(without, "at", ["p0", "l1-0"]) is None
    assert _find_landmark(default, "at", ["p0", "l1-0"]) is not None
    assert _find_landmark(default, "at", ["t1", "l1-3"]) is not None


def test_the_complete_test_promotes_a_member_and_collapses_its_set():
    """ Python parity with SearchLandmarksLiftedCompleteFactLandmarksTest. """
    off = search.LiftedFactLandmarkGeneratorOptions()
    off.complete_fact_landmarks = search.CompleteFactLandmarks.OFF

    without = search.LiftedFactLandmarkGenerator.create(_parse("landmark_lifted_complete_member"), off)
    with_members = search.LiftedFactLandmarkGenerator.create(_parse("landmark_lifted_complete_member"))

    assert len(_sets_over_predicate(without, "a")) == 1
    assert _find_landmark(without, "a", ["t1"]) is None

    assert _sets_over_predicate(with_members, "a") == []
    assert _find_landmark(with_members, "a", ["t1"]) is not None
    assert _find_landmark(with_members, "a", ["t2"]) is None


def test_verify_pi_plus_refuses_a_hand_built_non_landmark():
    """ Python parity with SearchLandmarksLiftedVerifyPiPlusTest. """
    problem = _parse("blocks_4")
    search.verify_pi_plus_fact_landmarks(problem, search.LiftedFactLandmarkGenerator.create(problem))

    on_predicate = problem.get_domain().get_fluent_predicate("on")
    b1 = problem.get_problem_or_domain_object("b1")
    b2 = problem.get_problem_or_domain_object("b2")
    not_a_landmark = problem.get_or_create_ground_atom(on_predicate, [b1, b2])

    unsound = search.FactLandmarkGraph.create(problem, [not_a_landmark.get_index()], [])
    with pytest.raises(Exception):
        search.verify_pi_plus_fact_landmarks(problem, unsound)
