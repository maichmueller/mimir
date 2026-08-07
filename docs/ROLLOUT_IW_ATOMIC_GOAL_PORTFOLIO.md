# Native Rollout IW(1) and Atomic-Goal Portfolio

**Status:** implementation handoff / source of truth  
**Date:** 2026-08-04  
**Repository:** `maichmueller/mimir`  
**Required branch:** `projectiveiw`  
**Consumer:** `maichmueller/hierarchical`

## 1. Revised decision

Implement true Bandres–Bonet–Geffner **Rollout IW(1)** in C++ on the
`projectiveiw` branch and expose a native atomic-goal portfolio consisting of:

```text
1 canonical breadth-first IW(1) certifier
K independent goal-guided Rollout-IW(1) accelerators
```

The production implementation must support **both** Mimir execution modes:

```text
grounded mode: training / feasibility-mask generation / small instances
lifted KPKC mode: validation and test-time search when pre-grounding is too costly
                  or cannot be completed
```

Lifted support is not a later optimization or optional extension. The portfolio
is not complete for the hierarchical use case until true Rollout IW, the
canonical IW certifier, and their parallel coordination all work with lifted
KPKC action and axiom generation.

The production search must not be implemented in Python. Python in
`hierarchical` should only:

- select/build the grounded atomic goal;
- choose grounded or lifted execution mode;
- choose native ordering/configuration modes;
- call one GIL-free Mimir entry point;
- translate the native result into worker plan/result objects.

The search threads, novelty bookkeeping, state/grounding repositories,
incumbent publication, cancellation, and breadth-first lower-bound certificate
belong in Mimir C++.

## 2. Critical terminology: the existing parallel-IW API is not Rollout IW

The branch already contains:

```text
include/mimir/search/algorithms/iw/parallel_rollouts.hpp
src/search/algorithms/iw/parallel_rollouts.cpp
docs/PARALLEL_IW_ROLLOUTS.md
```

`iw::find_rollouts_parallel(...)` currently runs **K independent ordinary
IW/BrFS searches with randomized breadth-layer ordering**. It is useful and
fast, but it is not the Rollout IW algorithm from Bandres et al. It does not:

- descend along one path across depth layers;
- maintain minimum feature depths `d[f]`;
- implement the four new/existing-node rollout cases;
- propagate `SOLVED` labels through a rollout tree.

Do not rename the existing API or silently change its semantics. Add a distinct
namespace/API for true Rollout IW, for example:

```text
mimir::search::rollout_iw
```

The existing `find_rollouts_parallel` may remain grounded-only. The **new true
Rollout-IW and atomic-goal portfolio APIs must support grounded and lifted KPKC
modes**.

## 3. Why lifted mode changes the systems design

The current grounded parallel-IW implementation can share one `Problem`, one
grounded applicable-action generator, and one grounded axiom evaluator because
all PDDL grounding is completed before search. During the search, the problem's
formalism repositories are frozen.

That assumption does not hold for lifted KPKC search:

- `KPKCLiftedApplicableActionGeneratorImpl::create_applicable_action_generator`
  generates an object binding and calls `m_problem->ground(action, binding)`;
- lifted axiom evaluation similarly grounds axiom instances on demand;
- `ProblemImpl::ground(...)` mutates the problem's ground repositories, action/
  axiom grounding tables, flat index/double-list maps, and related caches;
- the current `loki::IndexedHashSet` repositories are not a safe shared mutable
  target for K+1 independent search threads;
- a single shared lifted generator is also invalid because it owns mutable
  `DynamicAssignmentSets`, grounding-generator scratch, statistics, and caches.

Therefore, lifted parallelism cannot reuse the grounded model of “one shared
mutable `Problem` plus private state repositories.” It needs **worker-local
on-demand grounding state** while still sharing the parsed lifted model.

## 4. Required two-path ownership model

### 4.1 Grounded path

For grounded mode, reuse the design already implemented and measured in
`docs/PARALLEL_IW_ROLLOUTS.md`:

Shared across workers:

- parsed and fully grounded `Problem`;
- frozen formalism repositories;
- grounded applicable-action generator and match tree;
- grounded axiom evaluator.

Private per worker:

- `StateRepositoryImpl` with `PrivateInterningTables`;
- search tree/open data;
- novelty or feature-depth tables;
- ordering RNG and statistics.

This remains the preferred training path because the feasibility-mask and
worker-pretraining pipelines already operate on manageable, pre-grounded
instances.

