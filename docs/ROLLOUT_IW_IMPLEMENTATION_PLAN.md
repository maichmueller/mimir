# Rollout IW(1) + Atomic-Goal Portfolio — Implementation Plan

**Status:** authoritative implementation plan (supersedes the *design* in
`ROLLOUT_IW_ATOMIC_GOAL_PORTFOLIO.md`, which is suggestive only)
**Date:** 2026-08-06
**Branch:** `projectiveiw`, baseline `dd9c191dc`
**Verified against:** the actual code at that commit; every file:line below was
checked, not copied from the design doc.

Scope confirmed with the repo owner:

- Lifted KPKC execution is **first-class and mandatory**. The target workload
  is large IPC instances where exhaustive grounding is itself the bottleneck or
  impossible; a grounded-only implementation is not an acceptable milestone
  endpoint (it is an acceptable *intermediate* testing vehicle).
- The §19 benchmark suite of the design doc is a follow-up, not part of this
  plan. Learned scorers, symmetry-pruned accelerators, and shared KPKC
  static-data refactors are out of scope (see §8).
- The existing `iw::find_rollouts_parallel` keeps its current semantics,
  untouched.

---

## 1. Verified facts this plan rests on

These are load-bearing. Re-verify only if the baseline moves.

### 1.1 What lifted grounding mutates

`ProblemImpl::ground(...)` writes into **three independent, unsynchronized
stores** on the problem object:

1. `m_repositories` (interned ground atoms/literals/actions/axioms/conditions)
   — [problem.hpp:36](../include/mimir/formalism/problem.hpp);
2. `m_flat_index_list_map` / `m_flat_index_lists` (+ double-list twins) —
   written by `ground(ConjunctiveCondition)` at
   [problem.cpp:563-580](../src/formalism/problem.cpp), by
   `ground(ConjunctiveEffect)` at problem.cpp:608-609, and by
   `get_or_create_ground_conjunctive_condition` at problem.cpp:890-892;
3. `m_details.grounding.grounding_tables` (per-schema binding→object maps,
   resized/emplaced at problem.cpp:639/700 for actions, 718/737 for axioms)
   plus the **lazily initialized, `const`-but-mutable**
   `m_details.grounding.action_infos` (problem.cpp:898-922, triggered from
   `ground(Action, ...)` at problem.cpp:662).

Call sites that reach these during lifted search: KPKC action generator
([kpkc.cpp:1522, 1572, 1703](../src/search/applicable_action_generators/lifted/kpkc.cpp)),
KPKC axiom evaluator
([kpkc.cpp:766](../src/search/axiom_evaluators/lifted/kpkc.cpp)), and the
satisficing binding generators
([base_impl.hpp:75-107](../include/mimir/search/satisficing_binding_generators/base_impl.hpp)).

### 1.2 Repository parenting — one blocker, otherwise exactly what we need

`Repositories(const Repositories* parent)` exists
([repositories.hpp:188](../include/mimir/formalism/repositories.hpp),
[repositories.cpp:210-227](../src/formalism/repositories.cpp)) and
`loki::IndexedHashSet` gives the right semantics:

- a child **continues the parent index space**: new elements are stamped
  `parent->size() + local_offset` (indexed_hash_set.hpp:135, 251-252);
- lookup-by-index in a child resolves parent entries (indexed_hash_set.hpp:147-160);
- child writes never propagate upward.

**Blocker:** the loki constructor **throws on multi-level chaining**
(indexed_hash_set.hpp:54-61), and a problem's repositories are *already*
children of the domain's
([problem_builder.cpp:38-39](../src/formalism/problem_builder.cpp)). So
`Repositories(&problem->get_repositories())` throws today. Additionally,
`operator[]`/`at`/iteration access `m_parent->m_vector[pos]` **directly**
rather than recursing through the parent's own partitioning logic — correct
for depth 1, wrong for depth 2. Task T1 fixes both via a dependency patch.

Consequences that hold regardless (and that tests must encode):

- the shared parent must stay **frozen** while any child resolves indices
  (`parent_size()` is read dynamically);
- K sibling overlays assign **the same index to different objects**; no
  worker-local index, `State`, `PackedState`, or `GroundAction` may ever cross
  overlay boundaries.

### 1.3 Identity and goal representation

