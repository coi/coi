<div align="center">
  <img src="docs/images/logo.png" alt="Coi Logo" width="200"/>

# Coi

A component-based language for high-performance web apps. 
**Fast. Minimal. Type-safe.**

Compiles to WASM, JS, and HTML with tiny binaries and efficient updates for DOM, Canvas, and beyond.
</div>

> [!WARNING]
> This project is a work in progress. I don't recommend using it in production yet, as the language features and syntax are still evolving and may change.

## Features

- **Type-Safe Components**: Compile-time error checking with strictly typed props and state.
- **Incremental Updates**: Automatic, fine-grained DOM updates without Virtual DOM overhead.
- **Reference Props**: Pass state by reference with `&` for seamless parent-child synchronization.
- **Minimal Runtime**: Tiny WASM binaries with high-performance updates for DOM, Canvas, and more.
- **Integrated Styling**: Write standard HTML and scoped CSS directly in components.
- **Component Composition**: Build complex UIs from reusable components with typed props.
- **Animation Support**: Built-in `tick` lifecycle method for smooth animations and updates.
- **Lifecycle Hooks**: `init` block runs when a component mounts, perfect for setup logic.
- **Auto-Generated APIs**: Browser APIs (Canvas, Storage, Audio, etc.) are automatically generated from the [WebCC](https://github.com/io-eric/webcc) schema; new WebCC features instantly become available in Coi.
- **VS Code Extension**: Full language support with syntax highlighting, completions, hover docs, and signature help, also auto-generated from the schema.

## Example

```tsx
component StepButton {
    prop mut int& count;
    prop int amount;

    def update() : void {
        count += amount;
    }

    view {
        <button onclick={update}>
            "Add " {amount}
        </button>
    }
}

component App {
    mut int score = 0;

    style {
        .container {
            display: flex;
            flex-direction: column;
            align-items: center;
            gap: 20px;
            padding: 40px;
            background: #f0f2f5;
            border-radius: 16px;
        }
        .score {
            font-size: 64px;
            font-weight: 800;
            color: #1a73e8;
        }
        .controls {
            display: flex;
            gap: 12px;
        }
    }

    view {
        <div class="container">
            <div class="score">{score}</div>
            <div class="controls">
                <StepButton amount=1 &count={score} />
                <StepButton amount=5 &count={score} />
                <StepButton amount=10 &count={score} />
            </div>
        </div>
    }
}

app {
    root = App;
}
```

## Getting Started

### Building
Coi requires [WebCC](https://github.com/io-eric/webcc) to be installed. The build script will automatically initialize and build the WebCC submodule if it is not found on your system.

To build the compiler and the toolchain:
```bash
./build.sh
```

### Usage
To compile a `.coi` file:
```bash
coi App.coi --out ./dist
```

## State & References

Coi provides a powerful system for managing state and component communication:

### Mutability (`mut`)
Variables declared with `mut` are reactive. When a `mut` variable is modified (e.g., `count++`), Coi automatically triggers an update for any DOM elements or child components that depend on that variable.

```tsx
mut int count = 0; // Reactive state
int fixed = 10;    // Constant state (cannot be modified)
```

### Reference Props (`&`)
You can pass state to child components by reference using the `&` operator. This allows the child component to modify the parent's state directly.

1. **Declaration**: In the child component, declare the prop with `&`. Use `mut` if the component modifies the value.
   ```tsx
   prop mut int& count;
   ```

2. **Passing**: In the parent component, pass the variable with `&`.
   ```tsx
   <StepButton &count={score} />
   ```

When the child modifies `count`, the parent's UI (the `score` display) will automatically update to reflect the change. Coi handles the synchronization by generating efficient `onChange` callbacks.

### Function Props (`def`)
You can pass functions as props to components. Since functions are passed by reference, you must use the `&` operator when passing them. Function props are useful for triggering logic in the parent or for retrieving data.

```tsx
component CustomButton {
    prop string label;
    prop def onclick : void;

    view {
        <button onclick={onclick}>{label}</button>
    }
}

component App {
    def handleClick() : void {
        // Handle click
    }

    view {
        <CustomButton label="Click Me" &onclick={handleClick} />
    }
}
```

## Styling

Coi features a powerful styling system that combines the simplicity of CSS with component-level isolation.

### Scoped Styling
By default, styles defined within a `style { ... }` block are **scoped** to that component. Coi achieves this by automatically injecting a `coi-scope` attribute into your HTML elements and rewriting your CSS selectors to target only elements within that scope.

```tsx
component Card {
    style {
        /* This only affects divs inside the Card component */
        div {
            padding: 20px;
            border: 1px solid #eee;
        }
    }
    view {
        <div>"I am a scoped card"</div>
    }
}
```

### Global Styling
If you need to define global styles (e.g., for resets or base typography), you can use the `style global` block.

```tsx
component App {
    /* Global styles (not scoped) */
    style global {
        body {
            margin: 0;
            font-family: 'Inter', sans-serif;
        }
    }

    /* Scoped styles (only affects this component) */
    style {
        .container {
            max-width: 1200px;
        }
    }
}
```

## Platform APIs

Coi provides type-safe access to browser APIs through the [WebCC](https://github.com/io-eric/webcc) toolchain. These APIs are **automatically generated** from the WebCC schema. When new functionality is added to WebCC, Coi automatically gains new API endpoints and the VS Code extension is updated with completions and hover documentation.

### Type System

All platform types use a simple, consistent pattern:

- **`type`**: Defines a handle type (like `Canvas`, `Image`, `DOMElement`)
- **`shared def`**: Static/factory methods called on the type itself (e.g., `Canvas.createCanvas(...)`)
- **`def`**: Instance methods called on an instance (e.g., `canvas.getContext(...)`)

```tsx
// Definition (auto-generated from WebCC schema)
type Canvas {
    shared def createCanvas(id: string, width: float, height: float): Canvas
    def getContext(type: string): CanvasContext2D
    def setSize(width: float, height: float): void
}

type Storage {
    shared def setItem(key: string, value: string): void
    shared def removeItem(key: string): void
    shared def clear(): void
}
```

### Canvas Example

```tsx
component AnimatedBall {
    Canvas canvas;
    CanvasContext2D ctx;
    mut float x = 100.0;
    mut float y = 100.0;
    mut float dx = 3.0;
    mut float dy = 2.0;

    init {
        canvas = Canvas.createCanvas("game", 800.0, 600.0);
        ctx = canvas.getContext("2d");
    }

    tick(float dt) {
        // Clear and draw
        ctx.clearRect(0.0, 0.0, 800.0, 600.0);
        ctx.setFillStyle(66, 133, 244);
        ctx.beginPath();
        ctx.arc(x, y, 20.0, 0.0, 6.28318);
        ctx.fill();

        // Bounce logic
        x += dx;
        y += dy;
        if (x < 20.0) { dx = 3.0; }
        if (x > 780.0) { dx = -3.0; }
        if (y < 20.0) { dy = 2.0; }
        if (y > 580.0) { dy = -2.0; }
    }

    view {
        <div id="game" />
    }
}
```

### Storage Example

```tsx
component TodoApp {
    mut string input = "";
    mut string saved = "";

    def save() : void {
        Storage.setItem("todo", input);
        saved = input;
    }

    view {
        <div>
            <input value={input} oninput={/* update input */} />
            <button onclick={save}>"Save"</button>
            <p>"Saved: " {saved}</p>
        </div>
    }
}
```

### Image Example

```tsx
component Gallery {
    Canvas canvas;
    CanvasContext2D ctx;
    Image photo;

    init {
        canvas = Canvas.createCanvas("canvas", 400.0, 300.0);
        ctx = canvas.getContext("2d");
        photo = Image.load("photo.png");
    }

    def draw() : void {
        ctx.drawImage(photo, 0.0, 0.0);
        ctx.setFont("24px Arial");
        ctx.setFillStyleStr("#ffffff");
        ctx.fillText("My Photo", 20.0, 40.0);
    }

    view {
        <div>
            <div id="canvas" />
            <button onclick={draw}>"Draw"</button>
        </div>
    }
}
```

### Available APIs

| Module | Description |
|--------|-------------|
| `Canvas` | 2D drawing, paths, text, images, transformations |
| `Image` | Image loading for canvas rendering |
| `Audio` | Audio playback, volume, looping |
| `Storage` | Local storage (setItem, removeItem, clear) |
| `System` | Logging, page title, time, URL navigation |
| `Input` | Keyboard and mouse input, pointer lock |
| `DOMElement` | Direct DOM manipulation |
| `WebGL` | WebGL context and rendering |
| `WGPU` | WebGPU support |
| `Fetch` | HTTP requests |
| `WebSocket` | WebSocket connections |

## VS Code Extension

The COI Language extension provides a full-featured editing experience:

- **Syntax Highlighting**: Full grammar support for `.coi` files
- **Auto-Completions**: Types, methods, and component props
- **Hover Information**: See method signatures and parameter types
- **Signature Help**: Parameter hints while typing function calls

The extension's API definitions are **auto-generated** alongside the compiler. When WebCC adds new browser APIs, the extension automatically gains completions and documentation.

### Install

Install from the VS Code Marketplace: [COI Language](https://marketplace.visualstudio.com/items?itemName=coi-lang.coi-language)

Or install manually:
```bash
cd vscode-extension
npm install
npm run package
# Install the generated .vsix file in VS Code
```
