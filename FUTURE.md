I have a few plans and ideas for this language, adding them here :)

- Items are listed roughly in priority order (highest first).

- **Server-side**: Use `Coi` for servers too, keep server and client codebases unified. Run logic-only components on the server and share components and code between client and server.

- **Self-hosted Compiler**: Bootstrap the `Coi` compiler itself to be written in `Coi`. This creates a self-hosted language where the compiler can compile itself, making the language more cohesive and allowing compiler optimizations to benefit directly from language features.

- **Cross-platform**: Make `Coi` a lightweight, high-performance cross-platform language. Think Flutter-style multi-target compilation but much lighter, one `Coi` codebase should compile for web, native mobile, and desktop.

- **Direct WASM Target**: I want to add an additional `WASM` target to the `Coi` compiler that compiles directly to WebAssembly instead of just the C++ target. By skipping the C++ transpilation step, I want to enable much faster compile times and significantly better hot reloadability during development.

- **C++ integration**: Support existing C++ libraries. Drop a library into an `extensions/` folder; `Coi` will scan it, generate bindings (similar to browser API schema generation), and compile it into the final `WASM`.

- **Package manager**: A `Coi` package manager for publishing/consuming packages, versioning, and dependency management (don’t think I need to explain this one :P)

- **Time Travel & Deterministic Debugging**: Because `Coi` targets WebAssembly and has deterministic memory management (no GC), add a compiler flag that instruments binaries as a "flight recorder" to record state transitions and input events. Users can "Export Trace" and replay their exact session frame-by-frame in the VS Code extension, replay is 100% bit-identical.


If you have other ideas or want to iterate on these, hit me up on Discord :D
https://discord.gg/KSpWx78wuR
