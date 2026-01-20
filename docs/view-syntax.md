# View Syntax

Coi uses a JSX-like syntax for defining component views. This guide covers HTML elements, expressions, conditional rendering, and loops.

## Basic Elements

```tsx
view {
    <div class="container">
        <h1>Hello World</h1>
        <p>Welcome to Coi</p>
    </div>
}
```

## Expressions

Use curly braces `{}` to embed expressions:

```tsx
component Greeting {
    string name = "World";
    int count = 42;
    
    view {
        <div>
            <h1>Hello, {name}!</h1>
            <p>Count: {count}</p>
            <p>Double: {count * 2}</p>
        </div>
    }
}
```

## Event Handlers

Bind methods to events with `on<event>`:

### Click Events

```tsx
component Button {
    mut int clicks = 0;
    
    def handleClick() : void {
        clicks += 1;
    }
    
    view {
        <button onclick={handleClick}>Clicked {clicks} times</button>
    }
}
```

### Input Events

For `oninput` and `onchange`, the handler receives the input's current value as a `string`:

```tsx
component SearchBox {
    mut string query = "";
    
    def handleInput(string value) : void {
        query = value;
    }
    
    view {
        <input 
            type="text" 
            value={query}
            oninput={handleInput}
        />
    }
}
```

### Keyboard Events

For `onkeydown`, the handler receives the key code as an `int`:

```tsx
component KeyboardInput {
    mut string lastKey = "";
    
    def handleKeyDown(int keycode) : void {
        if (keycode == 13) {
            lastKey = "Enter";
        } else if (keycode == 27) {
            lastKey = "Escape";
        }
    }
    
    view {
        <input onkeydown={handleKeyDown} />
    }
}
```

### Event Handler Summary

| Event | Handler Signature | Description |
|-------|-------------------|-------------|
| `onclick` | `def handler() : void` | Mouse click |
| `oninput` | `def handler(string value) : void` | Input value changed |
| `onchange` | `def handler(string value) : void` | Input lost focus after change |
| `onkeydown` | `def handler(int keycode) : void` | Key pressed |

## Element References

Bind DOM elements to variables with `&=`:

```tsx
component CanvasApp {
    mut Canvas canvas;
    
    mount {
        canvas.setSize(800, 600);
    }
    
    view {
        <canvas &={canvas}></canvas>
    }
}
```

## Conditional Rendering

### Basic `<if>`

```tsx
view {
    <div>
        <if showContent>
            <p>Content is visible!</p>
        </if>
    </div>
}
```

### `<if>` with `<else>`

```tsx
view {
    <div>
        <if status == "active">
            <span class="green">Active</span>
        <else>
            <span class="red">Inactive</span>
        </else>
        </if>
    </div>
}
```

### Nested Conditions

```tsx
view {
    <div>
        <if score >= 90>
            <span>A</span>
        <else>
            <if score >= 80>
                <span>B</span>
            <else>
                <span>C</span>
            </else>
            </if>
        </else>
        </if>
    </div>
}
```

## List Rendering

### Range-based Loop

```tsx
view {
    <div class="list">
        <for i in 0:itemCount>
            <div class="item">Item {i}</div>
        </for>
    </div>
}
```

### Array Loop with Key

Array loops require a `key` attribute for efficient updates:

```tsx
component TodoList {
    mut TodoItem[] todos;
    
    view {
        <div>
            <for todo in todos key={todo.id}>
                <{todo} />
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

### Nested Loops

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

## Child Components

Component names **must start with an uppercase letter** to distinguish them from HTML elements.

### Basic Usage

```tsx
component App {
    view {
        <div>
            <Header title="My App" />
            <Content />
            <Footer />
        </div>
    }
}
```

### Passing Props

```tsx
// Value props (copied)
<Button label="Click me" size={24} />

// Reference props (two-way binding with &)
<Counter &count={score} />

// Move props (transfer ownership with :)
<Consumer :data={text} />

// Callback props
<ListItem &onRemove={handleRemove} />
```

### Prop Passing Summary

| Syntax | Mode | Description |
|--------|------|-------------|
| `prop={value}` | Copy | Value is copied to child |
| `&prop={value}` | Reference | Child can modify parent's value |
| `:prop={value}` | Move | Ownership transferred to child |

### Component Member References

Components declared as members can be rendered using `<{member}/>` syntax:

```tsx
component App {
    mut Editor editor;
    
    view {
        <div>
            <{editor} />  // Renders the editor member
        </div>
    }
}
```

This is especially useful in loops where each item is a component:

```tsx
component TodoList {
    mut TodoItem[] todos;
    
    view {
        <for todo in todos key={todo.id}>
            <{todo} &onRemove={removeTodo(todo.id)} />
        </for>
    }
}
```

With `<{todo}/>`, props like `id`, `text`, and `done` are automatically bound from the component instance. You only need to pass additional props like callbacks.

## Dynamic Styles

Embed expressions in style attributes:

```tsx
component Ball {
    mut float x = 100;
    mut float y = 100;
    string color = "#4285f4";
    
    view {
        <div 
            class="ball"
            style="left: {x}px; top: {y}px; background: {color};"
        ></div>
    }
}
```

## Next Steps

- [Styling](styling.md) — Scoped and global CSS
- [Components](components.md) — Component lifecycle, state management
- [Platform APIs](api-reference.md) — Canvas, Storage, Audio, and more
