#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec python3 "$script_dir/ai_sidecar.py" \
  --log-dir "$script_dir/../data/logs" "$@"
