from typing import Union

from pymimir.advanced.search import (
    BrFSObservationOptions as AdvancedBrFSObservationOptions,
)
from pymimir.advanced.search import (
    compute_brfs_transition_aggregates as _advanced_compute_transition_aggregates,
)

from .wrapper_formalism import GroundAction, Problem, State


# -----------------------
# Common search interface
# -----------------------

class SearchTree:
    """The states one search admitted into its search tree, in admission order.

    Node ``0`` is the root. Nodes carry native indices; ``get_actions``/``get_states`` turn a
    root-to-node path into Python objects, and only when called -- materializing them during the
    search is exactly the per-event Python cost this API exists to avoid.
    """

    def __init__(self, advanced_search_tree, problem: "Problem", owner=None) -> None:
        self._advanced_search_tree = advanced_search_tree
        self._problem = problem
        #: The native handler the tree lives in, held so it outlives the search call.
        self._owner = owner

    @property
    def nodes(self):
        """The nodes in admission order, each with ``state_index``, ``parent_index``,
        ``incoming_action_index`` and ``depth``."""
        return self._advanced_search_tree.nodes

    def __len__(self) -> int:
        return len(self._advanced_search_tree)

    def __repr__(self) -> str:
        return f"SearchTree(num_nodes={len(self)})"

    def find_node_by_state(self, state_index: int) -> "Union[int, None]":
        """The node index at which ``state_index`` was admitted, or None if it never was."""
        return self._advanced_search_tree.find_node_by_state(state_index)

    def get_action_indices(self, node_index: int) -> "list[int]":
        """Action indices from the root down to ``node_index``. Empty for the root."""
        return list(self._advanced_search_tree.get_action_indices(node_index))

    def get_state_indices(self, node_index: int) -> "list[int]":
        """State indices from the root down to ``node_index``, root first. Always one longer than the
        action path."""
        return list(self._advanced_search_tree.get_state_indices(node_index))

    def get_actions(self, node_index: int) -> "list[GroundAction]":
        """The actions of the root-to-node path, materialized on call."""
        advanced_problem = self._problem._advanced_problem
        return [
            GroundAction(advanced_problem.get_ground_action(index), self._problem)
            for index in self._advanced_search_tree.get_action_indices(node_index)
        ]

    def get_states(self, node_index: int) -> "list[State]":
        """The states of the root-to-node path, materialized on call."""
        state_repository = self._problem._search_context.get_state_repository()
        return [
            State(state_repository.get_state(state_repository.get_packed_state(index)), self._problem)
            for index in self._advanced_search_tree.get_state_indices(node_index)
        ]


def compute_transition_aggregates(transitions):
    """Totals over a transition log: how much was admitted and rejected at each depth, how large the
    novelty witnesses were, how many add and delete effects the admitted actions had, and how much
    each transition realized.

    Computed natively over the records, so nothing here needs a per-event Python callback.
    """
    return _advanced_compute_transition_aggregates(transitions)


class ArityObservation:
    """What one arity pass of an IW search observed.

    Each pass runs its own BrFS over its own novelty table, so its tree has its own root and its own
    node-index namespace: node 3 here and node 3 of another pass are unrelated.
    """

    def __init__(self, advanced_arity_observation, problem: "Problem", owner=None) -> None:
        self._advanced_arity_observation = advanced_arity_observation
        self._owner = owner
        #: The width this pass searched at.
        self.arity = advanced_arity_observation.arity
        #: Native BrFSStatistics of this pass; all-zero for a placeholder pass that ran no search.
        self.statistics = advanced_arity_observation.statistics
        #: This pass's admitted search tree.
        self.search_tree = SearchTree(advanced_arity_observation.search_tree, problem, owner)
        #: This pass's transition log; empty unless transition capture was requested.
        self.transitions = advanced_arity_observation.transitions

    def compute_transition_aggregates(self):
        """Totals over this pass's transition log. See :func:`compute_transition_aggregates`."""
        return compute_transition_aggregates(self.transitions)

    def __repr__(self) -> str:
        return (
            f"ArityObservation(arity={self.arity}, num_tree_nodes={len(self.search_tree)}, "
            f"num_transitions={len(self.transitions)})"
        )


