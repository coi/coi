# Coi Test Runner

The `tests/run.py` script is the unified entry point for running all tests in the Coi project. It handles unit tests, integration tests, and the visual inspection gallery.

## Prerequisites

- **Python 3.x**
- **Node.js 20+** (for web integration tests)
- **Web Browser** (Chrome or Chromium recommended for Playwright tests)

The runner will automatically install necessary Node.js dependencies in `tests/integration/web` when running integration tests or the gallery.

## Usage

Make sure the script is executable:

```bash
chmod +x tests/run.py
```

Run from the project root:

```bash
./tests/run.py <command> [options]
```

### Commands

#### 1. Run All Tests
Runs both unit and integration tests in sequence.

```bash
./tests/run.py all
```

#### 2. Unit Tests
Runs the compiler unit tests found in `tests/unit`. These tests compile `.coi` files and check for expected success (`_pass.coi`) or failure (`_fail.coi`).

```bash
./tests/run.py unit
```

#### 3. Integration Tests
Runs web-based integration tests using Playwright. These compile scenes, serve them, and run assertions in a browser.

```bash
# Run all integration tests (headless by default)
./tests/run.py integration

# Run specific scenes (wildcards supported)
./tests/run.py integration --scene input_*

# Run in headed mode (visible browser) to debug
./tests/run.py integration --headed --scene paint_rects
```

#### 4. Visual Gallery
Builds and serves scenes, captures screenshots, and generates an HTML gallery for manual visual inspection.

```bash
# Generate gallery for all scenes
./tests/run.py gallery

# Filter logic works the same as integration
./tests/run.py gallery --scene input_*

# Open the gallery in your default browser immediately
./tests/run.py gallery --open
```

#### 5. List Scenes
List all available scenes defined in `tests/integration/web/scenes_manifest.txt`.

```bash
./tests/run.py list
```

## Options

Common options for `integration`, `gallery`, and `all`:

- `--scene <pattern>`: Filter scenes by name (e.g., `input_*`).
- `--out <dir>`: Custom output directory for build artifacts and screenshots.
- `--size <WxH>`: Viewport size for screenshots/tests (default: `960x540`).
- `--browser <path>`: Path to a specific browser executable.

Run `./tests/run.py --help` or `./tests/run.py <command> --help` for more details.
