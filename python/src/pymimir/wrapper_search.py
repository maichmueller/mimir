from typing import Union

from .wrapper_formalism import GroundAction, State


# -----------------------
# Common search interface
# -----------------------

class SearchResult:
    def __init__(self, status, solution: 'Union[list[GroundAction], None]', solution_cost: 'Union[float, None]', goal_state: 'Union[State, None]', statistics=None, search_tree=None) -> None:
        """
        A class to encapsulate the result of a search operation.

        :param status: The status of the search operation (e.g., 'solved', 'unsolvable').
        :type status: str
        :param solution: A list of GroundAction representing the solution path.
        :type solution: list[GroundAction] or None
        :param solution_cost: The total cost of the solution.
        :type solution_cost: float or None
        :param goal_state: The final state reached by the search.
        :type goal_state: State or None
        :param statistics: Native search statistics, or None when they were not collected.
            Owns its data, so it stays readable after the search's event handler is gone.
        :param search_tree: The admitted search tree, or None when it was not captured.
            Owns its data, so it stays readable after the search's event handler is gone.
        """
        self.status = status
        self.solution = solution
        self.solution_cost = solution_cost
        self.goal_state = goal_state
        #: Native statistics of the search, or None. See `collect_statistics`.
        self.statistics = statistics
        #: The states admitted into the search tree, or None. See `capture_search_tree`.
        self.search_tree = search_tree


def _uses_python_callbacks(*callbacks) -> bool:
    """Whether the caller passed any per-event Python callback."""
    return any(callback is not None for callback in callbacks)


def _reject_native_observation_with_callbacks(collect_statistics: bool, capture_search_tree: bool) -> None:
    """Refuse to mix Python callbacks with native observation.

    Native collection lives in a C++ event handler and Python callbacks live in a Python subclass of
    one, and a search installs exactly one handler. Rather than silently handing back empty
    statistics, say which of the two the caller has to give up.
    """
    if collect_statistics or capture_search_tree:
        raise ValueError(
            "collect_statistics and capture_search_tree cannot be combined with per-event Python callbacks: "
            "the search installs a single event handler, which is either the native collector or your callback "
            "handler. Drop the callbacks to collect natively, or derive the same counts inside your callbacks."
        )
