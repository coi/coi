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

Coi uses two orthogonal modifiers:
- **`mut`**: Makes a variable mutable (can be changed from inside the component)
- **`pub`**: Makes a variable or method visible (can be accessed from outside the component)

**Variables:**

| Syntax | Inside Component | Outside Component | Description |
|--------|------------------|-------------------|-------------|
| `int x` | Read Only | Hidden | Private constant |
| `mut int x` | Read/Write | Hidden | Private mutable state |
| `pub int x` | Read Only | Read Only | Public constant |
| `pub mut int x` | Read/Write | Read Only | Public state (read-only outside) |

**Methods:**

| Syntax | Inside Component | Outside Component | Description |
|--------|------------------|-------------------|-------------|
| `def method() : void` | Callable | Hidden | Private method |
| `pub def method() : void` | Callable | Callable | Public method |

**Important**: You can never write to a component's member from outside. If you need to modify a child's state, use a setter method or reference parameters.

```tsx
component Counter(pub mut int count = 0) {
    mut int internal = 0;      // Private, mutable
    pub int version = 1;       // Public, constant
    
    def helper() : void { }    // Private method (not accessible from outside)
    pub def reset() : void {   // Public method (accessible from outside)
        count = 0;
    }
}

// Usage
component App {
    mut Counter counter;
    
    view {
        <div>
            <span>{counter.count}</span>     // ✅ Can read public state
            <span>{counter.version}</span>   // ✅ Can read public constant
            <button onclick={counter.reset}>Reset</button>  // ✅ Can call public method
            // ❌ counter.internal - Error: private member
            // ❌ counter.helper() - Error: private method
        </div>
    }
}
```

## Component Parameters

Components receive data through constructor-style parameters:

```tsx
component Editor(
    string label,           // Value parameter (copied by default, can be moved with :)
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

// Usage examples:
component App {
    mut string text = "Label";
    mut int count = 0;
    
    view {
        <div>
            // Default: copies text
            <Editor label={text} &value={count} &onChange={handleChange} />
            
            // Move: transfers ownership of text
            <Editor :label={text} &value={count} &onChange={handleChange} />
        </div>
    }
}
```

### Parameter Passing Modes

| Syntax | Mode | Usage in Call | Description |
|--------|------|---------------|-------------|
| `Type name` | Value | `comp(value)` or `comp(:value)` | Copied by default, can be moved with `:` |
| `mut Type& name` | Reference | `comp(&value)` | Component can read/modify parent's state |
| `def name(args) : ret` | Callback | `comp(&callback)` | Function passed from parent |

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

### Move Semantics (`:`)

Use `:` to transfer ownership of a value to the child component. The parent's variable becomes invalid after the move:

```tsx
<Editor :content={document} />
```

In code, use `:=` for move assignment:

```tsx
string source = "Hello";
string dest := source;  // Move assignment
// source is now invalid
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
                <{editor} />  // Uses existing member
            </if>
        </div>
    }
}
```

## Client-Side Routing

Coi has built-in client-side routing for single-page applications. Define routes with the `router {}` block and render the current route with `<route />`.

### Basic Routing

```tsx
import "pages/Home.coi";
import "pages/About.coi";
import "pages/Dashboard.coi";

component App {
    router {
        "/" => Home,
        "/about" => About,
        "/dashboard" => Dashboard
    }
    
    view {
        <div>
            <nav>
                <a href="/">Home</a>
                <a href="/about">About</a>
            </nav>
            <route />
        </div>
    }
}
```

### Passing Props to Routes

Pass callbacks or values to route components:

```tsx
component App {
    mut bool isLoggedIn = false;
    
    def handleLogin() : void {
        isLoggedIn = true;
    }
    
    def handleLogout() : void {
        isLoggedIn = false;
    }
    
    router {
        "/" => Landing(&handleLogin),
        "/dashboard" => Dashboard(&handleLogout)
    }
    
    view {
        <div>
            <NavBar />
            <route />
            <Footer />
        </div>
    }
}
```

### Programmatic Navigation

Use `System.navigate(path)` to navigate programmatically:

```tsx
component NavBar {
    def goToDashboard() : void {
        System.navigate("/dashboard");
    }
    
    def goToHome() : void {
        System.navigate("/");
    }
    
    view {
        <nav>
            <button onclick={goToHome}>Home</button>
            <button onclick={goToDashboard}>Dashboard</button>
        </nav>
    }
}
```

### How It Works

- Routes are defined in the `router {}` block with `"path" => Component` syntax
- The `<route />` element renders the component matching the current URL
- `System.navigate(path)` changes the URL and updates the view
- The router reads the initial URL on page load, so direct links work (e.g., `/dashboard`)
- Browser back/forward buttons work automatically via the History API

### Route Summary

| Feature | Syntax | Description |
|---------|--------|-------------|
| Define routes | `router { "/" => Home }` | Map paths to components |
| Render route | `<route />` | Placeholder for current route's component |
| Navigate | `System.navigate("/path")` | Programmatically change route |
| With props | `"/" => Page(callback)` | Pass props to route component |

## Next Steps

- [View Syntax](view-syntax.md) — JSX-like templates, conditionals, loops
- [Styling](styling.md) — Scoped and global CSS
- [Platform APIs](api-reference.md) — Canvas, Storage, Audio, and more
