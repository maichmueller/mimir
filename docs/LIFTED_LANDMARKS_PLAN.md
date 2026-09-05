# Lifted necessary-subgoal landmarks — implementation plan

Status: planned 2026-09-05, not started. Source: Wichlacz, Höller & Hoffmann,
*Landmark Heuristics for Lifted Classical Planning*, IJCAI 2022, §3.1
"Necessary Subgoals" (Proposition 1) and §3.3 (orderings). PDF:
https://www.ijcai.org/proceedings/2022/0647.pdf

Downstream driver: `hierarchical` (`~/GitHub/hierarchical`), ledger item 38c
(`docs/experiment_ledger.md`) — at test scale the grounded generator costs
37 min / 23.4 GB on childsnack `test/p30-hard` because it must instantiate the
delete-relaxed ground-action universe before it can back-chain. The lifted
extractor never enumerates a ground action, so a test-time hierarchical run can
stay in mimir's lifted context mode end to end.

## 0. What exists, and what is wrong with it

`ApproximateFactLandmarkGenerator::create(const IGrounder&, options)`
(`src/search/landmarks/fact_landmark_generator.cpp`):

1. `rpg::instantiate_actions(grounder)` → `grounder.create_ground_actions()`:
   the whole delete-relaxed-reachable ground action set. **This is the wall.**
2. h_max Dijkstra from `I`; per positive fluent proposition it records the
   *minimal-cost* unary achievers (`action_achievers_by_proposition`).
3. Back-chains from the positive fluent goal atoms, intersecting the positive
   fluent preconditions over those minimal-cost achievers (`std::set_intersection`,
   lines ~283-294). Every surviving atom is declared a fact landmark.
4. Step 4b (disjunctive, `max_disjunctive_landmark_size > 0`): from each
   *fact* landmark, groups the minimal-cost achievers' preconditions by
   predicate; a predicate present in every achiever yields the union of the
   contributed atoms as a disjunctive set. Then it **continues per member**:
   each member is expanded individually. Sets derived below the first level
   are therefore "stepping stones", not landmarks (the doc comment in the
   generator says so itself).
5. Stores per-ground-action vectors (`landmarks_achieved_by_action`, …) sized
   to the ground-action repository; `LandmarkTransitionOrderingStrategy`
   consumes them.

