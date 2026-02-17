# __PROJECT_NAME__

A Coi component package.

## Installation

Copy this package folder into your project's `.coi/pkgs/` directory.

## Usage

```tsx
// Import the package (re-exports all public components)
import "@__PROJECT_NAME__";

component App {
    view {
        <__MODULE_NAME__::Button label="Click me" />
    }
}
```

## Structure

```
__PROJECT_NAME__/
├── Mod.coi           # Package entry point (pub imports)
├── package.json      # Ready-to-submit registry metadata template
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

To test components, create a test app that imports this package:

```bash
coi init test-app
cd test-app
mkdir -p .coi/pkgs
# Copy your package into .coi/pkgs/ and import with @package-name
coi dev
```

## Publish to Registry

This template includes `package.json` for publishing to the [Coi Registry](https://github.com/coi-lang/registry).

See the [Publishing Guide](https://github.com/io-eric/coi/blob/main/docs/package-manager.md#publishing-to-the-registry) for step-by-step instructions.

## Learn More

- [Language Guide](https://github.com/io-eric/coi/blob/main/docs/language-guide.md)
- [Components](https://github.com/io-eric/coi/blob/main/docs/components.md)
- [Modules & Imports](https://github.com/io-eric/coi/blob/main/docs/language-guide.md#modules-and-imports)
