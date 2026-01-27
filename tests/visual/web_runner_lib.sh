#!/usr/bin/env bash
set -euo pipefail

coi_web_open_url() {
  local url="$1"
  if command -v xdg-open >/dev/null 2>&1; then
    xdg-open "$url" >/dev/null 2>&1 &
    disown || true
    return 0
  fi
  if command -v open >/dev/null 2>&1; then
    open "$url" >/dev/null 2>&1 &
    disown || true
    return 0
  fi
  return 1
}

coi_web_pick_free_port() {
  python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
}

coi_web_start_server() {
  local dir="$1"
  local port
  port="$(coi_web_pick_free_port)"
  python3 -m http.server "$port" --bind 127.0.0.1 --directory "$dir" >/dev/null 2>&1 &
  echo "$!:$port"
}

coi_web_stop_server() {
  local pid="$1"
  if [[ -n "$pid" ]]; then
    kill "$pid" >/dev/null 2>&1 || true
  fi
}

coi_web_touch_favicon() {
  local build_dir="$1"
  # Avoid noisy "Failed to load resource" 404 from Chrome's default /favicon.ico request.
  : >"$build_dir/favicon.ico"
}

coi_web_serve_dir_and_open() {
  local dir="$1"
  local html_file="$2"

  local port
  port="$(coi_web_pick_free_port)"
  python3 -m http.server "$port" --bind 127.0.0.1 --directory "$dir" >/dev/null 2>&1 &
  local pid="$!"

  local url="http://127.0.0.1:$port/${html_file}"
  if ! coi_web_open_url "$url"; then
    echo "warn: couldn't open browser; url: $url" >&2
  else
    echo "opened: $url"
  fi
  echo "server: pid=$pid (stop: kill $pid)"
}