Why "Approximate" is the honest name: Hoffmann–Porteous–Sebastia 2004 intersect
over *first-layer* RPG achievers and then **verify** each candidate (goal
unreachable in the RPG without the candidate's achievers); Richter–Helmert–
Westphal 2008 intersect over *possible first achievers* (reachable in the RPG
that never adds the landmark). mimir intersects over the h_max-minimal
achievers only and never verifies. A minimal counterexample: goal `g`;
`a1: pre p, add g`; `a2: pre r, add g`; `p` reachable in 1 step from `I`, `r`
reachable in 2 steps. `g`'s minimal-cost achievers are `{a1}` alone, so `p` is
declared a landmark, but the plan `(… r …), a2` never touches `p`. In practice
the surplus is mostly *initially-true* atoms (blocksworld `ontable(A)` for a
block on the table: `pick-up(A)` beats `unstack(A,x)` on h_max), which then
sit in the manager's vocabulary and in LIW's rank set as fake obligations.

## 1. Definitions (in mimir's vocabulary)

Task `Π = (P, O, A, I, G)`, typed objects, `:constants` are objects
(`problem->get_problem_and_domain_objects()`). Predicates are split by mimir
into `StaticTag` (never in any effect), `FluentTag`, `DerivedTag`. Scope, as for
the grounded generator: **positive fluent literals only** — negative
preconditions, derived atoms and numeric constraints are never intersected
(dropping constraints from consideration can only lose landmarks, never
soundness). Static literals are used only as *filters* (§2.3).

- **Partially ground atom (PGA)** `Q(w₁…wₙ)`, `wᵢ ∈ O ∪ {FREE}`. Free positions
  are independent variables (no co-reference), exactly the paper's "arbitrary
  variables xᵢ". Identity of a PGA = `(predicate, pattern)` where pattern is
  the tuple of objects with `FREE` markers.
- **inst(Q(w))** = all ground atoms `Q(o)` with `oᵢ = wᵢ` on bound positions
  and `oᵢ` type-compatible with `Q`'s i-th parameter on free positions
  (`is_subtypeeq(o->get_bases(), param->get_bases())`, `formalism/type.hpp`).
- **Landmark semantics we produce** (this is what Proposition 1 actually
  proves, and what mimir's `get_disjunctive_landmarks()` documents): *every
  plan traverses a state containing some member of the set*. A PGA whose
  positions are all bound is a **fact landmark**; otherwise the PGA is a
  **disjunctive landmark** whose members are a subset of `inst(Q(w))` (§2.4).
- **Achiever** of a PGA `P(u)`: a triple `(A, E, σ)` — action schema `A`, one of
  its conditional effects `E` (mimir stores every effect as a
  `ConditionalEffect`; the unconditional one has an empty condition and
  arity 0), and a partial substitution `σ` from the parameters of `A` and `E`
  to objects, such that some positive fluent literal `P(s₁…sₖ)` of `E`'s
  conjunctive effect unifies with `P(u)` under `σ`:
  for each bound position i (`uᵢ ∈ O`): if `sᵢ` is an Object then `sᵢ = uᵢ`
  is required; if `sᵢ` is a Variable `x` then `σ(x)` must be undefined or equal
  to `uᵢ`, and is set to `uᵢ`. Free positions impose nothing. Every unifiable
  effect literal is a *separate* achiever (an effect may add `P` twice).
  `pre(A,E,σ)` = positive fluent literals of `A->get_conjunctive_condition()`
  ∪ `E->get_conjunctive_condition()`, with `σ` applied. `static(A,E,σ)` = the
  static literals of both conditions (both polarities), with `σ` applied.
  Variable → binding slot: `Variable::get_parameter_index()`, action
  parameters first, then the conditional effect's own parameters
  (`src/formalism/problem.cpp:700-760` shows the layout the grounder uses).

## 2. The extraction rule

Input: `formalism::Problem` (either parse mode), options. State: worklist of
PGAs, table of PGAs keyed by identity, ordering edges.

**Seed.** Every positive fluent goal atom
(`problem->get_goal_condition()->get_precondition<PositiveTag, FluentTag>()`
yields atom indices; `get_repositories().get_ground_atom<FluentTag>(i)` gives
predicate and objects) becomes a fully bound PGA. (`include_positive_goal_facts`
parity with the grounded options.)

Pop `P(u)`; then:

### 2.1 Initially-true stop

If some member of `inst(P(u))` is true in `I` (`problem->get_fluent_initial_atoms()`,
matched by predicate and bound positions; exact index test when fully bound),
mark it `initially_true` and **do not expand it**. Reason (this is the one
point where the paper's proof is loose): the induction step needs "the first
state of the plan containing an instance of `P(u)` was produced by an action";
if `I` already contains an instance, no such action need exist and the derived
subgoal would be unsupported. The grounded generator stops at the same place
(`qualifying_unary_actions.empty()` ⇒ no predecessors). The landmark itself is
still recorded (trivially satisfied at `I`, parity with mimir).

### 2.2 Achiever collection

Enumerate `(A, E, σ)` as defined in §1 over `problem->get_domain()->get_actions()`.
Achievers found: `achievers(P(u))`. If empty, `P(u)` is unachievable in this
domain; keep it, derive nothing.

### 2.3 Static filter and disambiguation (option `use_static_filter`, default on)

Static atoms never change, so an achiever that has no statically consistent
ground instance has no applicable ground instance and can be dropped without
affecting soundness; a variable that every statically consistent instance binds
to the same object can be bound. Iterate to a fixpoint per achiever:

- For each positive static literal `S(t)` in `static(A,E,σ)`: the *pattern* is
  `t` with `σ` applied. If no atom in `problem->get_static_initial_atoms()`
  (index them by predicate once) matches the pattern on its bound positions,
  **drop the achiever**. For each variable `x` in the pattern,
  `cand(x)` = objects `o` such that some static atom matches the pattern with
  `x := o`; if `cand(x) = {o}` set `σ(x) := o` and repeat; if `cand(x) = ∅` drop
  the achiever. (`cand` is per-literal; intersect it across the literals that
  mention `x`.)
- For each negative static literal `¬S(t)` whose pattern is fully bound: drop
  the achiever if `S(t)` is in the static initial atoms. mimir keeps `=` as a
  static predicate (`formatter_impl.cpp:249`); whether its atoms are
  materialised in `get_static_initial_atoms()` must be checked — if not,
  handle `=` explicitly (equal bound objects ⇒ inconsistent) or skip it.
  Skipping is sound.

This is what makes childsnack come out right: `serve_sandwich` has static
`not_allergic_gluten(?c)`, `serve_sandwich_no_gluten` has `allergic_gluten(?c)`;
with `?c := child1` bound by the goal atom exactly one survives, and
`waiting(child1, ?p)` binds the place.

`mimir::formalism::PredicateAssignmentSets<StaticTag>` (`problem->get_static_assignment_sets()`,
`get_set(pred)[VertexAssignment(position, object)]` / `[EdgeAssignment(...)]`)
answers the one- and two-position pattern queries in O(1); a per-predicate scan
of the static atoms is the exact fallback for patterns with more bound
positions. Either is acceptable; document the choice.

### 2.4 Predicate intersection and the new subgoals (the paper's rule)

For each fluent predicate `Q`: `occ_j(Q)` = the literals of `pre(A_j,E_j,σ_j)`
with predicate `Q`. Skip `Q` unless every achiever has `occ_j(Q) ≠ ∅`. For a
**choice vector** `c = (ℓ₁…ℓ_m)`, one literal per achiever, the new PGA is
`Q(w)` with `wᵢ := o` if the i-th term of every chosen literal is the same
object `o` (after `σ_j`; domain constants are objects), else `FREE`.

Footnote 1 of the paper: when an achiever has several `Q`-literals, each choice
vector yields a sound landmark, and they differ (achiever 1 has `Q(a,x), Q(y,b)`,
achiever 2 has `Q(a,b)` ⇒ `Q(a,_)` and `Q(_,b)`). Enumerate all choice vectors
while `∏_j |occ_j(Q)| ≤ max_occurrence_combinations` (default 64); otherwise
use the first-occurrence vector only. Never mix positions across occurrences
(position-wise agreement over *different* occurrences is unsound).

Insert `Q(w)` into the table unless a PGA with the same predicate and with
`inst ⊆ inst(Q(w))` already exists (subsumption: an existing more-specific
landmark implies it, and — because its achiever set is a subset with more
bound preconditions — everything derivable from `Q(w)` is a generalisation of
something derivable from the specific one). Record the ordering edge
`Q(w) →_D P(u)` (the paper's "ordered directly before": some instance of
`Q(w)` holds in the state immediately before the first instance of `P(u)` is
made true). Push new PGAs onto the worklist.

### 2.5 Members: the ground vocabulary of a partial PGA

The graph consumers (`LandmarkCoordinates`, the manager's candidate scope)
need ground atom indices. For a PGA `Q(w)` derived through choice vector `c`:

    members(Q(w)) := ⋃_j { ℓ_j τ : τ binds the free variables Y_j of ℓ_j to
                           objects compatible with the *achiever's* parameter
                           types, and (σ_j ∪ τ) is statically consistent }

"Statically consistent" = every positive static literal of achiever j has a
matching static atom under `σ_j ∪ τ` on its bound positions (arc-consistent
over-approximation for literals that still contain other free variables; exact
when fully bound), and every fully bound negative static literal is absent.
Intern each member with `problem->get_or_create_ground_atom<FluentTag>(Q, objects)`
(`Problem` is `std::shared_ptr<ProblemImpl>`, non-const; interning is what a
lifted search does anyway). If the same identity is derived again from another
parent, members are **unioned** (each derivation is a sound set; the union is
a weaker but still sound set, and the two consumers only ever read the union).

- `|members| = 0` ⇒ the parent has no statically consistent achiever instance
  in this instance: drop the PGA, count it (`unachievable_subgoals`), and — if
  the parent is a real landmark — the task is unsolvable, which is not this
  code's business.
- `|members| = 1` and the PGA is partial ⇒ the single member is a **fact
  landmark** (every plan makes *some* member true and there is one). Option
  `promote_singleton_disjunctions` (default on): insert it as a fully bound
  PGA (dedup) so it is chained from as a fact.
- Fully bound PGAs have exactly their own atom as member; they stay fact
  landmarks even when no statically consistent achiever exists (derivation is
  what makes them landmarks; achievability is a separate question).

Soundness of `members` (in addition to Proposition 1): the first achiever of
`P(u)` in any plan is an applicable ground instance `a` of some `(A_j,E_j,σ_j)`;
applicable ⇒ statically consistent and type-correct ⇒ `a`'s precondition
instance of `ℓ_j` lies in `members`, and it is true in the state before `a`.

**Do not add delete-relaxed reachability to members.** It is the grounding
we are avoiding, and on childsnack it prunes nothing (Fišer 2020 removes 0
operators). Where it would help (rovers `at(?r, w)` for rovers that cannot
reach `w`), the lifted set is a *superset* of the grounded one — sound, less
precise. Measured, not fixed, in this iteration (§6).

### 2.6 Termination and cost

Each identity is expanded at most once and identities are finite
(`≤ Σ_Q (|O|+1)^{arity(Q)}`, in practice tens). Per expansion:
`O(|A| · |effect literals|)` unifications, one intersection over
`Σ_j |pre_j|` literals, and member instantiation `O(Σ_j ∏_{y∈Y_j} |O_type(y)| · |static_j|)`
— childsnack p30-hard: `ontray(?s,?t)` is 437 × #trays bindings. Expect
milliseconds to low seconds and megabytes, against 37 min / 23.4 GB.

## 3. What goes into `FactLandmarkGraph`

`FactLandmarkGraphImpl` (`include/mimir/search/landmarks/fact_landmark_graph.hpp`)
stays the single graph type — the consumers (`iw::LandmarkCoordinates`,
`IWOptions.landmark_novelty_graph`, `hierarchical.landmarks.LandmarkRepository`)
must not learn a second type. Extend it in place:

| field | grounded generator | lifted generator |
|---|---|---|
| `landmark_atom_indices` / mask | as today | every fully bound PGA's atom: goal atoms first (goal order), then discovery order, then promotions |
| `disjunctive_landmarks` | as today | `members(L)` for every partial PGA with `≥ 2` members; each ascending + unique; sets containing a fact landmark dropped (parity); exact duplicates dropped |
| `predecessors_by_atom` / `successors_by_atom` | as today | `→_D` edges **between fully bound PGAs only** (both endpoints fact landmarks); indexed by atom index, so accessors must bounds-check (a lifted problem's atom universe keeps growing after the graph is built — return an empty list beyond the stored size instead of indexing out of range) |
| achiever / first-achiever / per-action vectors | as today | **absent**. New `bool m_has_achiever_index` (`true` for the grounded generator). Every action-indexed accessor (`get_achiever_action_indices`, `get_achievers`, `get_first_achiever*`, `get_unique_achiever*`, `is_*landmark_achiever`, `get_landmarks_*achieved_by_action`) throws `std::logic_error("landmark graph carries no achiever index (built without grounding)")` when false. `LandmarkTransitionOrderingStrategy`'s constructor throws the same way. |
| new `std::vector<LiftedLandmark>` + `get_lifted_landmarks()` | empty | one record per PGA kept: `{ Predicate<FluentTag> predicate; std::vector<Object> binding /* nullptr = FREE */; IndexList member_atom_indices; std::optional<Index> fact_atom_index; IndexList parent_positions /* into this vector */; bool initially_true; }` — the intensional form, for diagnostics, the evaluation and hierarchical's logging. Edges touching a partial PGA live only here. |

Factory (the "`FactLandmarkGraph::create(...)`" of the brief), so a graph can
be built from atoms without a grounder:

```cpp
static FactLandmarkGraph FactLandmarkGraphImpl::create(formalism::Problem problem,
                                                       IndexList landmark_atom_indices,
                                                       std::vector<IndexList> disjunctive_landmarks,
                                                       std::vector<IndexList> predecessors_by_atom = {},
                                                       std::vector<IndexList> successors_by_atom = {},
                                                       std::vector<LiftedLandmark> lifted_landmarks = {});
```

It builds the mask, normalises the sets (sort/unique/drop-subsumed), sizes the
per-atom vectors to `max atom index + 1`, and sets `m_has_achiever_index = false`.
The grounded generator keeps its constructor path and sets the flag to `true`.

New generator, `include/mimir/search/landmarks/lifted_fact_landmark_generator.hpp`
+ `src/search/landmarks/lifted_fact_landmark_generator.cpp` (globbed by
`src/CMakeLists.txt`, no registration needed):

```cpp
struct LiftedFactLandmarkGeneratorOptions {
    bool include_positive_goal_facts = true;
    bool compute_greedy_necessary_orderings = true;  // fact–fact edges only
    bool use_static_filter = true;                   // §2.3
    size_t max_occurrence_combinations = 64;         // §2.4; 0 or 1 = first occurrence only
    size_t max_disjunctive_members = 0;              // §2.5; 0 = UNCAPPED. A set larger than the cap is dropped (mimir convention), never truncated.
    bool promote_singleton_disjunctions = true;      // §2.5
};

class LiftedFactLandmarkGenerator {
public:
    static FactLandmarkGraph create(const formalism::Problem& problem,
                                    const LiftedFactLandmarkGeneratorOptions& options = {});
};
```

Note the cap semantics differ from `FactLandmarkGeneratorOptions::max_disjunctive_landmark_size`
(there `0` = feature off; here a partial PGA *is* the feature and `0` =
uncapped). Name it differently, document it, and keep hierarchical's
`UNCAPPED_DISJUNCTIVE_SIZE` translation for the grounded path only.

Python (`python/src/pymimir/advanced/search/bindings.cpp`, re-exported in
`python/src/pymimir/advanced/search/__init__.py`): `LiftedFactLandmarkGeneratorOptions`
(all fields `def_rw`), `LiftedFactLandmarkGenerator.create(problem, options=...)`,
`FactLandmarkGraph.has_achiever_index()`, `FactLandmarkGraph.get_lifted_landmarks()`,
class `LiftedLandmark` with `get_predicate()`, `get_binding()` (list of
`Object | None`), `get_member_atom_indices()`, `get_fact_atom_index()`,
`get_parent_positions()`, `is_initially_true()`, `__str__` (e.g.
`ontray(?, ?)`, `at(?, table1)`). Returned pointers are repository-owned:
use `nb::rv_policy::reference` as the existing `get_unique_achiever` binding
does, or the double-free comment above it applies.

Version: this is a MINOR bump per `docs/VERSIONING.md` → **0.16.0**, in the
same commit as the bindings, with a row in the history table.

## 4. Tests

C++: `tests/unit/search/landmarks/lifted_fact_landmarks.cpp`, registered in
`tests/unit/CMakeLists.txt` next to `search_landmarks_fact_landmarks_test`.
Test data lives under `data/` (`blocks_4`, `gripper`, `delivery`, `childsnack`,
`data/ipc/<domain>-ipc/`, and the three `landmark_*` fixtures already there;
add `data/landmark_lifted_static/` and `data/landmark_lifted_occurrences/` for
T4/T5).

- **T1 blocks_4** — every goal atom is a fact landmark; `on(b2,b3)` derives
  `holding(b2)` and `clear(b3)` (single achiever `stack`); `holding(b2)`
  derives `clear(b2)` and `handempty` (both `pick-up` and `unstack` need
  them) and **no** `ontable`/`on` landmark. Assert the grounded generator on
  the same problem *does* report the initially-true `ontable`/`on` one — this
  is the documented difference, pinned.
- **T2 gripper** — `at(ball2, roomb)`: achievers `drop(ball2, roomb, ?g)`,
  fact predecessor `at-robby(roomb)`, disjunctive `{carry(ball2,left), carry(ball2,right)}`;
  identical to the grounded generator's set (`SearchLandmarksGripperDisjunctiveCarryTest`).
- **T3 childsnack** (`data/childsnack/test_problem.pddl`, plus the smallest
  `data/ipc/childsnack-ipc` instance) — allergic child ⇒ `no_gluten_sandwich(?)`
  set present; non-allergic child ⇒ absent; `at(?, <place of child>)` members =
  every tray × that one place (static disambiguation through `waiting`);
  `ontray(?, ?)` is **one** set; `at_kitchen_sandwich(?)` is one set derived
  from it; `at(?, kitchen)` is recorded and `initially_true` and has no
  children.
- **T4 static filter** — custom domain where one achiever schema's static
  precondition is unsatisfiable in the instance: with the filter the
  intersection ignores it (a fact landmark appears); with
  `use_static_filter = false` the same predicate comes out partial or absent.
- **T5 occurrence combinations** — custom domain with two `Q`-literals in one
  achiever: both choice landmarks are produced; `max_occurrence_combinations = 1`
  yields only the first-occurrence one.
- **T6 conditional effects** — reuse `data/landmark_cond_effect_dedup`: the
  effect condition joins the intersection; an unconditional and a conditional
  achiever of the same predicate are two achievers.
- **T7 initially-true stop** — a landmark with an initially-true instance has
  no parents derived through it (`get_lifted_landmarks()` shows no record with
  it as a parent).
- **T8 graph contract** — `has_achiever_index() == false`; each action-indexed
  accessor throws; `LandmarkTransitionOrderingStrategy` constructor throws;
  `get_landmark_atoms()` resolves; every set is ascending and unique, contains
  no fact landmark, and `get_disjunctive_landmark_atom_indices()` is the
  sorted union; promotion produces a fact landmark that is not in any set;
  predecessors/successors are symmetric and only between fact landmarks;
  accessors are safe for atom indices beyond the stored size.
- **T9 soundness oracle (the important one)** — for every landmark `L`
  (fact or set) whose members are all false in `I`, on `blocks_4`, `gripper`,
  `delivery`, `childsnack` and the smallest instance of each `data/ipc/*`
  domain: build delete-relaxed reachability from `LiftedGrounder::create_ground_actions()`
  (grounding is fine *inside a test*) with every ground action that adds a
  member of `L` removed, and assert the goal is **not** relaxed-reachable.
  A relaxed landmark is a landmark, so a failure here is an implementation
  bug. Run the same oracle over the grounded generator's disjunctive sets and
  its non-goal fact landmarks and *record* the failure counts in the test
  output (do not assert): that is the "stepping stones" and "approximate"
  evidence, and §6 reports it at scale.
- **T10 lifted end to end** — `SearchContext` in `LiftedOptions` mode: LIW(1)
  (`IWOptions.landmark_novelty_graph` = lifted graph, `landmark_novelty_disjunctive = true`,
  `landmark_novelty_all_private = true`) solves the gripper and childsnack
  test problems; the problem's fluent atom count after the search is larger
  than at graph build time (proves the resize path ran).

Python: `python/tests/search/test_lifted_fact_landmarks.py` — parity for T1,
T2, T3, T8 and a rendering check of `get_lifted_landmarks()`.

## 5. Wiring into `hierarchical` (second phase, same agent)

Everything below is in `~/GitHub/hierarchical`. Rules that apply there:
`CLAUDE.md` (graphify query first, `graphify update .` after code changes,
deploy only via the sync script — **do not deploy**), `docs/wiki/onboarding-second-eye.md`
§"Rules you cannot derive from the code", commit with `git commit --only <files>`
(the checkout is shared with other sessions).

1. `src/hierarchical/landmarks.py`
   - probe: `_REQUIRED_LIFTED_BINDINGS = ("LiftedFactLandmarkGenerator", "LiftedFactLandmarkGeneratorOptions")`
     plus `FactLandmarkGraph.has_achiever_index`; `lifted_landmark_bindings_available()`,
     `require_lifted_landmark_bindings()` in the style of the existing probes.
   - `LandmarkRepository(..., extractor: str = "grounded")` with `"lifted"`;
     `for_problem` branches: lifted ⇒ `search.LiftedFactLandmarkGenerator.create(problem._advanced_problem, opts)`
     with `max_disjunctive_members = disjunctive_landmark_size` (0 = uncapped —
     no `UNCAPPED_DISJUNCTIVE_SIZE` translation on this path) and **no
     `LiftedGrounder`**. `disjunctive_landmarks=False` under the lifted
     extractor means: keep the fact landmarks, expose no disjunctive members
     (the sets still exist in the graph; `ProblemLandmarks.disjunctive_*`
     stay empty) — document that this is a vocabulary choice, not a generator
     option.
   - `ProblemLandmarks.extractor: str` field. Atom matching against
     `_all_unrestricted_candidates` is unchanged (the wrapper's
     `new_ground_atom` interns into the same repositories the generator
     interned into).
   - `transition_ordering()` raises a descriptive `RuntimeError` for a lifted
     graph (no achiever index).
2. `src/hierarchical/experiment/train_args.py`: `--landmark_extractor {grounded,lifted}`,
   default `grounded` (byte-identical for every in-flight suite). Validation:
   `lifted` + `--worker_iw1_landmark_ordering` is an error; `lifted` requires
   the bindings (same pattern as the other landmark gates, tested in
   `test/test_landmark_flags.py`). `--landmark_extractor` joins the
   `config_report.py` key list.
3. Plumb through every `LandmarkRepository(` construction:
   `experiment/components.py:202` (`resolve_manager_candidate_components`),
   `training/worker_pretraining.py:1931` (`_ArcSite`, plus the
   `WorkerArcConfig` field and `_arc_config_from_args`),
   `experiment/worker_arc.py:3480` and `:4911`, `tools/worker_arc_probes.py`,
   `analysis/landmark_reachability.py`, `experiment/sibling_pair_yield_probe.py`.
4. Eval-set binding: the worker-arc dataset shards record
   `(label_family, manager_pairing, max_arity, landmark_disjunctive*)` and the
   reader refuses a mismatch (ledger 38b). The extractor changes the
   vocabulary, so it joins that binding; a shard without the key reads as
   `grounded`.
5. `src/hierarchical/plan.py` (the test-time entry point): add
   `--landmark_extractor` (default: the checkpoint's; log a warning when it
   differs) and `--ground/--no-ground` (`BooleanOptionalAction`, default:
   the checkpoint's `ground`). The parse mode is a memory setting, not a
   policy setting (`iw/support.py::create_iw_search_context` docstring), so
   overriding it at plan time is legitimate; with a lifted parse
   `worker_private_tables` resolves to off (it requires a grounded parse) —
   say so in the flag help.
6. **Proof that test time is grounding-free**: a test
   (`test/test_plan_lifted_landmarks_no_grounding.py`) that runs `plan.py`'s
   pipeline on a small childsnack instance with `--landmark_extractor lifted --no-ground`
   and a tiny checkpoint (build one with the existing tiny-train utilities in
   `test/train_integration_utils.py`), with `mm.advanced.search.LiftedGrounder`,
   `SearchContext.create` under `GroundedOptions`, `_core.create_sidecar_search_context`,
   `find_rollouts_iw_parallel` and `_core.capture_iw_trees` monkeypatched to
   raise. It must run to completion. If something on the path grounds, the
   test names it — fix it or gate it, and record the decision in the ledger.
   Candidates to check: `grounded_manager_iw_support_enabled` (only the
   grounded *manager backend* under `manager_only`), `IWGoalSupport` /
   feasibility mask (`Width1MaskCache`, `GroundedSearchContextCache` — off in
   the current recipe, `_nofeas`), `StateExplorer` (training only).
7. Docs: `docs/wiki/current-best-configuration.md` gets the flag in the
   recipe table with "default grounded, lifted is the test-time option";
   ledger `docs/experiment_ledger.md` item 38c gets a "landed" row with the
   commit; `graphify update .`.

Local build loop (the hrl env's pymimir 0.15.1 was installed from this
checkout — `direct_url.json` says `file:///Users/maichmueller/GitHub/mimir`):

```bash
# mimir C++ + gtest
cmake --build ~/github/mimir/cmake-build-release-local -j --target search_landmarks_fact_landmarks_test search_landmarks_lifted_fact_landmarks_test
~/github/mimir/cmake-build-release-local/tests/unit/search_landmarks_lifted_fact_landmarks_test
# pymimir into hrl (rebuilds; the dependency prefix under build/temp.* is cached)
cd ~/github/mimir && ~/miniconda3/envs/hrl/bin/python -m pip install . --no-deps
~/miniconda3/envs/hrl/bin/python -m pytest python/tests/search/test_fact_landmarks.py python/tests/search/test_lifted_fact_landmarks.py -q
# hierarchical: _core must be rebuilt after any pymimir install (memory: std::bad_cast otherwise)
cd ~/GitHub/hierarchical && ~/miniconda3/envs/hrl/bin/python -m pip install -e . --no-build-isolation --no-deps
~/miniconda3/envs/hrl/bin/python -m pytest test/test_landmark_repository.py test/test_landmark_flags.py test/test_landmark_subgoal_restriction.py test/test_iw_start_state_context.py -q
```

If `pip install .` fails at link with undefined `nanobind::detail::*` symbols,
that is the nanobind-generation problem from the memory notes: the hrl env
must carry `nanobind>=3,<4` and the build must pick it up (`cmake/NanobindAbi.cmake`
names both generations in its error).

## 6. Evaluation protocol (third phase, a separate agent; I only verify)

Question: *how do the two extractors differ, and what does the lifted one
propose that the grounded one does not (and vice versa), over our domains?*

Data: `~/GitHub/hierarchical/data/pddl/<domain>/{train,validation,test}` for
the twelve E6/E7 domains (barman, blocksworld-ipc-enhanced, childsnack-ipc,
ferry-ipc, floortile-ipc, logistics-6, miconic-ipc, rovers-ipc, satellite-ipc,
sokoban-ipc, spanner-ipc, transport-ipc) plus `blocks`. Train and validation
in full; test instances while the grounded generator fits a per-instance
budget of 120 s / 8 GB in its own process (the lifted one always runs).
Run each extractor in a **fresh process** with `resource.setrlimit` like the
09-05 ladder script did (`lm_probe.py`, quoted in ledger 38c's cost row), so a
blow-up cannot take the harness down.

Per instance record, for G = grounded uncapped-disjunctive
(`FactLandmarkGeneratorOptions.max_disjunctive_landmark_size = 1<<30`, depth 0)
and L = lifted (defaults):

- fact landmarks `F_G`, `F_L`; `F_G \ F_L` split into *initially true* /
  *Π⁺-landmark* (relaxed reachability without the atom's achievers fails to
  reach the goal — a true landmark the lifted method missed, i.e. a
  reachability effect) / *refuted* (a real plan avoiding the atom found by
  BrFS with states containing the atom pruned, on instances small enough) /
  *undetermined*. `F_L \ F_G` must all pass the Π⁺ oracle (a failure is a
  bug — stop and report it).
- member unions `M_G`, `M_L` (the manager's vocabulary under
  `--manager_subgoal_candidate_scope landmarks`): sizes, `M_G \ M_L`,
  `M_L \ M_G`, by predicate.
- set structure: number of sets, size histogram, and the Π⁺-oracle failure
  rate of G's sets by derivation depth (the stepping-stone effect) vs L's.
- wall time and peak RSS of each extractor.

Cost ladder on childsnack `test/p05,p10,p20,p30-hard` for L (G's numbers are
already recorded: 9.7 s / 3.3 GB, 46 s / 8.1 GB, 433 s / 16.7 GB,
2247 s / 23.4 GB; rerun G only at p05 and p10 for a same-machine baseline).

Deliverable: `~/GitHub/hierarchical/docs/reviews/<date>-lifted-landmarks/`
with `README.md` (per-domain table, the classification counts, five concrete
atom-level examples per domain of each direction of difference, and a
one-paragraph verdict per domain), the raw per-instance JSON/CSV, and the
scripts that produced them. Numbers only from files that were actually
produced; anything not run is listed as not run.

## 7. Worked expectation: childsnack, one allergic child `c1` waiting at `p1`

```
served(c1)                                   fact (goal)
  ← serve_sandwich_no_gluten(?s, c1, ?t, p1)   [serve_sandwich dropped: not_allergic_gluten(c1) absent; ?p := p1 via waiting(c1,?p)]
  at(?, p1)          members = {at(t, p1) : t tray}            disjunctive
  ontray(?, ?)       members = sandwiches × trays              disjunctive
  no_gluten_sandwich(?)  members = all sandwiches              disjunctive
    ← make_sandwich_no_gluten(?s, ?b, ?c) : at_kitchen_bread(?)/at_kitchen_content(?)/notexist(?) all initially true → recorded, not expanded
  ontray(?, ?) ← put_on_tray(?s, ?t) : at_kitchen_sandwich(?) [one set, all sandwiches], at(?, kitchen) [initially true]
  at(?, p1)    ← move_tray(?t, ?from, p1) : at(?, ?) [initially true]
```

For a non-allergic child the `no_gluten_sandwich` set is absent and both
`make_sandwich*` schemas are achievers of `at_kitchen_sandwich(?)`. At every
scale the fact landmarks are exactly the goal atoms — the same as the grounded
generator found (ledger 38c) — and the member union should coincide with the
grounded one on childsnack because relaxed reachability prunes nothing there;
the *set structure* will differ (one `ontray` set instead of hundreds of
singletons).

## 8. Out of scope, deliberately

- Reachability sharpening of members (a lifted relaxed fixpoint, Corrêa 2021 /
  Lauer 2021 style) — days of work, useless on childsnack; measure first.
- FAM-cut / DTG landmarks (§3.2 of the paper) — needs lifted mutex groups.
- Orderings between partial landmarks in the graph's atom-indexed vectors —
  kept in `LiftedLandmark.parent_positions` only.
- Making the grounded generator sound (HPS verification) — a different change;
  the lifted generator supersedes it where grounding is the wall.
