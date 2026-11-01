#!/usr/bin/env bash
# clang format and clang tidy for files added since base so existing files are never reformatted
set -euo pipefail
base=${1:?usage: lint_new_files.sh base-ref [build-dir]}
build=${2:-build}
mapfile -t files < <(git diff --diff-filter=A --name-only "$base"...HEAD -- '*.cpp' '*.h')
if [ ${#files[@]} -eq 0 ]; then
    echo "no new c++ files"
    exit 0
fi
clang-format --dry-run -Werror "${files[@]}"
headers=$(printf '%s\n' "${files[@]}" | grep '^src/.*\.h$' | sed 's/[.]/[.]/g' | paste -sd'|' || true)
filter=${headers:-^$}
for f in "${files[@]}"; do
    case $f in
        src/*.cpp)
            # sources behind a build option the build dir did not enable have no compile command
            if grep -qF "/$f\"" "$build/compile_commands.json"; then
                clang-tidy -p "$build" --quiet --header-filter="$filter" "$f"
            else
                echo "skipping clang-tidy for $f because $build does not compile it"
            fi
            ;;
    esac
done
