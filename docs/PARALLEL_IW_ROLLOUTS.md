# Design: K independent parallel IW(1)/BrFS rollouts over one shared `Problem`

Status: implemented and measured. See §6 for results.
Author's note: every claim in "Findings" below is backed by a measurement or a
direct source read, both reproducible via `exe/probe_parallel_rollouts.cpp`.

## 1. Goal

Run K independent, differently-seeded, stochastic IW(1) rollouts from the *same*
root state with real wall-clock parallelism, while sharing one already-parsed and
already-grounded `Problem`, so that PDDL parsing and domain grounding are paid
once rather than K times.

The consumer is a training pipeline that uses the intersection of K stochastic
IW(1) rollouts' reached-atom sets as a sound estimate of the width-1-reachable
atoms from a state. It needs, per rollout, the reached fluent-atom set — not
usually a plan.

Hard constraint (a): the existing single-rollout path must be **completely
unaffected** — no added branch, no atomic, no lock, no measurable overhead. The
synchronization strategy must live behind the new batched entry point.

## 2. Findings

### 2.1 The GIL is held for the whole search, and event handlers are the reason

`pymimir`'s `mm.iw()` constructs a Python-subclassed `IBrFSEventHandler` and
passes it into the C++ search (`python/src/pymimir/wrapper_search_width.py`).
The search calls back into that Python object on every expansion. This is why
the C++ call cannot simply release the GIL today, and it is why the batched API
must be a *separate* entry point that accepts only native (quiet) handlers.

### 2.2 `Repositories` is **frozen** during a grounded search — measured, not assumed

The earlier framing was that all K rollouts would race on the shared, lazily
growing `ProblemImpl`-level `Repositories`, and that this would need a mutex.

**That framing is wrong for the grounded search mode, and I disagree with it
explicitly.** `exe/probe_parallel_rollouts.cpp` snapshots the size of *every*
repository in `Repositories::get_hana_repositories()` before and after each
phase. Across a single rollout, K serial rollouts, and K parallel rollouts, on
both `blocks_4/p02-easy` and `logistics-6/logistics_p-23_a-12_c-18`, **every
single repository delta is zero**.

The reason is structural, not accidental:

- In grounded mode, `SearchContextImpl::create` runs `LiftedGrounder` to
  completion up front and hands the resulting pre-ground actions/axioms to
  match trees (`GroundedApplicableActionGeneratorImpl`,
  `GroundedAxiomEvaluatorImpl`). Both only *traverse* their match tree during
  search; neither calls `ProblemImpl::ground(...)` or
  `get_or_create_ground_atom(...)`.
- `ProblemImpl::m_details` is built eagerly in the constructor
  (`src/formalism/problem.cpp:170`), not lazily.
- `ProblemImpl::m_flat_index_list_map`, `m_flat_double_list_map`, and the three
  `SharedObjectPool` members are used only inside `src/formalism/problem.cpp`
  during grounding. They have no call sites anywhere in `src/search`.

So: **no mutex on `Repositories`, no scratchpad for ground atoms, no batched
merge of newly-interned atoms.** There is nothing to merge. The lazily-growing
ground-atom repository is a *lifted*-mode phenomenon; it does not apply here.

### 2.3 The one genuinely contended structure is the `valla` interning tables

`StateRepositoryImpl` interns each state's atom set into
`ProblemImpl::m_index_tree_table` and its numeric variables into
`m_double_leaf_table` (both `valla::IndexedHashSet`). These *do* grow during
search: +240,403 index-tree slots for a single logistics-6 rollout.

All ten call sites live in `src/search/state_repository.cpp`. Nothing else in
the codebase touches them.

`valla::IndexedHashSet` is *already* thread-safe: a 64-way `std::mutex` stripe
array plus `tbb::concurrent_vector` for stable indices plus a
`gtl::parallel_flat_hash_set` with per-submap mutexes. So K parallel rollouts
over a shared `Problem` are already **correct**. They are just catastrophically
**slow**.

### 2.4 Measurement: sharing the interning tables makes parallelism strictly harmful

