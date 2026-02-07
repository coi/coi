# Migration Guide: Breaking Changes

> [!CAUTION]
> This document outlines **breaking changes** in Coi's transition to a more declarative, reactive model. These changes are **fully implemented**. Review this guide to update your existing code.

---

## ⚠️ Direct DOM Manipulation → Declarative View

### DOM Manipulation Methods Removed

Direct DOM manipulation via `DOMElement` methods (such as `.appendChild()`, `.setInnerHtml()`, or `.addClass()`) **have been removed** from the public API.

**Why this change?**

In a modern reactive framework, the **View is a function of State**. When you manually manipulate the DOM, you create "side effects" that the framework cannot track. This leads to:

* **UI Inconsistency:** The framework may overwrite your manual changes during the next render cycle, causing elements to disappear or reset unexpectedly.
* **Performance Degradation:** Manual updates bypass the optimized reconciliation engine, leading to unnecessary browser repaints and "layout thrashing."
* **Source of Truth Conflict:** If the code says `view { <div>{count}</div> }` but you manually injected a `<span>` via a script, the code no longer accurately describes the UI, making debugging significantly harder.

---

### The New Role of `DOMElement`

The `DOMElement` type has transitioned from a **Constructor/Manipulator** to a **Reference Handle**.

| Use Case | Status | Alternative |
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

    def enterFullscreen() : void {
        videoEl.requestFullscreen();  // ✅ Browser API
    }

    view {
        <div>
            <video &={videoEl} src="video.mp4"></video>
            <button onclick={enterFullscreen}>Fullscreen</button>
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

## ⚠️ Import System → Public-Only Exports

### Only `pub` Members Are Importable

The `pub` keyword is required to expose components, enums, and data types **outside of their module**.

*   **Same Module:** Everything defined within a module is automatically visible to other files sharing the same `module Name;` declaration. You do **not** need `pub` for internal sharing.
*   **Cross-Module:** You must add `pub` if you intend for that member to be imported and used by a **different module**.

Additionally, **transitive imports are not allowed**; you must directly import any file whose components you use.

**What `pub` does:**

- **Cross-Boundary Access:** Allows components, data types, and enums to be used by files outside of their own module.
- **Internal Visibility:** Without `pub`, a member is "module-internal", visible to its siblings in the same module, but hidden from the rest of the application.
- **API Control:** Allows library authors to hide internal helper components and logic, exposing only the intended interface to the end-user.
- **Namespace Safety:** Public members from different modules are accessed via the `Module::Member` syntax, preventing naming conflicts in large projects.

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

// Now importable from other modules:
// import "Button.coi";
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

Each file can have a **single module declaration** on the first line. This defines the module scope for all components and types in that file. If omitted, the file belongs to the default (unnamed) module.

```tsx
// Button.coi
module TurboUI;  // ← First line, defines module scope
pub component Button { ... }

// Dashboard.coi
module TurboUI;  // Same module
pub component Dashboard { ... }

// App.coi (no module declaration = default module)
component App { ... }
```

**Why this design?**

This provides a clear mental model inspired by C++ namespaces:
- **Same named module:** Components share the same namespace (direct access after import)
- **Different module:** Explicit prefix required (prevents naming conflicts)

**This enables better library support** by allowing library authors to organize their code into logical modules while making it clear which components come from which library when used in application code.

### Access Rules

**You must always import the `.coi` file before using its components**, regardless of whether they're in the same module or not. Transitive imports are not allowed.

**Within the same named module:** Components can be used directly by name (no prefix required).

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

// App.coi (default module)
import "Button.coi";  // Import the file

component App {
    view {
        <TurboUI::Button />  // ✅ Module prefix required (different module)
    }
}
```

## Need Help?

- **Documentation:** [Getting Started](docs/getting-started.md) | [Components](docs/components.md) | [API Reference](docs/api-reference.md)
- **Discord:** [Join our community](https://discord.gg/KSpWx78wuR)
- **Issues:** [Report bugs or suggest features](https://github.com/io-eric/coi/issues)
