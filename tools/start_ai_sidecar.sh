#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -d "$script_dir/../bin" ]; then
  log_dir="$script_dir/../data/logs"
else
  local_game="$HOME/.local/share/Steam/steamapps/common/Half-Life/cstrike/addons/yapb"
  log_dir="$local_game/data/logs"
fi
log_dir=${YAPB_LOG_DIR:-$log_dir}
for arg in "$@"; do
  case "$arg" in
    --instances-root|--instances-root=*)
      exec python3 "$script_dir/ai_sidecar.py" "$@"
      ;;
  esac
done
exec python3 "$script_dir/ai_sidecar.py" \
  --log-dir "$log_dir" "$@"