- `GroundConjunctiveCondition` identity is **pointer identity of six
  `FlatIndexList*`** interned in the owning problem
  ([ground_conjunctive_condition.hpp:34, 41-52](../include/mimir/formalism/ground_conjunctive_condition.hpp)).
  The atomic goal must be canonicalized **once, in the parent problem, before
  overlays are created**; overlay workers use the parent pointer read-only
  (parent atom indices resolve inside children).
- `ProblemGoalStrategyImpl::create(problem, optional<GroundConjunctiveCondition>)`
  already supports custom goals
  ([goal_strategies.cpp:56-70](../src/search/algorithms/strategies/goal_strategies.cpp));
  working construction precedent for an atomic goal:
  [tests/unit/search/algorithms/brfs.cpp:396-416](../tests/unit/search/algorithms/brfs.cpp).
- `IGoalStrategy` methods are non-const virtuals — build **one strategy
  instance per worker**, never share one across threads.

### 1.4 KPKC ownership split

Per worker (mutable, cannot be shared):
`DynamicAssignmentSets` (rewritten per call, kpkc.cpp:1542),
per-schema `m_full_consistency_graph` scratch (V² bits), binding
scratch/statistics, event handlers. Shareable (immutable after construction):
`StaticConsistencyGraph` per schema, `StaticAssignmentSets`
([assignment_set.cpp:333-341](../src/formalism/assignment_set.cpp)).
Both assignment-set families cost **O((arity·|objects|)²) bits per predicate**
to build — this is the dominant per-worker setup cost and why
`StaticAssignmentSets` must be shared (T2) while `DynamicAssignmentSets`
duplication is accepted for v1.

**Critical constraint:** `create_applicable_action_generator` is a coroutine
that uses the generator's **single** `m_dynamic_assignment_sets`, initialized
per call (kpkc.cpp:1537-1542). At most one live enumeration per generator
instance — the rollout tree must **fully materialize** a node's applicable
actions before touching another node (see §4.3).

**Do not call** `prepare_parallel_applicable_action_generation()` /
`prepare_parallel_staged_successor_evaluation()` anywhere in the portfolio
path: they exhaustively pre-ground the type-legal atom universe
(axiom kpkc.cpp:160-221) — exactly what lifted mode exists to avoid.

Partial-binding completion already exists and is complete:
`PartialGroundActionSeed` + `create_applicable_actions_from_partial_binding`
([interface.hpp:67-104](../include/mimir/search/applicable_action_generators/interface.hpp),
kpkc.cpp:1467-1535), available only with symmetry pruning OFF
(kpkc.cpp:1445-1448). Unification precedent (preconditions):
[incremental_iw1.cpp:89-141](../src/search/algorithms/brfs/incremental_iw1.cpp).

### 1.5 Search infrastructure

- BrFS tests the goal **on pop, before expansion** (brfs.cpp:418) — the
  node-pop lower-bound certificate maps directly onto the existing loop.
  G-layer bookkeeping already exists (brfs.cpp:410-416, `on_finish_g_layer`).
- `DynamicNoveltyTable::resize_to_fit` already handles a **growing atom
  universe** ([novelty_table.cpp:43-84](../src/search/algorithms/iw/novelty_table.cpp))
  — canonical lifted IW(1) needs no novelty-table work.
- **No cancellation, no atomics, no shared deadline exist anywhere** in mimir
  (grep-verified). `iw::Options` does not even carry `max_time_in_ms` /
  `max_num_states`, and iw.cpp:81-107 never forwards them to BrFS. T5 adds all
  of this.
- A `State` must never cross a thread boundary: non-atomic pooled refcount
  ([shared_object_pool.hpp:58-76](../include/mimir/algorithms/shared_object_pool.hpp));
  the historical corruption bug is `09cbe5b5a`, defensive pattern at
  [parallel_rollouts.cpp:194-205](../src/search/algorithms/iw/parallel_rollouts.cpp).
  Dense `FlatBitset` descriptions + `get_or_create_state(FlatBitset, FlatDoubleList)`
  (state_repository.hpp:189-203) are the portable transport — valid across
  repositories **within one parent/overlay chain** (parent atom indices are
  stable in children; overlay-local indices are not portable anywhere).
- Threading substrate: vendored `BS::thread_pool`; detached tasks must catch
  their own exceptions (parallel_rollouts.cpp:224-273) or use `submit_task`
  (which captures into the future, BS_thread_pool.hpp:649-683).
