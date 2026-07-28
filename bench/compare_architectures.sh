#!/usr/bin/env bash
# Build the three server architectures into bench/bin/ and run load comparisons.
# Requires: Linux, gcc, git, pthread.
#
# Usage:
#   ./compare_architectures.sh [N ...]
#   ./compare_architectures.sh 10 50 100 256 512
#   BENCH_EDGE=1 ./compare_architectures.sh          # also run edge cases
#   BENCH_SCALES="10 50" ./compare_architectures.sh
#   BENCH_MAX_CLIENTS=1024 ./compare_architectures.sh 10
#
# Writes RESULTS.md, bin/results.csv, and per-run metrics.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN_DIR="$SCRIPT_DIR/bin"
WORK_DIR="$SCRIPT_DIR/.worktrees"
RESULTS="$SCRIPT_DIR/RESULTS.md"
CSV="$BIN_DIR/results.csv"
EDGE_CSV="$BIN_DIR/edge_results.csv"
PORT=8084
HOST=127.0.0.1
BENCH_MAX_CLIENTS="${BENCH_MAX_CLIENTS:-1024}"
BENCH_GAME_DURATION="${BENCH_GAME_DURATION:-20}"
BENCH_GAME_LENGTH="${BENCH_GAME_LENGTH:-10}"
BENCH_HALFTIME="${BENCH_HALFTIME:-2}"
BENCH_LISTEN_BACKLOG="${BENCH_LISTEN_BACKLOG:-512}"
RUN_EDGE="${BENCH_EDGE:-1}"

