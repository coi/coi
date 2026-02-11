# Getting Started

This guide will help you set up Coi and create your first component.

## Installation

### Prerequisites

Coi requires [WebCC](https://github.com/io-eric/webcc) to be installed. The build script will automatically initialize and build the WebCC submodule if it is not found on your system.

### Building from Source

To build the compiler and the toolchain, clone the repository and run the build script:

```bash
git clone https://github.com/io-eric/coi.git
cd coi
./build.sh
```

This will:
1. Initialize the WebCC submodule (if needed)
2. Build the WebCC toolchain
3. Generate type definition files (defs/web/*.d.coi) from WebCC schema
4. Build the Coi compiler

### Build Options

The build script supports these options:

```bash
./build.sh --rebuild-schema   # Force regenerate defs/web/*.d.coi from WebCC schema
./build.sh --rebuild-webcc    # Force rebuild the WebCC toolchain
./build.sh --help             # Show all available options
```

### Type Definition System

Coi uses `.d.coi` definition files for type information:

- **`defs/core/`** — Built-in types (int, string, bool, array, etc.) - source-controlled
- **`defs/web/`** — Web platform APIs auto-generated from WebCC schema - gitignored

The build system automatically detects when `deps/webcc/schema.def` changes:

1. Running `./build.sh` checks if WebCC's schema.def was modified
2. If changed, WebCC rebuilds automatically
3. Coi regenerates `defs/web/*.d.coi` to match the new schema

This means you can simply run `./build.sh` after editing `schema.def` and everything cascades correctly.

Use `--rebuild-schema` to force regeneration even when no changes are detected.

## CLI Commands

The Coi CLI provides commands for creating, building, and running projects.

### `coi init`

Create a new Coi project from template:

```bash
coi init my-app
```

This creates a new directory with a complete project structure:

```
my-app/
├── assets/
│   └── images/
├── src/
│   ├── App.coi
│   ├── layout/
│   │   ├── Footer.coi
│   │   └── NavBar.coi
│   ├── pages/
│   │   ├── About.coi
│   │   └── Home.coi
│   └── ui/
│       └── Button.coi
├── styles/
│   └── reset.css
└── README.md
```

If no name is provided, you'll be prompted to enter one:

```bash
coi init
# Project name: my-app
```

Project names must start with a letter or underscore and contain only letters, numbers, hyphens, and underscores.

### `coi build`

Build the project in the current directory:

```bash
cd my-app
coi build
```

This compiles `src/App.coi` and outputs to `dist/`:
- `dist/index.html` — Entry HTML file
- `dist/app.js` — JavaScript runtime
- `dist/app.wasm` — WebAssembly binary
- `dist/app.css` — Generated CSS bundle

Assets from the `assets/` folder are automatically copied to `dist/assets/`.

### `coi dev`

Build and start a local development server with hot reloading:

```bash
coi dev
```

This builds the project and starts a server at `http://localhost:8000`. The server automatically watches for changes to:
- `.coi` files in `src/`
- Files in `assets/` (images, fonts, etc.)
- `.css` files in `styles/`

When you save any watched file, the project rebuilds automatically and your browser refreshes with the latest changes.

#### Disable Hot Reloading

If you need to disable hot reloading (for debugging build issues or testing manual workflows):

```bash
coi dev --no-watch
```

This builds the project once and starts the dev server without file watching. To see changes, stop the server and run `coi dev --no-watch` again.

Press `Ctrl+C` to stop the dev server.

### Direct Compilation

Compile a single `.coi` file directly:

```bash
coi App.coi --out ./dist
```

#### Options

| Option | Description |
|--------|-------------|
| `--out, -o <dir>` | Output directory |
| `--cc-only` | Generate C++ only, skip WASM compilation |
| `--keep-cc` | Keep generated C++ files for debugging |

To keep the intermediate C++ file:

```bash
coi App.coi --out ./dist --keep-cc
```

This also generates `dist/App.cc` so you can inspect the generated C++ code.

## Your First Component

Create a file called `App.coi`:

```tsx
component App {
    mut int count = 0;

    def increment() : void {
        count += 1;
    }

    style {
        .container {
            padding: 20px;
            font-family: system-ui;
        }
        button {
            padding: 8px 16px;
            cursor: pointer;
        }
    }

    view {
        <div class="container">
            <h1>Count: {count}</h1>
            <button onclick={increment}>+1</button>
        </div>
    }
}

app { root = App; }
```

### App Configuration

The `app {}` block configures your application. Here are all available properties:

```tsx
app {
    root = App;                                    // Required: Root component
    title = "My App";                              // Page title (<title> tag)
    description = "A description for SEO";         // Meta description
    lang = "en";                                   // HTML lang attribute (default: "en")
}
```

| Property | Type | Required | Description |
|----------|------|----------|-------------|
| `root` | Component | Yes | The root component to render |
| `title` | String | No | Sets the page `<title>` tag |
| `description` | String | No | Sets `<meta name="description">` for SEO |
| `lang` | String | No | Sets the `<html lang="">` attribute (default: `"en"`) |

**Note:** If you have a `styles/` folder at the project root (next to `src/`), all `.css` files in it are automatically bundled into `app.css`.

For client-side routing, use the `router {}` block inside your root component. See [Components](components.md#client-side-routing) for details.

Compile and run:

```bash
coi App.coi --out ./dist
cd dist && python3 -m http.server
```

## Project Structure

For larger projects, organize your code into multiple files:

```
my-app/
├── src/
│   ├── App.coi
│   ├── components/
│   │   ├── Button.coi
│   │   └── Card.coi
│   └── layout/
│       ├── Header.coi
│       └── Footer.coi
├── dist/           # Generated output
└── build.sh
```

## Imports

Coi uses a strict, explicit import system.

```tsx
import "components/Button.coi";
import "layout/Header.coi";
```

### Import Rules

1. **Relative Paths**: Imports are relative to the current file.
2. **Explicit Only**: There are no "transitive imports". If `A` imports `B`, and `B` imports `C`, `A` cannot use `C` unless it imports `C` directly.
3. **Visibility**: You can only use components that are marked with `pub` if they are in a different module.

### Modules

You can organize files into named modules using the `module` keyword at the top of the file:

```tsx
// src/ui/Button.coi
module TurboUI;
pub component Button { ... }
```

- **Same Module:** Can access `Button` directly after import.
- **Different Module:** Must use fully qualified name `<TurboUI::Button />`.

## Getting Help

Getting stuck or need help? Join the [Coi Discord community](https://discord.gg/KSpWx78wuR) for fast support and discussions, or [open an issue](https://github.com/io-eric/coi/issues) on GitHub for bugs and feature requests.

## Next Steps

- [Language Guide](language-guide.md) — Types, control flow, operators
- [Components](components.md) — Component syntax, lifecycle, props
- [Styling](styling.md) — Scoped and global CSS
- [View Syntax](view-syntax.md) — JSX-like templates, conditionals, loops
- [Platform APIs](api-reference.md) — Canvas, Storage, Audio, and more
