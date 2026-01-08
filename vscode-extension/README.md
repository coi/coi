# COI Language

<p align="center">
  <img src="https://raw.githubusercontent.com/io-eric/coi/main/docs/images/logo.png" alt="COI Logo" width="128"/>
</p>

Language support for [COI](https://github.com/io-eric/coi) — a component-based language for high-performance web apps that compiles to WASM.

## Features

- **Syntax Highlighting** — Full grammar support for `.coi` files including `view`, `style`, `init`, `mount`, and `tick` blocks
- **Auto-Completions** — Platform APIs, types, methods, components, and keywords
- **Hover Information** — View method signatures, parameter types, and documentation
- **Signature Help** — Parameter hints while typing function calls
- **Document Formatting** — Format COI files with proper indentation and spacing

## Language Features

### Component Lifecycle

COI components support a complete lifecycle:

```coi
component MyComponent {
    // Pre-render initialization
    init {
        // Set up state before view renders
    }

    // Component view
    view {
        <div>"Hello World"</div>
    }

    // Post-mount initialization (DOM available)
    mount {
        // Access DOM elements, start timers, etc.
    }

    // Animation/update loop
    tick(float dt) {
        // Called every frame with delta time
    }
}
```

### Typed Callbacks

Pass callbacks with type-checked parameters:

```coi
component TodoItem(pub int id, pub string text, def onRemove(int) : void) {
    view {
        <div>
            {text}
            <button onclick={onRemove(id)}>"×"</button>
        </div>
    }
}
```

### Keyed Lists

Efficient list rendering with stable keys:

```coi
view {
    <for item in items key={item.id}>
        <Item data={item} />
    </for>
}
```

### Reference Parameters

Two-way binding with parent state:

```coi
component Counter(mut int& value) {
    def increment() : void {
        value++;  // Modifies parent's state
    }
}
```

### Visibility Modifiers

Control member visibility with `pub` and mutability with `mut`:

```coi
component Example {
    int hidden = 0;           // Private, immutable
    mut int mutableHidden;    // Private, mutable
    pub int visible = 0;      // Public, read-only
    pub mut int state = 0;    // Public, mutable internally
}
```

## Examples

### Platform APIs

```coi
// Static methods
Canvas canvas = Canvas.createCanvas("app", 800.0, 600.0);
Storage.setItem("key", "value");
System.log("Hello!");

// Instance methods
CanvasContext2D ctx = canvas.getContext2d();
ctx.setFillStyle(66, 133, 244);
ctx.fillRect(0.0, 0.0, 100.0, 100.0);
```

### Complete Component

```coi
component Counter {
    pub mut int count = 0;

    def increment() : void {
        count++;
    }

    view {
        <button onclick={increment}>
            "Count: " {count}
        </button>
    }

    style {
        button {
            padding: 10px 20px;
            background: #4285f4;
            color: white;
            border: none;
            border-radius: 4px;
            cursor: pointer;
        }
    }
}

app { root = Counter; }
```

## Settings

| Setting | Default | Description |
|---------|---------|-------------|
| `coi.definitionsPath` | `""` | Path to COI definition files (.d.coi). Leave empty for bundled definitions. |
| `coi.format.enable` | `true` | Enable COI document formatting. |

## Formatting

Format your COI files using:
- **Keyboard shortcut**: `Shift+Alt+F` (Windows/Linux) or `Shift+Option+F` (Mac)
- **Command Palette**: `Format Document`
- **Right-click**: `Format Document`

The formatter handles:
- Consistent indentation
- Operator spacing
- Component and function declaration formatting
- View block element formatting
