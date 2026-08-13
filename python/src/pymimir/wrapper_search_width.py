from typing import Callable, Literal, Union

from pymimir.advanced.formalism import GroundAction as AdvancedGroundAction
from pymimir.advanced.search import State as AdvancedState

from pymimir.advanced.search import BrFSOptions as AdvancedBrFSOptions
from pymimir.advanced.search import BrFSStatistics as AdvancedBrFSStatistics
from pymimir.advanced.search import BeamNoveltyMode as AdvancedBeamNoveltyMode
from pymimir.advanced.search import find_solution_brfs as advanced_brfs
from pymimir.advanced.search import find_solution_iw as advanced_iw
from pymimir.advanced.search import (
    ILayerOrderingStrategy as AdvancedILayerOrderingStrategy,
)
from pymimir.advanced.search import (
    AbstractedNoveltyPruningStrategy as AdvancedAbstractedNoveltyPruningStrategy,
)

# from pymimir.advanced.search import find_solution_siw as advanced_siw
from pymimir.advanced.search import IBrFSEventHandler as AdvancedBrFSEventHandler
from pymimir.advanced.search import (
    DefaultBrFSEventHandler as AdvancedDefaultBrFSEventHandler,
)
from pymimir.advanced.search import (
    ObservationBrFSEventHandler as AdvancedObservationBrFSEventHandler,
)
from pymimir.advanced.search import (
    CompositeBrFSEventHandler as AdvancedCompositeBrFSEventHandler,
)
from pymimir.advanced.search import (
    DefaultIWEventHandler as AdvancedDefaultIWEventHandler,
)
from pymimir.advanced.search import (
    ObservationIWEventHandler as AdvancedObservationIWEventHandler,
)
from pymimir.advanced.search import IWOptions as AdvancedIWOptions
from pymimir.advanced.search import (
    IWParallelRolloutOptions as AdvancedIWParallelRolloutOptions,
)
from pymimir.advanced.search import find_rollouts_iw_parallel as advanced_iw_parallel

# from pymimir.advanced.search import IWStatistics as AdvancedIWStatistics
# from pymimir.advanced.search import SIWOptions as AdvancedSIWOptions
# from pymimir.advanced.search import SIWStatistics as AdvancedSIWStatistics

from .wrapper_formalism import GroundAction, Problem, State
from .wrapper_search import (
    IWObservation,
    SearchResult,
    SearchTree,
    _build_brfs_observation_options,
    _uses_python_callbacks,
)


# ----------------------
# Width-based algorithms
# ----------------------


class _BrFSObservers:
    """The handlers one search runs with, and which of them observes what.

    A search installs exactly one event handler, so native collection and Python callbacks are
    combined by composing them rather than by making the caller pick one. Each child sees each event
    once, and each keeps its own counters, so composing cannot double-count.
    """

    def __init__(self, event_handler, statistics_handler, observation_handler) -> None:
        #: What to hand to the search.
        self.event_handler = event_handler
        #: The handler to read native statistics off, or None.
        self.statistics_handler = statistics_handler
        #: The handler holding the observation, or None.
        self.observation_handler = observation_handler

    def get_handlers(self) -> list:
        """Everything worth keeping alive alongside the result."""
        return [handler for handler in (self.event_handler, self.statistics_handler, self.observation_handler) if handler is not None]


