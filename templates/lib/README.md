# __PROJECT_NAME__

A Coi component library.

## Installation

Copy this library folder into your project and import it.

## Usage

```tsx
// Import the library (re-exports all public components)
import "__PROJECT_NAME__/Lib.coi";

component App {
    view {
        <__PROJECT_NAME__::Button label="Click me" />
    }
}
```

## Structure

```
__PROJECT_NAME__/
├── Lib.coi           # Library entry point (pub imports)
├── src/
│   ├── ui/           # UI components
│   │   └── Button.coi
│   └── api/          # API utilities
│       └── Client.coi
└── README.md
```

## Exports

**UI Components:**
- `Button` - A customizable button component

**API:**
- `Client` - HTTP client placeholder

## Development

To test components, create a test app that imports this library:

```bash
coi init test-app
cd test-app
# Copy your library into the project and import it
coi dev
```

## Learn More

- [Language Guide](https://github.com/io-eric/coi/blob/main/docs/language-guide.md)
- [Components](https://github.com/io-eric/coi/blob/main/docs/components.md)
- [Modules & Imports](https://github.com/io-eric/coi/blob/main/docs/language-guide.md#modules-and-imports)
