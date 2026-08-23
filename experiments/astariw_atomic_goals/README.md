# AStarIW vs. IW(k) on atomic goals

Measures `find_solution_astar_iw` against `find_solution_iw` on single-atom goals:
runtime, peak memory, and the plan itself.

Atomic goals are the right unit. A full IPC conjunctive goal is far beyond width 1
or 2, so running IW(1) on it only measures how fast it fails; one conjunct lands on
the width boundary the two algorithms actually disagree about. `pddl_goals.py`
splits the goal at the text level and re-emits the problem file byte-identical
except for `(:goal ...)`, so a goal variant differs from the original in nothing else.

## Fairness contract

One process per `(problem, goal, mode, width)` unit. The worker owns the parser, the
grounded problem, the state repository and the search tree, and exits right after
printing its JSON, so no structure built for one goal or one mode can be reused by
-- or inflate the peak RSS of -- the next. This is why `run_one.py` is a separate
program rather than a function: nothing short of process death frees a grounded
problem reliably enough to compare peak RSS across modes.

This is the opposite trade from `../liw_atomic_goals.py`, which deliberately shares
one grounding across all configurations of a problem. Two `Problem` instances of the
same PDDL can enumerate grounded actions in different orders, and under novelty
pruning generation order decides which candidate claims a contested tuple. Sharing a
grounding buys determinism; a fresh grounding per unit buys an honest memory number
and costs determinism. Expect a small floor of grounded-mode disagreements that are
not attributable to whatever you are testing -- measure it with a base-vs-base
control before reading anything into a base-vs-patched difference. The lifted context
does not have this problem.

```bash
python bench.py --data-root <ipc-data> --domains blocksworld spanner \
  --widths 1 2 --context lifted --heuristic ff --out results/run.jsonl
python report.py results/run.jsonl
```

`--heuristic {blind,ff,max,add}` builds its relaxation over a `LiftedGrounder`, so a
heuristic never forces the grounding a `--context lifted` run exists to avoid.

## A/B harness

`ab_bench.py` / `ab_report.py` compare two builds of the library. A shared machine's
load drifts over hours, so running one whole suite and then the other lets that drift
land entirely on one arm; instead both builds are measured back to back on the same
goal, in alternating order, and each unit yields a paired measurement. `ab_report.py`
leads with correctness -- "baseline solved, patched did not" must be zero -- and only
then reports cost.

Point `--python-base` / `--python-opt` at two virtualenvs. pymimir loads
`libmimir_core.so` through an `$ORIGIN/lib` rpath, so swapping just that file into a
copy of one wheel changes the search implementation and nothing else, which keeps the
two arms honest without building two full wheels. That trick breaks the moment a
struct crossing the module boundary changes layout (adding a field to
`astar_iw::Options`, say): the prebuilt bindings and the new core then disagree about
it, and the measurement is garbage rather than merely noisy. Rebuild the wheel for
those changes.