- `SearchContextImpl::create` enforces `shared_ptr` identity between problem,
  generator, and repository (search_context.cpp:92-104) — each overlay must be
  its own `Problem` handle with its own generator/evaluator/repository.

### 1.6 Build / bindings mechanics

- `src/CMakeLists.txt:10-21` globs sources **without** `CONFIGURE_DEPENDS`:
  new `.cpp` files require a CMake re-configure.
- New public headers must be added to
  [include/mimir/search/algorithms.hpp:25-38](../include/mimir/search/algorithms.hpp)
  to reach `mimir.hpp` and the bindings TU.
- C++ tests are registered **explicitly** in
  [tests/unit/CMakeLists.txt](../tests/unit/CMakeLists.txt) via `add_gtest`.
- Bindings all live in
  [python/src/pymimir/advanced/search/bindings.cpp](../python/src/pymimir/advanced/search/bindings.cpp)
  (`bind_module_definitions`, line 300; batched-rollouts block 1307-1374 is the
  model). GIL release = explicit `nb::gil_scoped_release` inside the lambda
  (line 1362); `nb::call_guard` is used nowhere. New names must be re-exported
  in `python/src/pymimir/advanced/search/__init__.py` (IW block, lines
  137-152). Interned-pointer rule: any `std::optional<const XImpl*>` return
  must use `rv_policy::reference_internal`, never `copy` (commit `e9077a27d`).

---

## 2. Design decisions

Numbered so review comments can reference them. D1-D4 deviate from or firm up
the design doc; the rest resolve its open choices.

- **D1 — Overlay = sibling `ProblemImpl`, not a new class.**
  `create_grounding_overlay(parent)` returns an ordinary
  `Problem` (`shared_ptr<ProblemImpl>`) whose repositories are parented on the
  parent problem's. Rationale: the entire search stack (KPKC generator, axiom
  evaluator, `StateRepositoryImpl`, `SearchContextImpl` identity checks)
  consumes `Problem` handles; a separate overlay class would force an
  interface refactor across all of them for zero benefit. The design doc
  offered both; the code makes this one obviously right.
