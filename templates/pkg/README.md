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
├── registry-entry.json # Ready-to-submit registry metadata template
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

This template includes `registry-entry.json` — your package's registry file.

Before submitting:

1. Replace `YOUR_GITHUB_USERNAME` with your GitHub username or org in `repository`
2. Fill in `description` and `keywords`
3. Set `compiler-drop.min` (optimistic) and `compiler-drop.tested-on` (verified)
4. Keep your GitHub repo license as MIT (registry CI enforces this)

To submit:

1. Copy `registry-entry.json` to the registry repo as `packages/__PROJECT_NAME__.json`
2. (Optional) place it in a shard directory, e.g. `packages/ab/__PROJECT_NAME__.json`
3. Open a PR

When releasing new versions, add entries to the `releases` array in your package file.

## Learn More

- [Language Guide](https://github.com/io-eric/coi/blob/main/docs/language-guide.md)
- [Components](https://github.com/io-eric/coi/blob/main/docs/components.md)
- [Modules & Imports](https://github.com/io-eric/coi/blob/main/docs/language-guide.md#modules-and-imports)
