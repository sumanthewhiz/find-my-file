"""Evaluate the production consumer against frozen, labeled WARP feature packets."""
import argparse
import hashlib
import json
import math
import statistics
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def metrics(order, relevance):
    relevant = {key for key, grade in relevance.items() if grade > 0}
    if not relevant:
        raise ValueError("An unlabeled query is not a successful evaluation")
    top = order[:10]
    dcg = sum((2 ** relevance.get(key, 0) - 1) / math.log2(rank + 2) for rank, key in enumerate(top))
    ideal = sum((2 ** grade - 1) / math.log2(rank + 2)
                for rank, grade in enumerate(sorted(relevance.values(), reverse=True)[:10]))
    return {
        "ndcg10": dcg / ideal,
        "mrr10": next((1 / (rank + 1) for rank, key in enumerate(top) if key in relevant), 0),
        "recall10": len(set(top) & relevant) / len(relevant),
        "top1": float(bool(top) and top[0] in relevant),
    }


def score(cases, observed):
    by_id = {row["id"]: row for row in observed}
    if len(by_id) != len(observed) or set(by_id) != {case["id"] for case in cases}:
        raise ValueError("Missing, duplicate or extra observations")
    failures, values = [], {"baseline": [], "contextual": []}
    cohorts = {}
    for case in cases:
        if case.get("synthetic") is not True:
            raise ValueError("Non-synthetic fixture")
        row = by_id[case["id"]]
        ids = {candidate["id"] for candidate in case["candidates"]}
        for arm in values:
            order = row[arm]
            if len(order) != len(ids) or set(order) != ids:
                failures.append(f"{case['id']}: {arm} candidate loss/duplication")
            if order != case["expected_" + arm]:
                failures.append(f"{case['id']}: {arm} differs from verified WARP ordering")
        if row["available"] != case["features"]["ok"]:
            failures.append(f"{case['id']}: feature parser availability mismatch ({row['status']})")
        if case["labels"]["fallback_order_unchanged"] and row["baseline"] != row["contextual"]:
            failures.append(f"{case['id']}: fallback changed ranking")
        if case["split"] != "heldout":
            continue
        scores = {arm: metrics(row[arm], case["labels"]["relevance"]) for arm in values}
        for arm in values:
            values[arm].append(scores[arm])
        for cohort in case["cohorts"]:
            entry = cohorts.setdefault(cohort, {"baseline": [], "contextual": []})
            for arm in values:
                entry[arm].append(scores[arm])
    def average(rows):
        if not rows:
            raise ValueError("No held-out cases")
        return {key: statistics.mean(row[key] for row in rows) for key in rows[0]}
    arms = {arm: average(rows) for arm, rows in values.items()}
    if arms["contextual"]["ndcg10"] - arms["baseline"]["ndcg10"] < 0.03:
        failures.append("No meaningful held-out gain")
    return {"passed": not failures, "cases": len(cases), "heldout_cases": len(values["baseline"]),
            "arms": arms, "cohorts": {name: {arm: average(rows) for arm, rows in data.items()}
                                    for name, data in cohorts.items()},
            "failures": failures,
            "scope": "Synthetic reranking/consumer validation, not live capture accuracy or real-user/full-disk retrieval."}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native-bin", type=Path, required=True)
    parser.add_argument("--corpus", type=Path, default=ROOT / "tests" / "fixtures" / "warp_search_replay.jsonl")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "build" / "context-eval")
    args = parser.parse_args()
    output = args.output_dir.resolve()
    if output == args.corpus.resolve().parent or output == args.native_bin.resolve().parent:
        raise ValueError("Keep evaluation outputs separate from inputs")
    output.mkdir(parents=True, exist_ok=True)
    report_path = output / "scorecard.json"
    report_path.write_text('{"passed":false,"status":"in_progress"}\n', encoding="utf-8")
    cases = [json.loads(line) for line in args.corpus.read_text(encoding="utf-8").splitlines() if line]
    header = hashlib.sha256((ROOT / "SearchRanking.h").read_bytes()).hexdigest()
    if any(case["ranking_header_sha256"] != header for case in cases):
        raise ValueError("Ranking policy differs from the verified WARP replay")
    native = str(args.native_bin.resolve())
    with (output / "native.log").open("w", encoding="utf-8") as log:
        subprocess.run([native], check=True, stdout=log, stderr=subprocess.STDOUT, timeout=60)
        observations = output / "observations.jsonl"
        subprocess.run([native, "--replay", str(args.corpus.resolve()), str(observations)],
                       check=True, stdout=log, stderr=subprocess.STDOUT, timeout=180)
    observed = [json.loads(line) for line in observations.read_text(encoding="utf-8").splitlines() if line]
    report = score(cases, observed)
    report["warp_commit"] = sorted({case["warp_commit"] for case in cases})
    report["consumer_binary_sha256"] = hashlib.sha256(args.native_bin.read_bytes()).hexdigest()
    report["corpus_sha256"] = hashlib.sha256(args.corpus.read_bytes()).hexdigest()
    report["ranking_header_sha256"] = header
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    lines = ["# FindMyFile synthetic context evaluation", "", report["scope"], "",
             f"Passed: **{report['passed']}**; {report['heldout_cases']} held-out / {report['cases']} total cases.",
             "", "| Metric | Baseline | WARP context |", "|---|---:|---:|"]
    for key in report["arms"]["baseline"]:
        lines.append(f"| {key} | {report['arms']['baseline'][key]:.4f} | {report['arms']['contextual'][key]:.4f} |")
    lines += ["", f"Failures: {len(report['failures'])}", *report["failures"]]
    (output / "scorecard.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(json.dumps({key: report[key] for key in ("passed", "cases", "heldout_cases", "arms", "failures")}, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