- **D2 — Patch loki's `IndexedHashSet` for multi-level chaining.** The design
  doc assumed `Repositories(&problem_repos)` works; it throws (§1.2). The
  patch is small (relax the guard, make index resolution recurse via the
  parent's public accessors) and follows the existing dependency-patch
  precedent (`dependencies/valla/patches/0001-...`, CMake-gated). No sound
  mimir-side workaround exists because the interned index is stamped inside
  loki (indexed_hash_set.hpp:135).
- **D3 — Share `StaticAssignmentSets` via `std::shared_ptr<const>`.** It is
  quadratic in object count per predicate (§1.4) and read-only after
  construction (already read concurrently today). Copying it K+1 times on
  exactly the instances this feature targets (huge object universes) is the
  wrong default. Mechanical refactor of `ProblemImpl::m_static_assignment_sets`.
- **D4 — Full per-node action materialization; no resumable cursors.** Forced
  by the one-live-coroutine constraint (§1.4). "No more children" is exact by
  construction, which is what SOLVED propagation needs.
- **D5 — Standalone `rollout_iw::find_solution` is mode-agnostic and
  single-threaded.** It grounds into whatever `SearchContext` it is given
  (sole user, so mutation is safe) and contains no frozen-repository
  assumption. Overlays are purely a *portfolio* concern.
- **D6 — Features = ground fluent atoms**, matching existing IW(1)
  (`ArityKNoveltyPruningStrategy`). Derived atoms are not features. The
  feature-depth table is a plain `std::vector<uint32_t>` indexed by fluent
  atom index, resized with `resize(overlay_fluent_atom_count, INF)` before
  indexing any newly grounded atom.
- **D7 — Cancellation/progress via a small shared-control struct of relaxed
  atomics**, checked once per BrFS pop / per rollout step / every N KPKC
  coroutine yields. No callbacks, no Python. Also fixes the existing gap:
  `iw::Options` gains and forwards `max_time_in_ms` / `max_num_states`.
- **D8 — Incumbent transport is schema+binding only.**
  `PlanStep { formalism::Action schema; formalism::ObjectList binding; }` —
  both parent/domain-stable (the object universe is fixed at parse; grounding
  never creates objects). Never a worker-local `GroundAction`. Final
  canonicalization grounds only the winning O(plan-length) steps into the
  parent, on the coordinator, **after** all workers have joined (this is also
  the only moment the parent may grow — after which no overlay index is ever
  resolved again).
- **D9 — Symmetry pruning OFF everywhere in the portfolio in v1** (certifier
  *and* accelerators). Enforced at construction; also required for
  partial-binding ordering (kpkc.cpp:1445-1448).
- **D10 — v1 accepts O(K) duplicated KPKC setup** (`DynamicAssignmentSets`,
  per-schema consistency graphs) per the design doc's own guidance. The
  raw-pointer-sharing precedent (`ParallelKPKCAxiomGrounder`, axiom
  kpkc.cpp:345-352) is the follow-up path, not v1.
- **D11 — Mode handling.** `GROUNDED` requires a grounded input context and
  reuses the proven shared-generator/private-repo pattern verbatim.
  `LIFTED_KPKC` requires a lifted KPKC input context and builds K+1 overlays.
  `INHERIT_CONTEXT` dispatches via the `dynamic_pointer_cast` probe
  (parallel_rollouts.cpp:165-172). No mode ever falls back to pre-grounding.
- **D12 — All validation happens on the calling thread before any task is
  spawned** (invalid mode/context, symmetry pruning on, statically
  unsatisfiable goal, empty schedules) so failures are exceptions, not aborts.

---

## 3. Components and tasks

Execute in order; each task ends with its listed gate green. New sources need
a CMake re-configure (§1.6).

### T1 — loki `IndexedHashSet` multi-level chaining patch

1. Determine how loki is provisioned (check `dependencies/CMakeLists.txt` /
   superbuild vs. Conan; `dependencies/installs/include/loki/...` is the
   installed copy). Mirror the valla patch mechanism:
   `dependencies/valla/patches/0001-indexed-hash-set-lock-free-read-path.patch`
   + its CMake application (`dependencies/valla/CMakeLists.txt:45-57`).
   **Pitfall:** the dependency CMake short-circuits when
   `dependencies/installs` is already populated — the patched header must
   actually land in the installed tree (wipe/rebuild the dependency or verify
   the installed header changed).
2. Patch content (`indexed_hash_set.hpp`):
   - remove the multi-chaining throw (lines 54-61);
   - `operator[]` / `at`: replace direct `m_parent->m_vector[pos]` with
     recursion through the parent's own `operator[]`/`at`;
   - audit `find` / `insert` / `get_or_create` parent probes and the
     `const_iterator` walk: every parent access must go through the parent's
     *public* recursive API, never its private members;
   - `size()` / `parent_size()` already recurse correctly.
3. Unless mimir builds loki from a patchable source checkout, fall back to a
   clearly-marked vendored header override — but prefer the patch.

**Gate:** new dependency-level or mimir-level unit test constructing a 3-level
chain (domain → problem → overlay) exercising: index continuation, cross-level
`get_or_create` dedup (parent hit returns parent element), `operator[]`
resolution at all three levels, iteration order.

### T2 — `ProblemImpl::create_grounding_overlay`

New factory in [problem.hpp](../include/mimir/formalism/problem.hpp) /
problem.cpp (no new file):

```cpp
static Problem create_grounding_overlay(const Problem& parent);
```

An overlay is a `ProblemImpl` that:

- **shares by pointer copy** all immutable description members (§1 inventory
  items 1, 3-18: domain handle, objects, predicates, initial/goal literal
  lists, axioms, requirements — all raw-pointer vectors into parent/domain
  repositories; cheap vector copies). Keep the parent's `m_index`;
- holds `Repositories m_repositories{&parent->get_repositories()}` (T1);
- holds **fresh, empty** `m_flat_index_list_map` / `m_flat_index_lists` /
  double-list twins, fresh `valla` interning tables, fresh grounding tables;
- rebuilds `m_details` against itself (cheap: hash maps, axiom stratification;
  the `GoalDetails` re-interning writes a few overlay-local flat lists —
  harmless). Force the **parent's** lazy
  `m_details.grounding.get_action_infos()` once before overlay creation and
  copy the materialized list into each overlay so no overlay recomputes
  consistency-graph vertices lazily mid-search;
- shares `StaticAssignmentSets` via the D3 `shared_ptr<const>` refactor;
- stores `Problem m_overlay_parent` (null for ordinary problems) to keep the
  parent alive and to make `is_grounding_overlay()` assertable.

`ProblemImpl` is non-copyable with a private ctor friended to
`ProblemBuilder` (problem.hpp:73-100) — add a private overlay ctor (or a
second friend factory); do **not** loosen public constructibility.

**Gate:** `tests/unit/formalism/problem_grounding_overlay.cpp` (register in
tests/unit/CMakeLists.txt):
overlay creation cost is O(description) — no reparse (assert repository sizes
start at parent values); grounding the same action schema/binding in two
overlays yields independent local entries with identical parent-relative
semantics but (potentially) different local indices; parent repositories and
flat maps are byte-size-unchanged after overlay grounding (reuse the hana
snapshot pattern from
[iw_parallel_rollouts.cpp:160-181](../tests/unit/search/algorithms/iw_parallel_rollouts.cpp));
a KPKC generator + axiom evaluator + state repository constructed against an
overlay run a small lifted BrFS end-to-end (harness precedent:
tests/unit/search/algorithms/iw.cpp:58+).

### T3 — Rollout IW(1) core (`mimir::search::rollout_iw`)

Files: `include/mimir/search/algorithms/rollout_iw.hpp`,
`src/search/algorithms/rollout_iw.cpp`; add the header to
`include/mimir/search/algorithms.hpp`; forward declarations in
`include/mimir/search/declarations.hpp`.

API essentially as the design doc §9 (`Options`, `Statistics`, `Result`,
`find_solution(const SearchContext&, const Options&)`), with:
`goal_strategy` (native only), `action_ordering` (T4 strategy),
`seed`, `max_depth`, `max_rollouts`, `max_num_states`, `max_time_in_ms`,
plus `const SearchControl* control` (T5) and
`uint32_t incumbent_bound` support for portfolio pruning.

Algorithm (Bandres–Bonet–Geffner, IW(1) features per D6):

- **Tree nodes** in a flat `std::vector<Node>`:
  `{ Index state_index; uint32_t parent; uint32_t incoming_action_pos;
     uint32_t depth; Label label /*NEW|OPEN|SOLVED*/;
     std::optional<std::vector<GroundAction>> actions;  // materialized on first need (D4)
     std::vector<uint32_t> children; /* parallel to actions, kInvalid = untried */ }`.
  States live in the context's `StateRepository` (interned; tree nodes are
  distinct even when states repeat — it is a tree over action sequences, no
  cross-branch state dedup).
