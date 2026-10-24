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

Reference parameters allow child components to modify the parent's state directly. A change made through the reference updates every component that holds it, however many levels apart: a write at the top refreshes the children built with `&value` and their children, and a write at the bottom refreshes the owner and its other children.

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
│  5. unmount {}  - The component is being destroyed     │
└─────────────────────────────────────────────────────────┘
```

### `init {}` - Pre-render Setup

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

### `mount {}` - Post-render Initialization

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

### `tick(float dt) {}` - Animation Loop

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
        <div style="left: ${x}px;"></div>
    }
}
```

### `unmount {}` - Teardown

Runs as the component is destroyed: when the `<if>` around it turns false, when a loop drops its row, when the router moves to another page, or when its parent goes. It runs first, while the component's handlers, timers and sockets are still in place, so it can say goodbye over a socket or tell its parent through a callback.

```tsx
component Poller(def onGone : void) {
    mut Timeout again;
    mount { load(); }
    def load() : void { again = System.setTimeout(3000, &load); }
    unmount { onGone(); }
    view { <p>polling</p> }
}
```

Most components need no `unmount` at all: after it, the compiler removes every event handler the component registered, cancels its pending `Timeout`s, closes its `WebSocket`s and destroys its child components. A timer or socket kept in a field can never outlive its component.

### Frames on demand

An app without any `tick` does no work between events: the compiler skips the frame loop, and every event (click, pointer move, fetch result) schedules one frame to handle it. Idle, the app runs no code at all.

An app with a `tick` runs it every frame by default. A notes or drawing app that only animates now and then can opt into frames on demand instead:

```tsx
app {
    root = App;
    tick = demand;
}
```

Now `tick` runs once per frame the app asked for: after an event, and after `System.requestFrame()`. To animate, request the next frame from inside `tick` while the animation is running:

```tsx
component Fade {
    mut float alpha = 1.0;
    mut bool fading = false;

    def start() : void {
        fading = true;
        System.requestFrame();
    }

    tick(float dt) {
        if (!fading) return;
        alpha -= dt;
        if (alpha <= 0.0) { alpha = 0.0; fading = false; }
        else System.requestFrame();
    }

    view {
        <button onclick={start} style="opacity: ${alpha};">Fade</button>
    }
}
```

Several `System.requestFrame()` calls before a frame produce one frame. With `tick = always` (the default) the call does nothing, since a frame is always coming.

### Lifecycle Summary

