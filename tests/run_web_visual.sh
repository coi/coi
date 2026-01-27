#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COI_BIN="$ROOT_DIR/coi"
MANIFEST="$ROOT_DIR/tests/visual/scenes_manifest.txt"
source "$ROOT_DIR/tests/visual/web_runner_lib.sh"

SCENE_FILTER=""
LIST_ONLY=0
OPEN_AFTER=0
OUT_DIR="${TMPDIR:-/tmp}/coi-web-visual"
CAPTURE_SIZE="960x540"

WEB_BROWSER="${WEB_BROWSER:-$(command -v google-chrome || true)}"

usage() {
  cat <<EOF
Usage:
  $0 [--scene <name>] [--list] [--open] [options]

Options:
  --scene <name>         Run only one scene (exact), or use prefix/glob (e.g. input_*, input_)
  --list                 List available scenes (honors --scene filter)
  --out <dir>            Output dir (default: $OUT_DIR)
  --size <WxH>           Screenshot size (default: $CAPTURE_SIZE)
  --open                 Serve and open an HTML gallery of outputs
  --browser <path>       Browser binary (default: $WEB_BROWSER)
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --help|-h) usage; exit 0;;
    --scene) SCENE_FILTER="${2:-}"; shift 2;;
    --list) LIST_ONLY=1; shift;;
    --open) OPEN_AFTER=1; shift;;
    --out) OUT_DIR="${2:-}"; shift 2;;
    --size) CAPTURE_SIZE="${2:-}"; shift 2;;
    --browser) WEB_BROWSER="${2:-}"; shift 2;;
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

load_scenes() {
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

SCENE_PATTERN=""
if [[ -n "$SCENE_FILTER" ]]; then
  if [[ "$SCENE_FILTER" == *"*"* ]]; then
    SCENE_PATTERN="$SCENE_FILTER"
  else
    SCENE_PATTERN="${SCENE_FILTER}*"
  fi
fi

names=()
paths=()
load_scenes names paths

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

index="$OUT_DIR/index.html"
cat >"$index" <<'HTML'
<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>COI Web Visual Gallery</title>
  <style>
    body{margin:0;padding:24px;font-family:system-ui,-apple-system,Segoe UI,Roboto,Helvetica,Arial,sans-serif;background:#0f1117;color:#e8e8e8}
    .grid{display:grid;grid-template-columns:1fr;gap:18px}
    .card{border:1px solid rgba(255,255,255,0.10);border-radius:12px;padding:14px;background:rgba(255,255,255,0.04)}
    h2{margin:0 0 10px 0;font-size:16px;font-weight:600}
    img{max-width:100%;height:auto;border-radius:10px;border:1px solid rgba(255,255,255,0.08);background:#000}
    .path{opacity:.7;font-size:12px}
  </style>
</head>
<body>
  <h1 style="margin:0 0 18px 0">COI Web Visual Gallery</h1>
  <div class="grid">
HTML

ran=0
for i in "${!names[@]}"; do
  name="${names[$i]}"
  rel_path="${paths[$i]}"
  if [[ -n "$SCENE_PATTERN" && "$name" != $SCENE_PATTERN ]]; then
    continue
  fi

  scene_path="$ROOT_DIR/$rel_path"
  if [[ ! -f "$scene_path" ]]; then
    echo "warn: missing scene file: $scene_path" >&2
    continue
  fi

  ran=$((ran + 1))
  echo "==> web visual: $name"

  scene_out="$OUT_DIR/$name"
  build_dir="$scene_out/build"
  mkdir -p "$build_dir"

  "$COI_BIN" "$scene_path" --out "$build_dir" >/dev/null
  coi_web_touch_favicon "$build_dir"

  srv="$(coi_web_start_server "$build_dir")"
  pid="${srv%%:*}"
  port="${srv#*:}"
  url="http://127.0.0.1:$port/index.html"

  png="$scene_out/capture.png"
  node "$ROOT_DIR/tests/visual/web_capture_playwright.mjs" --url "$url" --out "$png" --size "$CAPTURE_SIZE" --timeout-ms 12000 --browser "$WEB_BROWSER"
  coi_web_stop_server "$pid"

  cat >>"$index" <<HTML
    <div class="card">
      <h2>${name}</h2>
      <div class="path">${rel_path}</div>
      <div style="height:10px"></div>
      <img src="${name}/capture.png" />
    </div>
HTML
done

cat >>"$index" <<'HTML'
  </div>
</body>
</html>
HTML

if [[ "$ran" -eq 0 ]]; then
  echo "error: no scenes matched (scene_filter='${SCENE_FILTER:-}')" >&2
  exit 1
fi

echo "gallery: $index"
if [[ "$OPEN_AFTER" -eq 1 ]]; then
  coi_web_serve_dir_and_open "$OUT_DIR" "index.html"
fi