- **Feature-depth table** `std::vector<uint32_t> best_depth` over fluent atom
  indices, `INF`-initialized, root features registered at depth 0 at the start
  of every rollout batch per the reference algorithm; `resize` before indexing
  any atom ≥ current size (lifted growth).
- **Rollout:** start at root; at node `n`, ask the ordering strategy for a
  ranked permutation of `n`'s materialized actions; take the first candidate
  whose child is not SOLVED. Untried candidate → generate successor:
  - **Case 1** (new node, improves `best_depth` for ≥1 true feature): update
    depths, continue the rollout from it;
  - **Case 2** (new node, improves nothing): label it SOLVED, end rollout;
  already-generated child → descend:
  - **Case 3** (existing node, none of its true features currently at its best
    depth): label SOLVED, propagate, end rollout;
  - **Case 4** (existing node, some feature at its best depth): continue
    through it.
  Per-case counters in `Statistics`.
- **SOLVED propagation:** a node becomes SOLVED only when *all* materialized
  children are SOLVED (exact because of D4); propagate toward the root;
  `root_solved` terminates.
- **Goal and incumbent:** test the goal on every newly created state **before**
  any depth-bound reasoning. With incumbent bound `L`, a non-goal node at
  depth `d >= L - 1` is labeled SOLVED-for-improvement (goal test first).
  SOLVED labels persist once set (paper semantics).
- **Plan extraction:** walk parents; emit the D8 schema+binding steps (also
  build an ordinary `Plan` for the standalone entry point, via the context's
  own repository — safe single-threaded).
- **Budgets/cancellation:** check `control` + stopwatch per rollout step and
  every ~64 yields inside action materialization (§13.3 of the design doc).

**Gate:** `tests/unit/search/algorithms/rollout_iw.cpp` — see §5 group B.