| Block | When it runs | DOM available? | Use for |
|-------|--------------|----------------|---------|
| `init {}` | Before view | ❌ No | State setup, calculations |
| `view {}` | Creates DOM | Being created | Define HTML structure |
| `mount {}` | After view | ✅ Yes | Canvas setup, DOM measurements |
| `tick(dt) {}` | Every frame (or [on demand](#frames-on-demand)) | ✅ Yes | Animations, physics, updates |
| `unmount {}` | As the component is destroyed | ✅ Yes, still | A last word to a parent or a socket; timers and handlers are cleaned up for you |
| `listen {}` | Registers at mount, forgotten at destroy | ✅ Yes | Another component's signals, and the page's (`System.visibilityChange`, `Input.keyDown`, …; see the language guide) |

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

A parent can also read a child's `pub mut` state in its view, as `{timer.elapsed}` above, and through a chain of members (`{clock.timer.elapsed}` when `Clock` has a `pub mut Timer timer`): a change anywhere down the chain updates the binding. Calling one of the child's methods works the same way: `{timer.isOver(60)}` updates when a `pub mut` member that method reads changes, including through the child's other methods it calls. State the child keeps private doesn't announce its changes, so a method that depends only on that won't update the parent.

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
import "pages/NotFound.coi";

component App {
    router {
        "/" => Home;
        "/about" => About;
        "/dashboard" => Dashboard;
        else => NotFound;
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
        "/" => Landing(&handleLogin);
        "/dashboard" => Dashboard(&handleLogout);
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

### Dynamic Route Parameters

Capture part of the URL with a `:name` segment. The value is passed to the routed
component as a parameter **of the same name**, which the component reads like any
other prop:

```tsx
import "pages/User.coi";
import "pages/NotFound.coi";

component App {
    router {
        "/users/:id" => User;
        else => NotFound;
    }

    view {
        <div><route /></div>
    }
}
```

```tsx
// pages/User.coi
component User(int id) {
    init { loadUser(id); }

    view {
        <h1>User #{id}</h1>
    }
}
```

Visiting `/users/42` renders `User` with `id = 42`.

**The parameter's type is the constraint.** The router parses each segment to the
component parameter's declared type. Route parameters may be `string`, `int`, or
`bool`:

- `component User(int id)`: `/users/42` matches with `id = 42`; `/users/abc`
  does **not** match (it falls through to the next route, then `else`).
- `component User(string id)`: `/users/abc` matches with `id = "abc"`.

Because a segment that fails to parse simply doesn't match, a typed parameter
doubles as validation: the component never receives an invalid value.

**Mixing parameters and explicit arguments.** Path parameters fill the
component's parameters by name; any remaining parameters are filled by the
arguments passed in the router, in order:

```tsx
component User(int id, def onClose : void) { ... }

router {
    "/users/:id" => User(&handleClose);   // id from the URL, onClose passed explicitly
}
```

Multiple parameters are supported, e.g. `"/teams/:team/users/:id"` binds both
`team` and `id`.

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

### One component for many URLs

Usually every screen is its own route component, and the router mounts and unmounts them. Sometimes one component must stay mounted while the URL changes under it, because it holds what the screens share: an editor with its open document and undo history, an app with a storage connection and a sidebar. A trailing `/*` gives such a component every path under a prefix, and it picks the screen from the path itself:

```tsx
router {
    "/" => Landing;
    "/app/*" => App;     // /app, /app/note/42, /app/trash...
}
```

`App` is created once and stays while the URL moves around under `/app`: `System.navigate("/app/note/42")` from inside it changes the URL without remounting it, and back and forward reach it through `System.onPopstate`:

```tsx
component App {
    mount {
        System.onPopstate(&routed);   // back or forward landed on this path
        routed(System.getPathname()); // a deep link on load
    }
    def routed(string path) : void { ... pick the screen from the path ... }
    def openNote(string id) : void { ...; System.navigate(`/app/note/${id}`); }
}
```

A route without parameters keeps its component whenever the same route matches again; a route with `:params` is rebuilt for new values. Only `/app` is prerendered for a `/app/*` route; a host (and the generated service worker, and `coi dev`) serves `app/index.html` for any path under it. A host that answers a deep link with the root page instead still works: a prerendered page served for a path outside its route keeps its markup hidden until the app has taken over, so the visitor sees a blank page rather than the wrong one for a moment.

### How It Works

- Routes are defined in the `router {}` block with `"path" => Component;` syntax
- Dynamic segments (`"/users/:id"`) capture part of the URL into the component's same-named `string`/`int`/`bool` parameter
- Use `else => Component;` as a catch-all for unmatched routes (404 page)
- The `<route />` element renders the component matching the current URL
- `System.navigate(path)` changes the URL and updates the view
- The router reads the initial URL on page load, so direct links work (e.g., `/dashboard`)
- Browser back/forward buttons work automatically via the History API

### Route Summary

| Feature | Syntax | Description |
|---------|--------|-------------|
| Define routes | `router { "/" => Home; }` | Map paths to components |
| Default route | `else => NotFound;` | Catch-all for unmatched paths |
| Render route | `<route />` | Placeholder for current route's component |
| Navigate | `System.navigate("/path")` | Programmatically change route |
| With props | `"/" => Page(&callback);` | Pass props to route component |
| Dynamic param | `"/users/:id" => User;` | Capture a URL segment into `User`'s same-named param |
| Rest | `"/app/*" => App;` | `/app` and everything under it, one component that stays mounted; `System.onPopstate(&h)` tells it about back/forward |

## Next Steps

- [View Syntax](view-syntax.md) - JSX-like templates, conditionals, loops
- [Styling](styling.md) — Scoped and global CSS
- [Platform APIs](api-reference.md) — Canvas, Storage, Audio, and more
