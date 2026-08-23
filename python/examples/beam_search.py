"""Quick example: run beam-ordered BrFS and IW with goal-count ordering."""

from pathlib import Path

import pymimir
import pymimir.advanced.search as advanced_search

ROOT_DIR = (Path(__file__).parent.parent.parent).absolute()


def load_problem() -> pymimir.Problem:
    domain = pymimir.Domain(ROOT_DIR / "data" / "gripper" / "domain.pddl")
    return pymimir.Problem(domain, ROOT_DIR / "data" / "gripper" / "p-2-0.pddl")


def make_goal_count_ordering(problem: pymimir.Problem) -> advanced_search.GoalCountLayerOrderingStrategy:
    return advanced_search.GoalCountLayerOrderingStrategy(problem._advanced_problem)


def main():
    problem = load_problem()
    start_state = problem.get_initial_state()

    brfs_result = pymimir.brfs(problem,
                               start_state,
                               layer_ordering_strategy=make_goal_count_ordering(problem),
                               beam_width=4)
    print("BrFS status:", brfs_result.status)
    print("BrFS plan length:", len(brfs_result.solution) if brfs_result.solution is not None else 0)

    iw_result = pymimir.iw(problem,
                           start_state,
                           max_arity=2,
                           layer_ordering_strategy=make_goal_count_ordering(problem),
                           beam_width=4,
                           beam_novelty_mode="survivors_only")
    print("IW status:", iw_result.status)
    print("IW plan length:", len(iw_result.solution) if iw_result.solution is not None else 0)


if __name__ == "__main__":
    main()