### 4.2 Lifted KPKC path

For lifted mode, share only immutable lifted problem structure. Give every
canonical-IW or Rollout-IW worker its own **grounding overlay** containing:

- child formalism repositories;
- action and axiom grounding caches;
- flat index-list and double-list caches;
- grounding scratch/object pools;
- KPKC dynamic assignment sets and mutable binding-generator state;
- lifted applicable-action generator;
- lifted axiom evaluator;
- state repository and private state interning tables;
- search/novelty data.

The parent parsed model remains shared and immutable:

- domain and action/axiom schemas;
- objects, predicates, types, terms, and parameters;
- initial/goal descriptions;
- static problem details and static consistency information where safely
  shareable;
- other immutable preprocessing.

No search worker may call `ground(...)` against the shared parent problem.

## 5. Worker-local grounding overlays

### 5.1 Existing parent-repository support

`formalism::Repositories` already has a parent-backed constructor:

```cpp
explicit Repositories(const Repositories* parent = nullptr);
```

Each child `loki::IndexedHashSet` can therefore resolve objects already present
in the parent while storing newly grounded objects in a private local layer.
This is the right foundation for lifted worker isolation.

### 5.2 Required abstraction

Add a cheap factory or explicit abstraction such as one of:

```cpp
Problem ProblemImpl::create_grounding_overlay(const Problem& parent);
```

or

```cpp
class ProblemGroundingOverlayImpl;
using ProblemGroundingOverlay = std::shared_ptr<ProblemGroundingOverlayImpl>;
```

The exact class boundary may change, but it must provide the complete interface
needed by:

- KPKC lifted applicable-action generation;
- KPKC lifted axiom evaluation;
- state construction and successor application;
- atom-index lookup and novelty;
- plan extraction.

A worker overlay must own at least:

```text
Repositories child_repositories(parent = &base_problem.repositories)
action grounding tables
axiom grounding tables
flat index-list map/vector
flat double-list map/vector
grounding pools/scratch
state interning tables
```

It must reference or cheaply copy immutable parent data rather than reparsing
PDDL or performing exhaustive grounding.

### 5.3 Do not clone by reparsing

Creating K+1 independent `ProblemImpl::create(domain_file, problem_file)`
instances would avoid races but would repeat parsing and preprocessing and
would defeat the test-time purpose. It is not the intended design.

The target setup cost is:

```text
one parse + one immutable lifted preprocessing pass
+ O(K) lightweight grounding-overlay construction
```

not:

```text
O(K) parse + O(K) full preprocessing or pre-grounding
```

### 5.4 Refactor immutable and mutable problem data if necessary

`ProblemImpl` currently combines immutable problem description with mutable
search-time grounding state. A clean implementation may factor it into:

```text
ProblemStaticData          # shared
ProblemGroundingWorkspace  # private per worker
```

or an equivalent arrangement.

Likely mutable members include:

- `m_repositories` for newly grounded objects;
- `m_details.grounding.grounding_tables`;
- `m_flat_index_list_map` / `m_flat_index_lists`;
- `m_flat_double_list_map` / `m_flat_double_lists`;
- grounding-related object pools.

State interning (`valla` tables) is already made private through
`StateRepositoryImpl::PrivateInterningTables` and remains private as well.

Do not merely protect a shared `ProblemImpl::ground` with a mutex. That would be
correct but would serialize the dominant lifted operation and make K rollout
threads largely useless.

## 6. Index and object identity under overlays

### 6.1 Parent objects remain stable

Schemas, objects, predicates, initial atoms, and the selected manager goal
already present in the parent repository retain their parent identities and
indices when observed through a child repository.

Before launching the portfolio, canonicalize the selected positive fluent goal
atom once in the parent problem. Every overlay can then refer to that inherited
atom consistently.

### 6.2 Newly grounded objects are worker-local

The same action/atom tuple may receive different local indices in different
worker overlays because the grounding order differs. This is acceptable:

- novelty and feature-depth tables are private to each worker;
- states and ground actions never cross overlay boundaries;
- each search is semantically independent.

It is forbidden to compare worker-local `State`, `PackedState`, `GroundAction`,
or newly grounded atom indices across workers.

### 6.3 Dynamic feature-table growth

In lifted mode, the set of grounded fluent atoms grows during search. Rollout
IW's feature-depth table must therefore grow dynamically:

```cpp
best_depth.resize(local_ground_fluent_repository_size, INF_DEPTH);
```

