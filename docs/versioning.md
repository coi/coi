# Versioning

Coi uses a **Rolling Ecosystem** version model instead of Semantic Versioning (SemVer).

## The Coi Versioning Philosophy: Pond & Drop

### 1) Pond (The Stability Era)

The **Pond** represents the contract version of the language.

- Compatibility: Code in the same pond shares the same core syntax and ABI rules.
- When it changes: Pond increments only for breaking changes.
- Reset: When a new pond starts, drop resets to `0`.

In short: **if pond changes, the contract changed**.

> [!NOTE]
> We are currently in `Filling Pond 0`. We still allow breaking drops while the language core stabilizes; once the core is stable, we move on to the next pond.

### 2) Drop (The Progress Counter)

The **Drop** is the progress counter within the current pond.

- Direction: Drops are strictly additive (`Drop 500` includes everything from `Drop 499` plus more).
- Scope: New features, fixes, and platform improvements are delivered as higher drops in the same pond.
- Scale: Drop can grow very large over time; it tracks real development mileage.

In short: **drop is velocity inside a stable contract**.

## Registry Compatibility Fields

Registry package releases use:

- `compiler.pond`: required pond contract for that release.
- `compiler.min-drop`: minimum drop required in that pond.

Compatibility rule:

1. `compiler.pond` must match the compiler pond.
2. compiler drop must be `>= compiler.min-drop`.

If pond differs, compatibility is rejected even if drop is high.

## Declaring a New Pond in the Compiler

Compiler version metadata is generated from the `gen_version` rule in [build.ninja](../build.ninja).

When starting a new pond, update these values in that rule:

- `pond_number`: integer pond contract version
- `pond_start_commit_count`: total git first-parent commit count where the current pond starts. This is **not** the current drop.

Pond names are currently mapped in CLI code by `pond_number`:

- Pond `N` displays as `Filling Pond N` (including `Filling Pond 0`)

Runtime drop is computed as:

- `drop = GIT_COMMIT_COUNT - pond_start_commit_count`

Example:

- Pond starts when total commit count is `350` → set `pond_start_commit_count = 350`
- Later total commit count is `356` → runtime drop is `6`
