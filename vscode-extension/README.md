# COI Language

<p align="center">
  <img src="https://raw.githubusercontent.com/io-eric/coi/main/docs/images/logo.png" alt="COI Logo" width="128"/>
</p>

Language support for [COI](https://github.com/io-eric/coi) — a component-based language for high-performance web apps.

## Features

- **Syntax Highlighting** — Full grammar support for `.coi` files
- **Auto-Completions** — Platform APIs, types, methods, and components
- **Hover Information** — View method signatures and parameter types
- **Signature Help** — Parameter hints while typing function calls

## Examples

### Platform APIs

```coi
// Static methods
Canvas canvas = Canvas.createCanvas("app", 800.0, 600.0);
Storage.setItem("key", "value");
System.log("Hello!");

// Instance methods
CanvasContext2D ctx = canvas.getContext("2d");
ctx.setFillStyle(66, 133, 244);
ctx.fillRect(0.0, 0.0, 100.0, 100.0);
```

### Components

```coi
component Counter {
    mut int count = 0;

    view {
        <button onclick={() => count++}>
            "Count: " {count}
        </button>
    }
}
```