before indexing a newly created fluent atom.

Canonical IW(1)'s novelty table must likewise support the local repository's
expanding atom universe. The width guarantee is with respect to the complete
set of grounded fluent atoms generated by complete on-demand lifted search; it
does not require pre-enumerating that set before search.

### 6.4 Returning a plan safely

A plan assembled from one worker's local `GroundAction` pointers cannot outlive
that worker overlay unless the result retains the overlay. Prefer a stable
cross-overlay representation while workers run:

```cpp
struct LiftedPlanStep
{
    formalism::Action action_schema;      // parent-stable
    formalism::ObjectList binding;        // parent-stable objects
};
```

or compact schema/object indices.

When the portfolio stops:

1. cancel and join all workers;
2. take the winning schema-binding sequence;
3. materialize only those O(plan length) ground actions in the caller/parent
   problem on the coordinator thread;
4. return the ordinary Mimir `Plan` expected by Python.

This final sequential canonicalization is acceptable even when exhaustive
pre-grounding is impossible. It grounds only the chosen plan.

An alternative is a public lifted-plan result type, but the ordinary `Plan`
conversion should still be provided for compatibility.

## 7. KPKC generator and axiom evaluator ownership

### 7.1 Per-worker mutable instances

Every lifted search worker requires its own:

```text
KPKCLiftedApplicableActionGeneratorImpl
KPKCLiftedAxiomEvaluatorImpl
DynamicAssignmentSets
binding-generator scratch/counters
local grounding overlay
```

A single generator/evaluator cannot be invoked concurrently by the outer
portfolio threads in its current form.

### 7.2 Share immutable KPKC preprocessing

Constructing all KPKC condition grounders independently may duplicate static
consistency graphs and other schema preprocessing. Refactor where useful into:

```text
KPKCStaticData       # shared, immutable, problem/schema-level
KPKCWorkerState      # private dynamic assignments, touched edges, scratch
```

The existing `create_parallel_worker_context()` and parallel lookup-table code
provide useful precedent, but the outer portfolio needs a complete per-search
worker, not merely inner per-state action-generation scratch.

A first correct implementation may construct one complete KPKC generator and
axiom evaluator per overlay. Benchmark setup cost, then share immutable
preprocessing without sharing mutable grounding state.

### 7.3 Symmetry pruning

For the canonical width-1 certifier, use KPKC with symmetry pruning **off** in
version 1 unless an explicit proof and parity tests establish that a symmetry
mode preserves the required search semantics and shortest width-1 solution.
Rollout accelerators may later use diversified symmetry modes, but their output
remains an incumbent, not the certificate.

## 8. New C++ components

Suggested layout:

```text
include/mimir/search/algorithms/rollout_iw.hpp
src/search/algorithms/rollout_iw.cpp

include/mimir/search/algorithms/rollout_iw/action_ordering.hpp
src/search/algorithms/rollout_iw/action_ordering.cpp

include/mimir/search/algorithms/iw/atomic_goal_portfolio.hpp
src/search/algorithms/iw/atomic_goal_portfolio.cpp

include/mimir/formalism/problem_grounding_overlay.hpp       # if separate
src/formalism/problem_grounding_overlay.cpp
```

The source glob may pick up new `.cpp` files, but verify installation/export
headers, declarations, and `mimir/mimir.hpp` exposure.

## 9. Rollout-IW single-search API

Suggested shape:

```cpp
namespace mimir::search::rollout_iw
{
struct Options
{
    std::optional<State> start_state;
    GoalStrategy goal_strategy;
    ActionOrderingStrategy action_ordering_strategy;
    uint64_t seed = 0;
    uint32_t max_depth = std::numeric_limits<uint32_t>::max();
    uint64_t max_rollouts = std::numeric_limits<uint64_t>::max();
    uint64_t max_num_states = std::numeric_limits<uint64_t>::max();
    uint32_t max_time_in_ms = std::numeric_limits<uint32_t>::max();
};

struct Statistics
{
    uint64_t num_rollouts = 0;
    uint64_t num_generated_states = 0;
    uint64_t num_feature_depth_improvements = 0;
    uint64_t num_case_1 = 0;
    uint64_t num_case_2 = 0;
    uint64_t num_case_3 = 0;
    uint64_t num_case_4 = 0;
    uint64_t num_solved_propagations = 0;
    uint32_t max_rollout_depth = 0;
};

struct Result
{
    SearchResult search_result;
    Statistics statistics;
    bool root_solved = false;
};

Result find_solution(const SearchContext&, const Options& = Options());
}
```

