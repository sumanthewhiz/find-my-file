# Context quality: reproducible consumer validation

## Run locally

Use Windows 10/11, Visual Studio's C++ Desktop workload, Windows SDK, and Python
3.11+. No Python packages, model download, WARP installation or personal index
are required.

```powershell
.\tests\run_context_quality.ps1
.\tests\run_context_quality.ps1 -Platform ARM64 -Toolset v145
.\tests\run_context_quality.ps1 -Platform x64 -NoBuild
```

The wrapper builds `WarpContextTests.exe` and `SearchPipelineTests.exe` unless `-NoBuild` is supplied, runs
independent metric unit tests, then invokes the production context client and
shared ranker. Results go to `build\context-eval-<platform>`:

The wrapper and CI select `v145` for VS 18 and `v143` for VS 2022; `-Toolset`
can explicitly override that choice.

| File | Contents |
|---|---|
| `native.log` | Parser, relevance, exact/unseen, transport and worker checks |
| `observations.jsonl` | Actual consumer baseline/context rankings for every case |
| `scorecard.json` | Metrics, cohorts, failures, binary/corpus/ranker hashes |
| `scorecard.md` | Readable summary |

The native tests use uniquely named private fake pipes, not WARP's well-known
pipes. They exercise immediate/async multi-part messages, wrong executable
identity, timeout, size caps and latest-request cancellation. Relevance labels
are read only by the Python scorer, not by the native predictor.

Fallback diagnostics also distinguish an assessed zero from an unavailable
assessment, and preserve the original order in each no-boost state.

`SearchPipelineTests` compiles the application's actual filename retrieval,
index/cache, journal-application and request-builder code. Default tests use
only synthetic metadata and temporary caches: an `eval` prefix search must
retrieve a newly created Markdown guide and send its actual filename and match
tier. They also verify v3 checkpoint persistence, queued-change draining, legacy
v2 invalidation, and rotated/expired journal rejection.

Explicit local diagnostics (not run in CI):

```powershell
# Reads the existing index and contacts installed WARP; does not open the target.
.\build\tests\x64\SearchPipelineTests.exe --cached-query eval 'C:\Work\Search Evals Guide.md'
# Bypasses retrieval to isolate the production serializer/pipe/scorer, window in seconds.
.\build\tests\x64\WarpContextTests.exe --live-probe eval 'C:\Work\Search Evals Guide.md' 7200
```

These report the requested target's presence, match tier and score, plus aggregate
fallback counts; they do not dump unrelated file names or activity. The explicit
`--refresh-cached-query` variant requires elevation and rebuilds/saves the local
index from the MFT before the same diagnostic. It is not a default test.

## What the corpus establishes

`fixtures\warp_search_replay.jsonl` contains **272 synthetic labeled cases**:
68 tuning and 204 held-out, split by project across 4 and 12 groups respectively.
It preserves the **actual feature response** from WARP commit
`78fd2e3d26590baea81ec82705f8b80a4a258c38`; it does not fabricate a new
consumer-side "context score." The later server transport fix does not change
these feature values.

Each row contains the original candidates, independent relevance labels,
opaque reference/feature response, expected verified orders, a fixed replay
clock, and provenance hashes. The replay clock is used only by the offline
decoder test: live requests always use the current clock. Old reference IDs
are never sent to installed WARP.

The consumer must preserve every candidate, match WARP's verified order exactly,
preserve declared fallback order, accept all well-formed success packets and
reject error packets. It must gain at least 0.03 held-out nDCG@10. A parsing
failure cannot silently pass as successful baseline fallback.

Recorded consumer result:

| Held-out metric | Baseline | WARP context |
|---|---:|---:|
| nDCG@10 | 0.3760 | 0.7556 |
| MRR@10 | 0.2737 | 0.7158 |
| Recall@10 | 0.7255 | 0.8922 |
| Top-result success | 0.1618 | 0.6716 |

All 272 orders agree with WARP, with zero candidate-loss or fallback changes.
The [upstream WARP evaluation](https://github.com/sumanthewhiz/WARP/blob/dev/tests/SIGNAL_QUALITY.md)
also measures task boundaries/switches, grounding, paired-project confidence
intervals and the real pinned Qwen arm. This consumer test does not re-label
synthetic evidence as real-user relevance, live event accuracy, or full-disk
retrieval recall. The model arm added no gain; shipping ranking is deterministic.

## Exercise the actual UI safely

```powershell
msbuild 'Find My File!.vcxproj' /p:Configuration=Release /p:Platform=x64 /p:ContextDemoOnly=true
Copy-Item context-demo.json .\x64\Release\context-demo.json
.\x64\Release\FindMyFileContextDemo.exe
```

The dedicated demo build forces synthetic mode and runs without elevation.
Toggle **Use WARP context** to move between baseline and context ordering.
No personal files are inspected, no cloud credentials/index are loaded or
saved, and no WARP connection is opened. Selecting a result shows its supported
association, not an invented model narrative. Engineering artifacts remain
unsigned; signing/SmartScreen trust is separate from elevation.

For CI or on-demand UI verification, the demo-only build also supports:

```powershell
New-Item -ItemType Directory -Force build | Out-Null
$args = "--context-demo `"$PWD\context-demo.json`" --smoke-report `"$PWD\build\ui-smoke.txt`""
$process = Start-Process .\x64\Release\FindMyFileContextDemo.exe -ArgumentList $args -PassThru -Wait
if ($process.ExitCode -ne 0) { throw 'UI smoke failed' }
Get-Content .\build\ui-smoke.txt
```

This opens the **real application window**, toggles its actual checkbox through
the normal handler, reads displayed list-view paths/deltas and the properties
panel, writes a report and closes. It checks that the top result changes, the
candidate count is unchanged, the delta stays bounded and no live worker exists.
It also delivers an expired callback through the real UI completion handler
and verifies that baseline order is restored with every derived delta/reason cleared.
The regular elevated application does not implement this smoke switch.

## Regenerate from a newly verified WARP revision

Keep the original held-out labels and thresholds frozen. First run WARP's own
Phase C evaluation; then export its complete observations:

```powershell
python tests\export_warp_replay.py `
  --corpus ..\WARP\tests\fixtures\phase_c_search.jsonl `
  --observations ..\WARP\build\phase-c-quality\observations.jsonl `
  --ranking-header ..\WARP\SearchRanking.h `
  --warp-commit '<full evaluated 40-character commit>' `
  --output tests\fixtures\warp_search_replay.jsonl `
  --demo context-demo.json
```

Review the provenance and corpus diff before committing it. `SearchRanking.h`
is a byte-identical copy of WARP's policy, not an independently tuned fork;
the evaluator rejects a different header hash. A new task family or model
experiment needs separately labeled data, not edits that make failing held-out
targets agree with predictions.
