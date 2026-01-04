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
- **Reactive State Management**: Use `mut` for automatic, incremental DOM updates without Virtual DOM overhead.
- **Reference Props**: Pass state by reference with `&` for seamless parent-child synchronization.
- **Minimal Runtime**: Tiny WASM binaries with high-performance updates for DOM, Canvas, and more.
- **Integrated Styling**: Write standard HTML and scoped CSS directly in components.
- **Component Composition**: Build complex UIs from reusable components with typed props.
- **Animation Support**: Built-in `tick` lifecycle method for smooth animations and updates.

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

## Building

To build the compiler:

```bash
./build.sh
```

## Usage

To compile a `.coi` file:

```bash
coi App.coi --out ./dist
```

## VS Code Extension

A VS Code extension for syntax highlighting is included in the `vscode-extension/` directory. To use it:

1. Copy the `vscode-extension/` folder to your VS Code extensions directory (usually `~/.vscode/extensions/`).
2. Or, open the `vscode-extension/` folder in VS Code and press `F5` to run a development instance with the extension enabled.
