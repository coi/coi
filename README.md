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

- **Fine-Grained Reactivity**: State changes map directly to DOM elements at compile-time. Update only what changed, exactly where it changed, without Virtual DOM overhead.
- **Type-Safe Components**: Compile-time error checking with strictly typed parameters and state.
- **Typed Callbacks**: Pass callbacks with type-checked parameters using `def onAction(int) : void` syntax.
- **Keyed Lists**: Efficient list rendering with `key={item.id}` for optimal DOM updates on additions, removals, and reorders.
- **Reference Parameters**: Pass state by reference with `&` for seamless parent-child synchronization.
- **Private by Default**: Component members are private by default; use `pub` to expose them.
- **Minimal Runtime**: Tiny WASM binaries with high-performance updates for DOM, Canvas, and more.
- **Integrated Styling**: Write standard HTML and scoped CSS directly in components.
- **Component Composition**: Build complex UIs from reusable components with typed parameters.
- **View Control Flow**: Declarative `<if>`, `<else>`, and `<for>` tags for conditional rendering and list iteration directly in the view.
- **Animation & Lifecycle**: Built-in `tick {}` block for frame-based animations, `init {}` for pre-render setup, and `mount {}` for post-render initialization when DOM elements are available.
- **Auto-Generated APIs**: Browser APIs (Canvas, Storage, Audio, etc.) are automatically generated from the [WebCC](https://github.com/io-eric/webcc) schema; new WebCC features instantly become available in Coi.
- **VS Code Extension**: Full language support with syntax highlighting, completions, hover docs, and signature help, also auto-generated from the schema.

## Example
****
```tsx
component Counter(string label, mut int& value) {
    // label: passed by value
    // value: reference to parent's state (mut allows modification)

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
    mut int score;
    mut string message;

    init {
        score = 0;
        message = "Keep going!";
    }

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
            <else>
                <p>{message}</p>
            </else>
            </if>
        </div>
    }
}

app { root = App; }
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

## Imports

Coi supports importing other `.coi` files to organize your code into multiple files:

```tsx
import "components/Button.coi";
import "layout/Header.coi";
import "../shared/Utils.coi";
```

Imports are relative to the current file's location. All components defined in imported files become available in the current file.

## Language Basics

Coi is a statically-typed language with familiar C-style syntax. Here's an overview of the core features:

### Types
```tsx
int count = 42;          // Integer
float speed = 3.14;      // Floating point
string name = "Coi";     // String
bool active = true;      // Boolean

// Arrays
int[] scores = [10, 20, 30]; 
string[] tags = ["web", "fast"];
mut float[] prices = []; // Mutable empty array

// Array Methods
int len = scores.size();
scores.push(40);         // Add element (requires mut)
scores.pop();            // Remove last element (requires mut)
scores.clear();          // Remove all elements (requires mut)
bool empty = scores.isEmpty();
```

### Control Flow

**Conditionals:**
```tsx
if (x > 10) {
    message = "big";
} else {
    message = "small";
}
```

**Loops:**

Coi supports range-based loops and iterator-based foreach loops.

```tsx
// Range-based (start:end)
for i in 0:10 {
    sum += i;
}

// ForEach (Arrays)
for score in scores {
    total += score;
}
```

### View Control Flow

Coi supports conditional rendering and loops directly in the view using `<if>` and `<for>` tags.

**Conditional Rendering:**
```tsx
view {
    <div>
        <if showContent>
            <p>Content is visible!</p>
        </if>
        
        <if status == "active">
            <span class="green">Active</span>
        <else>
            <span class="red">Inactive</span>
        </else>
        </if>
    </div>
}
```

**List Rendering with Range:**
```tsx
view {
    <div class="list">
        <for i in 0:itemCount>
            <div class="item">Item {i}</div>
        </for>
    </div>
}
```

**List Rendering with Array:**

Array loops require a `key` attribute for efficient updates. The key uniquely identifies each item so Coi can track additions, removals, and reorders without rebuilding the entire list.

```tsx
component TodoItem(pub int id, pub string text, def onRemove(int) : void) {
    view {
        <div class="item">
            <span>{text}</span>
            <button onclick={onRemove(id)}>×</button>
        </div>
    }
}

component TodoList {
    mut TodoItem[] todos;
    mut int nextId = 0;

    init {
        todos.push(TodoItem(nextId++, "Learn Coi"));
        todos.push(TodoItem(nextId++, "Build something cool"));
    }

    def addTodo() : void {
        todos.push(TodoItem(nextId++, "New task"));
    }

    def removeTodo(int id) : void {
        mut TodoItem[] newTodos;
        for todo in todos {
            if (todo.id != id) {
                newTodos.push(todo);
            }
        }
        todos = newTodos;
    }

    view {
        <div class="todo-list">
            <button onclick={addTodo}>+ Add</button>
            <for todo in todos key={todo.id}>
                <TodoItem
                    id={todo.id}
                    text={todo.text}
                    &onRemove={removeTodo(id)}
                />
            </for>
        </div>
    }
}
```

When the array changes:
- Items with the same key are reused (not recreated)
- New keys trigger item creation
- Removed keys trigger item destruction
- Reordering moves existing DOM nodes

**Nested Loops:**
```tsx
view {
    <div class="grid">
        <for row in 0:3>
            <for col in 0:3>
                <div class="cell">{row},{col}</div>
            </for>
        </for>
    </div>
}
```

### Operators
```tsx
// Arithmetic
int sum = a + b;
int diff = a - b;
int prod = a * b;
int quot = a / b;
int mod = a % b;

// Comparison
if (x == y) { }  // equal
if (x != y) { }  // not equal
if (x < y)  { }  // less than
if (x > y)  { }  // greater than
if (x <= y) { }  // less or equal
if (x >= y) { }  // greater or equal

// Assignment
count += 10;
count -= 5;
count++;
count--;
```

## State & References

Coi provides a powerful system for managing state and component communication:

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
**Why?**: This protects encapsulation and ensures controlled, predictable state changes, preventing bugs and side effects.

```tsx
component Counter(pub mut int count = 0) {
    // count: public (readable outside), mutable (changeable inside)
    
    mut int internal = 0;      // Private, mutable
    pub int version = 1;       // Public, constant
    
    def helper() : void { }    // Private method
    pub def reset() : void {   // Public method
        count = 0;
    }
}

component App {
    view {
        <div>
            // Can READ counter.count (it's pub)
            // Cannot WRITE counter.count = 5 (not allowed from outside)
            // Must use counter.reset() to modify
            <Counter />
        </div>
    }
}
```

### Component Parameters
Components receive data through constructor-style parameters. Parameters can be:
- **Value parameters**: Passed by value (copied)
- **Reference parameters** (`&`): Passed by reference for two-way binding
- **Function parameters** (`def`): Callbacks passed by reference

```tsx
component Editor(
    string label,           // Value parameter with no default (required)
    mut int& value,         // Reference parameter (two-way binding)
    int step = 1,           // Value parameter with default
    def onChange : void     // Function parameter
) {
    def increment() : void {
        value += step;      // Modifies parent's state
        onChange();         // Call parent's callback
    }
    
    view {
        <button onclick={increment}>{label}</button>
    }
}
```

### Reference Parameters (`&`)
Reference parameters allow child components to modify the parent's state directly. 

**Note**: Reference parameters cannot be `pub` — they point to the parent's data, and exposing them would break encapsulation.

1. **Declaration**: In the component signature, declare the parameter with `&`. Use `mut` if the component modifies it.
   ```tsx
   component StepButton(mut int& count) { ... }
   ```

2. **Passing**: When using the component, bind with `&`.
   ```tsx
   <StepButton &count={score} />
   ```

When the child modifies `count`, the parent's UI (the `score` display) will automatically update. Coi handles synchronization by generating efficient `onChange` callbacks.

### Function Parameters (`def`)
You can pass functions as parameters to components. Since functions are passed by reference, use the `&` operator when passing them.

**Simple callback (no parameters):**
```tsx
component CustomButton(
    string label,
    def onclick : void
) {
    view {
        <button onclick={onclick}>{label}</button>
    }
}

component App {
    def handleClick() : void {
        // Handle click logic here
    }

    view {
        <CustomButton label="Click Me" &onclick={handleClick} />
    }
}
```

**Typed callback with parameters:**

Callbacks can have typed parameters. The compiler validates that the handler signature matches the callback definition.

```tsx
component ListItem(
    pub int id,
    pub string text,
    def onRemove(int) : void    // Callback expects an int parameter
) {
    view {
        <div>
            <span>{text}</span>
            <button onclick={onRemove(id)}>Delete</button>
        </div>
    }
}

component App {
    // Handler must accept int to match onRemove(int)
    def handleRemove(int itemId) : void {
        // Remove item with itemId
    }

    view {
        <ListItem id={1} text="Item" &onRemove={handleRemove(id)} />
    }
}
```

## Logic-Only Components

Components don't require a `view` block. You can create logic-only components for state management, timers, network handlers, etc.:

```tsx
component Timer {
    pub mut int elapsed = 0;
    pub mut bool running = false;
    
    tick(float dt) {
        if (running) {
            elapsed++;
        }
    }
    
    pub def start() : void {
        running = true;
    }
    
    pub def stop() : void {
        running = false;
    }
    
    pub def reset() : void {
        elapsed = 0;
    }
    
    // No view block needed!
}

component App {
    mut Timer timer;
    
    def toggleTimer() : void {
        if (timer.running) {
            timer.stop();
        } else {
            timer.start();
        }
    }
    
    view {
        <div>
            <span>Elapsed: {timer.elapsed}</span>
            <button onclick={toggleTimer}>Toggle</button>
        </div>
    }
}
```

Logic-only components are useful for:
- **State management**: Encapsulate complex state logic
- **Timers & animations**: Use `tick` for frame-based updates
- **Network/API handlers**: Manage async operations
- **Game logic**: Separate game state from rendering

## Styling

Coi features a powerful styling system that combines the simplicity of CSS with component-level isolation.

### Scoped Styling
By default, styles defined within a `style { ... }` block are **scoped** to that component. Coi achieves this by automatically injecting a `coi-scope` attribute into your HTML elements and rewriting your CSS selectors to target only elements within that scope.

```tsx
component Card {
    style {
        // This only affects divs inside the Card component
        div {
            padding: 20px;
            border: 1px solid #eee;
        }
    }
    view {
        <div>I am a scoped card</div>
    }
}
```

### Global Styling
If you need to define global styles (e.g., for resets or base typography), you can use the `style global` block.

```tsx
component App {
    // Global styles (not scoped)
    style global {
        body {
            margin: 0;
            font-family: 'Inter', sans-serif;
        }
    }

    // Scoped styles (only affects this component)
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
    mut Canvas canvas;
    mut CanvasContext2D ctx;
    mut float x = 100.0;
    mut float y = 100.0;
    mut float dx = 3.0;
    mut float dy = 2.0;

    init {
        canvas.setSize(800, 600);
        ctx = canvas.getContext("2d");
    }

    tick(float dt) {
        // Clear and draw
        ctx.clearRect(0, 0, 800, 600);
        ctx.setFillStyle(66, 133, 244);
        ctx.beginPath();
        ctx.arc(x, y, 20, 0, 6.28318);
        ctx.fill();

        // Bounce logic
        x += dx;
        y += dy;
        if (x < 20) { dx = 3; }
        if (x > 780) { dx = -3; }
        if (y < 20) { dy = 2; }
        if (y > 580) { dy = -2; }
    }

    view {
        <canvas &={canvas}></canvas>
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
            <input value={input} />
            <button onclick={save}>Save</button>
            <p>Saved: {saved}</p>
        </div>
    }
}
```

### Image Example

```tsx
component Gallery {
    mut Canvas canvas;
    mut CanvasContext2D ctx;
    Image photo;

    init {
        canvas.setSize(400, 300);
        ctx = canvas.getContext("2d");
        photo = Image.load("photo.png");
    }

    def draw() : void {
        ctx.drawImage(photo, 0, 0);
        ctx.setFont("24px Arial");
        ctx.setFillStyleStr("#ffffff");
        ctx.fillText("My Photo", 20, 40);
    }

    view {
        <div>
            <canvas &={canvas}></canvas>
            <button onclick={draw}>Draw</button>
        </div>
    }
}
```

### Available APIs

| Module       | Description                                      |
|--------------|--------------------------------------------------|
| `Canvas`     | 2D drawing, paths, text, images, transformations |
| `Image`      | Image loading for canvas rendering               |
| `Audio`      | Audio playback, volume, looping                  |
| `Storage`    | Local storage (setItem, removeItem, clear)       |
| `System`     | Logging, page title, time, URL navigation        |
| `Input`      | Keyboard and mouse input, pointer lock           |
| `DOMElement` | Direct DOM manipulation                          |
| `WebGL`      | WebGL context and rendering                      |
| `WGPU`       | WebGPU support                                   |
| `Fetch`      | HTTP requests                                    |
| `WebSocket`  | WebSocket connections                            |

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