def _make_brfs_observers(
    problem: "Problem",
    python_event_handler,
    collect_statistics: bool,
    observation_options,
) -> "_BrFSObservers":
    """Pick the BrFS handler(s) for one search.

    With no Python handler the search stays inside C++ for its whole run: a Python subclass is
    entered once per expansion and once per generated transition, which dominates a width search even
    when every callback is `None`. With one, the native observer is composed alongside it so the
    Python cost is paid only for the callbacks that were actually supplied.
    """
    observes_natively = collect_statistics or observation_options is not None

    if observation_options is not None:
        native_handler = AdvancedObservationBrFSEventHandler(problem._advanced_problem, observation_options)
        observation_handler = native_handler
    else:
        # Statistics alone need no observation machinery, and the default handler keeps no tree.
        native_handler = AdvancedDefaultBrFSEventHandler(problem._advanced_problem, quiet=True)
        observation_handler = None

    if python_event_handler is None:
        # A quiet native handler runs either way; whether its statistics are *reported* is what
        # `collect_statistics` decides.
        return _BrFSObservers(native_handler, native_handler if observes_natively else None, observation_handler)

    if not observes_natively:
        # Nothing native was asked for, so do not pay for a second observer.
        return _BrFSObservers(python_event_handler, None, None)

    composite = AdvancedCompositeBrFSEventHandler([native_handler, python_event_handler], statistics_source=0)
    return _BrFSObservers(composite, native_handler, observation_handler)


def _run_brfs_width_pruning(
    problem: "Problem",
    start_state: "State",
    pruning_strategy,
    layer_ordering_strategy: "Union[AdvancedILayerOrderingStrategy, None]" = None,
    max_next_layer_states: int = -1,
    beam_width: int = -1,
    beam_novelty_mode: 'Literal["all_tested", "survivors_only"]' = "all_tested",
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
    *,
    num_threads: int = -1,
    chunk_size: int = -1,
    relaxed_survivors_only_beam: bool = False,
    collect_statistics: bool = False,
    capture_search_tree: bool = False,
    capture_transitions: bool = False,
    capture_rejected_transitions: bool = False,
    capture_novel_witnesses: bool = False,
    capture_action_effect_summaries: bool = False,
    capture_realized_effects: bool = False,
) -> "SearchResult":
    use_callbacks = _uses_python_callbacks(
        on_expand_state,
        on_expand_goal_state,
        on_generate_state,
        on_generate_new_state,
        on_prune_state,
    )
    observation_options = _build_brfs_observation_options(
        capture_search_tree,
        capture_transitions,
        capture_rejected_transitions,
        capture_novel_witnesses,
        capture_action_effect_summaries,
        capture_realized_effects,
    )

    class EventHandler(AdvancedBrFSEventHandler):
        def __init__(self) -> None:
            super().__init__()

        def on_expand_state(self, advanced_state: "AdvancedState"):
            nonlocal problem, on_expand_state
            if on_expand_state:
                on_expand_state(State(advanced_state, problem))

        def on_expand_goal_state(self, advanced_state: "AdvancedState"):
            nonlocal problem, on_expand_goal_state
            if on_expand_goal_state:
                on_expand_goal_state(State(advanced_state, problem))

        def on_generate_state(self, advanced_state, advanced_action, action_cost, advanced_successor_state):
            nonlocal problem, on_generate_state
            if on_generate_state:
                on_generate_state(State(advanced_state, problem), GroundAction(advanced_action, problem), action_cost, State(advanced_successor_state, problem))

        def on_generate_state_in_search_tree(self, advanced_state, advanced_action, action_cost, advanced_successor_state):
            nonlocal problem, on_generate_new_state
            if on_generate_new_state:
                on_generate_new_state(State(advanced_state, problem), GroundAction(advanced_action, problem), action_cost, State(advanced_successor_state, problem))

        def on_generate_state_not_in_search_tree(self, advanced_state, advanced_action, action_cost, advanced_successor_state):
            nonlocal problem, on_prune_state
            if on_prune_state:
                on_prune_state(State(advanced_state, problem), GroundAction(advanced_action, problem), action_cost, State(advanced_successor_state, problem))

        def on_finish_g_layer(self, value: int):
            pass
        def on_start_search(self, arg: "AdvancedState"):
            pass
        def on_end_search(self, arg0: int, arg1: int, arg2: int, arg3: int, arg4: int, arg5: int):
            pass
        def on_solved(self, arg):
            pass
        def on_unsolvable(self):
            pass
        def on_exhausted(self):
            pass
        def get_statistics(self) -> "AdvancedBrFSStatistics":
            return AdvancedBrFSStatistics()

    observers = _make_brfs_observers(
        problem, EventHandler() if use_callbacks else None, collect_statistics, observation_options
    )

    advanced_options = AdvancedBrFSOptions()
    advanced_options.start_state = start_state._advanced_state
    advanced_options.event_handler = observers.event_handler
    if layer_ordering_strategy is not None:
        advanced_options.layer_ordering_strategy = layer_ordering_strategy
    if max_next_layer_states > 0:
        advanced_options.max_next_layer_states = max_next_layer_states
    if beam_width > 0:
        advanced_options.beam_width = beam_width
    advanced_options.beam_novelty_mode = (
        beam_novelty_mode
        if isinstance(beam_novelty_mode, AdvancedBeamNoveltyMode)
        else (AdvancedBeamNoveltyMode.ALL_TESTED if beam_novelty_mode == "all_tested" else AdvancedBeamNoveltyMode.SURVIVORS_ONLY)
    )
    advanced_options.randomize_equal_score_ties = randomize_equal_score_ties
    advanced_options.equal_score_tie_seed = 0 if equal_score_tie_seed is None else equal_score_tie_seed
    if max_depth >= 0:
        advanced_options.max_depth = max_depth
    advanced_options.iw1_precheck_add_effect_novelty = iw1_precheck_add_effect_novelty
    advanced_options.iw1_atom_first_mode = iw1_atom_first_mode
    advanced_options.iw1_atom_first_ratio = float(iw1_atom_first_ratio)
    advanced_options.iw1_incremental_first_applicability = iw1_incremental_first_applicability
    advanced_options.iw1_incremental_first_applicability_debug_crosscheck = iw1_incremental_first_applicability_debug_crosscheck
    advanced_options.relaxed_survivors_only_beam = relaxed_survivors_only_beam
    if num_threads > 1:
        advanced_options.parallel_beam_num_threads = num_threads
    if chunk_size > 0:
        advanced_options.parallel_beam_chunk_size = chunk_size
    advanced_options.pruning_strategy = pruning_strategy
    result = advanced_brfs(problem._search_context, advanced_options)
    status = result.status.name.lower()
    solution = [GroundAction(x, problem) for x in result.plan.get_actions()] if result.plan else None
    solution_cost = result.plan.get_cost() if result.plan else None
    goal_state = State(result.goal_state, problem) if result.goal_state else None
    statistics = observers.statistics_handler.get_statistics() if observers.statistics_handler is not None else None
    # Views into the handler, not copies -- the result keeps the handler alive for them.
    observation = observers.observation_handler.observation if observers.observation_handler is not None else None
    search_tree = SearchTree(observation.search_tree, problem, observers.observation_handler) if capture_search_tree else None
    transitions = observation.transitions if (observation is not None and observation_options.capture_admitted_transitions) else None
    return SearchResult(
        status,
        solution,
        solution_cost,
        goal_state,
        statistics,
        search_tree,
        transitions,
        None,
        observers.get_handlers(),
    )