`logistics-6/logistics_p-23_a-12_c-18`, K=16 rollouts, private `SearchContext`
per rollout, shared `Problem`. Total work is constant across rows:

| threads | wall clock | speedup vs serial |
|--------:|-----------:|------------------:|
| serial  |    2421 ms | 1.00x             |
| 1       |    2322 ms | 1.04x             |
| 2       |    3503 ms | 0.67x             |
| 4       |    6042 ms | 0.42x             |
| 8       |    9410 ms | 0.29x             |
| 16      |   11499 ms | 0.25x             |

Wall clock grows *monotonically* with thread count on constant work. Adding
threads makes it ~5x worse.

Crucially, in this phase the tables were already fully warm — the measured
insert delta was **+0**. Every one of those operations was a read hit. But
`valla::IndexedHashSet::insert` takes its stripe `std::mutex`
*unconditionally*, before the `find`:

```cpp
I insert(T slot) {
    std::lock_guard<std::mutex> lk(stripes[stripe_of(slot)]);
    if (auto it = m_uniqueness.find(slot); it != m_uniqueness.end())
        return *it;          // read hit — but the lock was already taken
    ...
}
```

Interning one state walks O(|state|) tree nodes, each taking a stripe lock. With
16 threads hammering 64 shared mutexes, every lock acquisition is a write to a
shared cache line. This is pure cache-line ping-pong, not logical contention —
which is why it degrades even at 2 threads and even with zero inserts.

### 2.5 Control: fully private `Problem`s scale nearly linearly

Same instance, K=16, 16 threads, each rollout on its own independently parsed
`Problem`:

| configuration                              | search wall clock | speedup |
|--------------------------------------------|------------------:|--------:|
| serial, shared `Problem`                    |           2679 ms |   1.00x |
| 16 threads, shared `Problem`                |          11040 ms |   0.24x |
| 16 threads, **fully private `Problem`s**    |        **297 ms** |**9.02x**|

Both parallel configurations produce results identical to the serial baseline
(state counts, reached-atom counts, statuses all match).

This isolates the cause beyond doubt: the shared interning tables are the
*entire* problem. Everything else — `Repositories`, match trees, state
repositories — is already fine.

But fully private `Problem`s cost 3668 ms of K x (parse + ground) setup, which is
exactly the cost we set out to avoid. It buys the speedup and gives the savings
straight back.

### 2.6 The match tree is already share-safe (this is the "scratchpad" prior art)

`MatchTreeImpl::generate_applicable_elements_iteratively`
(`src/search/match_tree/match_tree.cpp:82`) keeps its traversal stack in a
`static thread_local` and writes results into a caller-supplied vector. The tree
itself (`m_root`, `m_elements`) is read-only during traversal.

This is the same pattern as the existing parallel-beam path, which uses a
`thread_local StateRepositoryImpl::StagedSuccessorScratch`
(`src/search/algorithms/brfs/beam.cpp:605`) so workers can compute successors
without touching the shared repository, then interns them on the main thread.

Consequence: **one `ApplicableActionGenerator` and one `AxiomEvaluator` can be
shared by all K rollouts**, provided their event handlers are quiet (the
default). In quiet mode `on_finish_search_layer()` / `on_end_search()` are
no-ops (`.../grounded/event_handlers/base.hpp:75`); the only member write,
`m_statistics`, happens at match-tree construction time.

Sharing them matters: creating a second grounded `SearchContext` from an
existing `Problem` costs ~200 ms because it rebuilds the match tree. At K=16
that is ~3.2 s of setup against ~2.7 s of actual search.

## 3. Design

**Give each rollout private `valla` interning tables. Share everything else.**

Per rollout (private):
- `StateRepositoryImpl` — its `m_states`, `m_packed_states_by_index`,
  `m_fluent_atom_slots`, `m_reached_*` bitsets, `m_index_list` scratch, and
  `m_unpacked_state_pool` are already per-instance.
- **The two `valla` interning tables.** New.
- The novelty/pruning strategy and search nodes, which are already per-search.

Shared across all rollouts (read-only or already thread-safe):
- `Problem`, including `Repositories` (frozen, §2.2) and `m_details`.
- The grounded `ApplicableActionGenerator` and `AxiomEvaluator`, including their
  match trees (§2.6).

