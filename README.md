<div align="center">
  <img src="docs/images/logo.png" alt="Coi Logo" width="200"/>

# Coi

[![CI](https://github.com/io-eric/coi/actions/workflows/verifiy-and-publish.yml/badge.svg)](https://github.com/io-eric/coi/actions/workflows/verifiy-and-publish.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](http://makeapullrequest.com)
[![Discord](https://img.shields.io/badge/Discord-Join%20Us-7289da?logo=discord&logoColor=white)](https://discord.gg/KSpWx78wuR)

A modern, component-based language for building reactive web apps.  
**Type-safe. Fast. WASM-powered.**

For developers who want the productivity of component-based frameworks with the performance of native code. Fine-grained reactivity, strict typing, and a GC-free runtime for deterministic performance.

</div>

> [!IMPORTANT]
> **Recent Breaking Changes:** Significant changes to DOM manipulation and imports have been implemented. See [CHANGES.md](CHANGES.md) for the migration guide.

> [!NOTE]
> Coi is actively evolving. Some syntax may change in future releases.

## What You Can Build

Coi is designed for building reactive, interactive web applications:

- **Web Apps**: Dashboards, admin panels, SPAs with real-time updates
- **Canvas Apps**: Drawing tools, image editors, animations, or games
- **Computation-Heavy Apps**: Simulations, data processing, physics engines, where WASM shines
- **Content Sites**: Blogs, documentation sites, landing pages with dynamic components

Coi gives you composable components, fine-grained reactivity, type safety, and tiny bundle sizes.

## Features

### Performance
- **Fine-Grained Reactivity**: State changes map directly to DOM elements at compile-time. No Virtual DOM overhead.
- **No Garbage Collector**: Deterministic memory management with zero internal GC pauses. Predictable performance for animations and real-time apps.
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
- **Editor Extensions**: Syntax highlighting and completions available for [VS Code, Sublime Text, and Zed](docs/tooling.md).

## Benchmarks


### DOM Performance (Rows App)

> [!NOTE]
> **Work in Progress:** Coi currently trails in DOM performance benchmarks. However, this gap is largely architectural overhead that I'm actively addressing - read below for details.

In [benchmarks](benchmark/) comparing Coi, React, Vue, and Svelte, Coi delivers the smallest bundle size. DOM performance is still being optimized, see the chart below for current results.

<p align="center">
  <img src="benchmark/benchmark_results.svg" alt="Benchmark Results" width="600">
</p>

**Why the Current Gap Exists:**

Coi's architecture uses an asynchronous event and command buffering system to maintain a tiny runtime and stable WASM-JS interop. While this design ensures minimal bundle sizes and batched operations, it introduces a systematic frame delay:

- **Buffer Latency**: Events are queued and processed in cycles, adding delay between user actions and DOM updates
- **Polling Overhead**: On 165Hz displays (6.06ms per frame), the once-per-frame polling introduces an average ~6ms overhead regardless of how fast the WASM logic executes
- **Node Allocation**: DOM node allocation and rendering optimizations are still in progress

**Accounting for Overhead**: If we subtract the ~6ms systematic buffer overhead from Coi's results, the adjusted performance becomes highly competitive:
- Create 1,000 rows: ~26ms (comparable to React/Svelte at 25-26ms)
- Update 1,000 rows: ~9ms (matching or beating Svelte at 11ms)

I'm actively working on moving towards a synchronous event-based model to eliminate this buffering delay while preserving the benefits of batched operations and minimal runtime size.

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
  - macOS: `brew install llvm lld`
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
| `coi version` | Show the Coi compiler version |
| `coi <file.coi> --out <dir>` | Compile a single file |

### Project Structure

```
my-app/
├── assets/
│   └── images/
├── src/
│   ├── App.coi          # Entry point (required)
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
├── dist/                # Build output
└── README.md
```

- **`src/App.coi`** — The compiler always looks for this as the entry point.
- **`assets/`** — Automatically copied to `dist/assets/` on build.
- **`styles/`** — CSS files here are bundled into `app.css`.

## Documentation

- [Getting Started](docs/getting-started.md) — Installation, first project, imports
- [Language Guide](docs/language-guide.md) — Types, enums, control flow, operators
- [Components](docs/components.md) — State, lifecycle, props, references
- [View Syntax](docs/view-syntax.md) — JSX-like templates, `<if>`, `<for>`, events
- [Styling](docs/styling.md) — Scoped and global CSS
- [Platform APIs](docs/api-reference.md) — Canvas, Storage, Audio, Fetch, and more
- [Editor Support](docs/tooling.md) — VS Code, Sublime Text, and Zed extensions

## Community

Join the Coi community on Discord to get help, share projects, and discuss the language:

**[Join Discord Server](https://discord.gg/KSpWx78wuR)** 💬

## Editor Support

Coi has syntax highlighting and language support for VS Code, Sublime Text, and Zed.

See the [Editor Support & Tooling](docs/tooling.md) documentation for installation instructions and features.

## License

MIT © [io-eric](https://github.com/io-eric)
