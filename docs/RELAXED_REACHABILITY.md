# Exact delete-relaxed reachability over atoms

`include/mimir/search/relaxed_reachability.hpp` / `src/search/relaxed_reachability.cpp`.

Status: landed 2026-09-06 on `feat/relaxed-reachability`.

## 0. Why

`LiftedGrounder` (`src/search/grounders/lifted.cpp`) computes the delete-relaxed
fixpoint by building a delete-free copy of the problem and then enumerating
every applicable ground action of it with the KPKC generator, round after round,
until the atom set stops growing. The atoms it wants are a by-product of the
ground actions it has to instantiate to get them.

On childsnack `test/p30-hard` (292 children, 437 sandwiches, 292 breads, 292
contents, 10 trays) that is roughly 37 million `make_sandwich` instances —
37 minutes and 23 GB — for a relaxed-reachable atom set of **6597 atoms**. The
same fixpoint here costs **0.50 ms and 0.9 MB** on top of the parsed problem.

The lifted landmark generator needs three services that are impossible without
that enumeration:

1. which ground atoms are relaxed-reachable at all, to filter landmark members
   and disambiguate achiever bindings;
2. reachability with a set of atoms forbidden — never derived and removed from
   the initial state — one query per landmark (Richter–Helmert–Westphal
   "possible first achievers");
3. whether the goal is still relaxed-reachable under such a restriction, the
   complete delete-relaxation landmark test, one query per candidate.

## 1. The reading: atom-level Datalog

This is the Helmert 2009 / Fast Downward translator construction, specialised to
reachability. The schemas are read as Datalog rules over object tuples and never
instantiated as actions.

**Rules.** One rule per (action schema, conditional effect, positive fluent
effect literal). Head = the effect atom. Body = the positive fluent and derived
precondition literals of the schema's condition *and* of the effect's own
condition, plus the static literals of both, in both polarities. One rule per
axiom as well, with the derived atom as head, so `DerivedTag` predicates are
part of the same fixpoint. No stratification test is needed: negation only ever
reaches the static EDB, which no rule can write, so the fixpoint stays monotone.

