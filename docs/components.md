# Components

Components are the building blocks of Coi applications. This guide covers component syntax, state management, lifecycle, and communication patterns.

## Basic Structure

Component names **must start with an uppercase letter**:

```tsx
component Counter {
    mut int count = 0;
    
    def increment() : void {
        count += 1;
    }
    
    style {
        button { padding: 8px 16px; }
    }
    
    view {
        <div>
            <span>{count}</span>
            <button onclick={increment}>+</button>
        </div>
    }
}
```

## State & Mutability

### Mutability (`mut`) and Visibility (`pub`)

Coi uses two orthogonal modifiers for variables:
- **`mut`**: Makes a variable mutable (can be changed from inside the component)
- **`pub`**: Makes a variable visible (can be read from outside the component)

| Syntax | Inside Component | Outside Component | Description |
|--------|------------------|-------------------|-------------|
| `int x` | Read Only | Hidden | Private constant |
| `mut int x` | Read/Write | Hidden | Private mutable state |
| `pub int x` | Read Only | Read Only | Public constant |
| `pub mut int x` | Read/Write | Read Only | Public state (read-only outside) |

**Important**: You can never write to a component's member from outside. If you need to modify a child's state, use a setter method or reference parameters.

```tsx
component Counter(pub mut int count = 0) {
    mut int internal = 0;      // Private, mutable
    pub int version = 1;       // Public, constant
    
    def helper() : void { }    // Private method
    pub def reset() : void {   // Public method
        count = 0;
    }
}
```

## Component Parameters

Components receive data through constructor-style parameters:

```tsx
component Editor(
    string label,           // Value parameter (required)
    mut int& value,         // Reference parameter (two-way binding)
    int step = 1,           // Value parameter with default
    def onChange : void     // Function parameter (callback)
) {
    def increment() : void {
        value += step;
        onChange();
    }
    
    view {
        <button onclick={increment}>{label}</button>
    }
}
```

### Reference Parameters (`&`)

Reference parameters allow child components to modify the parent's state directly:

```tsx
// Declaration
component StepButton(mut int& count) {
    def add() : void {
        count += 1;  // Modifies parent's state
    }
    
    view {
        <button onclick={add}>+</button>
    }
}

// Usage
component App {
    mut int score = 0;
    
    view {
        <div>
            <span>{score}</span>
            <StepButton &count={score} />
        </div>
    }
}
```

### Function Parameters (`def`)

Pass callbacks to child components:

```tsx
// Simple callback
component Button(string label, def onclick : void) {
    view {
        <button onclick={onclick}>{label}</button>
    }
}

// Typed callback with parameters
component ListItem(
    pub int id,
    def onRemove(int) : void
) {
    view {
        <button onclick={onRemove(id)}>Delete</button>
    }
}

// Usage
component App {
    def handleClick() : void {
        System.log("Clicked!");
    }
    
    def handleRemove(int itemId) : void {
        // Remove item with itemId
    }
    
    view {
        <div>
            <Button label="Click" &onclick={handleClick} />
            <ListItem id={1} &onRemove={handleRemove(id)} />
        </div>
    }
}
```

## Component Lifecycle

```
┌─────────────────────────────────────────────────────────┐
│  1. init {}     - State initialization (no DOM yet)    │
│  2. view {}     - DOM elements are created             │
│  3. mount {}    - DOM is ready, elements accessible    │
│  4. tick(dt) {} - Called every frame (if defined)      │
└─────────────────────────────────────────────────────────┘
```

### `init {}` — Pre-render Setup

Runs **before** the view is rendered. Use for state initialization:

```tsx
component App {
    mut int[] data;
    mut string status;
    
    init {
        data = [1, 2, 3, 4, 5];
        status = "Ready";
        // ❌ Cannot access DOM elements here
    }
}
```

### `mount {}` — Post-render Initialization

Runs **after** the view is rendered. Use when you need DOM access:

```tsx
component CanvasApp {
    mut Canvas canvas;
    mut CanvasContext2D ctx;
    
    mount {
        // ✅ DOM is ready
        canvas.setSize(800, 600);
        ctx = canvas.getContext2d();
    }
    
    view {
        <canvas &={canvas}></canvas>
    }
}
```

### `tick(float dt) {}` — Animation Loop

Called every frame. `dt` is delta time in seconds:

```tsx
component AnimatedBall {
    mut float x = 0;
    mut float speed = 100;
    
    tick(float dt) {
        x += speed * dt;  // Move 100 pixels per second
        if (x > 400) x = 0;
    }
    
    view {
        <div style="left: {x}px;"></div>
    }
}
```

### Lifecycle Summary

| Block | When it runs | DOM available? | Use for |
|-------|--------------|----------------|---------|
| `init {}` | Before view | ❌ No | State setup, calculations |
| `view {}` | Creates DOM | Being created | Define HTML structure |
| `mount {}` | After view | ✅ Yes | Canvas setup, DOM measurements |
| `tick(dt) {}` | Every frame | ✅ Yes | Animations, physics, updates |

## Logic-Only Components

Components don't require a `view` block:

```tsx
component Timer {
    pub mut int elapsed = 0;
    pub mut bool running = false;
    
    tick(float dt) {
        if (running) {
            elapsed++;
        }
    }
    
    pub def start() : void { running = true; }
    pub def stop() : void { running = false; }
    pub def reset() : void { elapsed = 0; }
}

component App {
    mut Timer timer;
    
    view {
        <div>
            <span>Elapsed: {timer.elapsed}</span>
            <button onclick={timer.start}>Start</button>
        </div>
    }
}
```

## Persisting Component State

Components inside `<if>` branches are destroyed when the condition changes. To persist state, declare as a member:

```tsx
component App {
    mut bool showEditor = false;
    mut Editor editor;  // Persists across toggles
    
    view {
        <div>
            <button onclick={toggle}>Toggle</button>
            <if showEditor>
                <editor />  // Uses existing member
            </if>
        </div>
    }
}
```

## Next Steps

- [View Syntax](view-syntax.md) — JSX-like templates, conditionals, loops
- [Styling](styling.md) — Scoped and global CSS
- [Platform APIs](api-reference.md) — Canvas, Storage, Audio, and more