def iw(
    problem: "Problem",
    start_state: "State",
    max_arity: int,
    layer_ordering_strategy: "Union[AdvancedILayerOrderingStrategy, None]" = None,
    max_next_layer_states: int = -1,
    beam_width: int = -1,
    beam_novelty_mode: 'Literal["all_tested", "survivors_only"]' = "all_tested",
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
    *,
    num_threads: int = -1,
    chunk_size: int = -1,
    relaxed_survivors_only_beam: bool = False,
    max_num_states: int = -1,
    max_time_seconds: float = -1,
    collect_statistics: bool = False,
    capture_search_tree: bool = False,
    capture_transitions: bool = False,
    capture_rejected_transitions: bool = False,
    capture_novel_witnesses: bool = False,
    capture_action_effect_summaries: bool = False,
    capture_realized_effects: bool = False,
) -> "SearchResult":
    """Run IW(k) for increasing k up to ``max_arity``.

    Without any ``on_*`` callback the search runs entirely in C++: no Python event handler is
    installed and no event crosses the language boundary. With callbacks, the native observer is
    composed alongside them, so both run and the Python cost is paid only for the callbacks that
    were actually supplied.

    Every capture is off by default and each turns on only its own work:

    * ``collect_statistics`` -- native ``IWStatistics`` in ``result.statistics``;
    * ``capture_search_tree`` -- the admitted tree in ``result.search_tree``;
    * ``capture_transitions`` -- one record per admitted transition in ``result.transitions``;
    * ``capture_rejected_transitions`` -- rejected transitions in the same log;
    * ``capture_novel_witnesses`` -- makes the search compute novelty witnesses at all;
    * ``capture_action_effect_summaries`` -- a syntactic effect count cached per observed action;
    * ``capture_realized_effects`` -- the fluent atoms each transition actually changed.

    The last three decorate transition records, so any of them turns on admitted-transition capture.

    Each arity pass is observed separately: ``result.observation.by_arity[k]`` carries that pass's
    statistics, tree and transitions, and trees from different passes are never merged. For
    convenience ``result.search_tree`` and ``result.transitions`` refer to the last pass that
    actually ran a search.
    """
    assert isinstance(problem, Problem), "Problem must be an instance of Problem."
    assert isinstance(start_state, State), "Start state must be an instance of State."
    assert isinstance(max_arity, int), "Max arity must be an integer."
    assert max_arity > 0, "Max arity must be positive."
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
    assert isinstance(iw1_atom_first_ratio, float) or isinstance(
        iw1_atom_first_ratio, int
    ), "iw1_atom_first_ratio must be a float."
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
    assert layer_ordering_strategy is None or isinstance(
        layer_ordering_strategy, AdvancedILayerOrderingStrategy
    ), "layer_ordering_strategy must be an advanced ILayerOrderingStrategy or None."

    use_callbacks = _uses_python_callbacks(
        on_expand_state,
        on_expand_goal_state,
        on_generate_state,
        on_generate_new_state,
        on_prune_state,
    )
    observation_options = _build_brfs_observation_options(
        capture_search_tree,
        capture_transitions,
        capture_rejected_transitions,
        capture_novel_witnesses,
        capture_action_effect_summaries,
        capture_realized_effects,
    )

    # Define the event handler with the provided callback functions.
    class EventHandler(AdvancedBrFSEventHandler):
        def __init__(self) -> None:
            super().__init__()

        def on_expand_state(self, advanced_state: "AdvancedState"):
            nonlocal problem, on_expand_state
            if on_expand_state:
                state = State(advanced_state, problem)
                on_expand_state(state)

        def on_expand_goal_state(self, advanced_state: "AdvancedState"):
            nonlocal problem, on_expand_goal_state
            if on_expand_goal_state:
                state = State(advanced_state, problem)
                on_expand_goal_state(state)

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
        def on_finish_g_layer(self, value: int):
            pass

        def on_start_search(self, arg: "AdvancedState"):
            pass

        def on_end_search(
            self, arg0: int, arg1: int, arg2: int, arg3: int, arg4: int, arg5: int
        ):
            pass

        def on_solved(self, arg):
            pass

        def on_unsolvable(self):
            pass

        def on_exhausted(self):
            pass

        def get_statistics(self) -> "AdvancedBrFSStatistics":
            return AdvancedBrFSStatistics()

    observers = _make_brfs_observers(
        problem, EventHandler() if use_callbacks else None, collect_statistics, observation_options
    )
    # The IW-level handler holds the BrFS observer so it can take each pass's observation before the
    # next pass clears it; without an observer there is nothing to rotate, so the default will do.
    iw_event_handler = (
        AdvancedObservationIWEventHandler(problem._advanced_problem, observers.observation_handler)
        if observers.observation_handler is not None
        else AdvancedDefaultIWEventHandler(problem._advanced_problem, quiet=True)
    )

    advanced_options = AdvancedIWOptions()
    advanced_options.start_state = start_state._advanced_state
    advanced_options.brfs_event_handler = observers.event_handler
    advanced_options.iw_event_handler = iw_event_handler
    if layer_ordering_strategy is not None:
        advanced_options.layer_ordering_strategy = layer_ordering_strategy
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
    advanced_options.iw1_precheck_add_effect_novelty = iw1_precheck_add_effect_novelty
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
    advanced_options.max_arity = max_arity
    if max_num_states > 0:
        advanced_options.max_num_states = max_num_states
    if max_time_seconds > 0:
        # The budget spans the whole search, not each arity pass.
        advanced_options.max_time_in_ms = int(max_time_seconds * 1000)
    result = advanced_iw(problem._search_context, advanced_options)
    status = result.status.name.lower()
    solution = (
        [GroundAction(x, problem) for x in result.plan.get_actions()]
        if result.plan
        else None
    )
    solution_cost = result.plan.get_cost() if result.plan else None
    goal_state = State(result.goal_state, problem) if result.goal_state else None
    statistics = iw_event_handler.get_statistics() if (collect_statistics or observation_options is not None) else None

    observation = None
    search_tree = None
    transitions = None
    if observers.observation_handler is not None:
        observation = IWObservation(iw_event_handler.observation, problem, iw_event_handler)
        # The width-0 pass of optimized IW(1) runs no search, so the convenience views point at the
        # last pass that did.
        searched = [entry for entry in observation.by_arity if len(entry.search_tree) > 0]
        if searched:
            search_tree = searched[-1].search_tree if capture_search_tree else None
            transitions = searched[-1].transitions if observation_options.capture_admitted_transitions else None

    return SearchResult(
        status,
        solution,
        solution_cost,
        goal_state,
        statistics,
        search_tree,
        transitions,
        observation,
        observers.get_handlers() + [iw_event_handler],
    )