The standalone algorithm must work with either a grounded or lifted KPKC
`SearchContext`. It must not contain a branch that assumes the ground-atom
repository is frozen.

## 10. Native atomic-goal portfolio API

Suggested shape:

```cpp
enum class AtomicGoalPortfolioSearchMode
{
    GROUNDED,
    LIFTED_KPKC,
    INHERIT_CONTEXT,
};

namespace mimir::search::iw
{
struct AtomicGoalPortfolioOptions
{
    std::optional<State> start_state;
    GroundConjunctiveCondition atomic_goal;
    AtomicGoalPortfolioSearchMode search_mode =
        AtomicGoalPortfolioSearchMode::INHERIT_CONTEXT;
    uint32_t num_rollout_workers = 4;
    uint32_t num_threads = 0;
    uint64_t base_seed = 0;
    uint32_t max_time_in_ms = std::numeric_limits<uint32_t>::max();
    uint64_t max_total_expansions = std::numeric_limits<uint64_t>::max();
    std::vector<rollout_iw::ActionOrderingConfiguration> rollout_orderings;
};

struct AtomicGoalPortfolioResult
{
    SearchStatus status = SearchStatus::IN_PROGRESS;
    std::optional<Plan> plan;
    uint32_t plan_length = std::numeric_limits<uint32_t>::max();
    bool certified_optimal = false;
    uint32_t iw_lower_bound = 0;
    uint32_t iw_min_open_depth = 0;
    uint32_t iw_completed_depth = 0;
    uint32_t winning_worker = std::numeric_limits<uint32_t>::max();
    AtomicGoalPortfolioSearchMode executed_mode;
    std::string stop_reason;
    brfs::Statistics iw_statistics;
    std::vector<rollout_iw::Statistics> rollout_statistics;
};

AtomicGoalPortfolioResult
find_solution_atomic_goal_portfolio(const SearchContext&,
                                    const AtomicGoalPortfolioOptions&);
}
```

For a lifted input context, the portfolio factory should build K+1 worker-local
lifted overlay contexts. It must not fall back to pre-grounding.

## 11. Exact Rollout-IW algorithm requirements

Implement the algorithm from Bandres et al., not a DFS with a global IW novelty
table.

### 11.1 Feature-depth table

For IW(1), maintain:

```text
best_depth[local_fluent_atom_index]
```

initialized to infinity, with root features registered at depth 0 according to
the reference algorithm. In lifted mode, resize it as the local atom repository
grows.

### 11.2 Tree nodes

A rollout tree node needs at least:

```text
local state index / packed state
parent node index
incoming local ground-action index or schema-binding step
depth
NEW / OPEN / SOLVED state
applicable actions or resumable action cursor
per-child generated/solved state
```

Use indices/vectors rather than pointer-heavy heap allocation where possible.

### 11.3 Four rollout cases

Encode and test the paper's cases explicitly:

1. New node that improves the minimum depth of at least one feature: update the
   feature depths and continue the rollout.
2. New node that improves no feature depth: mark the rollout tip solved and stop
   that rollout.
3. Existing node for which none of its true features is currently at its best
   known depth: stop and propagate solved status.
4. Existing node with at least one feature true at its current best known depth:
   continue through an unsolved child.

Expose counters for each case.

### 11.4 SOLVED propagation

A node is SOLVED only when all applicable children have been generated and are
SOLVED/exhausted under current search semantics. This requires complete KPKC
action enumeration in lifted mode; a lazy/resumable action cursor is acceptable,
but “no more children” must be exact.

Guidance changes which unresolved child is selected first. It must not make an
untried binding disappear.

### 11.5 Goal test and plan extraction

Test the goal before applying an incumbent depth bound. Keep every macro as
primitive transitions. Extract a schema-binding plan from the local tree and
canonicalize it after worker shutdown as described above.

## 12. Native action ordering

Create a strategy interface that ranks applicable child actions at one node.
Minimum built-in modes:

```text
in_order
randomized(seed)
direct_goal_achiever_first
goal_regression_relevance
mixed_regression_random
```

For lifted mode, regression ordering should exploit partial bindings:

- unify the target atom with schema effects;
- bind target-implied action parameters;
- use KPKC partial-binding completion to enumerate applicable instances;
- try these instances first;
- then enumerate every remaining applicable action/binding.

This is ordering, not pruning.

