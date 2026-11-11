#!/bin/sh
set -eu

init=$1
manager=$2
helper=$3

if ! command -v unshare >/dev/null 2>&1; then
    echo "SKIP: unshare is unavailable"
    exit 77
fi
if unshare --mount --pid --fork --mount-proc true >/dev/null 2>&1; then
    namespace_args="--mount --pid --fork --mount-proc"
elif unshare --user --map-root-user --mount --pid --fork --mount-proc true >/dev/null 2>&1; then
    namespace_args="--user --map-root-user --mount --pid --fork --mount-proc"
else
    echo "SKIP: this runner cannot create mount/PID namespaces"
    exit 77
fi

tmp=$(mktemp -d)
pid=
cleanup() {
    if [ -n "$pid" ]; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM

mkdir "$tmp/units"
: > "$tmp/console"
cat > "$tmp/units/default.target" <<EOF
[Unit]
Description=PID namespace integration target
Wants=crash.service
After=crash.service
[Target]
EOF
cat > "$tmp/units/crash.service" <<EOF
[Unit]
Description=Crash Manager once, then verify recovery
[Service]
Type=oneshot
ExecStart=$helper $tmp/manager-crashed $tmp/manager-restarted $tmp/descendant.lock
EOF

# The namespace probe above selected either privileged or rootless isolation.
# Intentional word splitting expands the fixed option list only.
# shellcheck disable=SC2086
unshare $namespace_args "$init" \
    --console "$tmp/console" --manager "$manager" \
    --unit-dir "$tmp/units" --target default.target \
    --environment container:other &
pid=$!

i=0
while [ ! -f "$tmp/manager-restarted" ]; do
    if ! kill -0 "$pid" 2>/dev/null; then
        wait "$pid" || {
            echo "FAIL: ninit-init exited before Manager recovery"
            exit 1
        }
        echo "FAIL: ninit-init exited before Manager recovery"
        exit 1
    fi
    i=$((i + 1))
    if [ "$i" -ge 300 ]; then
        echo "FAIL: Manager did not recover within 30 seconds"
        exit 1
    fi
    sleep 0.1
done

wait "$pid" || {
    echo "FAIL: PID 1 did not complete the requested container shutdown"
    exit 1
}
pid=
echo "PASS: PID 1 cleaned the Manager's orphaned service tree before restart"
