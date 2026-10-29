#!/usr/bin/env bash
# Runs load_sweep over a range of offered request rates for three batching
# configurations and writes one CSV row per offered rate, holding the median
# of each measured column over REPEATS runs.
#
# usage: load_sweep.sh [out.csv]
#
# Environment overrides:
#   LOAD_SWEEP  path to the load_sweep binary (default: <repo>/build/batchd/load_sweep)
#   WORKERS     thread pool workers (default: 4)
#   DURATION_S  seconds of offered load per run; requests = rate * DURATION_S (default: 3)
#   RATES       space-separated offered rates in req/s
#   REPEATS     runs per offered rate; even counts take the lower middle (default: 5)
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"

out="${1:-load_sweep.csv}"
load_sweep="${LOAD_SWEEP:-$repo_root/build/batchd/load_sweep}"
workers="${WORKERS:-4}"
duration_s="${DURATION_S:-3}"
rates="${RATES:-100 200 400 600 800 1000 1500 2000 3000}"
repeats="${REPEATS:-5}"

if [[ ! -x "$load_sweep" ]]; then
  echo "load_sweep not found at $load_sweep; build with 'meson compile -C build'" >&2
  exit 1
fi

# name:max_batch_size:max_wait_ms
configs=(
  "no_batching:1:0"
  "batch_no_wait:32:0"
  "batch_wait_5ms:32:5"
)

header_written=0
: >"$out"
for config in "${configs[@]}"; do
  IFS=: read -r name max_batch_size max_wait_ms <<<"$config"
  for rate in $rates; do
    requests=$((rate * duration_s))
    rows=()
    for ((run = 1; run <= repeats; run++)); do
      echo "$name: rate=$rate req/s, requests=$requests, run $run/$repeats" >&2
      result="$("$load_sweep" --workers "$workers" \
        --max-batch-size "$max_batch_size" --max-wait-ms "$max_wait_ms" \
        --rate "$rate" --requests "$requests" --csv)"
      if [[ $header_written -eq 0 ]]; then
        echo "config,$(head -n 1 <<<"$result")" >>"$out"
        header_written=1
      fi
      rows+=("$(tail -n 1 <<<"$result")")
    done
    # Columns 1-5 are the run's parameters; 6-9 are throughput, p50, p99 and
    # mean batch size, each taken as the median independently.
    row="$(cut -d, -f1-5 <<<"${rows[0]}")"
    for col in 6 7 8 9; do
      median="$(printf '%s\n' "${rows[@]}" | cut -d, -f"$col" | sort -g |
        sed -n "$(((repeats + 1) / 2))p")"
      row+=",$median"
    done
    echo "$name,$row" >>"$out"
  done
done

echo "wrote $out" >&2
