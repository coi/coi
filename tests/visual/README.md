# Web visual + integration tests

This folder contains lightweight tooling to:

- capture **web** scene screenshots for manual review (gallery)
- run **web integration tests** with Playwright (scripted interactions + assertions)

## Setup

```bash
cd tests/visual
npm install
```

`playwright-core` is used with your system Chrome by default.
Set `WEB_BROWSER=/path/to/chrome` if needed.

## Web gallery (manual inspection)

```bash
./tests/run_web_visual.sh --open
./tests/run_web_visual.sh --scene input_* --out /tmp/coi-web-visual --open
```

## Web integration tests (Playwright)

Per-scene tests live next to the `.coi` file as `*.web_test.mjs` (ESM):

```js
export async function run({ page, expect }) { /* ... */ }
```

Run:

```bash
./tests/run_web_integration.sh --scene input_*
./tests/run_web_integration.sh --list
```

