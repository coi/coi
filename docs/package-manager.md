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
| `coi add <package> [version]` | Add latest compatible release or a specific version |
| `coi install` | Install all packages listed in `coi.lock` |
| `coi remove <package>` | Remove a package |
| `coi update [package]` | Update one package or all packages |
| `coi list` | List installed packages |

## Add a Package

```bash
coi add supabase
# or pin a specific release
coi add supabase 0.1.0
```

This command:
1. Resolves a release from the registry (latest release with compatible `compiler-drop.min`, unless a specific version is provided)
2. Downloads the package into `.coi/pkgs/<package>/`
3. Creates or updates `coi.lock`

Then import it in your Coi code:

```tsx
import "@supabase";
```

## Install from `coi.lock`

Use this after cloning a project or pulling dependency changes:

```bash
coi install
```

This reads `coi.lock` and installs exact versions into `.coi/pkgs/`.

## Update and Remove

Update one package:

```bash
coi update supabase
```

Update all packages:

```bash
coi update
```

Remove a package:

```bash
coi remove supabase
```

## Import Resolution

Package imports use the `@` prefix:

```tsx
import "@supabase";      // .coi/pkgs/supabase/Mod.coi
import "@ui-kit/Button"; // .coi/pkgs/ui-kit/Button.coi
```

## Create and Publish a Package

Use the package template to create a publishable package:

```bash
coi init my-pkg --pkg
```

Then:

1. Build your package code in `src/` and export public APIs from `Mod.coi`.
2. Fill out `registry-entry.json` (name, repository, description, keywords, release metadata).
3. Copy `registry-entry.json` into the registry repo as `packages/<your-package>.json`.
4. Open a PR to publish it.

For full registry rules and validation details, see [Registry docs](https://github.com/coi-lang/registry/blob/main/README.md).
