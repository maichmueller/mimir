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
from pymimir.advanced.search import IWOptions as AdvancedIWOptions

# from pymimir.advanced.search import IWStatistics as AdvancedIWStatistics
# from pymimir.advanced.search import SIWOptions as AdvancedSIWOptions
# from pymimir.advanced.search import SIWStatistics as AdvancedSIWStatistics

from .wrapper_formalism import GroundAction, Problem, State
from .wrapper_search import SearchResult


# ----------------------
# Width-based algorithms
# ----------------------


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
) -> "SearchResult":
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

    advanced_options = AdvancedBrFSOptions()
    advanced_options.start_state = start_state._advanced_state
    advanced_options.event_handler = EventHandler()
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
    return SearchResult(status, solution, solution_cost, goal_state)


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
) -> "SearchResult":
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

    advanced_options = AdvancedIWOptions()
    advanced_options.start_state = start_state._advanced_state
    advanced_options.brfs_event_handler = EventHandler()
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
    result = advanced_iw(problem._search_context, advanced_options)
    status = result.status.name.lower()
    solution = (
        [GroundAction(x, problem) for x in result.plan.get_actions()]
        if result.plan
        else None
    )
    solution_cost = result.plan.get_cost() if result.plan else None
    goal_state = State(result.goal_state, problem) if result.goal_state else None
    return SearchResult(status, solution, solution_cost, goal_state)


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
) -> "SearchResult":
    """Run BrFS with Abstracted IW(k) pruning.

    AIW keeps one object position per atom feature and abstracts the remaining slots.
    By default the abstracted slots retain type signatures. `base_abstracted=True`
    forces those slots to a universal type, yielding BAIW. With
    `preserve_goal_atoms=True`, positive goal atoms are represented only as full
    object-identity atoms.
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
    )
