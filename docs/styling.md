# Styling

Coi features a powerful styling system that combines the simplicity of CSS with component-level isolation.

## External Stylesheets

Place CSS files in a `styles/` folder at the project root (next to `src/`):

```
my-app/
├── src/
│   └── App.coi
├── styles/
│   ├── reset.css
│   ├── theme.css
│   └── utilities.css
└── assets/
```

All `.css` files in `styles/` are automatically bundled into `app.css` (sorted alphabetically). The `styles/` folder itself is **not** copied to `dist/` — only the bundled output.

Projects created with `coi init` include a `styles/reset.css` with a modern CSS reset. You can modify or remove it as needed.

This is useful for:
- Third-party CSS libraries (normalize.css, etc.)
- Shared design tokens and CSS variables
- Team style guides and utility classes

## Scoped Styling

By default, styles defined within a `style { ... }` block are **scoped** to that component. Coi achieves this by automatically injecting a `coi-scope` attribute into your HTML elements and rewriting your CSS selectors.

```tsx
component Card {
    style {
        // This only affects divs inside the Card component
        div {
            padding: 20px;
            border: 1px solid #eee;
        }
        
        .title {
            font-size: 18px;
            font-weight: bold;
        }
    }
    
    view {
        <div>
            <h2 class="title">Card Title</h2>
            <p>Card content</p>
        </div>
    }
}
```

## Global Styling

Use `style global` for styles that should apply everywhere:

```tsx
component App {
    // Global styles (not scoped)
    style global {
        * {
            box-sizing: border-box;
        }
        
        body {
            margin: 0;
            font-family: 'Inter', sans-serif;
            background: #f5f5f5;
        }
        
        a {
            color: #1a73e8;
            text-decoration: none;
        }
    }

    // Scoped styles (only affects this component)
    style {
        .container {
            max-width: 1200px;
            margin: 0 auto;
            padding: 20px;
        }
    }
    
    view {
        <div class="container">
            <slot />
        </div>
    }
}
```

## Combining Scoped and Global

You can have both in the same component:

```tsx
component Button {
    // Global button reset
    style global {
        button {
            border: none;
            background: none;
            cursor: pointer;
        }
    }
    
    // Scoped button styles
    style {
        .btn {
            padding: 8px 16px;
            border-radius: 4px;
            font-weight: 500;
        }
        
        .btn-primary {
            background: #1a73e8;
            color: white;
        }
        
        .btn-secondary {
            background: #e8eaed;
            color: #202124;
        }
    }
    
    view {
        <button class="btn btn-primary">Click me</button>
    }
}
```

## Dynamic Styles

Embed expressions directly in style attributes:

```tsx
component ProgressBar {
    pub int progress = 0;
    string color = "#4285f4";
    
    view {
        <div class="progress-container">
            <div 
                class="progress-bar"
                style="width: {progress}%; background: {color};"
            ></div>
        </div>
    }
}
```

## CSS Features

Coi supports standard CSS features:

### Selectors

```tsx
style {
    // Element selectors
    div { }
    button { }
    
    // Class selectors
    .container { }
    .btn.primary { }
    
    // Descendant selectors
    .card .title { }
    
    // Child selectors
    .list > .item { }
    
    // Pseudo-classes
    button:hover { }
    .item:first-child { }
    .link:active { }
}
```

### Media Queries

```tsx
style {
    .container {
        padding: 20px;
    }
    
    @media (max-width: 768px) {
        .container {
            padding: 10px;
        }
    }
}
```

### CSS Variables

```tsx
style global {
    :root {
        --primary-color: #1a73e8;
        --spacing: 16px;
    }
}

style {
    .button {
        background: var(--primary-color);
        padding: var(--spacing);
    }
}
```

### Flexbox and Grid

```tsx
style {
    .flex-container {
        display: flex;
        gap: 12px;
        align-items: center;
        justify-content: space-between;
    }
    
    .grid-container {
        display: grid;
        grid-template-columns: repeat(3, 1fr);
        gap: 16px;
    }
}
```

## Best Practices

1. **Use scoped styles by default** — Prevents style leakage between components
2. **Reserve global styles for resets and base typography** — Keep them minimal
3. **Use semantic class names** — `.card-title` over `.big-blue-text`
4. **Leverage CSS variables for theming** — Define in global, use in scoped

## Next Steps

- [View Syntax](view-syntax.md) — JSX-like templates, conditionals, loops
- [Components](components.md) — Component lifecycle, state management
- [Platform APIs](api-reference.md) — Canvas, Storage, Audio, and more
