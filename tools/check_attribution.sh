#!/usr/bin/env bash
# fails when a commit message or an added line names the tool vendor
set -euo pipefail
range=${1:?usage: check_attribution.sh base..head}
pattern='cla[u]de|anthr[o]pic'
# a range git cannot resolve must fail rather than check nothing
git rev-list "$range" >/dev/null
bad=0
if git log --format='%H %an %ae%n%B' "$range" | grep -inE "$pattern"; then
    echo "FAIL commit message or author matches"
    bad=1
fi
if git diff "$range" | grep -E '^\+' | grep -inE "$pattern"; then
    echo "FAIL added line matches"
    bad=1
fi
exit $bad
