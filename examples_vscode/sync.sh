#!/usr/bin/env bash
# Copy sketch.ino and diagram.json from ../examples/<ex>/ into the VS Code
# copies here (<ex>/src/sketch.ino, <ex>/diagram.json).
#
#   ./sync.sh          copy changed files
#   ./sync.sh --check  only report differences, exit 1 if any
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
src_root="$here/../examples"
check=0
[[ "${1:-}" == "--check" ]] && check=1

status=0
for dst in "$here"/*/; do
  ex="$(basename "$dst")"
  [[ -f "$dst/wokwi.toml" ]] || continue
  if [[ ! -d "$src_root/$ex" ]]; then
    echo "warning: $ex has no counterpart in examples/" >&2
    continue
  fi
  for pair in "sketch.ino:src/sketch.ino" "diagram.json:diagram.json"; do
    from="$src_root/$ex/${pair%%:*}"
    to="$dst${pair#*:}"
    cmp -s "$from" "$to" && continue
    status=1
    if (( check )); then
      echo "differs: $ex/${pair#*:}"
    else
      cp "$from" "$to"
      echo "updated: $ex/${pair#*:}"
    fi
  done
done

# Wokwi examples in examples/ without a VS Code copy
for src in "$src_root"/*/; do
  ex="$(basename "$src")"
  [[ -f "$src/diagram.json" && ! -d "$here/$ex" ]] &&
    echo "note: examples/$ex has no VS Code copy (create platformio.ini and wokwi.toml by hand)" >&2
done

if (( check )); then exit $status; fi
(( status )) || echo "all examples up to date"