### T4 — Action-ordering strategies

Files: `include/mimir/search/algorithms/rollout_iw/action_ordering.hpp` + src.

```cpp
class IActionOrderingStrategy {
    virtual void rank(const State&, const std::vector<GroundAction>&,
                      std::vector<uint32_t>& out_order) = 0; };
```

Built-ins: `in_order`; `randomized(seed)` (per-instance `std::mt19937_64`);
`direct_goal_achiever_first` — rank first the actions whose add effects
contain the goal atom (grounded: inspect ground effects; lifted: unify the
goal atom against schema **effect** literals — mirror
`unify_trigger_with_ground_atom` (incremental_iw1.cpp:89-141) but over
`conditional_effect->get_conjunctive_effect()->get_literals()`, then mark the
matching instances; the seed/completion machinery of §1.4 exists for this);
`goal_regression_relevance` — a one-time, schema-level regression
stratification: predicates achieving the goal predicate rank 0, predicates in
those achievers' preconditions rank 1, etc.; actions scored by the best rank
their add effects touch; `mixed_regression_random` — regression ranks with
seeded random tie-breaking. Ordering is **never pruning**: every applicable
action stays in the permutation (a dedicated test makes a hostile ordering
rank the width-1 path last and still solve).

### T5 — Shared control + certifier plumbing

New header `include/mimir/search/algorithms/search_control.hpp`:

```cpp
struct SearchControl {
    std::atomic<bool>     cancel{false};
    std::atomic<uint32_t> incumbent_length{UINT32_MAX};
    std::atomic<uint32_t> completed_depth{UINT32_MAX};   // certifier writes; UINT32_MAX = none
    std::atomic<uint64_t> total_expansions{0};
};
```

- `brfs::Options` gains `SearchControl* control = nullptr` (read `cancel`
  once per pop next to the existing stopwatch check, brfs.cpp:394-398 →
  new status `SearchStatus::CANCELED` or reuse `OUT_OF_TIME` with a distinct
  stop reason — prefer adding `CANCELED` to
  [utils.hpp:29-39](../include/mimir/search/algorithms/utils.hpp));
  publish `completed_depth` in the existing g-layer bookkeeping
  (brfs.cpp:410-416); bump `total_expansions` per expansion; optional
  early-stop when `completed_depth + 1 >= incumbent_length`.
- `iw::Options` gains and **forwards** `max_time_in_ms`, `max_num_states`,
  and `control` into each per-arity `brfs::Options` (fixing the existing gap
  at iw.cpp:81-107).
- Serial paths with `control == nullptr` must be zero-overhead (single
  branch); guard with a benchmark-eyeball, not speculation.

**Certificate semantics** (unchanged from the design doc, now tied to code):
goal test happens on pop, so once all depth-`L-1` nodes are popped non-goal,
no plan shorter than `L` exists *within the IW(1)-pruned space*; the monotone
lower bound is `completed_depth + 1`, and an incumbent of length `L` is
certified when `completed_depth >= L - 1`. The certifier runs `max_arity = 1`,
complete enumeration, no beam/caps/abstraction, symmetry off (D9), no
goal-oriented reordering.

### T6 — Atomic-goal portfolio (`mimir::search::iw`)

Files: `include/mimir/search/algorithms/iw/atomic_goal_portfolio.hpp` + src.
API as design doc §10 (`AtomicGoalPortfolioOptions/Result`,
`find_solution_atomic_goal_portfolio`), with `stop_reason`, per-worker
statistics, `certified_optimal`, `iw_lower_bound`, `winning_worker`,
`executed_mode`.

Coordinator sequence:

1. **Validate everything on the calling thread** (D12). Canonicalize the
   atomic goal in the parent if the caller passed literals rather than an
   interned condition; test static satisfiability via
   `ProblemGoalStrategyImpl`; strip any caller `State` from options and
   capture a `DenseStartState` (parallel_rollouts.cpp:53-65 pattern).
