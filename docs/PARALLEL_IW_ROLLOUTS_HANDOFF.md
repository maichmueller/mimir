# Handoff: parallel IW(1) rollouts are available in pymimir

For the `hierarchical` agent/session that requested the K=16 rollout investigation.
This is the "what changed and what you need to do" note. The reasoning and all
measurements live in `docs/PARALLEL_IW_ROLLOUTS.md` in the mimir repo.

Landed on mimir branch `projectiveiw` as three commits:

| commit | what |
|--------|------|
| `e9077a27d` | fix: nanobind double-free on optional interned handles |
| `2472c8510` | feat: batched parallel IW rollouts |
| `e28d68d39` | build: opt-in valla patch (not needed by you) |

## 1. What you can now do

K differently-seeded IW rollouts from one state, in true parallel, on one
already-parsed and already-grounded `Problem`:

```python
import pymimir as mm

problem = mm.Problem(mm.Domain(domain_path), problem_path, mode="grounded")
start = problem.get_initial_state()

results = mm.iw_parallel(
    problem, start, max_arity=1,
    seeds=list(range(16)),
    num_threads=16,
    max_next_layer_states=...,   # see section 3 -- you probably want this
)
# results[k].reached_fluent_atoms -> list[int] of ground-atom indices
# results[k].status, results[k].num_states, results[k].reached_derived_atoms

witness = set(results[0].reached_fluent_atoms)
for r in results:
    witness &= set(r.reached_fluent_atoms)
```

Measured on `logistics-6/logistics_p-23_a-12_c-18`, K=16 on 16 threads:

- `mm.iw_parallel` parallel: **0.29 s**
- same call with `num_threads=1`: 2.35 s (**8.1x**)
- your current loop of 16 `mm.iw()` calls: 2.80 s (**9.7x**)
- parse + grounding: 0.26 s, paid **once** rather than 16 times

Results are bit-identical to running the same seeds serially.

## 2. You must rebuild pymimir

The `hrl` env has been rebuilt already, but if you re-clone or reset:

```
source ~/miniconda3/etc/profile.d/conda.sh && conda activate hrl
cd ~/github/mimir && pip install . --no-build-isolation
```

Takes ~15 min. `mm.iw_parallel` and `mm.IWRolloutResult` are exported from the
top-level `pymimir` package.

## 3. Read this before you trust the numbers

### 3.1 IW(1) IS order-dependent -- an earlier claim of mine was wrong

I initially reported that exhaustive IW(1) reaches an order-independent atom set.
That was wrong; it was generalised from two domains that happen to be insensitive.
Exhaustive IW(1), K=16 seeds, no truncation:

| instance | distinct atom sets | per-rollout | intersection | union |
|----------|-------------------:|------------:|-------------:|------:|
| blocksworld p21-hard   | **16** | 20038-20134 | **8882** | 22483 |
| blocksworld p14-hard   | **16** | 13491-13562 | **5908** | 15262 |
| blocksworld p06-medium | **16** |     561-603 |  **302** |   865 |
| blocks_4 p02-easy      |      8 |       24-27 |       24 |    28 |
| gripper                |      1 |          10 |       10 |    10 |
| logistics-6 p-23       |      1 |         454 |      454 |   454 |

On blocksworld the K=16 intersection is ~44% of any single rollout, so the scheme
is doing real work there. On gripper and logistics-6 all seeds agree and the
intersection is free -- those two are **not** representative.

**Action:** re-run your tuple-graph calibration per domain. A domain where all
seeds agree tells you nothing about blocksworld.

### 3.2 Atom indices are per-`Problem`. This will bite you silently.

`reached_fluent_atoms` returns **indices**, and indices are assigned by each
`Problem`'s own repository in its own interning order. They are:

- comparable **within** one batch (that is what makes the intersection sound), and
  across rollouts of the same `Problem`;
- **not** comparable across two `Problem` objects -- including the same PDDL file
  parsed twice, and including grounded-vs-lifted.

I hit this myself and briefly concluded lifted and grounded explored different
state spaces. They do not. Keyed on atom *names* the reached sets match
(p14-easy 130 = 130, logistics-6 454 = 454, p06-medium 882 vs 880).

**Action:** audit anywhere you persist, cache, or cross-compare atom indices
between instances or between train and test. Key on names, or re-resolve indices
per `Problem`.

### 3.3 Grounded at train, lifted at test is fine

Confirmed: lifted `mm.iw()` is completely unaffected by this change, and the two
modes reach the same atom set (section 3.2). Only the *batched* entry point
requires grounded -- it raises on a lifted context rather than silently racing,
because in lifted mode the problem repositories grow during search and are not
thread-safe.

So: grounded + `iw_parallel` during training, lifted + `mm.iw()` at test time.
Just do not carry indices across the boundary.

### 3.4 `beam_width` is rejected; use `max_next_layer_states`

Rollouts are randomized via a layer-ordering strategy that cannot score states
eagerly, which is what a beam needs. `iw_parallel` raises if you pass
`beam_width`. Truncate with `max_next_layer_states` instead -- which is also what
creates divergence on the order-insensitive domains:

| `max_next_layer_states` (logistics-6, K=8) | distinct sets | intersection |
|-------------------------------------------:|--------------:|-------------:|
| unbounded | 1 | 454 |
| 100 | 8 | 356 |
| 20  | 8 | 297 |
| 5   | 8 | 281 |

## 4. Suggested next steps on your side

1. Swap the K=16 rollout loop over to `mm.iw_parallel` and re-measure the
   end-to-end training step. Expect roughly the numbers in section 1; confirm on
   *your* instance mix, not logistics-6.
2. Re-run the tuple-graph calibration on blocksworld specifically, now that the
   order-dependence is quantified. The width-1 witness set will be materially
   smaller than a single rollout suggests.
3. Grep for any place an atom index crosses a `Problem` boundary (section 3.2).
4. Pick `max_next_layer_states` per domain. Unbounded is free on some domains and
   meaningless on others.
5. Memory: grounded mode plus K private state repositories costs more RAM than
   one shared repository. Private valla tables added ~30 MB at K=16 on
   logistics-6; the grounded match tree is shared, so it is paid once. Watch it
   on your largest instances.

## 5. Known-unfixed, in the mimir repo

- `test_wrapper.py::TestDomain::test_predicate_typed_parameters` fails:
  `Parameter` has no `get_name()` in C++ (the name is on `get_variable()`). It is
  a missing API, not a bug I introduced. Left for the branch owner.
- `test_wrapper.py::TestSearchAlgorithms::test_iw_events` asserts hard-coded
  event counts that look stale relative to the IW(1) optimisation work on the
  `projectiveiw` branch. Also left alone.

Both predate this work; the Python suite is 91 passed / 2 failed before and after.