Do not invoke Python/Torch for every node. A later learned integration requires
native LibTorch/TorchScript, a compiled scorer, or an explicitly batched bridge.

## 13. Native portfolio coordination

### 13.1 Worker construction

Grounded mode:

- K+1 private state repositories;
- shared grounded generator/evaluator/problem.

Lifted mode:

- K+1 worker grounding overlays;
- K+1 private lifted KPKC generators;
- K+1 private lifted KPKC axiom evaluators;
- K+1 private state repositories;
- one shared immutable parsed model/static KPKC data where safe.

### 13.2 Shared incumbent

Use:

```text
atomic best length
mutex-protected schema-binding plan and source
atomic incumbent version
atomic cancellation flag
shared deadline / total-expansion budget
```

Never store a worker-local `GroundAction` as the only shared representation.

### 13.3 Cancellation

Add low-overhead native cancellation checks to BrFS and Rollout-IW loops. No
Python event callbacks. In lifted mode also check cancellation between action
schema/binding-generation chunks so one enormous KPKC enumeration cannot delay
shutdown indefinitely.

### 13.4 No nested parallelism by default

The outer portfolio uses K+1 search threads. Each lifted generator should run
single-threaded by default inside its worker. Avoid multiplying outer search
threads by inner KPKC/beam worker pools unless benchmarks justify it.

## 14. Canonical IW configuration and certificate

The certifier must use:

```text
max_arity = 1
original grounded fluent atoms, created on demand in lifted mode
no beam width
no max-next-layer cap
no abstracted novelty
complete action/binding generation
KPKC symmetry pruning off in version 1
no goal-oriented reordering before novelty admission
```

The current plain BrFS implementation tests the dynamic goal when a node is
**popped for expansion**. Therefore an incumbent of length `L` is certified
when:

```text
minimum OPEN depth >= L
```

The portfolio needs explicit, thread-safe native progress state:

```text
minimum open depth
last completely processed depth
monotone lower bound
```

If canonical IW finds a goal, stop immediately and return its shortest plan
under the width-1 assumptions. If a Rollout-IW worker finds length `L`, continue
until canonical IW finds a plan, its lower bound reaches `L`, or the budget
expires.

The certificate is identical in grounded and lifted modes as long as lifted
action generation is complete and the same unit-cost transition system is
searched.

## 15. Incumbent pruning inside Rollout IW

After an incumbent length `L` exists, a non-goal node at depth `d >= L - 1`
cannot lead to a strictly shorter plan. Test the goal first, then mark that
branch exhausted-for-improvement.

Use this to save work, but keep canonical IW's frontier as the only global
optimality certificate in version 1. `best_depth[f]` is not a goal-distance
lower bound.

## 16. Python bindings

Add advanced bindings for:

```text
RolloutIWOptions
RolloutIWStatistics
RolloutIWResult
AtomicGoalPortfolioSearchMode
AtomicGoalPortfolioOptions
AtomicGoalPortfolioResult
find_solution_rollout_iw
find_solution_atomic_goal_iw_portfolio
```

The high-level wrapper must expose grounded/lifted mode selection and release
the GIL for the whole native call. No Python callbacks are allowed in the
parallel native path.

## 17. Training versus test-time integration

The intended consumer policy is:

```text
training / feasibility-mask generation:
    grounded portfolio or grounded canonical IW

validation and test-time:
    lifted KPKC portfolio by default
    no exhaustive pre-grounding requirement
```

Grounded and lifted paths must share the same result contract, stopping rules,
and atomic-goal semantics. Training with grounded search is an efficiency
choice; it must not create a separate algorithm whose behavior cannot be used
at test time.

## 18. Tests

Suggested files:

```text
tests/unit/search/algorithms/rollout_iw.cpp
tests/unit/search/algorithms/atomic_goal_iw_portfolio.cpp
tests/unit/formalism/problem_grounding_overlay.cpp
python/tests/search/test_rollout_iw.py
```

Required general coverage:

1. Exact four-case Rollout-IW reference tests.
2. SOLVED propagation waits for every child.
3. Hostile ordering ranks the width-1 path last but still finds it.
4. Primitive/schema-binding plan extraction is valid.
5. Incumbent updates are monotone.
6. Cancellation safely stops every worker.
7. Length-1/2/3 tests verify the node-pop certificate.
8. Budget expiration returns a valid uncertified incumbent.
9. Existing serial IW and current parallel-IW behavior remain unchanged.