Nothing needs a mutex. Nothing needs a scratchpad-and-merge. The contention is
removed by *scoping*, not by *guarding* — which is what requirement (c) asked
me to check for, and in this case it is achievable everywhere.

### 3.1 Why private interning tables are semantically safe

A `valla::Slot` is meaningful only relative to the table that produced it.
`PackedStateImpl` stores three opaque slots and nothing else; its `Hash` and
`EqualTo` (`src/search/state_packed.cpp:60-66`) compare the raw slot values and
never consult a table. Every decode path — `read_sequence`,
`decode_from_unsigned_integrals` — lives inside `StateRepositoryImpl`, which
already knows which table it used.

So slot identity is *already* scoped to "the repository that created it". Giving
each repository its own table does not change any existing invariant; it just
stops two repositories from accidentally sharing a namespace they never needed
to share.

### 3.2 The one real correctness obligation: re-create the start state

Because slot values are table-local, a `State` created by repository A is not a
valid dedup key inside repository B. If the caller's root `State` were passed
into a rollout with a private table, a successor that returns to the root would
be interned fresh and not recognized as a duplicate.

The batched entry point therefore re-creates the start state inside each private
repository from its dense atom set, via
`get_or_create_state(GroundAtomList<FluentTag>, FlatDoubleList)`. This is cheap
(one state) and restores the invariant exactly.

### 3.3 How requirement (a) is satisfied

`StateRepositoryImpl` gains two reference members bound once at construction:

```cpp
valla::IndexedHashSet<valla::Slot<Index>, Index>& m_index_tree_table;
valla::IndexedHashSet<double, Index>&             m_double_leaf_table;
```

The existing constructor binds them to the `Problem`'s tables — the same objects
the code uses today. A second constructor allocates private tables owned by the
repository.

The ten hot-path sites change from `problem.get_index_tree_table()` (a call
returning a member reference) to `m_index_tree_table` (a member reference read).
Both compile to an address load. There is **no runtime branch, no flag, no
atomic, and no lock** on the serial path — the two configurations differ only in
which object the reference was bound to at construction. This is verified
empirically in §5 with a before/after `mm.iw()` micro-benchmark.

### 3.4 Python-facing API

A new native entry point that takes a batch of seeds and releases the GIL once
for the whole batch:

```python
results = mm.iw_parallel(
    problem, start_state, max_arity=1,
    seeds=[0, 1, ..., 15],
    num_threads=16,
    max_depth=...,
)
# -> list[RolloutResult], one per seed, in seed order
```

Each `RolloutResult` carries the status, optional plan, reached fluent-atom
indices, and state count. Deliberately, it accepts **no Python callbacks** —
that is what makes releasing the GIL for the whole batch sound, and it is the
difference from `mm.iw()`.

## 4. Known non-goals

Things deliberately not addressed, and why that is safe here:

1. **Lifted search mode.** In lifted mode `Repositories` *does* grow during
   search (`ProblemImpl::ground(...)` is called from the binding generators),
   and `loki::IndexedHashSet` — unlike `valla`'s — is a plain
   `absl::node_hash_set` + `std::vector` with no synchronization at all. The
   batched entry point will **reject** non-grounded contexts rather than
   silently race. Supporting lifted mode would need the scratchpad-and-merge
   scheme that turned out to be unnecessary for grounded mode.