def abstracted_iw(
    problem: "Problem",
    start_state: "State",
    width: int = 1,
    base_abstracted: bool = False,
    preserve_goal_atoms: bool = True,
    keep_depth_one_novel: bool = False,
    layer_ordering_strategy: "Union[AdvancedILayerOrderingStrategy, None]" = None,
    max_next_layer_states: int = -1,
    beam_width: int = -1,
    beam_novelty_mode: 'Literal["all_tested", "survivors_only"]' = "all_tested",
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
    *,
    num_threads: int = -1,
    chunk_size: int = -1,
    relaxed_survivors_only_beam: bool = False,
    collect_statistics: bool = False,
    capture_search_tree: bool = False,
    capture_transitions: bool = False,
    capture_rejected_transitions: bool = False,
    capture_novel_witnesses: bool = False,
    capture_action_effect_summaries: bool = False,
    capture_realized_effects: bool = False,
) -> "SearchResult":
    """Run BrFS with Abstracted IW(k) pruning.

    AIW keeps one object position per atom feature and abstracts the remaining slots.
    By default the abstracted slots retain type signatures. `base_abstracted=True`
    forces those slots to a universal type, yielding BAIW. With
    `preserve_goal_atoms=True`, positive goal atoms are represented only as full
    object-identity atoms.

    Without any ``on_*`` callback this runs entirely in C++, with no Python event handler installed;
    with callbacks, the native observer is composed alongside them. The capture flags are the ones
    documented on :func:`iw`, and since this is a single BrFS pass the results appear directly as
    ``result.statistics``, ``result.search_tree`` and ``result.transitions``.
    """
    assert isinstance(problem, Problem), "Problem must be an instance of Problem."
    assert isinstance(start_state, State), "Start state must be an instance of State."
    assert isinstance(width, int), "width must be an int."
    assert width in (1, 2, 3), "width must be one of 1, 2, or 3."
    assert isinstance(base_abstracted, bool), "base_abstracted must be a bool."
    assert isinstance(preserve_goal_atoms, bool), "preserve_goal_atoms must be a bool."
    assert isinstance(keep_depth_one_novel, bool), "keep_depth_one_novel must be a bool."
    pruning_strategy = AdvancedAbstractedNoveltyPruningStrategy.create(
        problem._advanced_problem,
        width,
        base_abstracted,
        preserve_goal_atoms,
        keep_depth_one_novel,
    )
    return _run_brfs_width_pruning(
        problem,
        start_state,
        pruning_strategy,
        layer_ordering_strategy,
        max_next_layer_states,
        beam_width,
        beam_novelty_mode,
        randomize_equal_score_ties,
        equal_score_tie_seed,
        max_depth,
        iw1_precheck_add_effect_novelty,
        iw1_atom_first_mode,
        iw1_atom_first_ratio,
        iw1_incremental_first_applicability,
        iw1_incremental_first_applicability_debug_crosscheck,
        on_expand_state,
        on_expand_goal_state,
        on_generate_state,
        on_generate_new_state,
        on_prune_state,
        num_threads=num_threads,
        chunk_size=chunk_size,
        relaxed_survivors_only_beam=relaxed_survivors_only_beam,
        collect_statistics=collect_statistics,
        capture_search_tree=capture_search_tree,
        capture_transitions=capture_transitions,
        capture_rejected_transitions=capture_rejected_transitions,
        capture_novel_witnesses=capture_novel_witnesses,
        capture_action_effect_summaries=capture_action_effect_summaries,
        capture_realized_effects=capture_realized_effects,
    )


