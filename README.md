<div align="center">
  <img src="docs/images/logo.png" alt="Coi Logo" width="200"/>

# Coi

[![CI](https://github.com/io-eric/coi/actions/workflows/verifiy-and-publish.yml/badge.svg)](https://github.com/io-eric/coi/actions/workflows/verifiy-and-publish.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](http://makeapullrequest.com)

A component-based language for high-performance web apps.  
**Fast. Minimal. Type-safe.**

Compiles to WASM, JS, and HTML with tiny binaries and efficient updates for DOM, Canvas, and beyond.

</div>

> [!WARNING]
> This project is a work in progress. I don't recommend using it in production yet, as the language features and syntax are still evolving and may change.

## Features

- **Fine-Grained Reactivity**: State changes map directly to DOM elements at compile-time. No Virtual DOM overhead.
- **Type-Safe Components**: Compile-time error checking with strictly typed parameters and state.
- **Reference Parameters**: Pass state by reference with `&` for seamless parent-child synchronization.
- **Private by Default**: Component members are private; use `pub` to expose them.
- **Minimal Runtime**: Tiny WASM binaries with high-performance updates for DOM, Canvas, and more.
- **Integrated DOM & Styling**: Write HTML elements and scoped CSS directly in components.
- **View Control Flow**: Declarative `<if>`, `<else>`, and `<for>` tags for conditional rendering and iteration.
- **Component Lifecycle**: Built-in `init {}`, `mount {}`, and `tick {}` blocks for setup and animations.
- **Auto-Generated APIs**: Browser APIs (Canvas, Storage, Audio, etc.) generated from [WebCC](https://github.com/io-eric/webcc) schema.
- **VS Code Extension**: Syntax highlighting, completions, hover docs, and formatting.

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

```bash
git clone https://github.com/io-eric/coi.git
cd coi
./build.sh
```

### Compile

```bash
coi App.coi --out ./dist
cd dist && python3 -m http.server
```

Open `http://localhost:8000` in your browser.

## Documentation

- [Getting Started](docs/getting-started.md) — Installation, first project, imports
- [Language Guide](docs/language-guide.md) — Types, enums, control flow, operators
- [Components](docs/components.md) — State, lifecycle, props, references
- [View Syntax](docs/view-syntax.md) — JSX-like templates, `<if>`, `<for>`, events
- [Styling](docs/styling.md) — Scoped and global CSS
- [Platform APIs](docs/api-reference.md) — Canvas, Storage, Audio, Fetch, and more

## VS Code Extension

The COI Language extension provides syntax highlighting, auto-completions, hover docs, and signature help.

Install from the [VS Code Marketplace](https://marketplace.visualstudio.com/items?itemName=coi-lang.coi-language) or build manually:

```bash
cd vscode-extension
npm install && npm run package
```

## License

MIT © [io-eric](https://github.com/io-eric)
