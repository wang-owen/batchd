#!/usr/bin/env bash
# Runs load_sweep over a range of offered request rates for three batching
# configurations and writes one CSV row per run.
#
# usage: load_sweep.sh [out.csv]
#
# Environment overrides:
#   LOAD_SWEEP  path to the load_sweep binary (default: <repo>/build/inference_srv/load_sweep)
#   WORKERS     thread pool workers (default: 4)
#   DURATION_S  seconds of offered load per run; requests = rate * DURATION_S (default: 3)
#   RATES       space-separated offered rates in req/s
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"

out="${1:-load_sweep.csv}"
load_sweep="${LOAD_SWEEP:-$repo_root/build/inference_srv/load_sweep}"
workers="${WORKERS:-4}"
duration_s="${DURATION_S:-3}"
rates="${RATES:-100 200 400 600 800 1000 1500 2000 3000}"

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
    echo "$name: rate=$rate req/s, requests=$requests" >&2
    result="$("$load_sweep" --workers "$workers" \
      --max-batch-size "$max_batch_size" --max-wait-ms "$max_wait_ms" \
      --rate "$rate" --requests "$requests" --csv)"
    if [[ $header_written -eq 0 ]]; then
      echo "config,$(head -n 1 <<<"$result")" >>"$out"
      header_written=1
    fi
    echo "$name,$(tail -n 1 <<<"$result")" >>"$out"
  done
done

echo "wrote $out" >&2
