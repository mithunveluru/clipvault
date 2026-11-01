#!/usr/bin/env bash
# median of five runs pinned to one core so before and after numbers compare
set -euo pipefail
bin=${1:?usage: bench.sh path/to/clipvault_bench [runs] [cpu]}
runs=${2:-5}
cpu=${3:-2}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
for i in $(seq "$runs"); do
    taskset -c "$cpu" "$bin" > "$tmp/run$i" 2>/dev/null
done
# every numeric line is reduced to the median of its first number
paste -d'\n' "$tmp"/run* | awk -v n="$runs" '
{ key = $0; sub(/[0-9][0-9.]*.*/, "", key); match($0, /[0-9][0-9.]*/); v = substr($0, RSTART, RLENGTH)
  rest = substr($0, RSTART + RLENGTH); vals[key] = vals[key] " " v; tail[key] = rest; if (!(key in seen)) { order[++k] = key; seen[key] = 1 } }
END { for (i = 1; i <= k; i++) { m = split(substr(vals[order[i]], 2), a, " "); asort(a); printf "%s%s%s\n", order[i], a[int((m + 1) / 2)], tail[order[i]] } }'
