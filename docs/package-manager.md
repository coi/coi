# Package Manager

Coi includes a built-in package manager for installing community packages from the Coi Registry.

- Registry: https://github.com/coi-lang/registry
- Package source location in your project: `.coi/pkgs/`
- Lock file: `coi.lock`

> [!NOTE]
> Coi resolves package imports from `.coi/pkgs/`. Commit `coi.lock` so everyone on your team installs the same package versions.

## Quick Commands

| Command | Description |
|---------|-------------|
| `coi add <package> [version]` | Add latest compatible release or a specific version (`coi/` scope auto-applied) |
| `coi install` | Install all packages listed in `coi.lock` |
| `coi remove <package>` | Remove a package |
| `coi upgrade [package]` | Upgrade one package or all packages |
| `coi list` | List installed packages |

## Add a Package

```bash
coi add @coi/supabase
# or pin a specific release
coi add @coi/supabase 0.1.0
```

This command:
1. Resolves a release from the registry (latest release with compatible `compiler.pond` and `compiler.min-drop`, unless a specific version is provided)
2. Downloads the package into `.coi/pkgs/<package>/`
3. Creates or updates `coi.lock`

See [Versioning](versioning.md) for the Pond & Drop compatibility model.

Then import it in your Coi code:

```tsx
import "@coi/supabase";
```

## Install from `coi.lock`

Use this after cloning a project or pulling dependency changes:

```bash
coi install
```

This reads `coi.lock` and installs exact versions into `.coi/pkgs/`.

## Upgrade and Remove

Upgrade one package:

```bash
coi upgrade @coi/supabase
```

Upgrade all packages:

```bash
coi upgrade
```

Remove a package:

```bash
coi remove @coi/supabase
```

## Import Resolution

Package imports use the `@` prefix:

```tsx
import "@coi/supabase";       // .coi/pkgs/coi/supabase/Mod.coi
import "@acme/utils";              // .coi/pkgs/acme/utils/Mod.coi
import "@acme/utils/Button";       // .coi/pkgs/acme/utils/Button.coi
```

## Creating a Package

To create a reusable component package:

```bash
coi init my-pkg --pkg
```

This creates a package structure:

```
my-pkg/
├── Mod.coi              # Package entry point (pub imports)
├── package.json         # Registry metadata template
├── src/
│   ├── ui/
│   │   └── Button.coi   # UI components
│   └── api/
│       └── Client.coi   # API utilities
└── README.md
```

### Package Entry Point (`Mod.coi`)

`Mod.coi` is the entry point that consumers import. Use `pub import` to re-export your public components:

```tsx
// Mod.coi
pub import "src/ui/Button";
pub import "src/api/Client";
```

Consumers then import your package:

```tsx
import "@your-org/my-pkg";

component App {
    view {
        <MyPkg::Button label="Click" />
    }
}
```

See [Re-exporting with pub import](language-guide.md#re-exporting-with-pub-import) for details.

### Testing Your Package

To test components locally before publishing:

```bash
coi init test-app
cd test-app
mkdir -p .coi/pkgs
cp -r ../my-pkg .coi/pkgs/
coi dev
```

Then import with `@your-org/my-pkg` in your test app.

## Publishing to the Registry

The [Coi Registry](https://github.com/coi-lang/registry) is the community package index. Packages created with `--pkg` include a `package.json` template ready for submission.

### Prepare `package.json`

Fill in these fields:

| Field | Description |
|-------|-------------|
| `name` | Package ID in `scope/name` format (must match `packages/<scope>/<name>.json` when submitted) |
| `repository` | GitHub URL (e.g., `https://github.com/you/my-pkg`) |
| `description` | Short description of your package |
| `keywords` | Search keywords (e.g., `["coi", "ui", "components"]`) |

For each release in the `releases` array:

| Field | Description |
|-------|-------------|
| `version` | Semver (e.g., `0.1.0`, `1.2.3-beta`) |
| `compiler.pond` | Compiler contract version (must match current pond) |
| `compiler.min-drop` | Minimum compiler drop your package supports inside that pond |
| `source.commit` | Git commit SHA (40 hex chars) |
| `source.sha256` | SHA256 of the commit tarball |
| `releasedAt` | Release date (YYYY-MM-DD) |

> [!TIP]
> Registry CI can auto-populate `source.commit` and `source.sha256` if you leave the placeholder values. Just make sure your repo has the release commit pushed.

### Submit to Registry

1. Fork the [registry repo](https://github.com/coi-lang/registry)
2. Copy your `package.json` to `packages/<scope>/<name>.json`
3. Validate locally:
   ```bash
   python3 scripts/validate_registry.py --offline
   ```
4. Open a PR — CI validates and can fill in missing source fields

### Publishing New Versions

To release a new version, add a new entry to the `releases` array in your package file (newest first) and open a PR.

### Requirements

- Repository must be public on GitHub
- License must be MIT (registry CI enforces this)
- Each release must pin exact source code via `commit` + `sha256` for supply chain security
