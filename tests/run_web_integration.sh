#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COI_BIN="$ROOT_DIR/coi"
MANIFEST="$ROOT_DIR/tests/visual/scenes_manifest.txt"
source "$ROOT_DIR/tests/visual/web_runner_lib.sh"

SCENE_FILTER=""
LIST_ONLY=0
HEADED=0
CAPTURE_SIZE="960x540"
OUT_DIR="${TMPDIR:-/tmp}/coi-web-it"

WEB_BROWSER="${WEB_BROWSER:-$(command -v google-chrome || true)}"

usage() {
  cat <<EOF
Usage:
  $0 [--scene <name>] [--list] [options]

Options:
  --scene <name>         Run only one scene (exact), or use prefix/glob (e.g. input_*, input_)
  --list                 List available web scenes (honors --scene filter)
  --out <dir>            Output dir (default: $OUT_DIR)
  --size <WxH>           Viewport size (default: $CAPTURE_SIZE)
  --browser <path>       Browser binary (default: $WEB_BROWSER)
  --headed               Run with a visible browser window
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --help|-h) usage; exit 0;;
    --scene) SCENE_FILTER="${2:-}"; shift 2;;
    --list) LIST_ONLY=1; shift;;
    --out) OUT_DIR="${2:-}"; shift 2;;
    --size) CAPTURE_SIZE="${2:-}"; shift 2;;
    --browser) WEB_BROWSER="${2:-}"; shift 2;;
    --headed) HEADED=1; shift;;
    *) echo "error: unknown arg: $1"; usage; exit 1;;
  esac
done

if [[ ! -x "$COI_BIN" ]]; then
  echo "Compiler not found. Building..."
  (cd "$ROOT_DIR" && ./build.sh) >/dev/null
fi

if [[ ! -x "$COI_BIN" ]]; then
  echo "error: expected executable at $COI_BIN" >&2
  exit 1
fi

if [[ ! -f "$MANIFEST" ]]; then
  echo "error: missing manifest: $MANIFEST" >&2
  exit 1
fi

if [[ ! -f "$ROOT_DIR/tests/visual/node_modules/playwright-core/package.json" ]]; then
  echo "error: missing web deps: tests/visual/node_modules/playwright-core" >&2
  echo "hint: run: (cd tests/visual && npm install)" >&2
  exit 1
fi

if [[ -z "$WEB_BROWSER" || ! -x "$WEB_BROWSER" ]]; then
  echo "error: web browser not found; set WEB_BROWSER or pass --browser" >&2
  exit 1
fi

trim_ws() {
  local s="$1"
  s="${s#"${s%%[![:space:]]*}"}"
  s="${s%"${s##*[![:space:]]}"}"
  printf '%s' "$s"
}

load_web_scenes() {
  local -n _names=$1
  local -n _paths=$2
  _names=()
  _paths=()
  while IFS= read -r raw || [[ -n "$raw" ]]; do
    line="${raw%%#*}"
    line="$(trim_ws "$line")"
    if [[ -z "$line" ]]; then
      continue
    fi
    IFS="|" read -r scene_name scene_path scene_backends <<<"$line"
    scene_name="$(trim_ws "$scene_name")"
    scene_path="$(trim_ws "$scene_path")"
    scene_backends="$(trim_ws "${scene_backends:-}")"
    case ",$scene_backends," in
      *",web,"*)
        _names+=("$scene_name")
        _paths+=("$scene_path")
        ;;
    esac
  done <"$MANIFEST"
}

pick_web_test_file() {
  local path="$1"
  local base="${path%.coi}"
  if [[ -f "${base}.web_test.mjs" ]]; then
    echo "${base}.web_test.mjs"
  else
    echo ""
  fi
}

names=()
paths=()
load_web_scenes names paths

SCENE_PATTERN=""
if [[ -n "$SCENE_FILTER" ]]; then
  if [[ "$SCENE_FILTER" == *"*"* ]]; then
    SCENE_PATTERN="$SCENE_FILTER"
  else
    SCENE_PATTERN="${SCENE_FILTER}*"
  fi
fi

if [[ "$LIST_ONLY" -eq 1 ]]; then
  for i in "${!names[@]}"; do
    n="${names[$i]}"
    if [[ -n "$SCENE_PATTERN" && "$n" != $SCENE_PATTERN ]]; then
      continue
    fi
    echo "$n"
  done
  exit 0
fi

rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

fail=0
ran=0

for i in "${!names[@]}"; do
  name="${names[$i]}"
  rel_path="${paths[$i]}"
  if [[ -n "$SCENE_PATTERN" && "$name" != $SCENE_PATTERN ]]; then
    continue
  fi

  scene_path="$ROOT_DIR/$rel_path"
  if [[ ! -f "$scene_path" ]]; then
    echo "error: missing scene file: $scene_path"
    fail=1
    continue
  fi

  ran=$((ran + 1))
  echo "==> web it: $name"

  scene_out="$OUT_DIR/$name"
  build_dir="$scene_out/build"
  rm -rf "$scene_out"
  mkdir -p "$build_dir"

  "$COI_BIN" "$scene_path" --out "$build_dir" >/dev/null
  coi_web_touch_favicon "$build_dir"

  test_file="$(pick_web_test_file "$scene_path")"

  srv="$(coi_web_start_server "$build_dir")"
  pid="${srv%%:*}"
  port="${srv#*:}"
  url="http://127.0.0.1:$port/index.html"

  screenshot="$scene_out/fail.png"
  node_args=(
    "$ROOT_DIR/tests/visual/web_integration_playwright.mjs"
    --url "$url"
    --size "$CAPTURE_SIZE"
    --timeout-ms 12000
    --browser "$WEB_BROWSER"
    --screenshot "$screenshot"
  )
  if [[ "$HEADED" -eq 1 ]]; then
    node_args+=(--headed)
  fi
  if [[ -n "$test_file" ]]; then
    node_args+=(--test "$test_file")
  fi

  set +e
  node "${node_args[@]}"
  rc=$?
  set -e

  coi_web_stop_server "$pid"

  if [[ "$rc" -ne 0 ]]; then
    echo "   FAIL: $name (see $screenshot)"
    fail=1
  else
    echo "   OK: $name"
    rm -f "$screenshot" 2>/dev/null || true
  fi
done

if [[ "$ran" -eq 0 ]]; then
  echo "error: no scenes matched (scene_filter='${SCENE_FILTER:-}')" >&2
  echo "hint: list scenes with: $0 --list" >&2
  exit 1
fi

exit "$fail"

