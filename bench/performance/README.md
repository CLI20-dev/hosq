# PR performance

`run.py --base BASE_CHECKOUT --head HEAD_CHECKOUT --output OUTPUT
--repository OWNER/REPO --base-sha FULL_SHA --head-sha FULL_SHA --run-id NUMBER`
builds this same standalone C++23 harness against each checkout's `include/`.
CMake and a configured Google Benchmark package are required. Both builds use
`-O3`, `NDEBUG`, and `HOSQ_USE_FAST_FLOAT=1`; the compiler and dependency environment
must be identical. The runner retains verbose build logs, CMake caches, compile
commands, verification output, and every raw Benchmark JSON/log in OUTPUT.

The eight 2048-record valid JSON workloads copy the generators, HOSQ schemas,
`run_hosq` functions, and checksums from `bench/comparison`. No other parser is
built. FNV-1a input hashes, byte counts, and parser checksums must agree across
revisions before timing starts. This scope does not measure invalid-input safety
or every API. Workload changes should be checked against the comparison sources.

Each workload runs in four separate processes, base/head/head/base (ABBA).
Defaults are five repetitions per process, minimum 0.15 seconds per repetition,
no warmup: ten CPU and wall samples per revision. `--repetitions`, `--min-time`,
and `--warmup` override these values. Results use medians; `cv_percent` is sample
standard deviation / mean of CPU time × 100 across all repetitions. CPU timing
is the primary comparison; wall timing supplies scheduling context. Hosted
runners are noisy, and repetitions within one process are correlated. This is
report-only evidence without regression thresholds or a pass/fail judgment.
Build, parse, verification, and measurement errors fail the runner.

`report.json` uses schema version 1 with repository, full base/head SHA, integer
run ID, protocol metadata, and exactly eight cases. Each case has `name`, `bytes`,
`input_hash` (decimal FNV-1a uint64 string), and `base`/`head` objects containing
`cpu_ns`, `wall_ns`, `cv_percent`, `samples`, and `processes`. `report.md` is a
local human-readable rendering; a trusted publisher should validate the JSON
and render its own comment instead of publishing arbitrary artifact Markdown.

Run analytical fixture tests with `python3 -m unittest discover -s bench/performance -p 'test_*.py'`.

GitHub Actions measures on PR creation, new commits, and reopening. A separate
`workflow_run` job updates one bot comment on the measured PR, after checking
the artifact's run/repository/SHA identities and the PR's current head. It runs
only trusted default-branch code; the measurement job has no write permission,
and the publisher never executes PR or artifact code. Raw files are retained
as a `performance-report` artifact for 30 days. Failed builds or measurements
fail the measuring job and do not produce a misleading performance comment.
Stale results are skipped; when GitHub omits the run's PR association, the
publisher also requires the measured base to remain current.

The publisher workflow must first land on the default branch. Consequently,
the PR introducing it can validate measurements and artifacts, but automatic
comments start after that merge. Fork workflows may require a maintainer's
normal GitHub Actions approval before measurement can run. No repository
permission settings need to be changed.
