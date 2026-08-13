from typing import Callable, Literal, Union

from pymimir.advanced.formalism import GroundAction as AdvancedGroundAction
from pymimir.advanced.search import State as AdvancedState

from pymimir.advanced.search import BrFSOptions as AdvancedBrFSOptions
from pymimir.advanced.search import BeamNoveltyMode as AdvancedBeamNoveltyMode

# from pymimir.advanced.search import BrFSStatistics as AdvancedBrFSStatistics
from pymimir.advanced.search import find_solution_brfs as advanced_brfs
from pymimir.advanced.search import IBrFSEventHandler as AdvancedBrFSEventHandler
from pymimir.advanced.search import (
    DefaultBrFSEventHandler as AdvancedDefaultBrFSEventHandler,
)
from pymimir.advanced.search import (
    SearchTreeBrFSEventHandler as AdvancedSearchTreeBrFSEventHandler,
)
from pymimir.advanced.search import (
    ILayerOrderingStrategy as AdvancedILayerOrderingStrategy,
)

from .wrapper_formalism import GroundAction, Problem, State
from .wrapper_search import (
    SearchResult,
    _reject_native_observation_with_callbacks,
    _uses_python_callbacks,
)


# -----------------
# Search algorithms
# -----------------


def brfs(
    problem: "Problem",
    start_state: "State",
    max_time_seconds: float = -1,
    max_num_states: int = -1,
    layer_ordering_strategy: "Union[AdvancedILayerOrderingStrategy, None]" = None,
    max_next_layer_states: int = -1,
    beam_width: int = -1,
    beam_novelty_mode: 'Union[Literal["all_tested", "survivors_only"], AdvancedBeamNoveltyMode]' = AdvancedBeamNoveltyMode.ALL_TESTED,
    randomize_equal_score_ties: bool = False,
    equal_score_tie_seed: "Union[int, None]" = None,
    max_depth: int = -1,
    iw1_precheck_add_effect_novelty: bool = False,
    iw1_atom_first_mode: bool = False,
    iw1_atom_first_ratio: float = 1.0,
    iw1_incremental_first_applicability: bool = False,
    iw1_incremental_first_applicability_debug_crosscheck: bool = False,
    on_expand_state: "Union[Callable[[State], None], None]" = None,
    on_expand_goal_state: "Union[Callable[[State], None], None]" = None,
    on_generate_state: "Union[Callable[[State, GroundAction, float, State], None], None]" = None,
    on_generate_new_state: "Union[Callable[[State, GroundAction, float, State], None], None]" = None,
    on_prune_state: "Union[Callable[[State, GroundAction, float, State], None], None]" = None,
    on_finish_g_layer: "Union[Callable[[float], None], None]" = None,
    *,
    num_threads: int = -1,
    chunk_size: int = -1,
    relaxed_survivors_only_beam: bool = False,
    stop_if_goal: bool = True,
    collect_statistics: bool = False,
    capture_search_tree: bool = False,
) -> "SearchResult":
    """
    Breadth-First Search (BrFS) search algorithm.

    :param problem: The problem to solve.
    :type problem: Problem
    :param start_state: The initial state from which to start the search.
    :type start_state: State
    :param max_time_seconds: Maximum time allowed for the search in seconds. Default is -1 (no limit).
    :type max_time_seconds: float
    :param max_num_states: Maximum number of states to explore. Default is -1 (no limit).
    :type max_num_states: int
    :param layer_ordering_strategy: Optional advanced layer ordering strategy used to reorder each frontier layer before expansion.
    :type layer_ordering_strategy: AdvancedILayerOrderingStrategy | None
    :param max_next_layer_states: Optional cap on how many successor states are admitted into the next layer when using ordered-layer expansion.
    :type max_next_layer_states: int
    :param beam_width: Optional true top-k beam width for the next layer. Mutually exclusive with max_next_layer_states.
    :type beam_width: int
    :param beam_novelty_mode: Beam novelty semantics, either "all_tested" or "survivors_only".
    :type beam_novelty_mode: Literal["all_tested", "survivors_only"]
    :param randomize_equal_score_ties: Whether equal-score candidates should be randomized instead of using generation order.
    :type randomize_equal_score_ties: bool
    :param equal_score_tie_seed: Optional deterministic seed for equal-score randomization. Defaults to 0 when unset.
    :type equal_score_tie_seed: int | None
    :param max_depth: Optional maximum discrete search depth. Values < 0 keep search unrestricted.
    :type max_depth: int
    :param iw1_precheck_add_effect_novelty: Enable IW(1) add-effect novelty precheck before successor generation.
        This only has an effect with a compatible width-1 novelty pruning strategy.
    :type iw1_precheck_add_effect_novelty: bool
    :param iw1_atom_first_mode: Enable the older atom-first IW(1) action ordering mode.
        This only has an effect with a compatible width-1 novelty pruning strategy.
    :type iw1_atom_first_mode: bool
    :param iw1_atom_first_ratio: Positive tuning parameter used by atom-first mode and the add-effect precheck machinery.
    :type iw1_atom_first_ratio: float
    :param iw1_incremental_first_applicability: Enable incremental discovery of newly first-applicable actions in supported width-1 setups.
    :type iw1_incremental_first_applicability: bool
    :param iw1_incremental_first_applicability_debug_crosscheck: Enable exact debug cross-checking for incremental first-applicability.
        Requires iw1_incremental_first_applicability=True.
    :type iw1_incremental_first_applicability_debug_crosscheck: bool
    :param num_threads: Thread count for parallel beam evaluation. Values <= 1 keep the serial path.
    :type num_threads: int
    :param chunk_size: Parallel beam chunk size. Values <= 0 keep the engine default.
    :type chunk_size: int
    :param relaxed_survivors_only_beam: Opt-in relaxed SURVIVORS_ONLY beam mode that only canonicalizes the merged worker-local top-k candidates.
    :type relaxed_survivors_only_beam: bool
    :param on_expand_state: Callback function called when a state is expanded.
    :type on_expand_state: Callable[[State], None]
    :param on_expand_goal_state: Callback function called when a goal state is expanded.
    :type on_expand_goal_state: Callable[[State], None]
    :param on_generate_state: Callback function called when a state is generated.
    :type on_generate_state: Callable[[State, GroundAction, float, State], None]
    :param on_generate_new_state: Callback function called when a new state is generated that has not been seen before.
    :type on_generate_new_state: Callable[[State, GroundAction, float, State], None]
    :param on_prune_state: Callback function called when a state is pruned.
    :type on_prune_state: Callable[[State, GroundAction, float, State], None]
    :param on_finish_g_layer: Callback function called when a layer of states is finished.
    :type on_finish_g_layer: Callable[[float], None]
    :param stop_if_goal: Whether to stop as soon as a goal is expanded. Set to False to exhaust the reachable state space.
    :type stop_if_goal: bool
    :param collect_statistics: Expose the native BrFSStatistics of the run as `result.statistics`.
        Requires that no callback is given: a search installs one event handler, and native
        collection and Python callbacks each want to be it.
    :type collect_statistics: bool
    :param capture_search_tree: Also expose the admitted search tree as `result.search_tree`. Implies
        `collect_statistics`, since the tree handler collects statistics as well.
    :type capture_search_tree: bool
    :return: A SearchResult object containing the status, solution, solution cost, and goal state.
    :rtype: SearchResult

    With no callback given, the search runs entirely in C++: no Python event handler is installed and
    no event crosses the language boundary.
    """
    assert isinstance(problem, Problem), "Problem must be an instance of Problem."
    assert isinstance(start_state, State), "Start state must be an instance of State."
    assert isinstance(
        max_time_seconds, (int, float)
    ), "max_time_seconds must be an int or float."
    assert isinstance(max_num_states, int), "max_num_states must be an int."
    assert isinstance(
        max_next_layer_states, int
    ), "max_next_layer_states must be an int."
    assert isinstance(beam_width, int), "beam_width must be an int."
    assert isinstance(
        beam_novelty_mode, AdvancedBeamNoveltyMode
    ) or beam_novelty_mode in (
        "all_tested",
        "survivors_only",
    ), "beam_novelty_mode must be either enum entry in 'BeamNoveltyMode', or a str matching 'all_tested' or 'survivors_only'."
    assert isinstance(
        randomize_equal_score_ties, bool
    ), "randomize_equal_score_ties must be a bool."
    assert equal_score_tie_seed is None or isinstance(
        equal_score_tie_seed, int
    ), "equal_score_tie_seed must be an int or None."
    assert isinstance(max_depth, int), "max_depth must be an int."
    assert isinstance(
        iw1_precheck_add_effect_novelty, bool
    ), "iw1_precheck_add_effect_novelty must be a bool."
    assert isinstance(iw1_atom_first_mode, bool), "iw1_atom_first_mode must be a bool."
    assert isinstance(iw1_atom_first_ratio, (int, float)), (
        "iw1_atom_first_ratio must be a float."
    )
    assert iw1_atom_first_ratio > 0, "iw1_atom_first_ratio must be positive."
    assert isinstance(
        iw1_incremental_first_applicability, bool
    ), "iw1_incremental_first_applicability must be a bool."
    assert isinstance(
        iw1_incremental_first_applicability_debug_crosscheck, bool
    ), "iw1_incremental_first_applicability_debug_crosscheck must be a bool."
    assert isinstance(num_threads, int), "num_threads must be an int."
    assert isinstance(chunk_size, int), "chunk_size must be an int."
    assert isinstance(
        relaxed_survivors_only_beam, bool
    ), "relaxed_survivors_only_beam must be a bool."
    assert isinstance(stop_if_goal, bool), "stop_if_goal must be a bool."
    assert layer_ordering_strategy is None or isinstance(
        layer_ordering_strategy, AdvancedILayerOrderingStrategy
    ), "layer_ordering_strategy must be an advanced ILayerOrderingStrategy or None."

    use_callbacks = _uses_python_callbacks(
        on_expand_state,
        on_expand_goal_state,
        on_generate_state,
        on_generate_new_state,
        on_prune_state,
        on_finish_g_layer,
    )
    if use_callbacks:
        _reject_native_observation_with_callbacks(collect_statistics, capture_search_tree)

    # Define the event handler with the provided callback functions.
    class EventHandler(AdvancedBrFSEventHandler):
        def __init__(self) -> None:
            super().__init__()

        def on_expand_goal_state(self, advanced_state: "AdvancedState"):
            nonlocal problem, on_expand_goal_state
            if on_expand_goal_state:
                state = State(advanced_state, problem)
                on_expand_goal_state(state)

        def on_expand_state(self, advanced_state: "AdvancedState"):
            nonlocal problem, on_expand_state
            if on_expand_state:
                state = State(advanced_state, problem)
                on_expand_state(state)

        def on_finish_g_layer(self, arg):
            nonlocal problem, on_finish_g_layer
            if on_finish_g_layer:
                on_finish_g_layer(arg)

        def on_generate_state(
            self,
            advanced_state: "AdvancedState",
            advanced_action: "AdvancedGroundAction",
            action_cost: float,
            advanced_successor_state: "AdvancedState",
        ):
            nonlocal problem, on_generate_state
            if on_generate_state:
                state = State(advanced_state, problem)
                action = GroundAction(advanced_action, problem)
                successor_state = State(advanced_successor_state, problem)
                on_generate_state(state, action, action_cost, successor_state)

        def on_generate_state_in_search_tree(
            self,
            advanced_state: "AdvancedState",
            advanced_action: "AdvancedGroundAction",
            action_cost: float,
            advanced_successor_state: "AdvancedState",
        ):
            nonlocal problem, on_generate_new_state
            if on_generate_new_state:
                state = State(advanced_state, problem)
                action = GroundAction(advanced_action, problem)
                successor_state = State(advanced_successor_state, problem)
                on_generate_new_state(state, action, action_cost, successor_state)

        def on_generate_state_not_in_search_tree(
            self,
            advanced_state: "AdvancedState",
            advanced_action: "AdvancedGroundAction",
            action_cost: float,
            advanced_successor_state: "AdvancedState",
        ):
            nonlocal problem, on_prune_state
            if on_prune_state:
                state = State(advanced_state, problem)
                action = GroundAction(advanced_action, problem)
                successor_state = State(advanced_successor_state, problem)
                on_prune_state(state, action, action_cost, successor_state)

        # The following events are ignored in this interface.
        def on_close_state(self, arg0):
            pass

        def on_end_search(self, arg0, arg1, arg2, arg3, arg4, arg5):
            pass

        def on_exhausted(self):
            pass

        def on_generate_state_not_relaxed(self, arg0, arg1, arg2, arg3):
            pass

        def on_generate_state_relaxed(self, arg0, arg1, arg2, arg3):
            pass

        def on_solved(self, arg):
            pass

        def on_start_search(self, arg0):
            pass

        def on_unsolvable(self):
            pass

    # Create options for the BrFS search
    advanced_options = AdvancedBrFSOptions()
    if max_time_seconds > 0:
        advanced_options.max_time_in_ms = int(max_time_seconds * 1000)
    if max_num_states > 0:
        advanced_options.max_num_states = max_num_states
    if max_next_layer_states > 0:
        advanced_options.max_next_layer_states = max_next_layer_states
    if beam_width > 0:
        advanced_options.beam_width = beam_width
    if isinstance(beam_novelty_mode, AdvancedBeamNoveltyMode):
        advanced_options.beam_novelty_mode = beam_novelty_mode
    else:
        advanced_options.beam_novelty_mode = (
            AdvancedBeamNoveltyMode.ALL_TESTED
            if beam_novelty_mode == "all_tested"
            else AdvancedBeamNoveltyMode.SURVIVORS_ONLY
        )
    advanced_options.randomize_equal_score_ties = randomize_equal_score_ties
    advanced_options.equal_score_tie_seed = (
        0 if equal_score_tie_seed is None else equal_score_tie_seed
    )
    if max_depth >= 0:
        advanced_options.max_depth = max_depth
    advanced_options.iw1_precheck_add_effect_novelty = (
        iw1_precheck_add_effect_novelty
    )
    advanced_options.iw1_atom_first_mode = iw1_atom_first_mode
    advanced_options.iw1_atom_first_ratio = float(iw1_atom_first_ratio)
    advanced_options.iw1_incremental_first_applicability = (
        iw1_incremental_first_applicability
    )
    advanced_options.iw1_incremental_first_applicability_debug_crosscheck = (
        iw1_incremental_first_applicability_debug_crosscheck
    )
    advanced_options.relaxed_survivors_only_beam = relaxed_survivors_only_beam
    if num_threads > 1:
        advanced_options.parallel_beam_num_threads = num_threads
    if chunk_size > 0:
        advanced_options.parallel_beam_chunk_size = chunk_size
    advanced_options.start_state = start_state._advanced_state
    # A native, quiet handler keeps a callback-free search inside C++; a Python subclass would be
    # entered once per expansion and once per generated transition even with every callback unset.
    if use_callbacks:
        event_handler = EventHandler()
    elif capture_search_tree:
        event_handler = AdvancedSearchTreeBrFSEventHandler(problem._advanced_problem)
    else:
        event_handler = AdvancedDefaultBrFSEventHandler(problem._advanced_problem, quiet=True)
    advanced_options.event_handler = event_handler
    if layer_ordering_strategy is not None:
        advanced_options.layer_ordering_strategy = layer_ordering_strategy
    advanced_options.stop_if_goal = stop_if_goal
    # Invoke the BrFS search algorithm
    result = advanced_brfs(problem._search_context, advanced_options)
    status = result.status.name.lower()
    solution = (
        [GroundAction(x, problem) for x in result.plan.get_actions()]
        if result.plan
        else None
    )
    solution_cost = result.plan.get_cost() if result.plan else None
    goal_state = State(result.goal_state, problem) if result.goal_state else None
    # Both getters return snapshots that own their data, so they outlive `event_handler`.
    statistics = (
        event_handler.get_statistics()
        if (collect_statistics or capture_search_tree)
        else None
    )
    search_tree = event_handler.get_search_tree() if capture_search_tree else None
    return SearchResult(status, solution, solution_cost, goal_state, statistics, search_tree)