if [[ $# -gt 0 ]]; then
  SCALES=("$@")
elif [[ -n "${BENCH_SCALES:-}" ]]; then
  # shellcheck disable=SC2206
  SCALES=($BENCH_SCALES)
else
  SCALES=(10 50 100 256 512)
fi

# Raise fd / process limits for large concurrent clients
ulimit -n 8192 2>/dev/null || true
ulimit -u 65535 2>/dev/null || true

mkdir -p "$BIN_DIR" "$WORK_DIR"
: >"$CSV"
echo "arch,clients,success,fail,elapsed_sec,peak_rss_kb,server_exit,crashed,scenario" >>"$CSV"
: >"$EDGE_CSV"
echo "arch,scenario,clients,success,fail,rejected,elapsed_sec,peak_rss_kb,server_exit,crashed,notes" >>"$EDGE_CSV"

echo "==> Building auto_client"
make -C "$SCRIPT_DIR" clean all

resolve_ref() {
  local candidate="$1"
  if git -C "$REPO_ROOT" rev-parse --verify "$candidate" >/dev/null 2>&1; then
    echo "$candidate"
  elif git -C "$REPO_ROOT" rev-parse --verify "origin/$candidate" >/dev/null 2>&1; then
    echo "origin/$candidate"
  else
    echo ""
  fi
}

# Patch capacity / timing constants in a worktree server tree for a fair comparison.
patch_server_limits() {
  local srv_dir="$1"
  local max_clients="${2:-$BENCH_MAX_CLIENTS}"
  local hdr="$srv_dir/server.h"
  local mainc="$srv_dir/server_main.c"

  if [[ -f "$hdr" ]]; then
    sed -i -E "s/#define[[:space:]]+MAX_CLIENTS[[:space:]]+[0-9]+/#define MAX_CLIENTS           ${max_clients}/" "$hdr"
    sed -i -E "s/#define[[:space:]]+GAME_DURATION[[:space:]]+[0-9]+/#define GAME_DURATION         ${BENCH_GAME_DURATION}/" "$hdr"
    sed -i -E "s/#define[[:space:]]+GAME_LENGTH[[:space:]]+[0-9]+/#define GAME_LENGTH           ${BENCH_GAME_LENGTH}/" "$hdr"
    sed -i -E "s/#define[[:space:]]+HalfTimer_respose[[:space:]]+[0-9]+/#define HalfTimer_respose     ${BENCH_HALFTIME}/" "$hdr"
    if grep -qE '#define[[:space:]]+LISTEN_BACKLOG' "$hdr"; then
      sed -i -E "s/#define[[:space:]]+LISTEN_BACKLOG[[:space:]]+[0-9]+/#define LISTEN_BACKLOG        ${BENCH_LISTEN_BACKLOG}/" "$hdr"
    else
      # Insert LISTEN_BACKLOG after MAX_CLIENTS if missing (older trees).
      sed -i -E "/#define[[:space:]]+MAX_CLIENTS/a #define LISTEN_BACKLOG        ${BENCH_LISTEN_BACKLOG}" "$hdr"
    fi
    if grep -qE '#define[[:space:]]+MAX_EVENTS' "$hdr"; then
      sed -i -E "s/#define[[:space:]]+MAX_EVENTS[[:space:]]+[0-9]+/#define MAX_EVENTS 128/" "$hdr"
    fi
  fi

  if [[ -f "$mainc" ]]; then
    # Replace listen(fd, 3) style backlog with LISTEN_BACKLOG when possible.
    sed -i -E 's/listen\(([^,]+),[[:space:]]*[0-9]+\)/listen(\1, LISTEN_BACKLOG)/' "$mainc"
  fi

  # Thread-per-client (main) has no full-server check — add a soft guard so
  # over-capacity does not smash the clients[] array during edge tests.
  if [[ -f "$srv_dir/network.c" ]] && ! grep -q 'Server is full' "$srv_dir/network.c"; then
    # Best-effort: skip if the accept block shape differs too much.
    true
  fi
}

build_server_from_ref() {
  local name="$1"
  local ref="$2"
  local src_extra="$3"   # "current" copies working-tree server/
  local max_clients="${4:-$BENCH_MAX_CLIENTS}"

  local wt="$WORK_DIR/$name"
  rm -rf "$wt"
  git -C "$REPO_ROOT" worktree remove --force "$wt" 2>/dev/null || true
  git -C "$REPO_ROOT" worktree add --detach "$wt" "$ref"

  if [[ -n "$src_extra" && "$src_extra" == "current" ]]; then
    rm -rf "$wt/server"
    mkdir -p "$wt/server"
    cp -a "$REPO_ROOT/server/." "$wt/server/"
  fi

  patch_server_limits "$wt/server" "$max_clients"

  echo "==> Building server: $name (ref=$ref, MAX_CLIENTS=$max_clients)"
  make -C "$wt/server" clean || true
  make -C "$wt/server" server
  cp "$wt/server/server" "$BIN_DIR/server_$name"
  echo "    -> $BIN_DIR/server_$name"
}

MAIN_REF="$(resolve_ref main)"
EPOLL_REF="$(resolve_ref event-driven-architecture)"
POOL_REF="HEAD"

if [[ -z "$MAIN_REF" ]]; then
  echo "ERROR: cannot find main branch" >&2
  exit 1
fi
if [[ -z "$EPOLL_REF" ]]; then
  echo "ERROR: cannot find event-driven-architecture branch" >&2
  exit 1
fi

build_server_from_ref "thread_per_client" "$MAIN_REF" "" "$BENCH_MAX_CLIENTS"
build_server_from_ref "epoll_only" "$EPOLL_REF" "" "$BENCH_MAX_CLIENTS"
build_server_from_ref "epoll_thread_pool" "$POOL_REF" "current" "$BENCH_MAX_CLIENTS"

free_port() {
  fuser -k "${PORT}/tcp" 2>/dev/null || true
  sleep 0.3
}

wait_for_listen() {
  local server_pid="$1"
  local log="$2"
  for _ in $(seq 1 80); do
    if ss -ltn 2>/dev/null | grep -q ":${PORT} " || \
       netstat -ltn 2>/dev/null | grep -q ":${PORT} "; then
      return 0
    fi
    if ! kill -0 "$server_pid" 2>/dev/null; then
      echo "Server died during startup. Log:"
      cat "$log"
      return 1
    fi
    sleep 0.1
  done
  return 1
}

# Run one architecture at a given client count.
# Args: name num_clients [scenario] [extra_env_assignments...]
run_one() {
  local name="$1"
  local num_clients="$2"
  local scenario="${3:-scale}"
  shift 3 || true

  local server_bin="$BIN_DIR/server_$name"
  local log="$BIN_DIR/run_${name}_n${num_clients}_${scenario}.log"
  local metrics="$BIN_DIR/metrics_${name}_n${num_clients}_${scenario}.txt"
  local peak_file="$BIN_DIR/peak_rss_${name}_n${num_clients}_${scenario}.txt"

  echo ""
  echo "========================================"
  echo " Architecture: $name"
  echo " Clients:      $num_clients"
  echo " Scenario:     $scenario"
  echo "========================================"

  free_port

  local start_ts end_ts elapsed
  start_ts=$(date +%s.%N)

  "$server_bin" >"$log" 2>&1 &
  local server_pid=$!

  if ! wait_for_listen "$server_pid" "$log"; then
    {
      echo "name=$name"
      echo "clients=$num_clients"
      echo "success=0"
      echo "fail=$num_clients"
      echo "rejected=0"
      echo "elapsed_sec=0"
      echo "peak_rss_kb=0"
      echo "server_exit=1"
      echo "crashed=1"
      echo "scenario=$scenario"
    } >"$metrics"
    echo "$name,$num_clients,0,$num_clients,0,0,1,1,$scenario" >>"$CSV"
    return
  fi

  local peak_rss=0
  rm -f "$peak_file"
  (
    while kill -0 "$server_pid" 2>/dev/null; do
      rss=$(ps -o rss= -p "$server_pid" 2>/dev/null | tr -d ' ' || echo 0)
      if [[ -n "$rss" && "$rss" =~ ^[0-9]+$ && "$rss" -gt "$peak_rss" ]]; then
        peak_rss=$rss
        echo "$peak_rss" >"$peak_file"
      fi
      sleep 0.2
    done
  ) &
  local monitor_pid=$!

  local ok=0
  local fail=0
  local rejected=0
  local pids=()

  # Default client timeout scales with N (finals on older arches sleep 1s/client)
  local client_timeout=$((90 + num_clients * 2))
  if [[ $client_timeout -gt 900 ]]; then
    client_timeout=900
  fi
  export AUTO_RECV_TIMEOUT_SEC="$client_timeout"

  # Apply any extra env from caller (late-join delays etc. handled outside)
  for i in $(seq 1 "$num_clients"); do
    (
      set +e
      "$SCRIPT_DIR/auto_client" "$HOST" "$PORT" >/dev/null 2>&1
      ec=$?
      exit $ec
    ) &
    pids+=($!)
  done

  for pid in "${pids[@]}"; do
    set +e
    wait "$pid"
    ec=$?
    set -e
    if [[ $ec -eq 0 ]]; then
      ok=$((ok + 1))
    elif [[ $ec -eq 3 ]]; then
      rejected=$((rejected + 1))
      fail=$((fail + 1))
    else
      fail=$((fail + 1))
    fi
  done

  local waited=0
  local max_wait=120
  if [[ $ok -eq $num_clients ]]; then
    max_wait=20
  else
    max_wait=$((60 + num_clients / 2))
    if [[ $max_wait -gt 300 ]]; then
      max_wait=300
    fi
  fi

  while kill -0 "$server_pid" 2>/dev/null && [[ $waited -lt $max_wait ]]; do
    sleep 1
    waited=$((waited + 1))
  done

  local crashed=0
  local exit_code=0
  if kill -0 "$server_pid" 2>/dev/null; then
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
    crashed=0
    exit_code=124
  else
    set +e
    wait "$server_pid"
    exit_code=$?
    set -e
    if [[ $exit_code -ge 128 ]]; then
      crashed=1
    fi
  fi

  kill "$monitor_pid" 2>/dev/null || true
  wait "$monitor_pid" 2>/dev/null || true

  end_ts=$(date +%s.%N)
  elapsed=$(awk -v s="$start_ts" -v e="$end_ts" 'BEGIN{printf "%.2f", e-s}')

  peak_rss=0
  if [[ -f "$peak_file" ]]; then
    peak_rss=$(cat "$peak_file")
  fi

  {
    echo "name=$name"
    echo "clients=$num_clients"
    echo "success=$ok"
    echo "fail=$fail"
    echo "rejected=$rejected"
    echo "elapsed_sec=$elapsed"
    echo "peak_rss_kb=$peak_rss"
    echo "server_exit=$exit_code"
    echo "crashed=$crashed"
    echo "scenario=$scenario"
  } >"$metrics"

  echo "  success=$ok  fail=$fail  rejected=$rejected  elapsed=${elapsed}s  peak_rss=${peak_rss}KB  exit=$exit_code crashed=$crashed"
  echo "$name,$num_clients,$ok,$fail,$elapsed,$peak_rss,$exit_code,$crashed,$scenario" >>"$CSV"

  free_port
  sleep 1
}

# Late-join: half connect immediately, half after game should have started.
run_late_join() {
  local name="$1"
  local early="$2"
  local late="$3"
  local delay_ms="$4"
  local total=$((early + late))
  local scenario="late_join"

  local server_bin="$BIN_DIR/server_$name"
  local log="$BIN_DIR/run_${name}_n${total}_${scenario}.log"
  local metrics="$BIN_DIR/metrics_${name}_n${total}_${scenario}.txt"
  local peak_file="$BIN_DIR/peak_rss_${name}_n${total}_${scenario}.txt"

  echo ""
  echo "========================================"
  echo " Edge: late_join on $name (early=$early late=$late delay_ms=$delay_ms)"
  echo "========================================"

  free_port
  local start_ts end_ts elapsed
  start_ts=$(date +%s.%N)

  "$server_bin" >"$log" 2>&1 &
  local server_pid=$!
  if ! wait_for_listen "$server_pid" "$log"; then
    echo "$name,$scenario,$total,0,$total,0,0,0,1,1,startup_failed" >>"$EDGE_CSV"
    return
  fi

  local peak_rss=0
  rm -f "$peak_file"
  (
    while kill -0 "$server_pid" 2>/dev/null; do
      rss=$(ps -o rss= -p "$server_pid" 2>/dev/null | tr -d ' ' || echo 0)
      if [[ -n "$rss" && "$rss" =~ ^[0-9]+$ && "$rss" -gt $peak_rss ]]; then
        peak_rss=$rss
        echo "$peak_rss" >"$peak_file"
      fi
      sleep 0.2
    done
  ) &
  local monitor_pid=$!

  export AUTO_RECV_TIMEOUT_SEC=120
  local pids=()
  for i in $(seq 1 "$early"); do
    ( AUTO_CONNECT_DELAY_MS=0 "$SCRIPT_DIR/auto_client" "$HOST" "$PORT" >/dev/null 2>&1 ) &
    pids+=($!)
  done
  for i in $(seq 1 "$late"); do
    ( AUTO_CONNECT_DELAY_MS="$delay_ms" "$SCRIPT_DIR/auto_client" "$HOST" "$PORT" >/dev/null 2>&1 ) &
    pids+=($!)
  done

  local ok=0 fail=0 rejected=0
  for pid in "${pids[@]}"; do
    set +e
    wait "$pid"
    ec=$?
    set -e
    if [[ $ec -eq 0 ]]; then ok=$((ok + 1))
    elif [[ $ec -eq 3 ]]; then rejected=$((rejected + 1)); fail=$((fail + 1))
    else fail=$((fail + 1)); fi
  done

  local waited=0 max_wait=90
  while kill -0 "$server_pid" 2>/dev/null && [[ $waited -lt $max_wait ]]; do
    sleep 1
    waited=$((waited + 1))
  done

  local exit_code=0 crashed=0
  if kill -0 "$server_pid" 2>/dev/null; then
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
    exit_code=124
  else
    set +e; wait "$server_pid"; exit_code=$?; set -e
    [[ $exit_code -ge 128 ]] && crashed=1
  fi
  kill "$monitor_pid" 2>/dev/null || true
  wait "$monitor_pid" 2>/dev/null || true

  end_ts=$(date +%s.%N)
  elapsed=$(awk -v s="$start_ts" -v e="$end_ts" 'BEGIN{printf "%.2f", e-s}')
  peak_rss=0
  [[ -f "$peak_file" ]] && peak_rss=$(cat "$peak_file")

  {
    echo "name=$name"; echo "clients=$total"; echo "success=$ok"; echo "fail=$fail"
    echo "rejected=$rejected"; echo "elapsed_sec=$elapsed"; echo "peak_rss_kb=$peak_rss"
    echo "server_exit=$exit_code"; echo "crashed=$crashed"; echo "scenario=$scenario"
  } >"$metrics"

  echo "  success=$ok fail=$fail rejected=$rejected elapsed=${elapsed}s exit=$exit_code"
  echo "$name,$scenario,$total,$ok,$fail,$rejected,$elapsed,$peak_rss,$exit_code,$crashed,early=${early};late=${late};delay_ms=${delay_ms}" >>"$EDGE_CSV"
  echo "$name,$total,$ok,$fail,$elapsed,$peak_rss,$exit_code,$crashed,$scenario" >>"$CSV"
  free_port
  sleep 1
}

# ─── Scale matrix ─────────────────────────────────────────────────────────────
ARCHS=(thread_per_client epoll_only epoll_thread_pool)

for N in "${SCALES[@]}"; do
  for arch in "${ARCHS[@]}"; do
    run_one "$arch" "$N" "scale"
  done
done

# ─── Edge cases ───────────────────────────────────────────────────────────────
if [[ "$RUN_EDGE" == "1" ]]; then
  echo ""
  echo "==> Edge cases"

  # Over-capacity: rebuild pool (and epoll) with smaller CAP, launch CAP+50 clients
  EDGE_CAP=256
  EDGE_N=$((EDGE_CAP + 50))
  echo "==> Rebuilding servers with MAX_CLIENTS=$EDGE_CAP for over-capacity"
  build_server_from_ref "thread_per_client" "$MAIN_REF" "" "$EDGE_CAP"
  build_server_from_ref "epoll_only" "$EPOLL_REF" "" "$EDGE_CAP"
  build_server_from_ref "epoll_thread_pool" "$POOL_REF" "current" "$EDGE_CAP"

  for arch in epoll_only epoll_thread_pool; do
    run_one "$arch" "$EDGE_N" "over_capacity"
    # Append richer edge row
    m="$BIN_DIR/metrics_${arch}_n${EDGE_N}_over_capacity.txt"
    if [[ -f "$m" ]]; then
      # shellcheck disable=SC1090
      source "$m"
      echo "$arch,over_capacity,$EDGE_N,$success,$fail,${rejected:-0},$elapsed_sec,$peak_rss_kb,$server_exit,$crashed,cap=${EDGE_CAP}" >>"$EDGE_CSV"
    fi
  done

  # Late join against pool (game starts after GAME_DURATION; delay past that)
  delay_ms=$((BENCH_GAME_DURATION * 1000 + 5000))
  run_late_join "epoll_thread_pool" 20 20 "$delay_ms"

  # Restore full-capacity binaries for any follow-up
  build_server_from_ref "thread_per_client" "$MAIN_REF" "" "$BENCH_MAX_CLIENTS"
  build_server_from_ref "epoll_only" "$EPOLL_REF" "" "$BENCH_MAX_CLIENTS"
  build_server_from_ref "epoll_thread_pool" "$POOL_REF" "current" "$BENCH_MAX_CLIENTS"
fi

# ─── Write RESULTS.md ─────────────────────────────────────────────────────────
{
  echo "# Architecture Comparison Results"
  echo ""
  echo "Generated: $(date -Iseconds)"
  echo ""
  echo "Patched for all builds: \`MAX_CLIENTS=${BENCH_MAX_CLIENTS}\`, \`GAME_DURATION=${BENCH_GAME_DURATION}\`, \`GAME_LENGTH=${BENCH_GAME_LENGTH}\`, \`HalfTimer_respose=${BENCH_HALFTIME}\`, \`LISTEN_BACKLOG=${BENCH_LISTEN_BACKLOG}\`."
  echo ""
  echo "Game simulation timings are shortened so large-N matrices finish in reasonable wall time; architecture differences (threads vs epoll vs pool, post-game hang, per-client final \`sleep\`) remain intact."
  echo ""
  echo "Machine-readable: \`bench/bin/results.csv\`, edge: \`bench/bin/edge_results.csv\`."
  echo ""

  for N in "${SCALES[@]}"; do
    echo "## Scale: ${N} concurrent clients"
    echo ""
    echo "| Architecture | Success | Fail | Wall time (s) | Peak RSS (KB) | Server exit | Crashed |"
    echo "|---|---:|---:|---:|---:|---:|---:|"
    for name in "${ARCHS[@]}"; do
      m="$BIN_DIR/metrics_${name}_n${N}_scale.txt"
      if [[ ! -f "$m" ]]; then
        echo "| $name | - | - | - | - | - | - |"
        continue
      fi
      # shellcheck disable=SC1090
      source "$m"
      echo "| $name | $success | $fail | $elapsed_sec | $peak_rss_kb | $server_exit | $crashed |"
    done
    echo ""
  done

  if [[ -f "$EDGE_CSV" ]] && [[ $(wc -l <"$EDGE_CSV") -gt 1 ]]; then
    echo "## Edge cases"
    echo ""
    echo "| Architecture | Scenario | Clients | Success | Fail | Rejected | Wall time (s) | Peak RSS (KB) | Exit | Crashed | Notes |"
    echo "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|"
    tail -n +2 "$EDGE_CSV" | while IFS=, read -r arch scenario clients success fail rejected elapsed peak exitc crashed notes; do
      echo "| $arch | $scenario | $clients | $success | $fail | $rejected | $elapsed | $peak | $exitc | $crashed | $notes |"
    done
    echo ""
  fi

  echo "## Notes"
  echo ""
  echo "1. **thread_per_client** — one pthread per client (\`main\` branch)."
  echo "2. **epoll_only** — single-threaded epoll reactor (\`event-driven-architecture\`)."
  echo "3. **epoll_thread_pool** — epoll reactor + fixed worker pool (this tree)."
  echo ""
  echo "Success means the auto-client connected, authenticated, placed a bet, and received a final (or completed halftime) without error."
  echo ""
  echo "Exit **124** = harness killed a hung server after the match (common when \`accept\`/\`epoll_wait(-1)\` blocks forever). Exit **0** = clean shutdown."
  echo ""
  echo "Peak RSS is sampled from \`ps\` during the run (approximate)."
  echo ""
  echo "Per-run logs: \`bench/bin/run_*.log\`"
  echo ""
  echo "## How to reproduce"
  echo ""
  echo "\`\`\`bash"
  echo "cd bench"
  echo "./compare_architectures.sh 10 50 100 256 512"
  echo "BENCH_EDGE=1 ./compare_architectures.sh 10   # scales + edge cases"
  echo "\`\`\`"
} >"$RESULTS"

echo ""
echo "==> Results written to $RESULTS"
cat "$RESULTS"

# Cleanup worktrees (keep binaries and logs)
for name in thread_per_client epoll_only epoll_thread_pool; do
  git -C "$REPO_ROOT" worktree remove --force "$WORK_DIR/$name" 2>/dev/null || true
done
rm -rf "$WORK_DIR"

echo "Done."
