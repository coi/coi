# Migration Guide: Breaking Changes

> [!CAUTION]
> This document outlines **breaking changes** as Coi transitions to a more declarative, reactive model. These changes are **now implemented**. Review this guide to update your existing code.

---

## ⚠️ Direct DOM Manipulation → Declarative View

### The Shift from Imperative to Declarative

Direct DOM manipulation via `DOMElement` methods (such as `.appendChild()`, `.setInnerHtml()`, or `.addClass()`) **have been removed** from the public API.

**Why this change?**

In a modern reactive framework, the **View is a function of State**. When you manually manipulate the DOM, you create "side effects" that the framework cannot track. This leads to:

* **UI Inconsistency:** The framework may overwrite your manual changes during the next render cycle, causing elements to disappear or reset unexpectedly.
* **Performance Degradation:** Manual updates bypass the optimized reconciliation engine, leading to unnecessary browser repaints and "layout thrashing."
* **Source of Truth Conflict:** If the code says `view { <div>{count}</div> }` but you manually injected a `<span>` via a script, the code no longer accurately describes the UI, making debugging significantly harder.

---

### The New Role of `DOMElement`

The `DOMElement` type is **not** being removed, but its purpose is being redefined. It is transitioning from a **Constructor/Manipulator** to a **Reference Handle**.

| Use Case | Status | Recommended Alternative |
| :--- | :--- | :--- |
| **Structure** (`appendChild`, `createElement`) | ❌ **Removed** | Define structure inside the `view {}` block using logic/loops. |
| **Styling** (`addClass`, `removeClass`) | ❌ **Removed** | Use reactive attribute bindings: `<div class={active ? 'on' : 'off'}>`. |
| **Content** (`setInnerText`) | ❌ **Removed** | Use variable interpolation: `<div>{myText}</div>`. |
| **HTML Content** (`setInnerHtml`) | ❌ **Removed** | Use `<raw>{htmlString}</raw>` in view. |
| **Browser APIs** (`requestFullscreen`, `focus`) | ✅ **Supported** | Use the `&={el}` binding to capture a reference and call these methods. |
| **Measurements** (`getBoundingClientRect`) | ✅ **Supported** | Use a reference to read physical properties of an element. |

> **Guideline:** Use the `view` to define **what** the element is. Use `DOMElement` references only to trigger **browser behaviors** that the view cannot describe.

**Example of Valid `DOMElement` Usage:**

```tsx
component VideoPlayer {
    mut DOMElement videoEl;

    fn enterFullscreen() {
        videoEl.requestFullscreen();  // ✅ Browser API
    }

    view {
        <div>
            <video &={videoEl} src="video.mp4"></video>
            <button @click={enterFullscreen}>Fullscreen</button>
        </div>
    }
}
```

### Migration Example

**❌ Old (Imperative):**

```tsx
component App {
    mut DOMElement container;

    mount {
        DOMElement div = DOMElement.createElement("div");
        div.setInnerHtml("<p>Dynamic content</p>");
        div.addClass("dynamic");
        container.appendChild(div);
    }

    view {
        <div &={container}></div>
    }
}
```

**✅ New (Declarative):**

```tsx
component App {
    mut bool showContent = true;
    mut string content = "Dynamic content";

    mount {
        showContent = true;
    }

    view {
        <div>
            <if showContent>
                <div class="dynamic">
                    <p>{content}</p>
                </div>
            </if>
        </div>
    }
}
```

---

## ⚠️ Canvas Initialization → View Binding

### `Canvas.createCanvas()` Has Been Removed

The `Canvas.createCanvas()` factory method **has been removed**. Canvas elements must be created declaratively in the `view` block and bound using the `&={canvas}` reference syntax.

**Why this change?**

This aligns canvas initialization with the declarative view model. The canvas element becomes part of your component's view definition rather than being imperatively created in lifecycle methods.

> **Lifecycle Note:** Component lifecycle runs in this order: `init {}` → `view {}` → `mount {}`. The `&={canvas}` binding populates the reference during view rendering, so it's safe to use in `mount {}`.

### Migration Example

**❌ Old (Factory Method):**

```tsx
component CanvasApp {
    mut Canvas canvas;
    mut CanvasContext2D ctx;
    
    mount {
        canvas = Canvas.createCanvas("myCanvas", 800, 600);
        ctx = canvas.getContext2d();
    }
    
    view {
        <canvas &={canvas}></canvas>
    }
}
```

