# Coi Compiler Source Code

This directory contains the Coi compiler source code, organized into logical modules that follow the compilation pipeline.

## Architecture Overview

The Coi compiler transforms `.coi` files into WebAssembly applications through several stages:

```
.coi files → Lexer → Parser → Type Checker → Code Generator → C++ → WebCC → WASM
```

## Directory Structure

### `frontend/` - Lexical Analysis & Parsing
- **lexer.{cc,h}** - Tokenizes source code into tokens
- **parser/** - Parser split into logical units (`core.cc`, `expr.cc`, `stmt.cc`, `view.cc`, `component.cc`, `parser.h`)
- **token.h** - Token type definitions

### `ast/` - Abstract Syntax Tree
- **ast.h** - Core AST node definitions and structures
- **node.cc** - Base AST node functionality
- **expressions.cc** - Expression nodes (literals, operators, function calls)
- **statements.cc** - Statement nodes (if, for, assignments)
- **definitions.cc** - Definition nodes (data types, enums)
- **component.cc** - Component AST and view tree generation
- **view.cc** - View hierarchy handling
- **formatter.cc** - Code formatting utilities

### `analysis/` - Semantic Analysis
- **type_checker.{cc,h}** - Type validation and compatibility checking
- **feature_detector.{cc,h}** - Detects which WebCC features are used
- **include_detector.{cc,h}** - Determines required C++ headers
- **dependency_resolver.{cc,h}** - Resolves component dependencies

### `defs/` - Definition File System
- **def_parser.{cc,h}** - Parses `.d.coi` definition files
- **def_loader.{cc,h}** - Loads and caches definition schemas
- Schema files in `defs/` define Web APIs (DOM, Canvas, Audio, etc.)

### `codegen/` - Code Generation
- **codegen.{cc,h}** - Main C++ code generator
- **json_codegen.{cc,h}** - JSON serialization for data structures
- **css_generator.{cc,h}** - CSS file generation from component styles

### `cli/` - Command Line Interface
- **cli.{cc,h}** - CLI commands (`init`, `build`, `dev`)
- **error.h** - Error handling and reporting utilities

### `tools/` - Build-Time Tools
- **gen_schema.cc** - Generates `.d.coi` files from WebCC schema definitions

### Root Files
- **main.cc** - Entry point, orchestrates the compilation pipeline

## Compilation Pipeline

### 1. Lexical Analysis (`frontend/lexer`)
Converts source code into a stream of tokens:
```
"let count = 0" → [KEYWORD(let), IDENTIFIER(count), EQUALS, NUMBER(0)]
```

### 2. Parsing (`frontend/parser`)
Builds an Abstract Syntax Tree from tokens:
- Parses components, data types, enums, and imports
- Constructs view hierarchies
- Validates syntax

### 3. Type Checking (`analysis/type_checker`)
Validates type correctness:
- Checks variable assignments
- Validates function call arguments
- Ensures prop types match
- Handles type normalization (int → int32, float → float32)

### 4. Dependency Analysis (`analysis/`)
- **include_detector** - Determines which C++ headers to include
- **feature_detector** - Detects WebCC features in use
- **dependency_resolver** - Orders components for correct initialization

### 5. Code Generation (`codegen/`)
Produces C++ code:
- Component classes with lifecycle methods
- Event handlers and state management
- View tree construction
- CSS extraction

### 6. WebCC Compilation (external)
The generated C++ is passed to WebCC which:
- Compiles to WebAssembly
- Generates JavaScript bindings
- Creates the final HTML file

## Key Concepts

### Definition Files (`.d.coi`)
- Define Web APIs available to Coi programs
- Located in `defs/web/`
- Generated from WebCC's schema by `tools/gen_schema`
- Cached in `defs/.cache/definitions.coi.bin` for fast loading

### Component Lifecycle
1. `init {}` - Initialize state and variables
2. `mount()` - Create and return root element
3. `tick {}` - Called on each frame (optional)
4. Event handlers - User interactions (onClick, onInput, etc.)

### View Tree
- Declarative UI defined in `view {}` blocks
- Compiled to imperative DOM manipulation
- Supports reactivity through state binding

## Building

```bash
# Build the compiler
ninja

# Run tests
python tests/run.py all

# Use the compiler
./coi build               # Build a project
./coi dev                 # Build and serve with hot reload
./coi file.coi --out dist # Compile a single file
```

## Adding New Features

### Adding a Web API
1. Add the API to WebCC's schema
2. Run `./build.sh` to regenerate `.d.coi` files
3. The API is now available in Coi code

### Adding Language Features
1. Update `frontend/lexer` for new tokens
2. Update `frontend/parser` for new syntax
3. Add AST nodes in `ast/` if needed
4. Update `analysis/type_checker` for type rules
5. Update `codegen/codegen` for C++ generation

### Adding CLI Commands
1. Add command parsing in `cli/cli.cc`
2. Implement the command logic
3. Update help text in `cli/cli.cc`

## Code Style

- Use C++20 features
- Prefer `std::string_view` for read-only strings
- Use references over pointers where possible
- Keep functions focused and small
- Comment non-obvious logic

## Common Tasks

### Debugging Compilation Errors
1. Check lexer output (tokens)
2. Verify parser constructs correct AST
3. Ensure type checker passes
4. Inspect generated C++ in `.coi/cache/app.cc` (use `--keep-cc`)

### Tracing Feature Detection
Feature detector scans the AST for Web API calls and determines which WebCC features to link.

### Understanding Type Resolution
The type checker normalizes types and checks compatibility, handling:
- Primitive types (int, float, string, bool)
- Handles (Canvas, Audio, Element, etc.)
- Arrays
- Data types and enums

## Testing

- **Unit tests**: `tests/unit/` - Test individual compiler components
- **Integration tests**: `tests/integration/` - Test full compilation pipeline
- **Benchmark**: `benchmark/` - Performance comparisons with other frameworks