2. **Build workers.** Grounded: K+1 × (`StateRepositoryImpl::create(evaluator,
   PrivateInterningTables{})` + shared generator) — verbatim
   parallel_rollouts.cpp:207-220. Lifted: K+1 × (overlay (T2) + own
   `KPKCLiftedApplicableActionGeneratorImpl(overlay)` with pruning OFF + own
   `KPKCLiftedAxiomEvaluatorImpl(overlay)` + `StateRepositoryImpl` bound to
   the overlay's tables + `SearchContextImpl::create(overlay, ...)`).
   Per-worker goal strategy against the parent-interned goal condition;
   per-worker quiet native event handlers; per-worker start state re-created
   from the dense description (parent indices — valid in overlays, §1.5).
3. **Run** on `BS::thread_pool` (`submit_task` for exception capture): worker
   0 = canonical IW(1) certifier (T5 options), workers 1..K = `rollout_iw`
   with per-worker seeds/ordering configurations (`rollout_orderings`,
   defaulting to a mixed portfolio).
4. **Incumbent:** `SearchControl` + `std::mutex` guarding
   `{ std::vector<PlanStep> best_plan; uint32_t source_worker; uint64_t version; }`.
   Publication: CAS-improve `incumbent_length`, then lock and store steps.
   Monotone by construction.
5. **Stop conditions:** certifier SOLVED (its plan is the certified-shortest
   under width-1 assumptions → cancel all); `completed_depth >= L - 1`
   (incumbent certified → cancel); budget/deadline (uncertified,
   `stop_reason` says so); all workers exhausted.
6. **Finalize:** join everything; then, on the coordinator, ground the winning
   schema+binding sequence into the **parent** problem step by step, replay it
   through the caller context's state repository to build the ordinary `Plan`,
   and assert applicability of every step (D8). Parent growth happens only
   here.

**Gate:** §5 groups C/D.

### T7 — Python bindings

New section in `bindings.cpp` after the batched-rollouts block (line 1374):
`RolloutIWOptions/Statistics/Result`, `ActionOrderingConfiguration`,
`AtomicGoalPortfolioSearchMode`, `AtomicGoalPortfolioOptions/Result`,
`find_solution_rollout_iw`, `find_solution_atomic_goal_iw_portfolio` — each
entry point as a lambda with `nb::gil_scoped_release` and **native-only**
strategies/handlers (reject Python trampolines up front, D12). Follow the
§1.6 mechanics: re-export in `advanced/search/__init__.py`; opaque vectors →
`init_declarations.hpp`; mind the interned-pointer `rv_policy` rule for
anything exposing `GroundConjunctiveCondition` or similar
(`optional<Plan>` is a value type and safe to copy). A high-level
`wrapper_search_width.py` convenience can follow later; the advanced API is
the deliverable.

**Gate:** `python/tests/search/test_rollout_iw.py` — §5 group E.

---

## 4. Things that will silently bite (checklist for the implementer)

1. One live KPKC enumeration per generator instance — never hold two
   suspended `create_applicable_action_generator` coroutines (§1.4/D4).
2. Never let a `State`/`PackedState`/pooled handle cross threads; dense
   descriptions only, and only downward within one parent chain (§1.5).
3. Parent must not grow while any overlay search resolves indices; growth is
   legal only in the T6 finalize step (§1.2, D8).
4. Same index ⇒ different objects across sibling overlays; never compare
   worker-local indices (§1.2).
5. Goal strategies and event handlers: one per worker, native, quiet
   (§1.3; the raced-statistics warning at state_repository.hpp:94-105).
6. Don't call `prepare_parallel_*` lookup-table builders — they pre-ground the
   universe (§1.4).
7. Detached pool tasks swallow exceptions into `std::terminate` — use
   `submit_task` or manual `exception_ptr` capture (§1.5).
8. New `.cpp` files: re-run CMake configure (no `CONFIGURE_DEPENDS`); new
   headers into `algorithms.hpp`; new tests into `tests/unit/CMakeLists.txt`
   (§1.6).
9. The dependency superbuild short-circuits on a populated
   `dependencies/installs` — verify the loki patch actually lands (T1).
10. `nb::rv_policy` interned-pointer rule for optional interned returns
    (§1.6).
11. Atom indices are per-`Problem`; never compare across problems, including
    grounded-vs-lifted contexts of the same PDDL
    (`PARALLEL_IW_ROLLOUTS_HANDOFF.md` §3.2).

---

## 5. Test plan

C++ tests via `add_gtest` in `tests/unit/CMakeLists.txt`; use the invariant-
comment style of `iw_parallel_rollouts.cpp`. Run the portfolio tests once
under TSAN (precedent: `tests/run_parallel_beam_tsan.sh`).

**A. Overlay (`tests/unit/formalism/problem_grounding_overlay.cpp`)** — T1/T2
gates above, plus: two overlays grounding disjoint action sets grow
independently; overlay destruction leaves the parent untouched.

**B. Rollout IW (`tests/unit/search/algorithms/rollout_iw.cpp`)**
- exact four-case accounting on a tiny hand-built domain where the case
  sequence is derivable by hand (counters must match exactly);
- SOLVED only after *every* child is SOLVED (a node with one unexpanded
  child must stay OPEN);
- hostile ordering ranks the width-1 path last → still solved (ordering ≠
  pruning);
- goal tested before incumbent-depth cutoff (goal exactly at depth `L-1`
  with incumbent `L` must be found);
- schema+binding plan extraction replays to a valid plan;
- works unchanged on a grounded context and on a lifted KPKC context
  (same domain/instance, same plan length);
- budget expiry returns a valid uncertified result.

**C. Portfolio grounded (`tests/unit/search/algorithms/atomic_goal_iw_portfolio.cpp`)**
- fixed seeds ⇒ deterministic result; serial (K=0) equals certifier-only;
- parent repositories and shared valla tables frozen during the run (hana
  snapshot), grow only during finalization, and only by O(plan length);
- certificate at plan lengths 1/2/3 (`certified_optimal`,
  `iw_lower_bound == plan_length`);
- cancellation stops all workers promptly (bounded wall-clock assert);
- incumbent monotonicity under a rollout worker that finds a longer plan
  first;
- existing serial IW and `find_rollouts_parallel` behavior unchanged
  (existing suites stay green).

**D. Portfolio lifted (same file)**
- K+1 lifted workers leave parent repositories/flat maps/grounding tables
  untouched while running (snapshot before/after the parallel phase,
  *before* finalization);
- overlays grow independently; same tuple, different local indices, no
  corruption (grow two overlays asymmetrically, then verify each resolves
  its own entries);
- axioms/derived predicates solved correctly with worker-local evaluators
  (use an axiom domain, e.g. blocksworld-derived or a `data/` domain with
  axioms);
- grounded and lifted portfolios agree on optimal plan length on small
  instances (`data/blocks_4`, `data/spanner`, gripper);
- numeric/conditional-effect inputs either work or are rejected by explicit
  v1 guards (assert the guard message, not a crash).

**E. Python (`python/tests/search/test_rollout_iw.py`)** — smoke both entry
points in both modes on small instances; GIL released (run in a thread and
assert main-thread progress); options/results round-trip; a Python-subclassed
goal strategy is rejected with a clear error.

---

## 6. Suggested implementation order

T1 → T2 (the mandatory-mode critical path first) in parallel with T3 → T4
(algorithm core is context-agnostic and testable on grounded contexts
immediately), then T5 → T6 → T7. The portfolio lands grounded-first only in
the sense that its grounded tests can run before the overlay tests finish —
lifted support is the same code path via D1/D5 and ships in the same change.

## 7. Definition of done

The design doc's §21 list applies verbatim, minus benchmarks, with the
additions: the loki chain patch is gated, tested, and documented; overlay
isolation and index-continuation tests pass under TSAN; `iw::Options` budget
forwarding is fixed and covered by a test.

## 8. Explicitly out of scope (follow-ups)

Benchmark suite (design doc §19; existing `tests/run_*` scripts are the
starting point); shared KPKC static-data refactor (D10); symmetry-pruned
accelerator modes; learned/LibTorch ordering; high-level Python wrapper
sugar; sharing overlay-ground actions across workers.

## 9. References

- `docs/ROLLOUT_IW_ATOMIC_GOAL_PORTFOLIO.md` — requirements source (suggestive).
- `docs/PARALLEL_IW_ROLLOUTS.md`, `docs/PARALLEL_IW_ROLLOUTS_HANDOFF.md` —
  measured grounded-parallel design + pitfalls (valla contention, index
  identity, order-dependence of reached-atom sets).
- `docs/IW1_INCREMENTAL_FIRST_APPLICABILITY.md` — partial-binding precedent.
- Bandres, Bonet, Geffner. *Planning With Pixels in (Almost) Real Time.*
  AAAI 2018. https://arxiv.org/abs/1801.03354