Required grounded coverage:

10. Grounded serial and parallel portfolios match for fixed seeds.
11. Private state repositories do not touch shared interning tables.
12. Grounded repositories remain frozen.

Required lifted coverage:

13. Standalone Rollout IW works with a lifted KPKC context.
14. Canonical lifted IW and lifted Rollout IW solve known width-1 atomic goals.
15. K+1 lifted workers do not mutate the shared parent repositories while
    running.
16. Each worker's overlay grows independently.
17. The same tuple may have different local indices without cross-worker
    corruption.
18. A returned schema-binding plan is canonicalized into a valid ordinary Plan.
19. Parent repositories grow only during final winner-plan materialization, not
    during parallel search.
20. Grounded and lifted modes agree on optimal plan length on small problems.
21. Lifted portfolio works on an instance where the grounded context cannot be
    constructed within a configured memory/time limit.
22. KPKC action enumeration remains complete with hostile ordering.
23. Axioms/derived predicates work with worker-local lifted evaluators.
24. Numeric/conditional-effect support either works or is explicitly rejected
    by validated version-1 guards.

## 19. Benchmarks

Compare:

```text
ordinary grounded IW(1)
ordinary lifted KPKC IW(1)
current grounded iw::find_rollouts_parallel
standalone true grounded Rollout IW(1)
standalone true lifted Rollout IW(1)
grounded atomic-goal portfolio
lifted atomic-goal portfolio
```

Measure:

- one-time parse/static preprocessing;
- per-worker overlay construction;
- time to first plan;
- time to certificate;
- incumbent length over time;
- grounded versus lifted memory;
- local grounding counts per worker and overlap between workers;
- scaling for K = 1, 2, 4, 8, 16;
- KPKC setup duplication before/after shared static-data refactoring;
- final plan-canonicalization cost;
- disabled cancellation-hook overhead on ordinary IW.

Use realistic test instances where pre-grounding dominates or fails, not only
small unit-test domains.

## 20. Cross-repository responsibility

### Mimir `projectiveiw`

Owns:

- true Rollout-IW C++ algorithm;
- grounded and lifted KPKC execution paths;
- worker-local grounding overlays;
- native action-ordering strategy interface;
- K+1 search construction and threading;
- shared incumbent and cancellation;
- canonical IW lower-bound reporting and certificate;
- schema-binding plan transport and final canonicalization;
- Python bindings;
- correctness and performance tests.

### Hierarchical `main`

Owns:

- deciding when a manager atom invokes the portfolio;
- converting the selected atom to the native atomic goal;
- choosing grounded mode for training and lifted mode for test-time;
- choosing ordering configurations, seeds, and budgets;
- consuming plan/certificate metadata;
- training/evaluation instrumentation;
- any later native-compatible learned scorer integration.

`hierarchical` must not reimplement Rollout IW, lifted grounding overlays,
SOLVED propagation, search threading, or optimality certification.

## 21. Definition of done

The Mimir work is complete only when:

- true Rollout IW exists as a distinct native algorithm on `projectiveiw`;
- current `find_rollouts_parallel` retains its ordinary grounded-IW semantics;
- standalone Rollout IW accepts grounded and lifted KPKC contexts;
- one native call runs canonical IW plus K independent Rollout-IW workers in
  both modes;
- grounded workers reuse the existing shared-grounding/private-state fast path;
- lifted workers use parent-backed private grounding overlays without reparsing
  or exhaustive pre-grounding;
- no worker mutates shared parent grounding state;
- winning local plans are returned safely through schema-binding
  canonicalization;
- the call releases the GIL;
- guidance is native and non-pruning;
- canonical IW exposes a tested node-pop lower bound;
- timeout results are explicitly uncertified;
- grounded and lifted parity/isolation/certificate/cancellation tests pass;
- benchmarks include instances where pre-grounding is impractical.

## 22. References

- Wilmer Bandres, Blai Bonet, and Hector Geffner. **Planning With Pixels in
  (Almost) Real Time.** AAAI 2018. <https://arxiv.org/abs/1801.03354>
- Miquel Junyent, Anders Jonsson, and Vicenç Gómez. **Deep Policies for
  Width-Based Planning in Pixel Domains.** <https://arxiv.org/abs/1904.07091>
- Nir Lipovetzky. **Planning for Novelty: Width-Based Algorithms for Common
  Problems in Control, Planning and Reinforcement Learning.**
  <https://arxiv.org/abs/2106.04866>
