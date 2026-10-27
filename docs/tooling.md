# Editor Support & Tooling

Coi has editor support for several popular code editors, providing syntax highlighting, completions, and other language features.

---

## VS Code (Official)

[![VS Code Extension](https://img.shields.io/badge/VS%20Code-Extension-blue?logo=visual-studio-code)](https://marketplace.visualstudio.com/items?itemName=coi-lang.coi-language)

Install from the [VS Code Marketplace](https://marketplace.visualstudio.com/items?itemName=coi-lang.coi-language) or search for "Coi" in the Extensions view.

**Repository**: [coi/vscode](https://github.com/coi/vscode)

Search for "Coi" in the Extensions view or build manually from the repository.

---

## Sublime Text (Community)

[![Package Control](https://img.shields.io/badge/Package%20Control-available-success?logo=sublime-text)](https://packagecontrol.io/packages/Coi)
[![GitHub](https://img.shields.io/github/stars/SublimeText/Coi?style=social)](https://github.com/SublimeText/Coi)

Install from [Package Control](https://packagecontrol.io/packages/Coi) or see the repository for manual installation instructions.

**Repository**: [SublimeText/Coi](https://github.com/SublimeText/Coi)

---

## Zed (Community)

[![GitHub](https://img.shields.io/github/stars/jturner/zed-coi?style=social)](https://github.com/jturner/zed-coi)

**Repository**: [jturner/zed-coi](https://github.com/jturner/zed-coi)

See the repository for manual installation instructions.

---

## AI Assistants & Agents

Coi is new, so an AI assistant won't know it well out of the box. To help, Coi ships a complete,
self-contained language and API reference you can load into an assistant's context.

- **`llms-full.txt`** (repository root): the full context. Language reference, every platform
  API with signatures, complete example programs, and a list of common mistakes to avoid.
- **`llms.txt`** (repository root): a short index in the [llms.txt](https://llmstxt.org) format
  that links the docs and points to the full context.

### `coi llms`

If the compiler is installed, any agent can fetch the context from the CLI:

```bash
coi llms            # print the full language context to stdout
coi llms --path     # print the path to llms-full.txt
coi llms > coi-context.txt   # save it to hand to an assistant
```

An agent with shell access can discover this on its own: `coi --help` lists `coi llms`, which
resolves to the same `llms-full.txt` that sits next to the compiler.

### `coi mcp`

`coi mcp` is a [Model Context Protocol](https://modelcontextprotocol.io) server, so an assistant
can use the compiler directly instead of guessing. It answers from the Coi you have installed, so
what it says matches your version. Tools:

| Tool | What it does |
|------|--------------|
| `check` | Compile-checks the project, some files, or a snippet, and writes nothing. Errors come back as `file:line` with the line's code, plus a hint when it's a JavaScript habit (`let`, `null`, ...). |
| `lookup` | Exact declarations from the compiler's definition files: `string`, `string.split`, `Canvas`, `System.setTimeout`, or a word like `clipboard`. |
| `docs` | The guide (`llms-full.txt`) by section: no topic lists the sections, `"routing"` or `"common mistakes"` returns them, `"all"` returns everything. |
| `search_packages` | Searches the registry. Shows the release your compiler can use and how to add and import it. |
| `test` | Runs the project's `test` blocks and reports failures with both sides of the comparison. |

The guide is also available as the resource `coi://guide`. The server tells the assistant to
look up APIs before using them and to run `check` before it finishes.

Add it to Claude Code (run in your project):

```bash
claude mcp add coi -- coi mcp
```

Most other clients (Cursor, Claude Desktop, Zed, VS Code) take a JSON entry like this:

```json
{
  "mcpServers": {
    "coi": { "command": "coi", "args": ["mcp"] }
  }
}
```

The tools work on the project in the client's working directory (or the nearest one above it),
and each takes a `project` argument for another folder. `check` writes nothing, so it is safe
while `coi dev` runs.

For an agent with a shell, `coi --check src/*.coi` runs the same check from the command line.

## Contributing

Don't see your editor? Contributions are welcome! Open a PR to add support for additional editors.