**EDB.** The static initial atoms (`problem->get_static_initial_atoms()`, which
include mimir's compiled unary type predicates such as `(sandwich ?s)`), the
fluent initial atoms, and one 0-ary relation holding the empty tuple that every
rule's first join starts from.

**Equality.** mimir keeps `=` as an ordinary static predicate but does not always
materialise its atoms (`data/childsnack` has one per object; an IPC childsnack
instance has none). Looking it up would therefore silently change meaning between
instances, so it is *evaluated* on object identity instead: a positive `=` merges
the two terms at compile time (or binds one to a constant, or kills the rule),
a negative `=` becomes a disequality guard. It never becomes a relation.

**What is dropped.** Negative *fluent* and *derived* preconditions, and numeric
constraints and effects. That is the delete relaxation itself: an atom once
derived is never retracted, so a condition over its absence cannot be evaluated.
It over-approximates reachability, which is the sound direction for every
consumer — a landmark test that answers "the goal is still reachable" must not be
wrong in the direction that invents landmarks.

**What is kept exactly.** Negative *static* preconditions, `(not (= ?x ?y))`
included. Deleting fluent effects cannot make a static atom go away, so the
condition is exactly evaluable in the relaxation. §5 measures the one place where
this makes the engine and `LiftedGrounder` disagree.

## 2. Rule splitting

A rule body is decomposed into a chain of binary joins. After each join the
intermediate is projected onto the variables that the head, some not-yet-joined
literal, or some not-yet-placed guard still needs; everything else is dropped on
the spot and the duplicate tuples collapse in the hash set.

This is the whole point. childsnack's

```
make_sandwich(?s, ?b, ?c):
   at_kitchen_bread(?b), at_kitchen_content(?c), notexist(?s)  ->  at_kitchen_sandwich(?s)
```

has `?b` and `?c` in no effect literal, and each occurs in exactly two body
literals (its `at_kitchen_*` atom and its type literal). Joining those two
adjacently kills the variable, so the accumulator drops back to the empty tuple
and the rule costs `|breads| + |contents| + |sandwiches|` probes instead of their
product. That is 1021 probes against 37 million ground actions.

Body literals also carry constants, repeated variables and, for the negative
statics and disequalities, guards. Constants and repeated variables become
column filters folded into the probe key; a guard is attached to the first step
after which all of its arguments are bound, and the projection is told to keep
those variables alive until then. A rule variable that no positive literal binds
— an action parameter occurring only in a negative precondition, say — is bound
by an explicit type-domain relation, because the grounder would still instantiate
that parameter over its declared type.

**Join order.** Greedy, with three rules that were each put there by a
measurement:

- *Connected first.* A literal sharing a variable with the accumulator always
  beats one that does not. Without this the greedy is seduced by the smallest
  relation in the body: sokoban's `(:constants down up left right - direction)`
  makes `direction(?dir)` four tuples, so a pure cost comparison opens with it,
  takes `box(?b)` next, and by the time the two `adjacent` literals are reached
  every intermediate is a cross product. This alone took sokoban `p30-hard` from
  over 300 s to 0.8 s.
- *Smallest intermediate, then fewest probes.* Among connected candidates the
  one whose projection materialises least wins, with the candidate-pair count as
  the tie-break. The cost of the rest of the chain is driven by what a step
  leaves behind, not by what it looked at.
- *Every possible opening.* The chain is planned once per possible first literal
  and the cheapest one is kept. A greedy chain is only as good as where it
  starts: childsnack's `serve_sandwich_no_gluten` opened at `place` (4 tuples) is
  dragged through `at` and `waiting` into a child-by-sandwich intermediate of
  34k rows, whereas opened at `no_gluten_sandwich` it joins `ontray`, drops `?s`
  immediately and never exceeds the eight trays. A trial plan allocates nothing,
  so this costs `|body|` passes of an `O(|body|²)` loop per rule; it took
  childsnack `p20-hard` from 14.0 ms to 0.31 ms and sokoban from 373 ms to
  126 ms.

**Sizes for the cost model** come from the declared types for the IDB relations
and from the *measured distinct values per column* for the static EDB. The latter
matters: sokoban's `adjacent(?l1, ?l2, ?dir)` has four directions in its third
column, not `|objects|`, and a fanout estimate built on the declared type would
be off by three orders of magnitude.

Feeding the *measured* IDB sizes back and compiling a second time was
implemented, measured and removed: after the seeded planner and the measured
column domains landed it changed no row of §4 by more than noise, while costing a
second construction-time fixpoint. The numbers that justified dropping it are in
§6.

**Shared prefixes.** Two rules over the same body — sokoban's `push` has three
positive effect literals, so three rules share every precondition — compile to
the same chain for as long as they still need the same variables, and the shared
prefix is computed once. On sokoban that removed a third of the intermediate
tuples.

**Tautological type literals.** mimir puts every compiled type predicate into
every action's static condition, so a five-parameter rule carries five
`(object ?x)` literals on top of the five that actually restrict something. A
unary static relation holding every object filters nothing, so it is dropped —
unless it is the only thing binding its variable, in which case it is what makes
the rule safe.

## 3. Evaluation

Relations are append-only sets of fixed-arity `uint32` object tuples: one flat
vector in insertion order plus an open-addressing table for deduplication on
insert. Insertion order is what makes semi-naive evaluation free of separate
delta relations — the tuples a step has not seen are exactly the suffix behind
the position it last looked at.

Each step therefore covers `[0, now_lhs) × [0, now_rhs)` minus
`[0, seen_lhs) × [0, seen_rhs)`, which is every pair exactly once. The bookkeeping
is **per step**, not per round: a relation can grow in the middle of a round, and
a round-global boundary would mark tuples as old for a step that never saw them.
(The first version of this used a round-global snapshot taken at the start of the
round, which also made the very first round see nothing new at all and derive
nothing; both bugs are the same bug.)

Probing uses hash indexes over the join columns and the constant columns
together. An index over the static EDB is built once at compile time and shared
by every query, because a static relation never changes. An index over an IDB
relation only ever consumes the tuples appended since it last refreshed. A
recursive rule can have its own head on the right — `above(x, y) :- on(x, z),
above(z, y)` — so the scan bound is fixed on entry and the right tuple is re-read
inside the loop rather than held across an insert.

**Complexity.** The fixpoint's work is bounded by the sum over split steps of the
number of candidate pairs the step ever examines, and every derived tuple is
examined once per step it feeds. With all variables projected as early as the
head and the remaining body allow, the tuple universe a step materialises is the
product of the *surviving* variables' domains rather than of all the rule's
parameters — which is precisely the difference between `|B| + |C| + |S|` and
`|B| · |C| · |S|` for `make_sandwich`. The engine never allocates anything
proportional to the ground-action count.

## 4. Restricted queries

`compute_restricted(forbidden)` and `is_goal_reachable_without(forbidden)` take
ground atoms named as predicate plus objects, so a caller can forbid an atom that
was never interned. The atoms are removed from the initial state and every
derivation whose head is one of them is dropped; a fresh set of derived relations
is built while the compiled plan and the whole static EDB (relations and indexes)
are reused. The derived relations are pre-sized from the unrestricted fixpoint's
measured sizes, which are exact upper bounds because a restricted result is a
subset. `is_goal_reachable_without` stops as soon as the goal is reached and is
consistently cheaper than the full table (§6: 9.2 ms against 24.1 ms on
transport, 3.0 ms against 7.5 ms on floortile).

Nothing is interned into the problem's repositories anywhere in the engine. The
caller decides which of the returned tuples deserve a `GroundAtom`.

**Semantics, and how it relates to the classical landmark test.** Forbidding
atoms is the Richter–Helmert–Westphal reading: no member of the set may ever
become true. Hoffmann–Porteous–Sebastia instead delete every action that achieves
a member, side effects included, which is strictly stronger — it can also lose an
atom the deleted action produced on the side. So

```
goal reachable under HPS action deletion  ==>  goal reachable under atom forbidding
```

and not conversely. Both directions are sound for declaring a landmark; the atom
reading is the more complete of the two. The test suite asserts the implication
on every query and reports how often the two verdicts differ (§5).

## 4b. Projected conjunctive queries

`ReachabilityTable::project(const ConjunctiveQuery&)` answers, for each query variable, the objects it can
take in *some* satisfying assignment of the whole conjunction over that table's relations. A query is a
conjunction of positive literals over static, fluent and derived predicates, plus `=` and `!=` between terms,
plus negative static literals where the engine enforces them for rules; terms are objects or variable indices.
This backs the lifted generator's `reachability_disambiguation = JOINT`: the query is an achiever's
precondition conjunction under a partial substitution, asked once per achiever per expansion.

It is evaluated with the same machinery as the fixpoint and never materialises the join. One unary head
relation is reserved per variable, each is planned as its own rule over the shared body -- so each chain
projects away everything its own head does not need -- and the prefix memo shares steps between two variables
wherever their chains still need the same variables. The query's auxiliaries live in a relation id space that
continues past the problem's, and a query database resolves an id below the problem's relation count through
the table it was opened on and above it through its own vector, so nothing is copied and the `Program` is
never mutated.

**Plans are cached by query shape.** The shape key records the predicates, the polarities and, per term,
whether it is a variable (and which) or *a* constant -- never which object. The objects travel separately in a
constant table that the plan indexes into, which is why `Slot`'s third side and `Step::const_rhs_columns`
carry slot indices rather than object ids. Asking the same achiever with a different binding is therefore a
hash lookup and a fresh constant vector, not a recompilation. Two things the shape cannot decide -- whether
two constants are equal, and whether a variable pinned twice was pinned to the same object -- are recorded as
checks against the call's constant table and evaluated before the plan runs.

Query variables that no positive literal binds range over every object, through an `$all_objects` relation
built once per problem, exactly as an action parameter occurring only in a negative precondition ranges over
its declared type.

Not thread-safe: the plan cache is shared and mutated on a miss.

## 4c. Witness derivations

With `RelaxedReachabilityOptions::record_witnesses` (default true) the unrestricted fixpoint records, for
every derived atom and every intermediate tuple, the plan step and the two body tuple positions of its
**first** derivation -- one `(step, left, right)` triple, 12 bytes, appended on the insert that created the
tuple. `ReachabilityTable::witness_query(forbidden)` then answers, per atom:

- `REACHABLE_WITHOUT` -- the stored derivation tree of the atom contains no forbidden atom at any node,
  initial atoms included. Since a derivation from `I` that never uses a forbidden atom as a body atom is still
  a derivation once those atoms are forbidden, the atom really is reachable in the restricted program. This is
  the half a caller may act on.
- `UNKNOWN` -- carries no information. Either the atom is not reachable at all, or the one derivation that was
  recorded runs through a forbidden atom and another may well exist.

Verdicts are memoised per query and shared across the trees, so the cost of asking *every* atom against one
forbidden set is linear in the number of derived tuples rather than one fixpoint per atom. The traversal is
iterative: a derivation chain is as deep as the atom's h-max layer, and sokoban's `at-robot` spreads one grid
cell per layer over an 8000-cell grid. A cycle would be a contradiction -- a body tuple always exists before
the head it produces -- so re-entering a node still on the stack is treated as "not certified" rather than
trusted.

Measured on the two instances the brief names (`witness_query` per landmark, then one `avoids` per reachable
atom of that landmark's own predicate):

| instance | fact landmarks | sampled | asks | time | REACHABLE_WITHOUT |
|---|---|---|---|---|---|
| ferry-ipc p30-hard | 2369 | 100 | 200,000 | 30.8 ms | 200,000 (**100%**) |
| blocksworld-ipc-enhanced p30-hard | 1462 | 100 | 165,224 | 16.6 ms | 165,097 (**99.92%**) |

The same 200,000 questions through `compute_restricted` would be 200,000 x 26 ms, about 1.4 hours.

## 5. Correctness

`tests/unit/search/relaxed_reachability.cpp`, target
`search_relaxed_reachability_test`.

- **Exactness.** For every problem file under `data/` in 30 domains — 50
  instances — the engine's reachable fluent *and* derived atom set is compared,
  atom for atom, against a ground-level delete-relaxed closure computed in the
  test from `LiftedGrounder::create_ground_actions()` and
  `create_ground_axioms()`. That closure is the ground truth: the grounder
  returns a superset of the truly reachable ground actions, and closing the
  initial state under a superset of the reachable actions yields exactly the
  reachable atom set. **50 instances, 0 mismatches.** `is_goal_reachable()`
  agrees with the same closure on all 50.
- **Restricted-query semantics.** On eight instances, every landmark the lifted
  generator produces plus every single reachable atom is used as a query — 168
  queries. For each: the restricted table equals the ground-level closure with
  the same atoms forbidden (fluent and derived), the early-exit goal query agrees
  with it, and the HPS achiever oracle's verdict implies the engine's. The
  achiever oracle agreed on 168/168 and was stricter on 0 in these domains.
- **Conditional effects.** `data/relaxed_reachability_chain`: `unlocked(?g)` is
  added only by the conditional branch of `open`, whose condition `charged(?g)`
  is produced by another action gated on a static. An engine that read the
  conditional effect as unconditional would reach `unlocked(g2)`; one that
  dropped conditional effects would reach neither.
- **Axioms.** `data/relaxed_reachability_axiom`: `above` is the transitive
  closure of the fluent `on`, a genuinely recursive rule that the fixpoint has to
  iterate — one pass reaches `above(b1, b2)` but not `above(b1, b3)`.
- **Negative statics.** `data/relaxed_reachability_negative_static` pins the one
  real disagreement with `LiftedGrounder`, §7.
- **Determinism.** Two constructions of the same problem produce byte-identical
  tables *in the same tuple order*, and identical statistics, and so do two
  restricted queries. The engine iterates only vectors, never a hash map, so its
  output order is a function of its input.

## 6. Measured

Apple M-series, Release, serial, **one process per row** so that the peak RSS is
about that instance and nothing else. Instances are the largest test instance of
each domain under `hierarchical/data/pddl/<domain>/test/`, plus the childsnack
ladder. `restr-mean` / `restr-max` are 20 `compute_restricted` calls each
forbidding one random reachable atom; `goal-mean` is the same 20 through
`is_goal_reachable_without`. `parseRSS` is what mimir's parser cost, `engineRSS`
what the engine added on top of the parsed problem.

```
instance                                       objs   parse(ms) compile(ms) fixpoint(ms)   atoms  rules steps  aux-tuples  restr-mean(ms) restr-max(ms) goal-mean(ms) parseRSS(MB) engineRSS(MB) peakRSS(MB)
barman-ipc/barman-c40-i12-s51-...-002-hard        121        4.85        3.78         1.00    5623     23   211       16769           0.517         0.624         0.306          4.0           3.0        15.0
blocks/blocks-50-3                                 50        1.63        0.06         0.20    2601      9    13         151           0.132         0.152         0.131          2.7           1.0        11.7
blocksworld-ipc-enhanced/p30-hard                 488       10.41        0.15        14.85  239609      9    15        1467           9.788        10.166         9.817          7.5          71.4        86.8
childsnack-ipc/p30-hard                          1327       19.38        0.57         0.50    6597      7    39        8562           0.244         0.285         0.247         12.6           1.0        21.5
ferry-ipc/p30-hard                               1461       23.06        0.39        41.18  475800      4    15      478724          22.609        23.169        16.606         13.5          83.8       105.2
floortile-ipc/p29-hard                           1022       41.35        0.93        12.10   36550     11    54      166498           7.502         7.682         2.950         23.3           8.0        39.2
logistics-6/logistics_p-27_a-13_c-20              120        2.72        0.38         0.73    2271      6    51        9477           0.406         0.463         0.397          3.3           1.4        12.7
miconic-ipc/p30-hard                              681      118.73        1.12         0.44    1651      4    11        2237           0.326         0.348         0.331         45.5           0.5        60.4
rovers-ipc/p26-hard                               555     2792.85       17.48        30.18   16251     11    83      423112          18.795        20.796        12.195        534.9          20.4       563.2
satellite-ipc/p30-hard                            402       17.11        0.36         1.42   11171      5    27        5688           1.113         1.154         1.022         10.3           1.6        19.8
sokoban-ipc/p30-hard                             9884      310.45        4.75       125.60  638945      4    27     1034743         118.193       131.823       115.480        164.4         181.7       354.0
spanner-ipc/p30-hard                              833       17.01        0.45         0.28    2294      3    21        5142           0.110         0.167         0.106         10.2           0.8        18.9
transport-ipc/p23-hard                            295       41.09        0.86        28.93   24474      5    47      115525          24.090        24.901         9.231         19.8           8.3        36.0
childsnack-ipc/p05-hard                           360        6.32        0.35         0.10     986      7    39        1630           0.039         0.055         0.040          5.7           0.5        14.1
childsnack-ipc/p10-hard                           555        9.20        0.40         0.17    1929      7    39        2842           0.079         0.100         0.080          7.0           0.5        15.5
childsnack-ipc/p13-hard                           670       10.96        0.42         0.19    2364      7    39        3432           0.091         0.116         0.091          8.0           0.7        16.7
childsnack-ipc/p15-hard                           749       11.60        0.44         0.24    2899      7    39        4077           0.113         0.147         0.115          8.5           0.7        17.1
childsnack-ipc/p20-hard                           939       14.63        0.49         0.31    3989      7    39        5424           0.151         0.199         0.153          9.9           1.0        18.8
childsnack-ipc/p25-hard                          1133       17.37        0.53         0.44    5226      7    39        6926           0.206         0.249         0.207         11.2           1.3        20.4
childsnack-ipc/p30-hard                          1327       19.74        0.58         0.49    6597      7    39        8562           0.245         0.288         0.247         12.3           0.9        21.1
```

Against the targets (fixpoint ≤ 5 s and ≤ 500 MB on childsnack `p30-hard`; a
restricted query ≤ 100 ms there; comparable on the largest test instance of
every domain):

- childsnack `p30-hard`: fixpoint **0.50 ms** and **21.5 MB** peak, restricted
  query **0.245 ms**. Four orders of magnitude inside the budget on all three.
- Every fixpoint is ≤ **126 ms**; the slowest is sokoban `p30-hard`, whose answer
  is 638,945 atoms (79 boxes × ~8000 locations).
- Every peak RSS is ≤ **105 MB** except sokoban (354 MB) and rovers (563 MB).
  rovers is the only row over 500 MB and **535 of its 563 MB are mimir's parser**
  — the engine adds 20 MB on top of the parsed problem, and its 2.8 s parse time
  dwarfs the 30 ms fixpoint.
- Restricted queries: **sokoban `p30-hard` misses the 100 ms target at 118 ms
  mean / 132 ms max.** Every other row is ≤ 24 ms. The reason is not a bad plan
  but the answer size: a restricted query recomputes the fixpoint from scratch,
  and sokoban's fixpoint is 126 ms because it derives 638k atoms through 1.0M
  intermediate tuples. Closing that gap needs incremental re-derivation
  (over-delete then re-derive) rather than a better join order; it is not
  implemented.

Ladder: childsnack scales linearly in the instance size (0.10 → 0.49 ms fixpoint
and 0.039 → 0.245 ms per restricted query from `p05` to `p30`), which is the
shape the splitting was built for — the atom count grows 986 → 6597 and the cost
tracks it.

**What witnesses cost.** Memory, measurably: +27 MB on sokoban `p30-hard` (1,673,688 witnesses), +14 MB on
ferry (954,524), and under 1 MB wherever the fixpoint stays small. Time, not measurably: the fixpoint
difference between recording and not was inside this machine's run-to-run variance, which for sokoban
`p30-hard` is 135-192 ms over five runs of the *same* configuration. Restricted queries are unaffected, since
only the unrestricted fixpoint records.

**The refinement pass that was removed.** With the seeded planner and measured
column domains in place, compiling a second plan from measured IDB sizes and
running a second fixpoint changed nothing outside noise (sokoban fixpoint
129.9 ms refined vs 131.7 ms not, restricted 107.8 vs 109.2; ferry 39.2 vs 40.6;
transport 29.7 vs 29.7; childsnack `p30` 0.46 vs 0.51) while doubling
construction cost. Before those two changes it had been worth up to 1.4× on some
domains and *cost* up to 1.4× on others, which is why it existed at all.

## 7. The negative-precondition caveat, measured

mimir's `DeleteRelaxTranslator` drops **every** negative literal regardless of
tag (`src/formalism/translator/delete_relax.cpp`, `filter_positive_literals`;
`translate_level_2_impl` returns `nullptr` for a negative literal). So
`LiftedGrounder`'s delete-free exploration also ignores negative *static*
conditions, which this engine evaluates exactly. `create_ground_actions()` then
re-applies `is_statically_applicable` to the *unrelaxed* action, so the returned
list is filtered — but only per action, not transitively.

`data/relaxed_reachability_negative_static` makes the difference concrete:

```
step1(?i): start(?i), not blocked(?i)  ->  mid(?i)
step2(?i): mid(?i)                     ->  goal(?i)
init: start(a), start(b), blocked(b)
```

The engine reaches `mid(a)`, `goal(a)` and neither `mid(b)` nor `goal(b)`. The
delete-free exploration instantiates `step1(b)` and reaches all four; the static
filter removes `step1(b)` from `create_ground_actions()`, but `step2(b)` has no
static condition of its own and survives — so the returned ground-action universe
contains an action whose own precondition is unreachable. The test asserts this
verbatim.

This never showed up as an atom-level mismatch in §5, and that is not luck:
closing the initial state under the *returned* action list cannot derive `mid(b)`
either, so the closure and the engine agree atom for atom on all 50 instances.
The over-approximation lives in the ground-action universe, which is exactly the
thing this engine exists not to build.

Setting `RelaxedReachabilityOptions::enforce_negative_static_conditions = false`
reproduces the delete-free exploration's reading and reaches strictly more; it
exists so that this difference can be demonstrated rather than argued about.
The default is exact.

Negative fluent and derived preconditions are ignored by both, as the relaxation
requires. Numeric constraints are ignored by both.

## 8. API

```cpp
namespace mimir::search {

struct RelaxedReachabilityOptions {
    bool enforce_negative_static_conditions = true;
    bool record_witnesses = true;
};

enum class WitnessVerdict { REACHABLE_WITHOUT, UNKNOWN };

class QueryTerm {                       // QueryTerm::of_object(o) / QueryTerm::of_variable(k)
public:
    bool is_variable() const; Object get_object() const; uint32_t get_variable() const;
};
struct QueryLiteral    { PredicateVariant predicate; std::vector<QueryTerm> terms; bool polarity = true; };
struct ConjunctiveQuery {
    size_t num_variables = 0;
    std::vector<QueryLiteral> literals;
    std::vector<std::pair<QueryTerm, QueryTerm>> equalities, disequalities;
};

class RelaxedReachability {
public:
    using ForbiddenAtom     = std::pair<Predicate<FluentTag>, ObjectList>;
    using ForbiddenAtomList = std::vector<ForbiddenAtom>;

    static std::shared_ptr<const RelaxedReachability>
    create(const Problem&, const RelaxedReachabilityOptions& = {});

    bool is_reachable(Predicate<FluentTag>,  const ObjectList&) const;
    bool is_reachable(Predicate<DerivedTag>, const ObjectList&) const;
    bool is_reachable(GroundAtom<FluentTag>) const;
    bool is_reachable(GroundAtom<DerivedTag>) const;

    ReachableTuples get_reachable_tuples(Predicate<FluentTag>) const;   // size(), get_arity(), operator[]
    ReachableTuples get_reachable_tuples(Predicate<DerivedTag>) const;

    size_t get_num_reachable_atoms() const;
    bool   is_goal_reachable() const;
    const ReachabilityTable& get_table() const;

    ReachabilityTable compute_restricted(const ForbiddenAtomList&) const;
    ReachabilityTable compute_restricted(const GroundAtomList<FluentTag>&) const;
    bool is_goal_reachable_without(const ForbiddenAtomList&) const;
    bool is_goal_reachable_without(const GroundAtomList<FluentTag>&) const;

    const Problem&                        get_problem() const;
    const RelaxedReachabilityOptions&     get_options() const;
    const RelaxedReachabilityStatistics&  get_statistics() const;
};

/* On a table, in addition to the read-only half of the above: */
class ReachabilityTable {
public:
    std::vector<ObjectList> project(const ConjunctiveQuery&) const;

    bool         has_witnesses() const;
    size_t       get_num_witnesses() const;
    WitnessQuery witness_query(const ForbiddenAtomList&) const;          // throws without witnesses
    WitnessQuery witness_query(const GroundAtomList<FluentTag>&) const;
};

class WitnessQuery {
public:
    WitnessVerdict avoids(Predicate<FluentTag>,  const ObjectList&) const;  // also DerivedTag,
    WitnessVerdict avoids(GroundAtom<FluentTag>) const;                     // also GroundAtom<DerivedTag>
    size_t get_num_memoised() const;
};

}
```

`ReachabilityTable` carries the same `is_reachable` / `get_reachable_tuples` /
`get_num_reachable_atoms` / `is_goal_reachable` surface plus
`get_num_reachable_fluent_atoms`, `get_num_reachable_derived_atoms` and
`get_num_fixpoint_rounds`. It owns its derived relations and shares the compiled
plan and the static EDB through a `shared_ptr`, so it outlives the
`RelaxedReachability` that produced it. A `ReachableTuples` is a *view* and is
valid only as long as the table it came from.

Python: `pymimir.advanced.search.RelaxedReachability`, `ReachabilityTable`,
`ReachableTuples`, `RelaxedReachabilityOptions`, `RelaxedReachabilityStatistics`.
