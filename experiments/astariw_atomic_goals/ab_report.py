"""Report an A/B run: correctness first (results must be identical), then cost."""

import json
import statistics as st
import sys
from collections import defaultdict

paths = sys.argv[1:] or ["results/ab_grounded_blind.jsonl"]
rows = []
for p in paths:
    for line in open(p):
        if line.strip():
            rows.append(json.loads(line))

print("=" * 104)
print("CORRECTNESS -- the patched core must return the same answer on every unit")
print("=" * 104)
mismatch_status, mismatch_len, mismatch_plan, errors, budget_only, opt_only = [], [], [], [], [], []
for r in rows:
    b, o = r["base"], r["opt"]
    tag = f'{r["context"]}/{r["heuristic"]} {r["domain"]} {r["problem"]} #{r["goal_index"]} k={r["width"]} {r["mode"]}'
    if b.get("status") in ("WORKER_ERROR", "HARNESS_TIMEOUT") or o.get("status") in ("WORKER_ERROR", "HARNESS_TIMEOUT"):
        errors.append((tag, b.get("status"), o.get("status")))
        continue
    if b["status"] != o["status"]:
        # Neither build solved it and they merely ran out of different budgets: a
        # faster build reaches the state cap inside the time cap. Not a disagreement
        # about the answer.
        if b["status"] != "SOLVED" and o["status"] != "SOLVED":
            budget_only.append((tag, b["status"], o["status"]))
        elif o["status"] == "SOLVED":
            # The patched build solved something the baseline could not finish in budget.
            opt_only.append((tag, b["status"], o["status"]))
        else:
            mismatch_status.append((tag, b["status"], o["status"]))
    if b.get("status") == "SOLVED" and o.get("status") == "SOLVED" and b.get("plan_length") != o.get("plan_length"):
        mismatch_len.append((tag, b.get("plan_length"), o.get("plan_length")))
    elif b.get("status") == "SOLVED" and b.get("plan") != o.get("plan"):
        mismatch_plan.append(tag)

print(f"  units compared              : {len(rows)}")
print(f"  baseline solved, patched did not : {len(mismatch_status)}   <-- must be 0")
print(f"  patched solved, baseline did not : {len(opt_only)}   (a win: the speedup beat the budget)")
print(f"  both unsolved, other budget : {len(budget_only)}   (benign: faster build hits the state cap first)")
print(f"  plan-length mismatches      : {len(mismatch_len)}")
print(f"  same length, other plan     : {len(mismatch_plan)}")
print(f"  worker errors/timeouts      : {len(errors)}")
for tag, a, c in (mismatch_status + mismatch_len)[:15]:
    print(f"    MISMATCH {tag}: base={a} opt={c}")
for tag in mismatch_plan[:8]:
    print(f"    DIFFERENT ACTION SEQUENCE {tag}")
for tag, a, c in errors[:8]:
    print(f"    ERROR {tag}: base={a} opt={c}")

print()
print("=" * 118)
print("COST -- medians over units solved by both builds; ratio is opt / base, below 1 means the patch is faster")
print("=" * 118)
groups = defaultdict(list)
for r in rows:
    if r["base"].get("status") == "SOLVED" and r["opt"].get("status") == "SOLVED":
        groups[(r["context"], r["heuristic"], r["mode"], r["domain"], r["width"])].append(r)

print("%-9s %-6s %-9s %-12s %2s %4s | %9s %9s %6s | %8s %8s %6s | %9s %9s" % (
    "context", "h", "mode", "domain", "k", "n", "s base", "s opt", "ratio", "MB base", "MB opt", "ratio", "exp base", "exp opt"))
print("-" * 118)
for key in sorted(groups):
    rs = groups[key]
    tb = [r["base"]["search_wall_s"] for r in rs]
    to = [r["opt"]["search_wall_s"] for r in rs]
    mb = [r["base"]["peak_rss_kb"] / 1024 for r in rs]
    mo = [r["opt"]["peak_rss_kb"] / 1024 for r in rs]
    tr = st.geometric_mean([max(y, 1e-6) / max(x, 1e-6) for x, y in zip(tb, to)])
    mr = st.geometric_mean([y / x for x, y in zip(mb, mo)])
    print("%-9s %-6s %-9s %-12s %2d %4d | %9.4f %9.4f %6.2f | %8.1f %8.1f %6.2f | %9.0f %9.0f" % (
        key[0], key[1], key[2], key[3], key[4], len(rs),
        st.median(tb), st.median(to), tr, st.median(mb), st.median(mo), mr,
        st.median([r["base"]["num_expanded"] for r in rs]),
        st.median([r["opt"]["num_expanded"] for r in rs])))

print()
print("=" * 104)
print("AGGREGATE -- all units, including those neither build solved")
print("=" * 104)
agg = defaultdict(lambda: [0.0, 0.0, 0, 0.0, 0.0])
for r in rows:
    b, o = r["base"], r["opt"]
    if "search_wall_s" not in b or "search_wall_s" not in o:
        continue
    a = agg[(r["context"], r["heuristic"], r["mode"], r["width"])]
    a[0] += b["search_wall_s"]
    a[1] += o["search_wall_s"]
    a[2] += 1
    a[3] = max(a[3], b["peak_rss_kb"] / 1024)
    a[4] = max(a[4], o["peak_rss_kb"] / 1024)
print("%-9s %-6s %-9s %2s %5s | %11s %11s %7s | %10s %10s" % (
    "context", "h", "mode", "k", "n", "total s base", "total s opt", "saved", "max MB base", "max MB opt"))
print("-" * 104)
for key in sorted(agg):
    tb, to, n, mb, mo = agg[key]
    print("%-9s %-6s %-9s %2d %5d | %11.1f %11.1f %6.0f%% | %10.0f %10.0f" % (
        key[0], key[1], key[2], key[3], n, tb, to, 100 * (1 - to / tb) if tb else 0, mb, mo))

print()
print("Solved counts (a patch must not change these):")
cov = defaultdict(lambda: [0, 0, 0])
for r in rows:
    c = cov[(r["context"], r["heuristic"], r["mode"], r["width"])]
    c[0] += 1
    c[1] += r["base"].get("status") == "SOLVED"
    c[2] += r["opt"].get("status") == "SOLVED"
for key in sorted(cov):
    n, b, o = cov[key]
    flag = "" if b == o else "   <-- DIFFERS"
    print("  %-9s %-6s %-9s k=%d  n=%3d  base solved %3d  opt solved %3d%s" % (key[0], key[1], key[2], key[3], n, b, o, flag))
