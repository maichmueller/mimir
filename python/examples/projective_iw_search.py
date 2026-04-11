"""Quick example: run BrFS with projective IW(1) pruning."""

import argparse
from collections import defaultdict, deque
from pathlib import Path

import pymimir.advanced.search as search
from pymimir.advanced import formalism
from pymimir.wrapper_formalism import Problem

ROOT_DIR = (Path(__file__).parent.parent.parent).absolute()


class CustomBrFSEventHandler(search.IBrFSEventHandler):
    """A custom event handler to react on events during iw search.

    It is useful to collect custom statistics or create a search tree.
    """

    def __init__(self, problem: formalism.Problem):
        super().__init__()

        self.problem = problem
        self.statistics = search.BrFSStatistics()
        self._novel_by_transition = defaultdict(deque)
        self.num_generated = 0
        self.num_novel_generated = 0

    def _pop_cached_novel_indices(
        self, state: search.State, successor_state: search.State
    ):
        key = (state.get_index(), successor_state.get_index())
        if self._novel_by_transition[key]:
            return self._novel_by_transition[key].popleft()
        return []

    def on_expand_state(self, state: search.State):
        pass

    def on_expand_goal_state(self, state: search.State):
        pass

    def supports_novel_witness_events(self):
        return True

    def on_generate_state(
        self,
        state: search.State,
        action: formalism.GroundAction,
        action_cost: float,
        successor_state: search.State,
    ):
        self.num_generated += 1

    def on_generate_state_with_novel_witness(
        self,
        state: search.State,
        action: formalism.GroundAction,
        action_cost: float,
        successor_state: search.State,
        novel_fluent_atom_indices: list[int],
    ):
        key = (state.get_index(), successor_state.get_index())
        self._novel_by_transition[key].append(list(novel_fluent_atom_indices))

    def on_generate_state_in_search_tree(
        self,
        state: search.State,
        action: formalism.GroundAction,
        action_cost: float,
        successor_state: search.State,
    ):
        self.num_novel_generated += 1
        idxs = self._pop_cached_novel_indices(state, successor_state)
        repos = self.problem.get_repositories()
        novel_atoms = repos.get_fluent_ground_atoms_from_indices(list(idxs))
        print(
            "Generated State.",
            "Novel atoms:",
            *[str(a) for a in novel_atoms] if novel_atoms else ["<none>"],
        )

    def on_generate_state_not_in_search_tree(
        self,
        state: search.State,
        action: formalism.GroundAction,
        action_cost: float,
        successor_state: search.State,
    ):
        # Keep cache aligned with event stream.
        self._pop_cached_novel_indices(state, successor_state)

    def on_finish_g_layer(self, g_value: int):
        pass

    def on_start_search(self, start_state: search.State):
        pass

    def on_end_search(
        self,
        num_reached_fluent_atoms: int,
        num_reached_derived_atoms: int,
        num_states: int,
        num_nodes: int,
        num_actions: int,
        num_axioms: int,
    ):
        print(
            "Search stats:",
            f"reached_fluent_atoms={num_reached_fluent_atoms}",
            f"reached_derived_atoms={num_reached_derived_atoms}",
            f"states={num_states}",
            f"nodes={num_nodes}",
            f"actions={num_actions}",
            f"axioms={num_axioms}",
        )
        print(
            "Event counts:",
            f"generated={self.num_generated}",
            f"novel_generated={self.num_novel_generated}",
        )

    def on_solved(self, plan: search.Plan):
        pass

    def on_unsolvable(self):
        pass

    def on_exhausted(self):
        pass

    def get_statistics(self):
        return self.statistics


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--basis",
        choices=["classical", "projective", "projective_typed"],
        default="projective_typed",
        help="Novelty basis to use.",
    )
    parser.add_argument(
        "--keep-depth-one-novel",
        action="store_true",
        help="Keep all depth-1 children as novel in projective modes.",
    )
    parser.add_argument(
        "--domain",
        type=str,
        default=str(ROOT_DIR / "data" / "blocks_4" / "domain.pddl"),
        help="Domain PDDL path.",
    )
    parser.add_argument(
        "--problem",
        type=str,
        default=str(ROOT_DIR / "data" / "blocks_4" / "test_problem.pddl"),
        help="Problem PDDL path.",
    )
    args = parser.parse_args()

    domain_filepath = args.domain
    problem_filepath = args.problem

    search_context = search.SearchContext.create(
        domain_filepath, problem_filepath, search.SearchContextOptions()
    )
    problem = search_context.get_problem()

    start_state, _ = search_context.get_state_repository().get_or_create_initial_state()

    goal_strategy = search.ProblemGoalStrategy.create(problem)
    event_handler = None

    if args.basis == "classical":
        iw_options = search.IWOptions()
        iw_options.start_state = start_state
        iw_options.goal_strategy = goal_strategy
        iw_options.max_arity = 1
        event_handler = CustomBrFSEventHandler(problem)
        iw_options.brfs_event_handler = event_handler
    else:
        brfs_options = search.BrFSOptions()
        brfs_options.start_state = start_state
        brfs_options.goal_strategy = goal_strategy
        brfs_options.pruning_strategy = (
            search.ProjectiveArityOneNoveltyPruningStrategy.create(
                problem,
                typed_projection=(args.basis == "projective_typed"),
                keep_depth_one_novel=args.keep_depth_one_novel,
            )
        )
        event_handler = CustomBrFSEventHandler(problem)
        brfs_options.event_handler = event_handler

    print(
        "Running:",
        f"basis={args.basis}",
        f"keep_depth_one_novel={args.keep_depth_one_novel}",
        f"domain={domain_filepath}",
        f"problem={problem_filepath}",
    )

    if args.basis == "classical":
        result = search.find_solution_iw(search_context, iw_options)
    else:
        result = search.find_solution_brfs(search_context, brfs_options)

    print("Status:", result.status.name)
    if result.plan:
        print("Plan length:", len(result.plan.get_actions()))
        print(result.plan)


if __name__ == "__main__":
    main()