**✅ New (View Binding):**

```tsx
component CanvasApp {
    mut Canvas canvas;
    mut CanvasContext2D ctx;
    
    mount {
        canvas.setSize(800, 600);
        ctx = canvas.getContext2d();
    }
    
    view {
        <canvas &={canvas}></canvas>
    }
}
```

**Key Changes:**
1. Remove `Canvas.createCanvas()` call
2. Use `<canvas &={canvas}></canvas>` in your view
3. Call `canvas.setSize()` in `mount {}` to configure dimensions
4. The canvas reference is automatically populated when the view renders

---

## ⚠️ Import System → Public-Only Exports

### Only `pub` Members Are Importable

The import system **now requires** the `pub` keyword for components, enums, and data types to be importable from other files.

**Why this change?**

This provides better encapsulation and makes module boundaries explicit. By default, everything is private to its module unless you explicitly export it. **This change enables better library support** by allowing library authors to control their public API surface and hide internal implementation details from consumers.

### Migration Example

**❌ Old (Implicit Export):**

```tsx
// Button.coi
component Button {  // Implicitly exported
    view { <button>Click</button> }
}
```

**✅ New (Explicit Export):**

```tsx
// Button.coi
pub component Button {  // Explicitly exported
    view { <button>Click</button> }
}

// Now importable from other files:
// import "./Button.coi";
```

**What Requires `pub`:**

| Type | Needs `pub`? | Example |
| :--- | :--- | :--- |
| Components | ✅ Yes | `pub component Button {}` |
| Data Types | ✅ Yes | `pub data User { string name; }` |
| Enums | ✅ Yes | `pub enum Status { ... }` |

---

## ⚠️ Module Scoping & Namespaces

### One Module Declaration Per File

Each file starts with a **single module declaration** on the first line. This defines the module scope for all components and types in that file.

```tsx
// Button.coi
module TurboUI;  // ← First line, defines module scope
pub component Button { ... }

// Dashboard.coi
module TurboUI;  // Same module
pub component Dashboard { ... }

// App.coi
module Main;  // Different module (Main is the default for app root)
pub component App { ... }
```

**Why this design?**

This provides a clear mental model inspired by C++ namespaces:
- **Same module:** Components share the same namespace (direct access)
- **Different module:** Explicit prefix required (prevents naming conflicts)

**This enables better library support** by allowing library authors to organize their code into logical modules while making it clear which components come from which library when used in application code.

### Access Rules

**You must always import the `.coi` file before using its components**, regardless of whether they're in the same module or not.

**Within the same module:** Components can be used directly by name (no prefix required).

```tsx
// Button.coi
module TurboUI;
pub component Button {
    view { <button>Click</button> }
}

// Dashboard.coi
module TurboUI;
import "Button.coi";  // Import the file

pub component Dashboard {
    view {
        <Button />  // ✅ Direct access (same module, no prefix needed)
    }
}
```

**Across modules:** Use the namespace prefix (`ModuleName::Component`).

```tsx
// Button.coi
module TurboUI;
pub component Button {
    view { <button>Click</button> }
}

// App.coi
module Main;
import "Button.coi";  // Import the file

pub component App {
    view {
        <TurboUI::Button />  // ✅ Module prefix required (different module)
    }
}
```

---

## Summary of Changes

| Feature | Before | After | Reason |
| :--- | :--- | :--- | :--- |
| DOM Structure | `DOMElement.createElement()` | `view { <div>...</div> }` | Declarative UI |
| DOM Styling | `element.addClass()` | `<div class={...}>` | Reactive bindings |
| Canvas Init | `Canvas.createCanvas()` | `<canvas &={canvas}>` | Consistent view model |
| Imports | All components importable | Only `pub` components | Explicit module boundaries |
| Module Access | Implicit/unclear scope | Same module: no prefix, Different module: `Module::Component` | Clear scoping rules |

---

## Need Help?

- **Documentation:** [Getting Started](docs/getting-started.md) | [Components](docs/components.md) | [API Reference](docs/api-reference.md)
- **Discord:** [Join our community](https://discord.gg/KSpWx78wuR)
- **Issues:** [Report bugs or suggest features](https://github.com/io-eric/coi/issues)
