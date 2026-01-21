<div align="center">
  <img src="docs/images/logo.png" alt="Coi Logo" width="200"/>

# Coi

[![CI](https://github.com/io-eric/coi/actions/workflows/verifiy-and-publish.yml/badge.svg)](https://github.com/io-eric/coi/actions/workflows/verifiy-and-publish.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](http://makeapullrequest.com)
[![Discord](https://img.shields.io/badge/Discord-Join%20Us-7289da?logo=discord&logoColor=white)](https://discord.gg/KSpWx78wuR)

A modern, component-based language for building reactive web apps.  
**Type-safe. Fast. WASM-powered.**

For developers who want the productivity of component-based frameworks with the performance of native code. Fine-grained reactivity, strict typing, and zero GC pauses.

</div>

> [!NOTE]
> Coi is actively evolving. Some syntax may change in future releases.

## What You Can Build

Coi is designed for building reactive, interactive web applications:

- **Web Apps**: Dashboards, admin panels, SPAs with real-time updates
- **Data Visualization**: Interactive charts, graphs, analytics tools with thousands of elements
- **Content-Heavy Sites**: Blogs, documentation sites, landing pages with dynamic components
- **Canvas Apps**: Drawing tools, image editors, animations, or even games

Whether you're building a typical web app or something graphics-intensive, Coi gives you composable components, fine-grained reactivity, type safety, and WASM performance.

## Features

### Performance
- **Fine-Grained Reactivity**: State changes map directly to DOM elements at compile-time. No Virtual DOM overhead.
- **No Garbage Collector**: Deterministic memory management with zero GC pauses. Predictable performance for animations and real-time apps.
- **Batched Operations**: Browser API calls (DOM, Canvas, Storage, etc.) are batched to minimize WASM-JS interop overhead, reducing boundary-crossing costs (see [WebCC](https://github.com/io-eric/webcc) for implementation details).
- **Minimal Runtime**: Tiny WASM binaries with high-performance updates for DOM, Canvas, and more.

### Type System & Safety
- **Strict Typing**: Compile-time error checking with strongly typed parameters and state.
- **Reference Parameters**: Pass state by reference with `&` for seamless parent-child synchronization.
- **Move Semantics**: Explicit ownership transfer with `:` to prevent accidental copying.
- **Private by Default**: Component members are private; use `pub` to expose them.

### Developer Experience
- **Component-Based**: Composable, reusable components with props, state, and lifecycle blocks.
- **Integrated DOM & Styling**: Write HTML elements and scoped CSS directly in components.
- **View Control Flow**: Declarative `<if>`, `<else>`, and `<for>` tags for conditional rendering and iteration.
- **Component Lifecycle**: Built-in `init {}`, `mount {}`, and `tick {}` blocks for setup and animations.
- **Type-Safe Platform APIs**: Browser APIs (Canvas, Storage, Audio, etc.) defined in `.d.coi` files, auto-generated from [WebCC](https://github.com/io-eric/webcc) schema.
- **VS Code Extension**: Syntax highlighting, completions, hover docs, and formatting.

## Benchmarks

Coi is designed for high-performance and minimal footprint. In [benchmarks](benchmark/) comparing Coi, React, and Vue:
Coi's fine-grained reactivity and minimal WASM runtime deliver smaller bundles and faster DOM updates with no Virtual DOM overhead.

<p align="center">
  <img src="benchmark/benchmark_results.svg" alt="Benchmark Results" width="600">
</p>

See the [benchmark/](benchmark/) directory for details and instructions on how to run it yourself.

## Example

```tsx
component Counter(string label, mut int& value) {
    def add(int i) : void {
        value += i;
    }

    style {
        .counter {
            display: flex;
            gap: 12px;
            align-items: center;
        }
        button {
            padding: 8px 16px;
            cursor: pointer;
        }
    }

    view {
        <div class="counter">
            <span>{label}: {value}</span>
            <button onclick={add(1)}>+</button>
            <button onclick={add(-1)}>-</button>
        </div>
    }
}

component App {
    mut int score = 0;

    style {
        .app {
            padding: 24px;
            font-family: system-ui;
        }
        h1 {
            color: #1a73e8;
        }
        .win {
            color: #34a853;
            font-weight: bold;
        }
    }

    view {
        <div class="app">
            <h1>Score: {score}</h1>
            <Counter label="Player" &value={score} />
            <if score >= 10>
                <p class="win">You win!</p>
            </if>
        </div>
    }
}

app {
    root = App;
    title = "My Counter App";
    description = "A simple counter built with Coi";
    lang = "en";
}
```

## Quick Start

### Install

**Prerequisites:**
- Clang 16+ (required for full C++20 support)
  - Ubuntu/Debian: `sudo apt install clang-16`
  - macOS: `brew install llvm`
  - Fedora: `sudo dnf install clang`

```bash
git clone https://github.com/io-eric/coi.git
cd coi
./build.sh
```

### Create a Project

```bash
coi init my-app
cd my-app
coi dev
```

Open `http://localhost:8000` in your browser.

### CLI Commands

| Command | Description |
|---------|-------------|
| `coi init [name]` | Create a new project |
| `coi build` | Build the project |
| `coi dev` | Build and start dev server |
| `coi <file.coi> --out <dir>` | Compile a single file |

### Project Structure

```
my-app/
├── src/
│   ├── App.coi          # Entry point (required)
│   └── components/      # Your components
├── assets/              # Static files (images, fonts, etc.)
└── dist/                # Build output
```

- **`src/App.coi`** — The compiler always looks for this as the entry point.
- **`assets/`** — Automatically copied to `dist/assets/` on build.

## Documentation

- [Getting Started](docs/getting-started.md) — Installation, first project, imports
- [Language Guide](docs/language-guide.md) — Types, enums, control flow, operators
- [Components](docs/components.md) — State, lifecycle, props, references
- [View Syntax](docs/view-syntax.md) — JSX-like templates, `<if>`, `<for>`, events
- [Styling](docs/styling.md) — Scoped and global CSS
- [Platform APIs](docs/api-reference.md) — Canvas, Storage, Audio, Fetch, and more

## VS Code Extension

The Coi Language extension provides syntax highlighting, auto-completions, hover docs, and signature help.

Install from the [VS Code Marketplace](https://marketplace.visualstudio.com/items?itemName=coi-lang.coi-language) or build manually:

```bash
cd vscode-extension
npm install && npm run package
```

## License

MIT © [io-eric](https://github.com/io-eric)
