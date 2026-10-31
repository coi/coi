# Getting Started

This guide will help you set up Coi and create your first component.

## Installation

### Prerequisites

Coi requires [WebCC](https://github.com/io-eric/webcc) to be installed. The build script will automatically initialize and build the WebCC submodule if it is not found on your system.

### Building from Source

To build the compiler and the toolchain, clone the repository and run the build script:

```bash
git clone https://github.com/coi/coi.git
cd coi
./build.sh
```

This will:
1. Initialize the WebCC submodule (if needed)
2. Build the WebCC toolchain
3. Generate type definition files (defs/web/*.d.coi) from WebCC schema
4. Build the Coi compiler

### Build Options

The build script supports these options:

```bash
./build.sh --rebuild-schema   # Force regenerate defs/web/*.d.coi from WebCC schema
./build.sh --rebuild-webcc    # Force rebuild the WebCC toolchain
./build.sh --help             # Show all available options
```

### Type Definition System

Coi uses `.d.coi` definition files for type information:

- **`defs/core/`** — Built-in types (int, string, bool, array, etc.) - source-controlled
- **`defs/web/`** — Web platform APIs auto-generated from WebCC schema - gitignored

The build system automatically detects when `deps/webcc/schema.def` changes:

1. Running `./build.sh` checks if WebCC's schema.def was modified
2. If changed, WebCC rebuilds automatically
3. Coi regenerates `defs/web/*.d.coi` to match the new schema

This means you can simply run `./build.sh` after editing `schema.def` and everything cascades correctly.

Use `--rebuild-schema` to force regeneration even when no changes are detected.

## CLI Commands

The Coi CLI provides commands for creating, building, and running projects.

### `coi init`

Create a new Coi project from template:

```bash
coi init my-app
```

This creates a new directory with a complete project structure:

```
my-app/
├── assets/
│   └── images/
├── src/
│   ├── App.coi
│   ├── layout/
│   │   ├── Footer.coi
│   │   └── NavBar.coi
│   ├── pages/
│   │   ├── About.coi
│   │   └── Home.coi
│   └── ui/
│       └── Button.coi
├── styles/
│   └── reset.css
└── README.md
```

#### Creating a Package

To create a reusable component package instead of an app:

```bash
coi init my-pkg --pkg
```

This creates a package structure with `Mod.coi` as the entry point and a `package.json` template for publishing.

See [Package Manager](package-manager.md#creating-a-package) for the full workflow on creating and publishing packages.

If no name is provided, you'll be prompted to enter one:

```bash
coi init
# Project name: my-app
```

Project names must start with a letter or underscore and contain only letters, numbers, hyphens, and underscores.

### `coi build`

Build the project in the current directory:

```bash
cd my-app
coi build
```

This compiles `src/App.coi` and outputs to `dist/`:
- `dist/index.html` — Entry HTML file
- `dist/app.js` — JavaScript runtime
- `dist/app.wasm` — WebAssembly binary
- `dist/app.css` — Generated CSS bundle

Assets from the `assets/` folder are automatically copied to `dist/assets/`.

Files in `public/` are copied to the root of `dist/` as they are: a page written by hand
(`public/checkout/index.html` is served at `/checkout`), `robots.txt`, a file a host needs. The
service worker caches them like any other built file, so a page there opens offline too. A file
with the name of one the build writes (`index.html`, `app.js`) is replaced by the build's.

### `coi dev`

Build and start a local development server with hot reloading:

```bash
coi dev
```

This builds the project and starts a server at `http://localhost:8000` (or the next free port; `COI_PORT=8001 coi dev` starts from another one, handy when two projects run side by side). The server automatically watches for changes to:
- `.coi` files in `src/`
- Files in `assets/` (images, fonts, etc.) and `public/`
- `.css` files in `styles/`

When you save any watched file, the project rebuilds automatically and your browser refreshes with the latest changes.

#### Disable Hot Reloading

If you need to disable hot reloading (for debugging build issues or testing manual workflows):

```bash
coi dev --no-watch
```

This builds the project once and starts the dev server without file watching. To see changes, stop the server and run `coi dev --no-watch` again.

Press `Ctrl+C` to stop the dev server.

### Direct Compilation

Compile a single `.coi` file directly:

```bash
coi App.coi --out ./dist
```

#### Options

| Option | Description |
|--------|-------------|
| `--out, -o <dir>` | Output directory |
| `--cc-only` | Generate C++ only, skip WASM compilation |
| `--keep-cc` | Keep generated C++ files for debugging |

To keep the intermediate C++ file:

```bash
coi App.coi --out ./dist --keep-cc
```

This also generates `dist/App.cc` so you can inspect the generated C++ code.

### Package Management

Coi has a built-in package manager for adding community packages:

```bash
coi add @coi/pdf       # Add a package
coi install                # Install from coi.lock
```

Then import it:

```tsx
import "@coi/pdf";
```

See [Package Manager](package-manager.md) for the full workflow.

## Your First Component

Create a file called `App.coi`:

```tsx
component App {
    mut int count = 0;

    def increment() : void {
        count += 1;
    }

    style {
        .container {
            padding: 20px;
            font-family: system-ui;
        }
        button {
            padding: 8px 16px;
            cursor: pointer;
        }
    }

    view {
        <div class="container">
            <h1>Count: {count}</h1>
            <button onclick={increment}>+1</button>
        </div>
    }
}

app { root = App; }
```

### App Configuration

The `app {}` block configures your application. Here are all available properties:

```tsx
app {
    root = App;                                    // Required: Root component
    title = "My App";                              // Page title (<title> tag)
    description = "A description for SEO";         // Meta description
    lang = "en";                                   // HTML lang attribute (default: "en")
    base = "/";                                     // Deploy base path (default: "/")
    tick = always;                                 // Frame loop: always (default) or demand
    pwa = true;                                    // Installable, works offline
    icon = "assets/icon.png";                      // Favicon and app icon
    touchIcon = "assets/touch-icon.png";           // iPhone and iPad home screen icon
    theme = "#0b3d2e";                             // Browser UI color
    titlebar = overlay;                            // Installed app draws its own title bar
    prerender = true;                              // Every route as HTML at build time
    head = "src/head.html";                        // Extra lines for every page's <head>
}
```

| Property | Type | Required | Description |
|----------|------|----------|-------------|
| `root` | Component | Yes | The root component to render |
| `title` | String | No | Sets the page `<title>` tag |
| `description` | String | No | Sets `<meta name="description">` for SEO |
| `lang` | String | No | Sets the `<html lang="">` attribute (default: `"en"`) |
| `base` | String | No | Deploy base path, emitted as `<base href="">` (default: `"/"`) |
| `pwa` | `true` / `false` | No | Generates a manifest and a service worker so the app can be installed and starts offline, see [Installable and offline](#installable-and-offline) |
| `start` | `"/app"` | No | Where the installed app opens (the manifest's `start_url`): a site with a start page in front of an app opens on the app. The `base` by default |
| `icon` | String | No | Path in the build output, e.g. `"assets/icon.png"`. Used as favicon and app icon |
| `touchIcon` | String | No | The icon on an iPhone or iPad home screen: a square 180×180 PNG without transparency (iOS rounds the corners itself). `icon` by default; iOS can't use an SVG and shows a screenshot of the page instead |
| `theme` | String | No | CSS color for the browser's UI (address bar, title bar of the installed app). `System.setThemeColor` changes it at runtime, e.g. for a dark theme |
| `titlebar` | `system` / `overlay` | No | `overlay` lets the installed app draw into its title bar, see [Your own title bar](#your-own-title-bar) |
| `prerender` | `true` / `false` / `["/app/note"]` | No | Renders every static route to HTML at build time, plus the listed paths, see [Prerendering](#prerendering) |
| `head` | String | No | A file (path from the project root) whose lines go into every page's `<head>`: analytics, a font, a small script |
| `tick` | `always` / `demand` | No | `demand` runs `tick` only on requested frames, see [Frames on demand](components.md#frames-on-demand) |

### Prerendering

`prerender = true;` renders every page to HTML at build time, so it shows at once and search engines and link previews can read it. When the app has loaded, it hydrates the page.

- Every static route of the root's `router` is rendered (`"/"` without one): `"/"` into `index.html`, `"/about"` into `about/index.html`. Routes with parameters aren't.
- Browser-only calls (storage, fetch, canvas) return nothing at build time; the app fills those parts in when it hydrates. `System.isPrerender()` is `true` then, and `System.navigate` does nothing: a page is the page at its path, so a mount that sends a signed-out visitor to `/login` renders its own screen at build and redirects in the browser.
- `coi dev` doesn't prerender.
- A `"/app/*"` route gets one page, `app/index.html`, which a host serves for every path under it (the generated service worker and `coi dev` do). `prerender = ["/app/note"];` renders more pages under such a route: the app runs with that pathname, so it can render that screen's frame (an empty note) for `/app/note/<id>` links. A page served for a path outside its own route hides its markup until the app has taken over.

A site with a start page in front of an app is two routes:

```tsx
component Site {
    router {
        "/" => Landing;
        "/app" => App;
    }
    view { <div><route /></div> }
}

app { root = Site; prerender = true; }
```

**Note:** If you have a `styles/` folder at the project root (next to `src/`), all `.css` files in it are automatically bundled into `app.css`.

**Deploying under a subpath:** Assets (`app.js`, `app.css`) and client-side routes resolve against the `base` path. When your site is served from a subpath rather than the domain root, for example a GitHub project page at `https://user.github.io/repo/`, set `base` to that subpath so assets load correctly:

```tsx
app {
    root = App;
    base = "/repo/";   // matches https://user.github.io/repo/
}
```

Leave `base` as `"/"` (the default) for root deploys. `coi dev` serves the app under its `base` too, so `http://localhost:8000/` leads to `http://localhost:8000/repo/`.

### Installable and offline

With `pwa = true`, `coi build` also writes `manifest.webmanifest` and `sw.js` next to `index.html`:

- **Installable.** Browsers offer to install the app (Chrome's install button, "Add to Home Screen" on phones). It then opens in its own window with `title` as its name and `icon` as its icon. Use a square PNG of at least 512×512 in `assets/`; SVG works too, but then give `touchIcon` a PNG for iPhones and iPads. Safari has no install prompt: people install from Share, then Add to Home Screen (iPhone, iPad), or File, then Add to Dock (Mac). `System.installState` says which (3 or 4), so the app can show those steps where its install button would be.
- **Offline.** The service worker caches every file of the build and serves them from the cache first, so the app starts without a connection, on any route. Requests to your API are not cached: they go to the network as usual.
- **On demand.** Files in a folder named `lazy` (`assets/lazy/…`, at any depth) are left out of that first download: big files only some visits need, like fonts for an export. The service worker keeps each one the first time the app fetches it, so it works offline after one use. They count toward the build's version, so a change to one alone is an update too; a new build starts a new cache, and they are fetched again when next used.
- **Updates.** Each build gets a new version. An open app checks for it when it loads, downloads it in the background, and the next start runs the new build.
- **Development.** `coi dev` never uses the service worker, and removes one left over from a `coi build` on the same address, so you always see your latest edits.

Service workers need HTTPS (or `localhost`). Keep data the user creates in IndexedDB, not in the build: the cache is replaced on every update.

### More than one language

Put the app's texts in `lang/en.json`, next to `src/`, one object of them; groups are only for reading (`"settings": {"title": "Settings"}` is the key `settings.title`):

```json
{
  "settings": { "title": "Settings" },
  "toast": { "moved": "Moved {0} to the trash" },
  "notebooks": { "one": "# notebook", "other": "# notebooks" }
}
```

```coi
<h2>{Lang.t("settings.title")}</h2>
<small>{Lang.t("notebooks", count)}</small>
toast = Lang.t("toast.moved", [title]);
```

- **Checked.** A key `lang/en.json` doesn't have is a compile error, so a typo never reaches the page.
- **Arguments and plurals.** `{0}`, `{1}`, ... take `args` in order. A key with `one` and `other` under it is a plural: it takes the count after the key, `Lang.t("notebooks", n)`, and the form for `n` is picked by the language's own rule (`Intl.PluralRules`): `one` and `other` for English, and `zero`, `two`, `few`, `many` where a language has them; `#` is `n`. A plural without a count, or a count for a text that isn't one, doesn't build.
- **Links and bold words inside a sentence.** A sentence with an element in the middle stays one text, its elements marked as tags, so each language can put them where its word order needs them. `<name>words</name>` is a tag with words in it, `<name/>` one without:

  ```json
  "agree": "By continuing you agree to our <terms>Terms</terms> and <privacy>Privacy Policy</privacy>.",
  "signedIn": "Signed in as <b>{0}</b>"
  ```

  ```coi
  <p>{Lang.t("agree", terms = <a href="/terms" />, privacy = <a href="/privacy" />)}</p>
  <p>{Lang.t("signedIn", [name])}</p>
  ```

  The view says what each tag is, by name, after the args; a tag named like an HTML element (`<b>`, `<em>`, `<code>`) is that element unless the view gives another. The tag's words go first in its element, before anything the element holds itself, and `{0}`, `{1}`, ... fill them too. What reaches the page is the text and the elements, as if written out by hand: no HTML is parsed at run time, so an argument is always text. Every language has to have the tags `en.json` gives the text, in any order; one that drops or renames a tag, or a view that doesn't say what a tag is, doesn't build. A text is cut at its tags only where a view shows it as all of its `{...}`; in code, in an attribute or inside a bigger expression it reads as written, tags and all, so a text that is HTML to show with `<raw>` is left alone. A plural can't have tags.
- **English is built in.** It works offline and in prerendered pages, and needs no download.
- **Other languages** are files beside it, named by their code: `lang/de.json`, `lang/pt-BR.json`, with the same keys. The build writes each to `lazy/lang/`, every text in `en.json`'s order; a key it leaves out stays English, and one `en.json` doesn't have is pointed out. Before the app mounts, the visitor's language file is fetched: the choice `Lang.use` kept, else the first of the browser's languages there is a file for (`de-AT` takes `de`). It's kept offline after the first time, like any `lazy` file, and the app never shows English first.
- **Choosing.** `Lang.available()` is `["en", "de", ...]`, `Lang.code()` the one in use, `Lang.use("de")` keeps the choice and starts the app again in it, `Lang.use("")` forgets it (the browser's again), and `Lang.chosen()` is the choice kept, `""` when there is none: what a language picker shows.

Prerendered pages are English for now.

### Your own title bar

With `titlebar = overlay`, an installed app on desktop Chrome or Edge has no separate title bar: the page starts at the top of the window and the window buttons float over its top corner, like most desktop apps. The user can switch back to the normal bar from the window menu; other browsers and phones keep it.

The page sees the free part of the bar through CSS `env()` values (all unset without the overlay, so the fallbacks apply):

```css
:root {
    --bar-left: env(titlebar-area-x, 0px);                                                   /* macOS: buttons on the left */
    --bar-right: calc(100vw - env(titlebar-area-x, 0px) - env(titlebar-area-width, 100vw));  /* Windows, Linux: on the right */
    --bar-height: env(titlebar-area-height, 0px);
}
.header { padding-right: var(--bar-right); app-region: drag; }    /* drag the window by it */
.header button { app-region: no-drag; }                            /* buttons in it stay clickable */
```

The buttons sit on `theme`; keep it the color of your header, and call `System.setThemeColor` when that changes.

For client-side routing, use the `router {}` block inside your root component. See [Components](components.md#client-side-routing) for details.

Compile and run:

```bash
coi App.coi --out ./dist
cd dist && python3 -m http.server
```

## Project Structure

For larger projects, organize your code into multiple files:

```
my-app/
├── lang/           # en.json and the other languages (optional)
├── src/
│   ├── App.coi
│   ├── components/
│   │   ├── Button.coi
│   │   └── Card.coi
│   └── layout/
│       ├── Header.coi
│       └── Footer.coi
├── dist/           # Generated output
└── build.sh
```

## Imports

Coi uses a strict, explicit import system.

```tsx
// Local imports (relative to current file)
import "components/Button.coi";
import "layout/Header.coi";

// Package imports (from .coi/pkgs/)
import "@coi/pdf";            // resolves to .coi/pkgs/coi/pdf/Mod.coi
import "@acme/utils/Button.coi";       // resolves to .coi/pkgs/acme/utils/Button.coi
```

### Import Rules

1. **Relative Paths**: Local imports are relative to the current file.
2. **Package Imports**: Paths starting with `@` resolve to `.coi/pkgs/`. Use scoped names like `@scope/name`.
3. **Explicit Only**: There are no "transitive imports". If `A` imports `B`, and `B` imports `C`, `A` cannot use `C` unless it imports `C` directly.
4. **Visibility**: You can only use components that are marked with `pub` if they are in a different module.

### Modules

You can organize files into named modules using the `module` keyword at the top of the file. Module names must start with an uppercase letter:

```tsx
// src/ui/Button.coi
module TurboUI;
pub component Button { ... }
```

- **Same Module:** Can access `Button` directly after import.
- **Different Module:** Must use fully qualified name `<TurboUI::Button />`.

## Getting Help

Getting stuck or need help? Join the [Coi Discord community](https://discord.gg/KSpWx78wuR) for fast support and discussions, or [open an issue](https://github.com/coi/coi/issues) on GitHub for bugs and feature requests.

## Next Steps

- [Language Guide](language-guide.md) — Types, control flow, operators
- [Components](components.md) — Component syntax, lifecycle, props
- [Styling](styling.md) — Scoped and global CSS
- [View Syntax](view-syntax.md) — JSX-like templates, conditionals, loops
- [Platform APIs](api-reference.md) — Canvas, Storage, Audio, and more
