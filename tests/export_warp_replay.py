"""Export labeled synthetic WARP packets; never read an activity database."""
import argparse
import hashlib
import json
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--observations", type=Path, required=True)
    parser.add_argument("--warp-commit", required=True)
    parser.add_argument("--ranking-header", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--demo", type=Path, required=True)
    args = parser.parse_args()
    if len(args.warp_commit) != 40 or any(c not in "0123456789abcdef" for c in args.warp_commit):
        raise ValueError("Provide the full evaluated WARP commit")
    cases = [json.loads(line) for line in args.corpus.read_text(encoding="utf-8").splitlines() if line]
    observations = [json.loads(line) for line in args.observations.read_text(encoding="utf-8").splitlines() if line]
    by_id = {row["id"]: row for row in observations}
    if len(by_id) != len(observations) or set(by_id) != {case["id"] for case in cases}:
        raise ValueError("Corpus/observation IDs must match exactly")
    provenance = {"warp_commit": args.warp_commit, "corpus_sha256": sha(args.corpus),
                  "observations_sha256": sha(args.observations), "ranking_header_sha256": sha(args.ranking_header)}
    rows = []
    for case in cases:
        observed = by_id[case["id"]]
        if observed.get("synthetic") is not True:
            raise ValueError("Only explicitly synthetic observations may be exported")
        packet = observed["features"]
        rows.append({
            "schema": "findmyfile.warp-replay.v1", "synthetic": True,
            "id": case["id"], "group": case["group"], "split": case["split"],
            "cohorts": case["cohorts"], **provenance,
            "query": case["query"], "intent": case["intent"], "mode": case["mode"],
            "candidates": case["candidates"], "labels": case["labels"],
            "features": packet, "replay_now_ms": packet.get("expires_ms", 60000) - 59000,
            "expected_baseline": observed["baseline"], "expected_contextual": observed["deterministic"],
        })
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("".join(json.dumps(row, ensure_ascii=False, separators=(",", ":")) + "\n"
                                   for row in rows), encoding="utf-8")
    demo = next(row for row in rows if row["split"] == "heldout"
                and "ambiguous" in row["id"] and row["features"].get("ok") is True
                and row["expected_baseline"][0] != row["expected_contextual"][0])
    args.demo.write_text(json.dumps(demo, ensure_ascii=False, separators=(",", ":")) + "\n", encoding="utf-8")
    print(f"Exported {len(rows)} synthetic packets; demo: {demo['id']}")


if __name__ == "__main__":
    main()
