#!/usr/bin/env bash
# release gate that fails when clipvault code opens an internet socket
# runs everything under xvfb with a private session bus and home so the live desktop is never touched
set -euo pipefail
build=$(cd "${1:?usage: check_no_network.sh build-dir}" && pwd)
root=$(cd "$(dirname "$0")/.." && pwd)

if [ -z "${CLIPVAULT_ISOLATED:-}" ]; then
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    mkdir -p "$tmp/home" "$tmp/run"
    chmod 700 "$tmp/run"
    exec env -i PATH="$PATH" HOME="$tmp/home" XDG_RUNTIME_DIR="$tmp/run" \
        XDG_CONFIG_HOME="$tmp/home/.config" XDG_DATA_HOME="$tmp/home/.local/share" \
        GSETTINGS_BACKEND=memory CLIPVAULT_ISOLATED="$tmp" \
        xvfb-run -a dbus-run-session -- "$0" "$build"
fi
tmp=$CLIPVAULT_ISOLATED

if grep -rnE 'QTcpSocket|QTcpServer|QUdpSocket|QSslSocket|QNetworkAccessManager|QNetworkRequest|QDnsLookup|QHostInfo|getaddrinfo|AF_INET' "$root/src"; then
    echo "FAIL network api referenced in src"
    exit 1
fi

trace() {
    local name=$1
    shift
    strace -f -qq -e trace=socket,connect -o "$tmp/trace.$name" "$@" >/dev/null 2>&1 || true
}

trace bench "$build/clipvault_bench" 300
for t in "$build"/tests/test_*; do
    [ -x "$t" ] && [ -f "$t" ] && trace "$(basename "$t")" "$t"
done
trace app timeout 8 "$build/clipvault" --background --db "$tmp/home/app.db"

# the app must at least reach the x server or the trace proves nothing
if ! grep -q 'AF_UNIX' "$tmp/trace.app"; then
    echo "FAIL app trace is empty so the check did not run"
    exit 1
fi
if grep -lE 'AF_INET6?' "$tmp"/trace.*; then
    grep -hE 'AF_INET6?' "$tmp"/trace.* | head -20
    echo "FAIL internet socket opened"
    exit 1
fi
echo "PASS no internet sockets in $(ls "$tmp"/trace.* | wc -l) traced programs"
