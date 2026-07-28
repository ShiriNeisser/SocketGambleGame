# SocketGambleGame

A multiplayer TCP + UDP-multicast betting game written in C. Three components:

- `server/` — game server (epoll reactor + fixed thread pool). TCP for auth/bet/final result, UDP multicast for live score updates.
- `client/` — interactive terminal client.
- `bench/` — headless `auto_client` plus `compare_architectures.sh`, a multi-scale load-test harness that compares this tree against other server-architecture branches.

## Cursor Cloud specific instructions

### Build / lint / test / run

Pure C project. `gcc` and `make` are the only toolchain and are already installed; there are no package-manager dependencies to fetch. There is no separate linter — the Makefiles compile with `-Wall -Wextra` (see `server/Makefile`, `client/Makefile`, `bench/Makefile`), so a clean `make` is the lint signal.

- Build: `make -C server`, `make -C client`, `make -C bench`.
- Run server: `./server/server` (binds TCP `PORT 8084` and UDP multicast `239.0.0.1:8085`; constants in `server/server.h`).
- Run client: `./client/client` (connects to `127.0.0.1:8084`).
- Load-test harness (automated test): `cd bench && ./compare_architectures.sh 10` (see `bench/README.md` for scales/env knobs).

### Non-obvious gotchas

- The server (and client) `printf` to a pipe/file is block-buffered — logs look empty until flush. Use `stdbuf -oL -eL ./server` when redirecting output to a file/log.
- Game timing is hardcoded in `server/server.h`: `GAME_DURATION` (30s countdown before kickoff) then `GAME_LENGTH` (30s match), so a full session takes ~60s+. A client must authenticate and bet during the pre-game countdown. Do not edit these to speed up a manual run; instead use the bench harness env knobs (`BENCH_GAME_DURATION`, `BENCH_GAME_LENGTH`, `BENCH_HALFTIME`), which patch throwaway worktree builds only.
- The interactive client reads from stdin in sequence: password, then `<team> <amount>` (team 0=tie,1,2), then a `YES`/`NO` halftime reply. It can be driven non-interactively, e.g. `printf '1234\n1 100\nNO\n' | ./client`. The auth password is `SECRET_PASSWORD "1234"`.
- Compiled binaries `server/server` and `client/client` are committed to the repo even though `.gitignore` lists them. `make clean` deletes them and shows them as deleted in `git status`; restore with `git checkout -- server/server client/client` if you did not mean to change them.
- `bench/compare_architectures.sh` creates git worktrees under `bench/.worktrees/` for the `main` (thread-per-client) and `event-driven-architecture` (epoll-only) branches, so those refs must be fetchable from `origin`. It also overwrites the tracked file `bench/RESULTS.md` and writes to `bench/bin/`; run `git checkout -- bench/RESULTS.md` afterward if you don't intend to commit new results.
- UDP multicast (loopback) works in the Cloud VM — clients receive live minute-by-minute score broadcasts.
