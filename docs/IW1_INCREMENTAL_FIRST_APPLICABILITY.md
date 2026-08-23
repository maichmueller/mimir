# IW(1) Incremental First-Applicability

This patch adds an optional width-1 search optimization:

- `brfs::Options::iw1_incremental_first_applicability`
- `brfs::Options::iw1_incremental_first_applicability_debug_crosscheck`

At the root, BrFS keeps the baseline behavior and fully enumerates applicable ground actions. For non-root states, the new path avoids full applicable-action regeneration. Instead, it reuses a problem-static lifted precondition-trigger index, derives partial bindings from the parent transition's changed literals, completes those bindings with the lifted applicable-action generator, removes local duplicates, and filters out globally `ever_tested` ground actions.

## Option glossary

The user-facing knobs are:

- `iw1_precheck_add_effect_novelty`
  - Enables add-effect novelty precheck before successor generation.
  - In the benchmark CLI this is selected by `--iw1-action-selection action_first` or `both`.
- `iw1_atom_first_mode`
  - Enables the older atom-first IW(1) action ordering mode.
  - In the benchmark CLI this is selected by `--iw1-action-selection atom_first` or `both`.
  - It is intentionally unsupported together with incremental first-applicability.
- `iw1_atom_first_ratio`
  - Positive tuning parameter used by the atom-first mode and the add-effect precheck machinery.
- `iw1_incremental_first_applicability`
  - Enables the new optimization.
  - Root states still use full applicable-action generation.
  - Non-root states incrementally discover newly first-applicable, never-tested actions from the parent transition deltas.
- `iw1_incremental_first_applicability_debug_crosscheck`
  - Debug-only exact-set verification.
  - It checks that optimized candidate generation matches the baseline full applicable-action generation semantics.

The name `incremental first-applicability` is deliberate:

- `incremental` because non-root expansion works from changed literals in the incoming transition instead of regenerating the full applicable-action set
- `first-applicability` because the optimization only cares about actions that become applicable for the first time and have not already entered the real IW(1) testing pipeline

For projective IW(1), the common benchmark action-selection presets are:

- `off`: neither add-effect precheck nor atom-first
- `action_first`: add-effect precheck on, atom-first off
- `atom_first`: add-effect precheck on, atom-first on

So if you want typed projective IW(1) with add-effect precheck, without atom-first, and with incremental discovery, the intended combination is:

- novelty basis: `projective_typed`
- action-selection mode: `action_first`
- incremental mode: enabled

## Correctness assumptions

The optimization is only enabled when all of the following hold:

- width is exactly 1
- no conditional effects
- no numeric preconditions or numeric effects
- no axioms
- no derived predicates in action preconditions
- deterministic STRIPS-style fluent add/delete effects
- the applicable-action generator supports partial-binding completion

The implementation is conservative. Unsupported configurations are rejected instead of approximated.

## Supported and unsupported scope

Supported in this patch:

- classical IW(1)
- projective IW(1)
- plain BrFS
- beam search with `BeamNoveltyMode::ALL_TESTED`
- root full applicable-action generation
- non-root incremental discovery
- composition with `iw1_precheck_add_effect_novelty`
- optional exact debug cross-check against baseline full generation

Not supported in this patch:

- width greater than 1
- beam search with `BeamNoveltyMode::SURVIVORS_ONLY`
- lifted partial-binding completion with symmetry pruning enabled

Incremental first-applicability still rejects:

- `iw1_atom_first_mode`
- ordered-layer non-beam search
- relaxed `SURVIVORS_ONLY` beam search

Support is keyed to the beam novelty mode, not to whether the `ALL_TESTED` beam path runs through the serial or parallel beam machinery. The explicit
rejection is only for `BeamNoveltyMode::SURVIVORS_ONLY`, where replay-based novelty updates do not match the global `ever_tested_ground_actions`
semantics used by incremental first-applicability.

## Preprocessing and reuse

The lifted precondition-trigger map is built once during BrFS search initialization and reused for the whole search. It is not rebuilt per state, per transition, or per layer.

For each changed fluent atom in a non-root transition:

- added atoms trigger only positive lifted preconditions of the same predicate
- deleted atoms trigger only negative lifted preconditions of the same predicate

This preserves the "newly satisfied precondition" argument used by the optimization.

## Composition order

When `iw1_precheck_add_effect_novelty` is also enabled, the pipeline is:

1. candidate generation
   - root: full applicable-action generation
   - non-root: incremental discovery of newly applicable, never-tested actions
2. add-effect novelty precheck
3. real testing / successor generation

`ever_tested_ground_actions` is updated only for actions that survive the add-effect precheck and actually enter the width-1 testing pipeline. Actions filtered out by the precheck are not marked tested.

## Debug cross-check

When `iw1_incremental_first_applicability_debug_crosscheck` is enabled, every non-root state handled by the incremental path is also checked against the baseline full applicable-action generation path:

- `baseline_never_tested = full_applicable_actions - ever_tested_ground_actions`
- the incremental candidate set must match that set exactly

If add-effect precheck is enabled, the debug path also compares the filtered sets exactly by applying the precheck to `baseline_never_tested` with a throwaway controller and checking that the filtered baseline matches the filtered incremental candidate set.

Any mismatch throws with the state id, incoming parent action, changed literals, incremental candidates, missing actions, and spurious actions.

## Small Spanner regression counters

`tests/unit/search/algorithms/brfs.cpp` includes a regression on `data/spanner/iw1_incremental_regression.pddl`. The expected counters for that instance are:

- root actions fully enumerated: `5`
- non-root states using incremental path: `1`
- changed atoms processed: `3`
- partial seeds created: `0`
- non-root states with zero returned actions: `1`

This matches the intended behavior for Spanner-like cases: the root enumerates the tightening actions once, they enter the global `ever_tested` set once actually tested, and the immediate child expansion has no newly applicable never-tested actions to regenerate.
