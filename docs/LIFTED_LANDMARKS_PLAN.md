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

**Producibility filter (added 2026-09-05, after measurement).** Keep a member
`m` only if `m` is a fluent initial atom, or some `(A, E, ℓ)` with `m`'s
predicate has a positive fluent effect literal that unifies with `m`'s objects
— constants matching, variables type-compatible, repeated variables consistent
— whose achiever survives the §2.3 static filter. Memoize per identity, and
short-circuit the whole test for a predicate no schema adds at all.

*Proof.* If no such `(A, E, ℓ)` exists, no ground action can add `m`, and `m`
is not in `I`, so `m` is false in every reachable state. "Every plan traverses
a state containing some member" quantifies over reachable states, so `m` was
never one of the members a plan could have made true and removing it leaves the
set a landmark. ∎ (Applies to member sets only. A *fully bound* PGA keeps its
own atom regardless: its landmark-hood comes from the derivation, and whether it
is achievable is a separate question — an unachievable fact landmark means the
task is unsolvable, which is not this code's business.) An emptied set drops the
PGA exactly as an empty one does; a set left with one member promotes exactly as
one derived with one member does.

Why it is not optional: mimir classifies a predicate as fluent as soon as some
effect *deletes* it, so "fluent" does not imply "anything adds it". miconic's
`origin` is deleted by `board` and added by nothing; on `test/p30-hard`
(485 passengers, 196 floors) `origin(?, ?)` was 95,060 members of which 485 can
ever hold, LIW's rank set was 96,209 against the grounded generator's 1,634, and
every one of those atoms was interned into the problem's repositories. spanner's
`at` is added only by `walk`, which binds a `?m - man`, so `at(spannerₙ, l)` and
`at(nutₙ, l)` are unreachable the same way (rank 26,207 → 1,807; 75,394 interned
atoms → 2,294). The test is asked *before* interning, so the atoms it rejects
never reach the repositories at all.

**This is still not delete-relaxed reachability, and that stays out of scope.**
The filter asks only whether a schema can produce the atom — a lifted question,
answered from the schemas and the static atoms. Where fluent reachability would
help beyond it (rovers `at(?r, w)` for a rover that cannot reach `w`, floortile,
which this filter does not move at all), the lifted set remains a *superset* of
the grounded one — sound, less precise. Measured, not fixed (§6).

### 2.6 Termination and cost

Each identity is expanded at most once and identities are finite
(`≤ Σ_Q (|O|+1)^{arity(Q)}`, in practice tens). Per expansion:
`O(|A| · |effect literals|)` unifications, one intersection over
`Σ_j |pre_j|` literals, and member instantiation `O(Σ_j ∏_{y∈Y_j} |O_type(y)| · |static_j|)`
— childsnack p30-hard: `ontray(?s,?t)` is 437 × #trays bindings. Expect
milliseconds to low seconds and megabytes, against 37 min / 23.4 GB.

### 2.7 Self-dependent preconditions (added 2026-09-05, after measurement)

Proposition 1 intersects over *all* achievers of `P(u)`. Some of them cannot be
the **first** achiever, and intersecting over those loses landmarks: on
blocksworld, expanding `clear(b)` the achievers are `unstack(?x, b)`,
`stack(b, ?y)` and `putdown(b)`; the last two need `holding(b)`, and every adder
of `holding(b)` needs `clear(b)` itself, so neither can go first — but the
intersection over all three is empty and the chain dies with a landmark waiting
one step below. Hoffmann–Porteous–Sebastia and Richter–Helmert–Westphal exclude
such achievers on an RPG that never adds the landmark; this is that exclusion at
schema level, against `I` instead of an RPG.

**Gate.** Apply the rule to record `R = P(u)` only when *no instance of the
pattern* `P(u)` is true in `I`. This is stricter than §2.1's member-level stop
and cannot be folded into it: an instance of the pattern that is not a member
leaves the landmark perfectly unsatisfied at `I` — so §2.1 correctly expands —
while destroying the argument below.

**Rule.** For each statically filtered achiever `A_j` and each positive fluent
precondition `Q(v)` of `A_j` under `σ_j`:

- `adders(Q(v))` := every `(A, E, ℓ)` whose positive fluent effect literal
  unifies with `Q(v)` on `Q(v)`'s bound positions and survives the static filter
  (`try_build_achiever` with `Q(v)` as the pattern).
- an adder **needs** `P(u)` iff, under its own `σ'`, its positive fluent
  preconditions contain a `P(s)` with `s_i σ' = u_i` at every position `u` binds
  (free positions of `u` impose nothing) — i.e. every ground instance of that
  precondition lies in `inst(P(u))`.
- if **every** adder needs `P(u)` — vacuously true when there are none, e.g.
  miconic's `origin` — then `matches` := the fluent initial atoms of `Q` agreeing
  with `Q(v)` on its bound positions, whose objects at free positions lie in the
  current candidate domains (a repeated variable agreeing with itself per atom).
  `matches = ∅` ⇒ **drop** `A_j`. Otherwise intersect each free variable's domain
  with its values over `matches`, and bind it when the domain becomes a singleton.

Rerun the static filter after a pass and iterate to a joint fixpoint: a binding
the rule discovers can collapse another precondition's adder set. §2.4/§2.5 then
proceed unchanged over the narrowed achievers.

**Proof.** Let `π` be any plan and `s_k` the first state of `π` containing an
instance of `P(u)`; `a` is the action producing `s_k`, an applicable ground
instance of some `A_j` (achievers are collected for the whole pattern). By the
gate and the choice of `k`, no instance of `P(u)` is true in `I` or in any state
before `s_k`. `a`'s ground precondition `q`, an instance of `Q(v)`, holds in
`s_{k-1}`. If `q ∉ I` then some earlier action `b` added `q`; `b` is applicable,
hence a statically consistent instance of an adder of `Q(v)`, hence needs an
instance of `P(u)` true before it — contradicting the choice of `k`. So `q ∈ I`,
`a`'s binding restricted to `Q(v)`'s variables is one of `matches`, and the
narrowed domains contain it. Dropping and narrowing therefore keep `a` among the
achievers, so the intersection over the narrowed achievers is sound by
Proposition 1. ∎

What it recovers, on the four shapes the evaluation's residue analysis named
(45 of 221 sampled residue atoms were landmarks of this kind): blocksworld
`clear(x)` derives `on(y, x)`/`clear(y)`/`arm-empty` for the `y` actually on `x`,
and on `data/blocks_4` the lifted fact-landmark set becomes **identical** to the
grounded one; ferry `on(car)` derives `at-ferry(l)` for the car's own initial
location; miconic `boarded(p)` derives `lift-at(f)` for `p`'s origin floor;
logistics `in(p, ?)` derives `at(?, l)` over the vehicles at `p`'s initial
location instead of nothing. On `test/p30-hard` it also collapses floortile's
member union — the one §2.5's producibility filter could not touch — from 28,980
to 840, and its rank set from 29,400 to 2,769.

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
## 9. Phase 6: relaxed reachability inside the lifted generator (pymimir 0.16.1)

Every add-on below is its own option on `LiftedFactLandmarkGeneratorOptions`,
**on by default** (the one exception is §9.5's `all` level, whose cost is
stated there), with no automatic time-budget degradation: the hardest-instance
ladder (§10) decides per-domain settings afterwards. The engine is the
grounding-free delete-relaxed reachability of `feat/relaxed-reachability`
(Datalog rules per (schema, effect, add literal) + axioms, rule splitting into
binary joins with projection, semi-naive fixpoint, restricted queries that never
derive a forbidden atom). Write `R` for the fixpoint from `I` with nothing
forbidden and `R_{¬X}` for the fixpoint that never derives any atom of `X`
(and drops `X ∩ I`, which is empty for every landmark this generator expands, by
§2.1). Both are exact Π⁺ reachability: an atom absent from `R_{¬X}` is false in
every state reachable along a prefix in which no atom of `X` was ever made true.

The invariant that every option must keep, in one sentence: **soundness comes
from Proposition 1 applied to the first action of a plan that adds a MEMBER of
the landmark being expanded**, and every narrowing below only removes achiever
instances that cannot be that action, or members that no reachable state holds.

### 9.1 `reachability_filter_members` (option 1)

After §2.5 (and the static filter of phase 4, which stays as the cheap
pre-pass), drop every member that is not in `R`. Sound: "every plan makes some
member true" quantifies over reachable states, and an atom outside `R` is in
none of them. Singleton sets are promoted to facts as before; an emptied set is
a bug (the parent's first achiever's precondition instance is reachable), so
assert rather than tolerate it. This is the filter §2.5 declined to build and
phase 4 approximated; it closes the rovers `at(?r, w)` and logistics
cross-city-truck inflation (a truck's city is a fluent initial atom, so no
static analysis can exclude it).

### 9.2 `reachability_disambiguation` (option 2): `off | per_literal | joint`, default `joint`

When an achiever `(A, E, σ)` is built (§2.2–2.3), narrow each free variable's
candidate domain by what `R` can supply:

- `per_literal`: for each positive fluent precondition `Q(v)` under `σ`, the
  variable at each free position may only take the objects that appear at that
  position in some atom of `R ∩ inst(Q(v))` (repeated variables agreeing per
  atom, as in §2.7's `matches`). Iterate with the static filter to a fixpoint.
- `joint`: the projection of the *conjunction* of `A`'s positive preconditions
  (static and fluent, under `σ`) over `R` onto each variable — the set of values
  for which the precondition join is non-empty. This is exactly the set of
  Π⁺-applicable ground instances of the achiever projected per variable, and
  the engine computes it with the same split-rule machinery it uses for
  effects (one projected rule per variable, bound positions as constants),
  **without materialising the join** — materialising it is the grounding wall.

Drop the achiever if any domain becomes empty. Sound: the first achiever of a
member is applicable in a reachable state, so all its preconditions are in `R`
jointly. Members derived from a narrowed achiever (§2.5) shrink accordingly.

#### 9.2 as built

`joint` is the default and calls `ReachabilityTable::project(ConjunctiveQuery)`.
The query is assembled per achiever, and five details of the assembly are
load-bearing:

1. **Only free slots that OCCUR in a literal become query variables**, and they
   are renumbered densely `0..k-1` with `num_variables == k`; a slot `σ` fixed
   is a query *constant*, which is what lets the engine cache a plan by shape
   and reuse it across expansions that differ only in their objects. Declaring a
   variable that occurs in no literal is not harmless: the engine projects an
   empty list for it (it cannot enumerate the universe), and the intersection
   below would read that as "no admissible value" and drop a perfectly good
   achiever.
2. Dense renumbering is also what kept this caller clear of the engine defect
   fixed in `1d7a15029`: a term whose variable index was at or past
   `num_variables` used to resolve to the query's *constant slot 0*. The
   triggering pattern is numbering variables by parameter slot while sizing
   `num_variables` by how many are free. This generator has renumbered densely
   since the first `joint` commit, and merging the engine fix left every
   extracted landmark on twelve instances byte-identical, so that defect never
   fired here. The engine now throws `std::invalid_argument` instead.
3. `=` static literals become the query's builtin `equalities` / `disequalities`
   rather than relations to look up.
4. Negative fluent preconditions are dropped: they are meaningless under the
   delete relaxation.
5. The engine returns each projection sorted by its **own** object ids, which
   are not this file's `get_index()` order. `std::set_intersection` over two
   differently ordered ranges silently returns nonsense, so each projection is
   re-sorted by object index before being intersected with the achiever's
   candidate domain.

**An achiever with no free occurring slot still has to be checked.** Returning
early there — nothing to narrow, so keep it — is what made `joint` keep
achievers `per_literal` correctly dropped, and it was the whole of the
`joint`-vs-`per_literal` discrepancy on blocksworld: with `?ob` pinned,
`putdown(b3)` has no free slot, it needs `holding(b3)`, no state of
`R_{¬clear(b3)}` holds it, and once it survives, the §2.4 intersection over it
and `unstack` is empty and the entire chain below `clear(b3)` disappears. A
ground conjunction needs no join: each precondition instance is tested against
the table directly.

Cost, and the reason the two levels are still both offered: under option 3
`project` runs a join **against a table**, and a witness store cannot
enumerate, so every `joint` expansion pays one real restricted fixpoint.
`per_literal` asks only membership questions and can take the shortcut of
§9.3, which on most instances removes nearly all of them. Path consistency over
arc consistency is what that buys.

### 9.3 `first_achievers_restricted` (option 3): RHW possible first achievers

When expanding a landmark with member set `M` (a fact is `M = {f}`), compute
`R_{¬M}` and restrict the achievers to the ones whose instances can be **first**:

- collect achievers for the pattern as in §2.2, then narrow each free variable
  at a pattern position to the objects that appear at that position in `M`
  (position-wise projection of the member set — an over-approximation of "adds
  a member", sound because it is a superset);
- apply §9.2's narrowing with `R_{¬M}` in place of `R` (same `per_literal |
  joint` level as option 2); drop achievers whose domains empty;
- §2.4/§2.5 proceed over the survivors, and the members of every derived
  landmark are additionally intersected with `R_{¬M}`.

Proof. Let `a` be the first action of a plan that adds a member of `M`; it
exists because `M ∩ I = ∅` (§2.1). Every state before it holds no member, so
every atom in those states is in `R_{¬M}`; `a` is applicable in the last of
them, hence its preconditions are jointly in `R_{¬M}` and its binding lies in
the narrowed domains; `a` adds a member, so its binding at the pattern positions
lies in `M`'s projection. Its precondition instances are in `R_{¬M}`, which
justifies the member intersection. ∎

This subsumes §2.7 exactly: "every adder of `Q(v)` needs `P(u)`, so `Q(v)` is
initial" is the one-level syntactic approximation of "`Q(v)` reachable in
`R_{¬M}`". With option 3 on, §2.7 is **not applied** (it is redundant and its
pattern-level gate is strictly weaker than the member-level argument above);
with option 3 off, §2.7 runs as in phase 5. Note what changes: the argument is
about the first *member* producer, which is what Proposition 1 needs, whereas
§2.7 argued about the first *pattern* producer and therefore had to gate on
pattern instances in `I`. That gate is what left all 25 residual Π⁺ losses of
phase C (logistics `at(t1, l1-3)`, `at(p0, l1-0)`): with `R_{¬M}`, expanding
`in(p0, ?)` over its members keeps only `load-truck(p0, t1, l1-3)` (p0 cannot
leave `l1-3` without ever being in a vehicle), and the chain
`at(p0, l2-x) → in(p0, t2) → at(p0, l2-0) → in(p0, a) → at(p0, l1-0)` closes
with one restricted fixpoint per expansion.

Cost model: one restricted fixpoint per landmark expansion (facts and sets
alike). This is the term the ladder must measure; do not cache across
expansions unless the cache is exact (a restricted fixpoint for `M` is not
reusable for `M' ≠ M`).

#### 9.3 as built: member-set identity, and the witness shortcut

**Record identity.** Once first achievers are restricted, the member set is what
carries the information — it is what `R_{¬M}` is computed from — so under option
3 (and only then, so an all-off run stays byte-identical to 0.16.0) a partial
record is identified by `(pattern, member set)`, with subset dedupe over records
of the same predicate. This was diagnosed on sokoban `train/p11`, where four
Π⁺-certified landmarks were missing, and it had **two** causes, the second
visible only once the first was removed:

1. §2.4 **pattern subsumption** — the dominant one. The goal record
   `at(box1, loc_4_2)` is more specific than the derived `at(box1, ?)`, so the
   partial record was dropped and the corridor chain died at its first step.
   Disabling that check alone recovers two of the four atoms.
2. The **union-merge on (predicate, binding)**. The remaining steps are the same
   pattern with a different singleton member set each time, so the second
   derivation merged into the first record and was never expanded again.

Both are the same mistake at different sites: pattern identity throws the member
set away. RHW does not have the problem because its nodes *are* the sets. p11
goes from 4 facts / 0 sets / 5 records to 13 / 9 / 35.

**The witness shortcut (`per_literal` only).** `R_{¬M} ⊆ R`, so a restricted
membership test may enumerate its candidates from the unrestricted table and
filter each one: the filter is exact, so the answer is identical, and the
enumeration never needs the restricted table. The filter itself is answered by
the witness store first.

The witness store is a **property of `R`, not a cache of restricted results**.
It is built once, by the unrestricted fixpoint, and records a derivation for
each derived atom; `avoids(atom, M)` reports `REACHABLE_WITHOUT` when that atom
has a derivation touching no member of `M`, which is a proof that the atom is in
`R_{¬M}`. Nothing about any particular `M` went into building it, so it is
equally valid for every `M`, and the §9.2 warning about not reusing a restricted
fixpoint across expansions is untouched: no restricted fixpoint is being reused.
A `REACHABLE_WITHOUT` verdict is sound and exact; the store never answers
"unreachable", only "proved reachable without `M`" or "unknown".

On the first genuinely unknown atom of an expansion — the recorded derivations
all touch `M`, which is *not* a proof of unreachability — the real
`compute_restricted(M)` is computed, at most once per expansion, and answers
that question and every later one of that expansion. The extracted landmarks are
therefore identical to always computing the fixpoint; only the count of
fixpoints changes. `MIMIR_LANDMARK_STATS=1` prints expansions, real restricted
fixpoints and records, which is how the two levels are compared.

Option 4a cannot use the shortcut either: it asks a reachability question about
a *different* forbidden set per fact, and it needs a decision, not a positive
proof, so it stays one fixpoint per fact.

**Lifetime.** `RelaxedReachability::get_table()` returns a reference the engine
owns, unlike the self-sufficient table `compute_restricted` hands back. The
unrestricted index built over it, and every witness query taken from it, must
not outlive the engine; in the generator both live in
`extract_lifted_fact_landmarks`, with the engine declared first.

### 9.4 `verify_pi_plus` (option 4a): certify every extracted fact

After extraction, for every non-goal, non-initial fact landmark `f`: goal
reachable in `R_{¬f}` ⇒ **defect** (the extraction proof was violated) — throw
with the atom named, never silently drop. One restricted fixpoint per fact.
This is a check of the generator, not a source of landmarks; it is on by
default so that any future rule that breaks soundness is caught on the first
instance that exercises it.

### 9.5 `complete_fact_landmarks` (option 4b): `off | members | all`, default `members`

The complete characterisation of Π⁺ fact landmarks: an atom `x ∉ I` is a
landmark iff the goal is unreachable in `R_{¬x}`. Test candidates and promote
the ones that pass to fact landmarks (with `parent_positions` empty and a
`LiftedLandmark` record whose binding is fully bound):

- `members`: every member of every disjunctive set (e.g. sokoban's
  7,424-member set on `p27-hard`), so that a set that is a landmark only
  through one of its members becomes that fact;
- `all`: every atom of `R \ I` that is not already a fact landmark.

One restricted fixpoint per candidate. `all` is `|R \ I|` fixpoints: on sokoban
`p30-hard` that is 638,945 queries at ~120 ms each, about 21 hours for one
instance, which is why it is the one option that is **opt-in** rather than on
by default — it is still implemented in full and the ladder (§10) measures it
on every domain under the 1 h cap. `members` is bounded by the member sets
(sokoban `p27-hard`'s 7,424-member set is the largest, ~12 min at sokoban's
query cost). Orderings: greedy-necessary orderings for promoted facts are not
derived (they have no achiever chain); record `initially_true = false`.

Follow-up, not in this phase: the complete Π⁺ fact-landmark set can be computed
in ONE label-propagation fixpoint over the engine's rules (Keyder, Richter &
Helmert 2010 on the AND/OR graph: each derived atom carries the intersection
over its derivations of the union of its body atoms' label sets plus itself),
which replaces `|R \ I|` restricted fixpoints by one fixpoint with sparse label
sets. Worth building only if the ladder shows `all` recovers landmarks that
`members` does not on domains where it matters.

### 9.6 What does NOT change

- The static filter (§2.3), the phase-4 member-achievability pre-pass, and
  §2.7 (when option 3 is off) stay as they are: cheap, output-identical when
  every reachability option is off. **With every option off the generator
  must produce byte-identical output to aef34b9dd** — that is the regression
  test for the integration.
- `FactLandmarkGraph` contract (§3): unchanged. Promoted facts from §9.5 get
  `LiftedLandmark` records like any other fact.
- Engine invocation: the engine is built once per `create(...)` from the same
  `Problem` (no `LiftedGrounder`, no ground actions anywhere on the path);
  the parity test "lifted extractor interns 0 atoms beyond the grounder's
  universe" must still hold, so the engine's atom interning goes through the
  problem repositories by identity exactly as the generator's does.

### 9.7 Bindings, version, hierarchical

- All options on `LiftedFactLandmarkGeneratorOptions` in pymimir, with the
  enums exposed; `setup.py` `__version__ = "0.16.1"`; `docs/VERSIONING.md` row (released as 0.16.1 by the user's decision, although the MINOR rule would say 0.17.0).
- hierarchical: every option reachable from the CLI (`train_args.py`,
  `plan.py` override, `config_report.py`, W&B config), defaults equal to the
  generator defaults; `LandmarkRepository` forwards them; the parity and
  no-grounding tests extended so that a full-options lifted extraction on a
  grounded parse still interns 0 atoms and never constructs a `LiftedGrounder`.

### 9.8 Tests (gtest + pytest)

1. Every-option-off equals aef34b9dd output on the nine p30-hard instances and
   the fixture set (dump + diff).
2. Option 1 alone: rovers `at(?r, w)` members equal `R ∩ inst`; logistics
   cross-city trucks absent; no set emptied.
3. Option 3 on logistics: the five-step chain above yields `at(p0, l1-0)` and
   `at(t1, l1-3)` as facts on a fixture built for it.
4. Option 4a: a deliberately unsound graph (test hook) throws with the atom named.
5. Option 4b `members`: sokoban fixture where one member of a set is a landmark
   and the set collapses to that fact.
6. Soundness oracle (T9) over all option combinations that the ladder will
   use: 0 refuted.
7. Determinism over option combinations.

## 10. Phase D: the hardest-instance ladder (evaluation agent)

For each of the 13 domains: the largest test instance, lifted extraction with
each option toggled individually from the all-on default (all-on, −1, −2,
−3, −4a, −4b, all-off), serial, one process each: wall, peak RSS, facts, sets,
members, rank. Plus the childsnack ladder p05…p30-hard all-on. Plus the Π⁺
oracle over the all-on output (expected 0 refuted) and the residue closure
(expected: all 25 `pattern_true_in_I` atoms recovered, and the 34 cross-city
`l_only` rows gone). The per-domain default for test time is then read off the
table against the 1 h/problem budget, by the user.