def projective_iw(
    problem: "Problem",
    start_state: "State",
    typed_projection: bool = False,
    keep_depth_one_novel: bool = False,
    keep_goal_nonunary_atoms: bool = False,
    layer_ordering_strategy: "Union[AdvancedILayerOrderingStrategy, None]" = None,
    max_next_layer_states: int = -1,
    beam_width: int = -1,
    beam_novelty_mode: 'Literal["all_tested", "survivors_only"]' = "all_tested",
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
    *,
    num_threads: int = -1,
    chunk_size: int = -1,
    relaxed_survivors_only_beam: bool = False,
    collect_statistics: bool = False,
    capture_search_tree: bool = False,
    capture_transitions: bool = False,
    capture_rejected_transitions: bool = False,
    capture_novel_witnesses: bool = False,
    capture_action_effect_summaries: bool = False,
    capture_realized_effects: bool = False,
) -> "SearchResult":
    """Compatibility alias for Abstracted IW(1).

    ``typed_projection=False`` maps to Base-Abstracted IW(1), while
    ``typed_projection=True`` maps to typed Abstracted IW(1). The legacy
    ``keep_goal_nonunary_atoms`` flag maps to ``preserve_goal_atoms``.
    """
    assert isinstance(typed_projection, bool), "typed_projection must be a boolean."
    assert isinstance(
        keep_goal_nonunary_atoms, bool
    ), "keep_goal_nonunary_atoms must be a boolean."
    return abstracted_iw(
        problem,
        start_state,
        width=1,
        base_abstracted=not typed_projection,
        preserve_goal_atoms=keep_goal_nonunary_atoms,
        keep_depth_one_novel=keep_depth_one_novel,
        layer_ordering_strategy=layer_ordering_strategy,
        max_next_layer_states=max_next_layer_states,
        beam_width=beam_width,
        beam_novelty_mode=beam_novelty_mode,
        randomize_equal_score_ties=randomize_equal_score_ties,
        equal_score_tie_seed=equal_score_tie_seed,
        max_depth=max_depth,
        iw1_precheck_add_effect_novelty=iw1_precheck_add_effect_novelty,
        iw1_atom_first_mode=iw1_atom_first_mode,
        iw1_atom_first_ratio=iw1_atom_first_ratio,
        iw1_incremental_first_applicability=iw1_incremental_first_applicability,
        iw1_incremental_first_applicability_debug_crosscheck=iw1_incremental_first_applicability_debug_crosscheck,
        on_expand_state=on_expand_state,
        on_expand_goal_state=on_expand_goal_state,
        on_generate_state=on_generate_state,
        on_generate_new_state=on_generate_new_state,
        on_prune_state=on_prune_state,
        num_threads=num_threads,
        chunk_size=chunk_size,
        relaxed_survivors_only_beam=relaxed_survivors_only_beam,
        collect_statistics=collect_statistics,
        capture_search_tree=capture_search_tree,
        capture_transitions=capture_transitions,
        capture_rejected_transitions=capture_rejected_transitions,
        capture_novel_witnesses=capture_novel_witnesses,
        capture_action_effect_summaries=capture_action_effect_summaries,
        capture_realized_effects=capture_realized_effects,
    )


