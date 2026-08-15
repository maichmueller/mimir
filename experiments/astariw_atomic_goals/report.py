"""Aggregate the JSONL produced by bench.py into a paired IW vs. AStarIW report."""

from __future__ import annotations

import argparse
import json
import statistics as st
from collections import defaultdict

SOLVED = "SOLVED"


def load(paths):
    rows = []
    for p in paths:
        with open(p) as fh:
            for line in fh:
                line = line.strip()
                if line:
                    rows.append(json.loads(line))
    return rows


def key(r):
    return (r["domain"], r["problem"], r["goal_index"], r["width"])


def fmt(x, spec=".2f", dash="-"):
    return dash if x is None else format(x, spec)


def geomean(xs):
    xs = [x for x in xs if x is not None and x > 0]
    if not xs:
        return None
    return st.geometric_mean(xs)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("jsonl", nargs="+")
    ap.add_argument("--label-a", default="iw")
    ap.add_argument("--label-b", default="astar_iw")
    ap.add_argument("--plans", action="store_true", help="print differing plans")
    args = ap.parse_args()

    rows = load(args.jsonl)
    paired = defaultdict(dict)
    for r in rows:
        paired[key(r)][r["mode"]] = r

    A, B = args.label_a, args.label_b

    # ---------------- coverage + status ----------------
    print("=" * 108)
    print("COVERAGE AND STATUS  (one row per domain x width; N = single-atom goals attempted)")
    print("=" * 108)
    hdr = f"{'domain':<13}{'k':>2} {'N':>4} | {'IW solved':>10} {'A*IW solved':>12} | {'both':>5} {'IW only':>8} {'A*IW only':>10} {'neither':>8}"
    print(hdr)
    print("-" * 108)
    buckets = defaultdict(list)
    for (dom, prob, gi, w), d in sorted(paired.items()):
        buckets[(dom, w)].append(d)

    totals = defaultdict(int)
    for (dom, w), ds in sorted(buckets.items()):
        n = len(ds)
        sa = sum(1 for d in ds if d.get(A, {}).get("status") == SOLVED)
        sb = sum(1 for d in ds if d.get(B, {}).get("status") == SOLVED)
        both = sum(1 for d in ds if d.get(A, {}).get("status") == SOLVED and d.get(B, {}).get("status") == SOLVED)
        only_a = sa - both
        only_b = sb - both
        neither = n - both - only_a - only_b
        print(f"{dom:<13}{w:>2} {n:>4} | {sa:>10} {sb:>12} | {both:>5} {only_a:>8} {only_b:>10} {neither:>8}")
        totals[(w, "n")] += n
        totals[(w, "sa")] += sa
        totals[(w, "sb")] += sb
        totals[(w, "both")] += both
    print("-" * 108)
    for w in sorted({w for _, w in buckets}):
        print(f"{'ALL':<13}{w:>2} {totals[(w,'n')]:>4} | {totals[(w,'sa')]:>10} {totals[(w,'sb')]:>12} | {totals[(w,'both')]:>5}")

    # ---------------- non-solved status breakdown ----------------
    print()
    print("=" * 108)
    print("TERMINATION REASON WHEN NOT SOLVED")
    print("=" * 108)
    reasons = defaultdict(lambda: defaultdict(int))
    for (dom, prob, gi, w), d in paired.items():
        for m in (A, B):
            s = d.get(m, {}).get("status")
            if s and s != SOLVED:
                reasons[(m, w)][s] += 1
    for (m, w) in sorted(reasons):
        items = ", ".join(f"{k}={v}" for k, v in sorted(reasons[(m, w)].items(), key=lambda kv: -kv[1]))
        print(f"{m:<10} k={w}: {items}")

    # ---------------- runtime / memory / plan on the commonly solved set ----------------
    print()
    print("=" * 118)
    print("PAIRED COMPARISON ON GOALS SOLVED BY BOTH  (medians; ratio = A*IW / IW, >1 means A*IW costs more)")
    print("=" * 118)
    print(f"{'domain':<13}{'k':>2} {'n':>4} | {'search s IW':>12} {'search s A*':>12} {'ratio':>7} | "
          f"{'peakMB IW':>10} {'peakMB A*':>10} {'ratio':>7} | {'exp IW':>9} {'exp A*':>9} | {'len IW':>7} {'len A*':>7} {'same plan':>10}")
    print("-" * 118)
    for (dom, w), ds in sorted(buckets.items()):
        both = [d for d in ds if d.get(A, {}).get("status") == SOLVED and d.get(B, {}).get("status") == SOLVED]
        if not both:
            print(f"{dom:<13}{w:>2} {0:>4} |" + " (no commonly solved goal)")
            continue
        ta = [d[A]["search_wall_s"] for d in both]
        tb = [d[B]["search_wall_s"] for d in both]
        ma = [d[A]["peak_rss_kb"] / 1024 for d in both]
        mb = [d[B]["peak_rss_kb"] / 1024 for d in both]
        ea = [d[A]["num_expanded"] for d in both]
        eb = [d[B]["num_expanded"] for d in both]
        la = [d[A]["plan_length"] for d in both]
        lb = [d[B]["plan_length"] for d in both]
        same = sum(1 for d in both if d[A]["plan"] == d[B]["plan"])
        tr = geomean([max(y, 1e-6) / max(x, 1e-6) for x, y in zip(ta, tb)])
        mr = geomean([y / x for x, y in zip(ma, mb)])
        print(f"{dom:<13}{w:>2} {len(both):>4} | {st.median(ta):>12.4f} {st.median(tb):>12.4f} {fmt(tr,'.2f'):>7} | "
              f"{st.median(ma):>10.1f} {st.median(mb):>10.1f} {fmt(mr,'.2f'):>7} | {st.median(ea):>9.0f} {st.median(eb):>9.0f} | "
              f"{st.median(la):>7.1f} {st.median(lb):>7.1f} {f'{same}/{len(both)}':>10}")

    # ---------------- plan quality ----------------
    print()
    print("=" * 108)
    print("PLAN LENGTH DIFFERENCES ON COMMONLY SOLVED GOALS")
    print("=" * 108)
    diffs = []
    for (dom, prob, gi, w), d in sorted(paired.items()):
        if d.get(A, {}).get("status") == SOLVED and d.get(B, {}).get("status") == SOLVED:
            da, db = d[A]["plan_length"], d[B]["plan_length"]
            if da != db:
                diffs.append((dom, prob, gi, w, da, db, d[A].get("goal_atom") or d[B].get("goal_atom")))
    if not diffs:
        print("none: every commonly solved goal yielded the same plan length in both modes")
    else:
        print(f"{len(diffs)} goals differ in plan length")
        print(f"{'domain':<13}{'problem':<12}{'goal':>5}{'k':>3} {'len IW':>7} {'len A*':>7}  goal atom")
        for dom, prob, gi, w, da, db, atom in diffs[:60]:
            print(f"{dom:<13}{prob:<12}{gi:>5}{w:>3} {da:>7} {db:>7}  {atom}")

    # ---------------- differing action sequences at equal length ----------------
    same_len_diff_plan = 0
    for d in paired.values():
        if d.get(A, {}).get("status") == SOLVED and d.get(B, {}).get("status") == SOLVED:
            if d[A]["plan_length"] == d[B]["plan_length"] and d[A]["plan"] != d[B]["plan"]:
                same_len_diff_plan += 1
    print()
    print(f"Goals where both solved with equal plan length but a different action sequence: {same_len_diff_plan}")

    # ---------------- worst-case wall clock ----------------
    print()
    print("=" * 108)
    print("TOTAL WALL CLOCK SPENT PER MODE (all units, including unsolved / timed-out)")
    print("=" * 108)
    spent = defaultdict(float)
    cnt = defaultdict(int)
    for r in rows:
        m, w = r.get("mode"), r.get("width")
        if m is None:
            continue
        spent[(m, w)] += r.get("search_wall_s", 0.0) or 0.0
        cnt[(m, w)] += 1
    for k_ in sorted(spent):
        print(f"{k_[0]:<10} k={k_[1]}: {spent[k_]:9.1f} s of search over {cnt[k_]} units "
              f"(mean {spent[k_]/max(cnt[k_],1):.2f} s)")

    if args.plans:
        print()
        print("=" * 108)
        print("SAMPLE DIFFERING PLANS")
        print("=" * 108)
        shown = 0
        for (dom, prob, gi, w), d in sorted(paired.items()):
            if shown >= 4:
                break
            if d.get(A, {}).get("status") == SOLVED and d.get(B, {}).get("status") == SOLVED and d[A]["plan"] != d[B]["plan"]:
                print(f"\n--- {dom} {prob} goal#{gi} k={w} :: {d[A]['goal_atom']}")
                print(f"  IW   ({d[A]['plan_length']}): " + " ".join(d[A]["plan"]))
                print(f"  A*IW ({d[B]['plan_length']}): " + " ".join(d[B]["plan"]))
                shown += 1


if __name__ == "__main__":
    main()
