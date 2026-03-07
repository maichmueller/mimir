"""Quick example: run BrFS with projective IW(1) pruning."""

from pathlib import Path

import pymimir.advanced.search as search

ROOT_DIR = (Path(__file__).parent.parent.parent).absolute()


def main():
    domain_filepath = str(ROOT_DIR / "data" / "gripper" / "domain.pddl")
    problem_filepath = str(ROOT_DIR / "data" / "gripper" / "p-2-0.pddl")

    search_context = search.SearchContext.create(domain_filepath, problem_filepath, search.SearchContextOptions())
    problem = search_context.get_problem()

    start_state, _ = search_context.get_state_repository().get_or_create_initial_state()

    brfs_options = search.BrFSOptions()
    brfs_options.start_state = start_state
    brfs_options.event_handler = search.DefaultBrFSEventHandler(problem)
    brfs_options.goal_strategy = search.ProblemGoalStrategy.create(problem)
    brfs_options.pruning_strategy = search.ProjectiveArityOneNoveltyPruningStrategy.create(problem,
                                                                                           typed_projection=False,
                                                                                           keep_depth_one_novel=True)

    result = search.find_solution_brfs(search_context, brfs_options)

    print("Status:", result.status.name)
    if result.plan:
        print("Plan length:", len(result.plan.get_actions()))
        print(result.plan)


if __name__ == "__main__":
    main()