2. **Fixing `valla::IndexedHashSet`'s read path.** Taking the stripe lock before
   the `find` is the root cause of §2.4. This was **prototyped and measured**, not
   just speculated about: hoisting the `find` out of the stripe lock into a
   double-checked fast path (safe, because `m_uniqueness` is a
   `gtl::parallel_flat_hash_set` that locks its own submaps, and indices are
   stable) gives, on logistics-6 p-23:

   | configuration                        | unpatched | patched | verdict            |
   |--------------------------------------|----------:|--------:|--------------------|
   | serial IW(1) (median of 9)           | 155919 us |154676 us| 0.8% --- noise     |
   | K=16, 16 threads, **shared** tables   |  11040 ms | 4412 ms | 2.5x better, still 1.7x **slower** than serial |
   | K=16, 16 threads, **private** tables  |    313 ms | 313 ms  | unaffected         |

   So the patch is worthless single-threaded, and even at 2.5x it does not make
   sharing the tables competitive --- the residual cost is `gtl`'s own per-submap
   mutex on every `find`. It is irrelevant to the design actually shipped, which
   never touches the shared tables.

   The patch is nonetheless **provided**, as
   `dependencies/valla/patches/0001-indexed-hash-set-lock-free-read-path.patch`,
   applied by `ExternalProject_Add`'s `PATCH_COMMAND` behind the CMake option
   `MIMIR_PATCH_VALLA_LOCKFREE_READS` (default **OFF**). Default off because a
   patch against a pinned `GIT_TAG` breaks the build on the next tag bump, for a
   benefit no current code path consumes. Enable it if you ever intern into one
   shared table from several threads:

   ```
   cmake -S dependencies -B dependencies/build \
       -DCMAKE_INSTALL_PREFIX=dependencies/installs \
       -DMIMIR_PATCH_VALLA_LOCKFREE_READS=ON
   ```

   Caveat: `dependencies/valla/CMakeLists.txt` short-circuits on
   `find_package(valla QUIET ... NO_DEFAULT_PATH)`, so with `dependencies/installs`
   already populated the `ExternalProject` never re-runs and the option appears to
   do nothing. Remove `dependencies/installs/include/valla`,
   `dependencies/installs/lib/cmake/valla` and
   `dependencies/build/valla/src/valla-stamp` first.

   The patch step is idempotent (`git checkout -- .` before `git apply`), so
   re-running the dependency build does not fail on an already-patched tree. Still
   worth proposing upstream for other consumers.

3. **Cross-rollout state identity.** With private tables, two `State`s from
   different rollouts are no longer comparable by packed identity, and their
   `Index` values are independent. Callers must compare via dense atom sets. The
   consumer here intersects reached-atom bitsets, which is index-based on
   *ground atoms* (globally stable, since `Repositories` is frozen) and
   therefore unaffected.

4. **Losing cross-rollout slot memoization.** Sharing one table lets rollout 2..K
   reuse rollout 1's interned subtrees. Private tables give that up, so total CPU
   across the batch rises. §2.5 shows this is a good trade: 9x wall-clock, and
   the memory is bounded (~240k slots x 8 bytes x K ≈ 30 MB at K=16).

5. **`mm.iw()`'s own GIL behavior.** Unchanged. It still holds the GIL because it
   still supports Python event handlers. The batched path is strictly additive.

## 5. Verification plan

1. Existing C++ test suite passes unchanged.
2. `python/tests/test_wrapper.py` passes unchanged.
3. New test: K parallel rollouts produce results identical to K serial rollouts
   with matching seeds.
4. New benchmark: real wall-clock speedup at K=16.
5. Before/after `mm.iw()` micro-benchmark on two instances, proving the
   non-batched path's timing is unchanged (requirement (a), empirically).

## 6. Results

### 6.1 Speedup (`logistics-6/logistics_p-23_a-12_c-18`, K=16, 16 threads)

Via `exe/probe_parallel_rollouts.cpp` phase 6:

| configuration                                   | setup   | search   | speedup |
|-------------------------------------------------|--------:|---------:|--------:|
| serial, one shared `Problem`                     |  210 ms |  2350 ms |   1.00x |
| naive parallel, shared interning tables          |  210 ms | 11040 ms |   0.21x |
| K fully private `Problem`s                       | 3436 ms |   259 ms |   9.07x |
| **batched API (private interning tables only)**  |**210 ms**|**274 ms**|**8.58x**|

The batched API gets the speedup of fully private `Problem`s while keeping the
one-time setup of a single shared one.

From Python (`hrl`, K=16, 16 threads, same instance):

- `mm.iw_parallel(..., num_threads=16)`: **0.288 s**
- `mm.iw_parallel(..., num_threads=1)`: 2.318 s -> **8.06x**
- serial loop of 16 `mm.iw()` calls (GIL-bound): 2.802 s -> **9.74x**

