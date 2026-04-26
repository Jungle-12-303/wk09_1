#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "usage: $0 <threads|userprog|vm> <test-name>" >&2
  exit 2
fi

project="$1"
test_name="$2"

case "$project" in
  threads|userprog|vm) ;;
  *)
    echo "unknown Pintos project: $project" >&2
    exit 2
    ;;
esac

if [[ "$test_name" == *.c ]]; then
  echo "Use the test name, not the source file name: ${test_name%.c}" >&2
  exit 2
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$repo_root/pintos/$project/build"
pintos_cmd="$repo_root/pintos/utils/pintos"

if [ ! -x "$pintos_cmd" ]; then
  echo "pintos runner not found: $pintos_cmd" >&2
  exit 2
fi

if [ ! -f "$build_dir/os.dsk" ]; then
  echo "missing $build_dir/os.dsk; run make in pintos/$project first" >&2
  exit 2
fi

cd "$build_dir"

"$pintos_cmd" --gdb -- -q run "$test_name" &
pintos_pid=$!

cleanup() {
  kill "$pintos_pid" 2>/dev/null || true
  wait "$pintos_pid" 2>/dev/null || true
}

trap cleanup EXIT INT TERM

sleep 0.5
echo "GDBSERVER_READY"

wait "$pintos_pid"