class IWObservation:
    """One :class:`ArityObservation` per attempted arity, in the order the arities were attempted.

    Optimized IW(1) reports a width-0 pass that never runs a search. It appears here with empty
    statistics and an empty tree rather than being dropped, so an entry's position keeps matching the
    width it stands for.
    """

    def __init__(self, advanced_observation, problem: "Problem", owner=None) -> None:
        self._advanced_observation = advanced_observation
        self._owner = owner
        self.by_arity = [
            ArityObservation(advanced_observation.get_arity_observation(index), problem, owner)
            for index in range(advanced_observation.get_num_arities())
        ]

    def __len__(self) -> int:
        return len(self.by_arity)

    def __repr__(self) -> str:
        return f"IWObservation(by_arity={self.by_arity!r})"


class SearchResult:
    def __init__(
        self,
        status,
        solution: 'Union[list[GroundAction], None]',
        solution_cost: 'Union[float, None]',
        goal_state: 'Union[State, None]',
        statistics=None,
        search_tree=None,
        transitions=None,
        observation=None,
        native_handlers=None,
    ) -> None:
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
        :param search_tree: The admitted search tree, or None when it was not captured.
        :param transitions: The captured transition log, or None when none was captured.
        :param observation: For IW, the per-arity observation; None for single-pass searches.
        :param native_handlers: The native event handlers the search ran with. Held because
            `search_tree`, `transitions` and `observation` are views into them, not copies, and
            would dangle once the handlers were collected.
        """
        self.status = status
        self.solution = solution
        self.solution_cost = solution_cost
        self.goal_state = goal_state
        #: Native statistics of the search, or None. See `collect_statistics`.
        self.statistics = statistics
        #: The states admitted into the search tree, or None. See `capture_search_tree`.
        self.search_tree = search_tree
        #: The classified transitions, or None. See `capture_transitions`.
        self.transitions = transitions
        #: For IW, one entry per attempted arity. See :class:`IWObservation`.
        self.observation = observation
        self._native_handlers = list(native_handlers) if native_handlers else []

    def compute_transition_aggregates(self):
        """Totals over `transitions`, or None when no transition was captured.

        See :func:`compute_transition_aggregates`.
        """
        return None if self.transitions is None else compute_transition_aggregates(self.transitions)


def _uses_python_callbacks(*callbacks) -> bool:
    """Whether the caller passed any per-event Python callback."""
    return any(callback is not None for callback in callbacks)


def _build_brfs_observation_options(
    capture_search_tree: bool = False,
    capture_transitions: bool = False,
    capture_rejected_transitions: bool = False,
    capture_novel_witnesses: bool = False,
    capture_action_effect_summaries: bool = False,
    capture_realized_effects: bool = False,
) -> "Union[AdvancedBrFSObservationOptions, None]":
    """Translate the wrapper flags into native observation options, or None if nothing was asked for.

    Everything that decorates a transition -- witnesses, effect summaries, realized deltas -- has
    nowhere to live unless transitions are recorded, so asking for any of them turns on
    admitted-transition capture. Asking for rejected transitions does not otherwise change what is
    recorded about the admitted ones.
    """
    decorates_transitions = capture_novel_witnesses or capture_action_effect_summaries or capture_realized_effects
    capture_admitted = capture_transitions or capture_rejected_transitions or decorates_transitions

    if not (capture_search_tree or capture_admitted):
        return None

    options = AdvancedBrFSObservationOptions()
    options.capture_search_tree = capture_search_tree
    options.capture_admitted_transitions = capture_admitted
    options.capture_rejected_transitions = capture_rejected_transitions
    options.capture_novel_witnesses = capture_novel_witnesses
    options.capture_action_effect_summaries = capture_action_effect_summaries
    options.capture_realized_effects = capture_realized_effects
    return options
