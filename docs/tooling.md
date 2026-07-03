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

## Contributing

Don't see your editor? Contributions are welcome! Open a PR to add support for additional editors.
