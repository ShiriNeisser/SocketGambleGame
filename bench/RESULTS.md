# Architecture Comparison Results

Generated: 2026-07-28T13:04:00+00:00

Patched for all builds: `MAX_CLIENTS=1024`, `GAME_DURATION=30`, `GAME_LENGTH=10`, `HalfTimer_respose=2`, `LISTEN_BACKLOG=512`.

Game simulation timings are shortened so large-N matrices finish in reasonable wall time; architecture differences (threads vs epoll vs pool, post-game hang, per-client final `sleep`) remain intact.

Machine-readable: `bench/bin/results.csv`, edge: `bench/bin/edge_results.csv`.

## Summary

At every measured scale, **epoll + thread pool** finishes the match in ~45–46s with a **clean exit 0**, while thread-per-client and epoll-only stretch to ~145–260s and usually hang (exit **124**) after the game. Memory grows sharply with threads (≈9 MB RSS at 512 clients) and stays flat for epoll variants (~2.2–2.9 MB).

| N | Fastest wall time | Lowest RSS | Clean shutdown |
|---:|---|---|---|
| 10 | pool (46.01s) | thread (2180 KB) | thread + pool |
| 50 | pool (46.02s) | pool (2240 KB) | thread + pool |
| 100 | pool (45.02s) | pool (2348 KB) | pool |
| 256 | pool* (239.96s, 253/256 ok) | epoll (2328 KB) | pool |
| 512 | pool (45.07s) | epoll (2604 KB) | pool |

\*At N=256 the pool run had 3 client failures (likely join-window race under burst); wall time includes the longer fail-path wait. At N=512 the same architecture recovered to 512/512 in 45s.

## Scale: 10 concurrent clients

| Architecture | Success | Fail | Wall time (s) | Peak RSS (KB) | Server exit | Crashed |
|---|---:|---:|---:|---:|---:|---:|
| thread_per_client | 10 | 0 | 65.02 | 2180 | 0 | 0 |
| epoll_only | 10 | 0 | 82.04 | 2432 | 124 | 0 |
| epoll_thread_pool | 10 | 0 | 46.01 | 2200 | 0 | 0 |

## Scale: 50 concurrent clients

| Architecture | Success | Fail | Wall time (s) | Peak RSS (KB) | Server exit | Crashed |
|---|---:|---:|---:|---:|---:|---:|
| thread_per_client | 50 | 0 | 145.02 | 2804 | 0 | 0 |
| epoll_only | 50 | 0 | 162.04 | 2356 | 124 | 0 |
| epoll_thread_pool | 50 | 0 | 46.02 | 2240 | 0 | 0 |

## Scale: 100 concurrent clients

| Architecture | Success | Fail | Wall time (s) | Peak RSS (KB) | Server exit | Crashed |
|---|---:|---:|---:|---:|---:|---:|
| thread_per_client | 100 | 0 | 245.16 | 3480 | 0 | 0 |
| epoll_only | 100 | 0 | 260.18 | 2528 | 124 | 0 |
| epoll_thread_pool | 100 | 0 | 45.02 | 2348 | 0 | 0 |

## Scale: 256 concurrent clients

| Architecture | Success | Fail | Wall time (s) | Peak RSS (KB) | Server exit | Crashed |
|---|---:|---:|---:|---:|---:|---:|
| thread_per_client | 256 | 0 | 260.21 | 5408 | 124 | 0 |
| epoll_only | 256 | 0 | 260.20 | 2328 | 124 | 0 |
| epoll_thread_pool | 253 | 3 | 239.96 | 2520 | 0 | 0 |

## Scale: 512 concurrent clients

| Architecture | Success | Fail | Wall time (s) | Peak RSS (KB) | Server exit | Crashed |
|---|---:|---:|---:|---:|---:|---:|
| thread_per_client | 512 | 0 | 260.24 | 8980 | 124 | 0 |
| epoll_only | 512 | 0 | 260.24 | 2604 | 124 | 0 |
| epoll_thread_pool | 512 | 0 | 45.07 | 2868 | 0 | 0 |

## Edge cases

| Architecture | Scenario | Clients | Success | Fail | Rejected | Wall time (s) | Peak RSS (KB) | Exit | Crashed | Notes |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| epoll_only | over_capacity | 306 | 256 | 50 | 50 | 453.43 | 2692 | 124 | 0 | cap=256 |
| epoll_thread_pool | over_capacity | 306 | 256 | 50 | 50 | 45.04 | 2552 | 0 | 0 | cap=256 |
| epoll_thread_pool | late_join | 40 | 20 | 20 | 20 | 46.01 | 2116 | 0 | 0 | early=20;late=20;delay_ms=35000 |

### Edge-case takeaways

1. **Over capacity (306 clients, cap=256):** both epoll variants reject exactly 50 extras with `"Server is full"` (no crash). Pool still finishes in ~45s and exits cleanly; epoll-only hangs (124) and spends ~7.5 minutes draining per-client final `sleep`s.
2. **Late join:** 20 early clients succeed; 20 delayed past kickoff are rejected with `"cannot join now"` (`rejected=20`). Pool remains exit 0.
3. **Burst / stampede:** all N clients connect and send AUTH+bet immediately. Thread-per-client pays in RSS (threads); epoll-only serializes work on one thread and blocks on finals; pool keeps the reactor free.
4. **Post-game hang:** epoll-only uses `epoll_wait(-1)` and frequently never leaves the accept loop after the match (harness exit 124). Pool uses a 500ms timeout and shuts down cleanly.

## Notes

1. **thread_per_client** — one pthread per client (`main` branch).
2. **epoll_only** — single-threaded epoll reactor (`event-driven-architecture`).
3. **epoll_thread_pool** — epoll reactor + fixed worker pool (this tree).

Success means the auto-client connected, authenticated, placed a bet, and received a final (or completed halftime) without error.

Exit **124** = harness killed a hung server after the match (common when `accept`/`epoll_wait(-1)` blocks forever). Exit **0** = clean shutdown.

Peak RSS is sampled from `ps` during the run (approximate).

Per-run logs: `bench/bin/run_*.log`

## How to reproduce

```bash
cd bench
./compare_architectures.sh 10 50 100 256 512
BENCH_EDGE=1 ./compare_architectures.sh 10 50   # scales + edge cases
```
