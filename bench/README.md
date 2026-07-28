# Benchmark harness

Compares the three server architectures under the same concurrent client load.

| Binary / branch | Architecture |
|---|---|
| `main` | Thread-per-client |
| `event-driven-architecture` | EPOLL only |
| current tree (`epoll_thread_pool`) | EPOLL + thread pool |

The harness patches `MAX_CLIENTS`, `GAME_DURATION`, `GAME_LENGTH`, `HalfTimer_respose`, and `LISTEN_BACKLOG` in all builds so scale comparisons are fair.

## Files

- `auto_client.c` — headless client (auth → bet → halftime → final)
- `compare_architectures.sh` — multi-scale load test + edge cases; writes `RESULTS.md` and `bin/results.csv`
- `RESULTS.md` — latest comparison tables + edge-case notes

## Run (Linux)

```bash
cd bench
chmod +x compare_architectures.sh
./compare_architectures.sh 10 50 100 256 512
BENCH_EDGE=1 ./compare_architectures.sh 10 50   # include over-capacity + late-join
```

Environment knobs:

| Variable | Default | Meaning |
|---|---|---|
| `BENCH_MAX_CLIENTS` | 1024 | Patched into all server builds |
| `BENCH_GAME_DURATION` | 30 | Join window before kickoff |
| `BENCH_GAME_LENGTH` | 10 | Simulated match length (shortened for harness throughput) |
| `BENCH_HALFTIME` | 2 | Halftime pause seconds |
| `BENCH_LISTEN_BACKLOG` | 512 | `listen()` backlog |
| `BENCH_EDGE` | 1 | Run over-capacity + late-join |
| `BENCH_SCALES` | (args) | Alternate way to pass scales |

Requires `gcc`, `git`, `make`, `timeout`, and `pthread`. Raises `ulimit -n` to 8192 when permitted.

## Latest headline (2026-07-28)

At **512 concurrent clients**, epoll+thread-pool finished in **45.07s** with exit **0** and ~2.9 MB RSS. Thread-per-client and epoll-only took ~260s, hung (exit 124), and thread RSS reached ~9 MB. See `RESULTS.md` for the full matrix and edge cases.
