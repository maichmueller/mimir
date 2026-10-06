"""The grounded generator yields a state's applicable actions in the same order in every process.

The grounded applicable-action generator walks a match tree, so the order in which it yields a
state's actions is the order of the tree's leaves, and that is decided while the tree is built: each
node is split on the best-scoring atom, and under the default frequency metric many atoms tie. A tie
used to go to whichever split a container hashed on `GroundAtom` pointers happened to visit first --
that is, to a heap address, which moves between processes under ASLR. Every process then saw the
same actions in its own order, and any search that stops early, prunes by novelty or breaks ties
inherited that order.

One process cannot show this reliably, so each check enumerates in fresh interpreters and compares.
The walk itself visits states in an order of its own (successors sorted by action index), so the
only thing left to differ is the generator's order.
"""

import subprocess
import sys

from pathlib import Path

import pytest

ROOT_DIR = (Path(__file__).parent.parent.parent.parent).absolute()

PROBLEMS = [
    ("blocks_4", "test_problem.pddl"),
    ("gripper", "test_problem.pddl"),
    ("logistics", "test_problem.pddl"),
    ("miconic", "test_problem.pddl"),
]

NUM_PROCESSES = 6
MAX_STATES = 300

ENUMERATE = r"""
import sys

import pymimir.advanced.formalism as formalism
import pymimir.advanced.search as search

domain_filepath, problem_filepath, max_states = sys.argv[1], sys.argv[2], int(sys.argv[3])
problem = formalism.Problem.create(domain_filepath, problem_filepath, formalism.ParserOptions())
context = search.SearchContext.create(problem, search.SearchContextOptions(search.GroundedOptions()))
repository = context.get_state_repository()
generator = context.get_applicable_action_generator()

state, _ = repository.get_or_create_initial_state()
frontier, seen, num_states = [state], {state.get_index()}, 0
while frontier and num_states < max_states:
    state = frontier.pop(0)
    num_states += 1
    actions = list(generator.generate_applicable_actions(state))
    print(state.get_index(), *(action.get_index() for action in actions))
    for action in sorted(actions, key=lambda action: action.get_index()):
        successor, _ = repository.get_or_create_successor_state(state, action, 0.0)
        if successor.get_index() not in seen:
            seen.add(successor.get_index())
            frontier.append(successor)
"""


def _enumerate_in_fresh_processes(domain_name: str, problem_name: str) -> list[str]:
    argv = [
        sys.executable,
        "-c",
        ENUMERATE,
        str(ROOT_DIR / "data" / domain_name / "domain.pddl"),
        str(ROOT_DIR / "data" / domain_name / problem_name),
        str(MAX_STATES),
    ]
    processes = [
        subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        for _ in range(NUM_PROCESSES)
    ]
    outputs = []
    for process in processes:
        stdout, stderr = process.communicate()
        assert process.returncode == 0, stderr
        outputs.append(stdout)
    return outputs


@pytest.mark.parametrize("domain_name, problem_name", PROBLEMS)
def test_grounded_successor_order_is_the_same_in_every_process(domain_name, problem_name):
    outputs = _enumerate_in_fresh_processes(domain_name, problem_name)

    # Guard against a vacuous pass: the walk must have reached states that branch.
    assert any(len(line.split()) > 2 for line in outputs[0].splitlines())
    num_orders = len(set(outputs))
    assert num_orders == 1, f"{num_orders} distinct successor orders over {NUM_PROCESSES} processes"