# ----------------------------------------
# Batched parallel width-based rollouts
# ----------------------------------------


class IWRolloutResult:
    """The outcome of one rollout in a :func:`iw_parallel` batch."""

    def __init__(self, status: str, num_states: int, reached_fluent_atoms: "list[int]", reached_derived_atoms: "list[int]") -> None:
        self.status = status
        self.num_states = num_states
        #: Fluent ground-atom indices reached by this rollout. Indices are stable across
        #: rollouts, so these may be intersected across the batch.
        self.reached_fluent_atoms = reached_fluent_atoms
        self.reached_derived_atoms = reached_derived_atoms

    def __repr__(self) -> str:
        return (
            f"IWRolloutResult(status={self.status!r}, num_states={self.num_states}, "
            f"|reached_fluent_atoms|={len(self.reached_fluent_atoms)})"
        )


def iw_parallel(
    problem: "Problem",
    start_state: "State",
    max_arity: int,
    seeds: "list[int]",
    *,
    num_threads: int = 0,
    max_depth: int = -1,
    max_next_layer_states: int = -1,
    beam_width: int = -1,
    iw1_precheck_add_effect_novelty: bool = False,
    iw1_atom_first_mode: bool = False,
    iw1_atom_first_ratio: float = 1.0,
    iw1_incremental_first_applicability: bool = False,
) -> "list[IWRolloutResult]":
    """Run one stochastic IW rollout per seed, in parallel, over one shared problem.

    Every rollout starts from ``start_state`` and uses a randomized layer ordering seeded
    from its own entry in ``seeds``, so distinct seeds give distinct rollouts. Results are
    returned in seed order and are identical to running the same seeds one at a time.

    Unlike :func:`iw`, this releases the GIL for the whole batch and therefore accepts no
    Python callbacks. It requires a grounded search context; a lifted one raises.

    :param seeds: One rollout is run per seed.
    :param num_threads: Worker threads; 0 means use all cores (capped at ``len(seeds)``).
    """
    assert isinstance(problem, Problem), "Problem must be an instance of Problem."
    assert isinstance(start_state, State), "Start state must be an instance of State."
    assert isinstance(max_arity, int) and max_arity > 0, "Max arity must be a positive integer."
    assert isinstance(seeds, (list, tuple)) and all(
        isinstance(s, int) for s in seeds
    ), "seeds must be a list of ints."
    assert isinstance(num_threads, int) and num_threads >= 0, "num_threads must be a non-negative int."

    advanced_options = AdvancedIWOptions()
    advanced_options.start_state = start_state._advanced_state
    advanced_options.max_arity = max_arity
    if max_depth >= 0:
        advanced_options.max_depth = max_depth
    if max_next_layer_states > 0:
        advanced_options.max_next_layer_states = max_next_layer_states
    if beam_width > 0:
        advanced_options.beam_width = beam_width
    advanced_options.iw1_precheck_add_effect_novelty = iw1_precheck_add_effect_novelty
    advanced_options.iw1_atom_first_mode = iw1_atom_first_mode
    advanced_options.iw1_atom_first_ratio = float(iw1_atom_first_ratio)
    advanced_options.iw1_incremental_first_applicability = iw1_incremental_first_applicability

    batch_options = AdvancedIWParallelRolloutOptions()
    batch_options.seeds = list(seeds)
    batch_options.num_threads = num_threads
    batch_options.options = advanced_options

    results = advanced_iw_parallel(problem._search_context, batch_options)
    return [
        IWRolloutResult(
            r.status.name.lower(),
            r.num_states,
            list(r.reached_fluent_atoms),
            list(r.reached_derived_atoms),
        )
        for r in results
    ]