### 6.2 Requirement (a): the serial path is unchanged

Two binaries built from the same tree with and without the change, run
interleaved (`exe/bench_serial_iw.cpp`, median of 9-31 runs per invocation):

| instance             | before (median) | after (median) |
|----------------------|----------------:|---------------:|
| logistics-6 p-23     |    140147 us    |   140949 us    |
| blocks_4 p02-easy    |        18 us    |       18 us    |
| gripper test_problem |         8 us    |        8 us    |
| miconic test_problem |         4 us    |        4 us    |

Differences are under 1% and within run-to-run noise. This is expected: the
serial path's only change is `problem.get_index_tree_table()` (an out-of-line
call returning a member reference) becoming `m_index_tree_table` (a member
reference bound at construction). No branch, no flag, no atomic, no lock.

### 6.3 Correctness

- 39/39 existing C++ tests pass.
- 9 new tests in `tests/unit/search/algorithms/iw_parallel_rollouts.cpp` pass.
- `python/tests/test_wrapper.py`: 90 passed, 2 failed, 1 crash --- **identical
  before and after the change**, so no regression. The pre-existing failures are
  `TestDomain::test_predicate_typed_parameters`,
  `TestSearchAlgorithms::test_iw_events`, and a hard abort in
  `test_numeric_function_typed_parameters`. The last is the known
  interned-pointer lifetime bug: `DomainImpl::get_auxiliary_function_skeleton` is
  bound with `nb::rv_policy::copy` over an interned `*Impl`
  (`python/src/pymimir/advanced/formalism/bindings.cpp:733`), so nanobind frees an
  object it does not own. The fix is `nb::rv_policy::reference`, but that file is
  outside this change's scope and was left untouched.

### 6.4 Order dependence of the reached-atom set

IW(1)'s reached-atom set **is order-dependent**, and strongly so on some domains.
A state is admitted only if it makes some atom true for the first time, so an
atom reachable only *through* a state that a given ordering pruned is never
found by that ordering. Different seeds therefore explore genuinely different
subsets.

Exhaustive IW(1) (no truncation of any kind), K=16 seeds:

| instance                | distinct sets | per-rollout | intersection | union |
|-------------------------|--------------:|------------:|-------------:|------:|
| blocksworld p21-hard    |            16 | 20038-20134 |     **8882** | 22483 |
| blocksworld p14-hard    |            16 | 13491-13562 |     **5908** | 15262 |
| blocksworld p06-medium  |            16 |     561-603 |      **302** |   865 |
| blocks_4 p02-easy       |             8 |       24-27 |           24 |    28 |
| gripper test_problem    |             1 |          10 |           10 |    10 |
| logistics-6 p-23        |             1 |         454 |          454 |   454 |

On blocksworld the intersection is roughly **44%** of any single rollout --- the
K-way intersection is doing substantial work. gripper and logistics-6 happen to
be insensitive to ordering; they are **not** representative, and an earlier draft
of this document wrongly generalised from them.

Practical consequence: validate the K and the truncation settings per domain.
A domain where all seeds agree tells you nothing about a domain like blocksworld.

Truncation (e.g. `max_next_layer_states`) adds further divergence on top of this.
On logistics-6 p-23, which is order-insensitive when exhaustive, K=8 gives:

| `max_next_layer_states` | per-rollout atoms | distinct sets | intersection |
|------------------------:|------------------:|--------------:|-------------:|
| unbounded               |           all 454 |             1 |          454 |
| 100                     |           377-378 |             8 |          356 |
| 20                      |           304-316 |             8 |          297 |
| 5                       |           318-331 |             8 |          281 |

## 7. Additional non-goals discovered during implementation

6. **`beam_width` in the batched API.** Rollouts are randomized via
   `RandomizedLayerOrderingStrategy`, which does not support the eager scoring a
   beam requires, so BrFS throws. The batched entry point rejects `beam_width`
   up front on the calling thread. Left unsupported because randomization and
   beam ranking are two different, mutually exclusive ways of choosing what to
   expand. Use `max_next_layer_states` to truncate instead.

