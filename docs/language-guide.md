# Language Guide

Coi is a statically-typed language. This guide covers the core language features.

## Naming Conventions

Coi enforces naming conventions to distinguish between different constructs:

| Construct | Convention | Example | Enforced |
|-----------|------------|---------|----------|
| Components | `UpperCase` | `component Counter` | ✓ Yes |
| Pod types | `UpperCase` | `pod User` | ✓ Yes |
| Enums | `UpperCase` | `enum Mode` | ✓ Yes |
| Modules | `UpperCase` | `module MyLib` | ✓ Yes |
| Methods | `lowerCase` | `def handleClick()` | ✓ Yes |
| Variables | `lowerCase` | `mut int count` | Recommended |

**Enforced conventions** will cause compile errors:

```tsx
// ✓ Correct
pod User { string name; }
enum Status { Active, Inactive }
def handleClick() : void { }

// ✗ Compile errors
pod user { }       // Error: Pod type name must start with uppercase
enum status { }    // Error: Enum type name must start with uppercase
module myLib;      // Error: Module name must start with uppercase
def HandleClick()  // Error: Method name must start with lowercase
```

**Why this matters:** When you write `Name(...)`, Coi treats it as component/type construction. Writing `name(...)` is a function call. This distinction enables clean JSX-like syntax without ambiguity.

## Modules and Imports

Coi uses a comprehensive module system to organize code and control visibility.

### Module Declaration

Each file defines which module it belongs to using the `module` keyword at the top of the file. Module names must start with an uppercase letter.

```tsx
// Button.coi
module TurboUI;  // Defines scope "TurboUI"

pub component Button { ... }
```

If no `module` declaration is present, the file belongs to the **default module**.

### Visibility (`pub`)

By default, all components, types, and functions are **module-internal**. They are only visible to other files within the same module. To make them available to other modules, you must use the `pub` keyword.

| Keyword | Visibility | Description |
|---------|------------|-------------|
| (none) | Module-Internal | Visible to any file with the same `module Name;` |
| `pub` | Public | Visible to any file that imports it |

### Importing

Use the `import` statement to make components from other files available.

```tsx
// Local imports (relative to current file)
import "ui/Button.coi";
import "utils/Math.coi";

// Package imports (from .coi/pkgs/)
import "@coi/supabase";       // resolves to .coi/pkgs/coi-lang/supabase/Mod.coi
import "@acme/utils/Button.coi";       // resolves to .coi/pkgs/acme/utils/Button.coi
```

**Key Rules:**
1. **Explicit Imports:** You must import every file you use directly.
2. **Package Imports:** Paths starting with `@` resolve to `.coi/pkgs/`. Just `@<scope>/<package-name>/` imports `Mod.coi` by default.
3. **Accessing Components:**
   - **Same Module:** Access components directly by name (e.g., `<Button />`).
   - **Different Module:** Access via module prefix (e.g., `<TurboUI::Button />`).

### Re-exporting with `pub import`

Use `pub import` to re-export components from another file. This is useful for creating package entry points:

```tsx
// Mod.coi - Package entry point
module MyPkg;

pub import "src/ui/Button.coi";
pub import "src/ui/Card.coi";
pub import "src/ui/Input.coi";
```

Now consumers can import just the package to access all components:

```tsx
// App.coi
import "@your-org/my-pkg";  // imports Mod.coi by default

component App {
    view {
        <MyPkg::Button label="Click" />
        <MyPkg::Card title="Hello" />
    }
}
```

Without `pub import`, consumers would need to import each component file directly.

## Types

### Primitive Types

```tsx
// Integers - signed
int8 tiny = 127;           // 8-bit signed (-128 to 127)
int16 small = 32000;       // 16-bit signed
int32 count = 42;          // 32-bit signed (default)
int64 big = 9000000000;    // 64-bit signed
int num = 42;              // Alias for int32

// Integers - unsigned
uint8 byte = 255;          // 8-bit unsigned (0 to 255)
uint16 port = 8080;        // 16-bit unsigned
uint32 id = 4000000000;    // 32-bit unsigned
uint64 huge = 10000000000; // 64-bit unsigned

// Hexadecimal literals (0x prefix)
int GL_ARRAY_BUFFER = 0x8892;      // WebGL constant
uint32 color = 0xFF00FF;           // RGB color value
int mask = 0x4000;                 // Bit mask

// Binary literals (0b prefix)
int flags = 0b1010;                // Binary value (10 in decimal)
int bitMask = 0b11110000;          // Bit mask pattern

// Floating point
float32 precise = 3.14;    // 32-bit float (single precision)
float64 speed = 3.14159;   // 64-bit float (double precision)
float ratio = 0.5;         // Alias for float64

// Other primitives
string name = "Coi";       // String
bool active = true;        // Boolean
```

### Arrays

```tsx
// Dynamic arrays - size can change at runtime
int[] scores = [10, 20, 30]; 
string[] tags = ["web", "fast"];
mut float[] prices = [];     // Mutable empty array

// Fixed-size arrays - size known at compile time
int[5] fixedNums = [1, 2, 3, 4, 5];
float[3] coords = [0.0, 0.0, 0.0];

// Repeat initializer [value; count]
int[] zeros = [0; 100];      // Array of 100 zeros
int[] tens = [10; 5];        // [10, 10, 10, 10, 10]
```

### Dynamic Array Methods (T[])

```tsx
mut int[] nums = [1, 2, 3];

// Size and state
int len = nums.size();       // Get length
bool empty = nums.isEmpty(); // Check if empty

// Modification (requires mut)
nums.push(4);                // Add element to end
nums.pop();                  // Remove last element
nums.clear();                // Remove all elements
nums.remove(0);              // Remove element at index
nums.sort();                 // Sort in ascending order

// Search
int idx = nums.indexOf(2);   // Find index of value (-1 if not found)
bool has = nums.contains(2); // Check if value exists

// Index access
int first = nums[0];
nums[0] = 100;               // Requires mut
```

### Fixed-Size Array Methods (T[N])

```tsx
mut int[5] fixed = [5, 3, 1, 4, 2];

// Size and state
int len = fixed.size();       // Get length (always N)
bool empty = fixed.isEmpty(); // Check if empty

// Modification (requires mut)
fixed.sort();                 // Sort in ascending order
fixed.fill(0);                // Fill all elements with value

// Search
int idx = fixed.indexOf(3);   // Find index of value (-1 if not found)
bool has = fixed.contains(3); // Check if value exists

// Index access
int first = fixed[0];
fixed[0] = 100;               // Requires mut
```

### Strings

```tsx
string text = "Hello, World!";

// Properties
int len = text.length();      // Get length
bool empty = text.isEmpty();  // Check if empty

// Access
int ch = text.charAt(0);      // Get character code at index

// Substrings
string sub = text.subStr(7);      // "World!" - from index to end
string sub2 = text.subStr(0, 5);  // "Hello" - from index with length

// Search
bool has = text.contains("World"); // Check if substring exists

// Trimming
string trimmed = text.trim();       // Remove whitespace from both ends
string left = text.trimStart();     // Remove leading whitespace
string right = text.trimEnd();      // Remove trailing whitespace
```

### String Interpolation

**Both string types support `${expr}` interpolation:**

```tsx
string name = "Alice";
int age = 25;

// Regular strings
string greeting = "Hello, ${name}! You are ${age} years old.";

// Template strings (backticks)
string greeting2 = `Hello, ${name}! You are ${age} years old.`;
```

**Escaping interpolation:**

Use `\$` to include a literal `$` character:

```tsx
string code = "Use \${variable} for interpolation";
// Result: "Use ${variable} for interpolation"
```

**Regular strings (`"..."`)** - Require escape sequences:

```tsx
string text = "Line 1\nLine 2";  // Newline with \n
string quote = "He said \"Hello\"";  // Quotes with \"
string path = "C:\\Users\\name";  // Backslash with \\
```

**Template strings (`` `...` ``)** - Multi-line, minimal escaping:

```tsx
string title = "Breaking Bad";
int id = 1;

// Multi-line with interpolation - perfect for JSON!
string jsonData = `
[
    {"title": "${title}", "id": ${id}},
    {"title": "Game of Thrones", "id": 2}
]`;

// Literal braces don't need escaping (only ${} is interpolation)
string json = `{"name": "Alice", "age": 25}`;
string html = `<div class="title">${title}</div>`;

// Only escape backticks themselves
string example = `Use \` for backticks`;
```

**Key differences:**
- Regular strings: Need `\"`, `\n`, `\\` for quotes, newlines, backslashes
- Template strings: Preserve whitespace/newlines, only escape `` \` ``
- Both: Support `${variable}` interpolation and `\$` escaping

## Math Library

Coi provides a built-in `Math` type with mathematical constants and functions. All members are accessed using `Math.name` syntax.

### Constants

```tsx
float pi = Math.PI;          // π ≈ 3.14159
float halfPi = Math.HALF_PI; // π/2 ≈ 1.57079
float tau = Math.TAU;        // 2π ≈ 6.28318
float d2r = Math.DEG2RAD;    // Degrees to radians multiplier
float r2d = Math.RAD2DEG;    // Radians to degrees multiplier
```

### Basic Functions

```tsx
float absolute = Math.abs(-5.0);      // 5.0
float squareRoot = Math.sqrt(16.0);   // 4.0
```

### Trigonometric Functions

All trig functions use radians:

```tsx
float sineValue = Math.sin(Math.PI / 2.0);     // 1.0
float cosineValue = Math.cos(0.0);              // 1.0
float tangentValue = Math.tan(Math.PI / 4.0);  // 1.0

// Convert degrees to radians
float angleRad = 90.0 * Math.DEG2RAD;
float sine = Math.sin(angleRad);  // 1.0
```

### Random Number Generation

```tsx
// Random float between 0.0 and 1.0
float rand = Math.random();

// Seeded random (for reproducible sequences)
float seeded = Math.random(42);  // Same seed produces same result
```

### Utility Functions

```tsx
// Min/Max
float smaller = Math.min(10.0, 20.0);   // 10.0
float larger = Math.max(10.0, 20.0);    // 20.0

// Clamp value between min and max
float clamped = Math.clamp(15.0, 0.0, 10.0);  // 10.0

// Linear interpolation
float mid = Math.lerp(0.0, 100.0, 0.5);  // 50.0 (t=0.5 is halfway)

// Rounding
float down = Math.floor(3.7);   // 3.0
float up = Math.ceil(3.2);      // 4.0
float nearest = Math.round(3.5); // 4.0
```

### Example Usage

```tsx
component Circle {
    mut float angle = 0.0;
    mut float x = 0.0;
    mut float y = 0.0;
    
    def update() : void {
        angle = angle + 0.1;
        
        // Calculate circular motion
        x = Math.cos(angle) * 100.0;
        y = Math.sin(angle) * 100.0;
        
        // Keep angle in valid range
        if (angle > Math.TAU) {
            angle = 0.0;
        }
    }
}
```

## Pod Types

Pod types (Plain Old Data) are simple value types (like structs in other languages) that group related fields together. Unlike platform types (Canvas, Audio, etc.), pod types are **copyable** and can be freely passed around.

### Declaring Pod Types

Pod types can be declared globally or inside components:

```tsx
// Global pod type
pod User {
    string name;
    int age;
    string email;
}

// Inside a component
component App {
    pod Config {
        string host;
        int port;
        bool secure;
    }
    
    mut Config config;
}
```

### Field Rules

- **No modifiers**: Pod fields cannot use `pub` or `mut` modifiers
- **Value types only**: Pod fields cannot contain no-copy types like `Canvas`, `Audio`, `WebSocket`, etc.
- **Any primitive or pod type**: Can use `int`, `float`, `string`, `bool`, arrays, and other pod types

```tsx
pod Point {
    float x;
    float y;
}

pod Rectangle {
    Point topLeft;     // Nested pod type - OK
    Point bottomRight;
    string label;
}

pod BadData {
    Canvas canvas;     // ERROR: Canvas is a no-copy type
    Audio sound;       // ERROR: Audio is a no-copy type
}
```

### Initialization

Use aggregate initialization syntax with curly braces. Both positional and named field syntax are supported:

```tsx
component App {
    mut User user;
    
    init {
        // Positional initialization (fields in declaration order)
        user = User{"Alice", 25, "alice@example.com"};
        
        // Named initialization (any order, more readable)
        user = User{name = "Alice", age = 25, email = "alice@example.com"};
        
        // Named with trailing comma
        user = User{
            name = "Bob",
            age = 30,
            email = "bob@example.com",
        };
        
        // Move with named syntax
        mut string email = "charlie@example.com";
        user = User{name = "Charlie", age = 35, email := email};
    }
}
```

### Copying and Moving

Pod types are value types and support both copying and moving:

```tsx
mut User user1 = User{"Bob", 30, "bob@example.com"};

// Copy (both variables are valid)
mut User user2 = user1;

// Move (user3 becomes invalid after move)
mut User user3 = User{"Charlie", 35, "charlie@example.com"};
mut User user4 := user3;  // user3 is now invalid
```

### References

Pod types support references like any other type:

```tsx
def updateUser(mut User& u) : void {
    u.age = u.age + 1;
}

mut User user = User{"Dave", 28, "dave@example.com"};
updateUser(&user);  // Pass by reference
```

### Arrays of Pod Types

You can create arrays of pod types:

```tsx
pod Item {
    string name;
    int quantity;
}

mut Item[] inventory = [
    Item{"Apple", 5},
    Item{"Banana", 3},
    Item{"Orange", 7}
];

// Use in loops
for item in inventory {
    print(item.name);
}
```

### JSON Parsing

Pod types can be automatically parsed from JSON using `Json.parse()` and consumed via `match`:

```tsx
pod User {
    string name;
    int age;
    string email;
}

component App {
    mut User user;

    init {
        string json = `{"name": "Alice", "age": 25}`;
        match (Json.parse(User, json)) {
            Success(User data, Meta meta) => {
                user = data;
                if (meta.has(User.name)) {
                    System.log("Name found: " + data.name);
                }
            };
            Error(string error) => {
                System.log("Parse error: " + error);
            };
        };
    }
}
```

For each pod type, a corresponding **Meta struct** is automatically generated. Check field presence with `meta.has(Type.field)`:

```tsx
if (meta.has(User.name)) { /* ... */ }
if (meta.has(User.email)) { /* ... */ }
```

See [API Reference - JSON](api-reference.md#json) for more details and examples.

### Reactivity

Pod types participate in Coi's reactivity system. When you modify a field of a pod type, the entire object is marked as modified:

```tsx
component UserProfile {
    mut User user;
    
    init {
        user = User{"Eve", 22, "eve@example.com"};
    }
    
    def updateEmail(string newEmail) : void {
        user.email = newEmail;  // Marks 'user' as modified
        // View will automatically re-render
    }
    
    view {
        <div>
            <p>Name: {user.name}</p>
            <p>Email: {user.email}</p>
        </div>
    }
}
```

## Enums

Enums define a set of named constants. They can be declared inside components, as shared (accessible from other components), or globally.

### Basic Enums

```tsx
// Inside a component
component App {
    enum Mode {
        Idle,
        Running,
        Paused
    }
    
    mut Mode currentMode = Mode::Idle;
    
    def start() : void {
        currentMode = Mode::Running;
    }
}

// Global enum (outside any component)
enum Status {
    Pending,
    Success,
    Error
}

component Handler {
    mut Status status = Status::Pending;
}
```

### Shared Enums

Shared enums can be accessed from other components using the `ComponentName.EnumName::Value` syntax:

```tsx
component Theme {
    shared enum Mode {
        Light,
        Dark
    }
    
    mut Mode current = Mode::Light;
}

component Settings {
    // Access shared enum from another component
    mut Theme.Mode selectedTheme = Theme.Mode::Dark;
}
```

### Enum Features

- Enums implicitly convert to/from `int` for easy serialization
- Comparison with `==` and `!=` works as expected
- Trailing commas are allowed in enum value lists
- `.size()` returns the number of enum values

```tsx
// Implicit int conversion
mut Mode mode = Mode::Idle;
int modeValue = mode;        // enum -> int
mode = 2;                    // int -> enum

// Get enum size (useful for iteration)
int count = Mode.size();     // returns 3 (Idle, Running, Paused)

// Comparison
if (mode == Mode::Running) {
    // ...
}
```

## Control Flow

### Conditionals

```tsx
if (x > 10) {
    message = "big";
} else {
    message = "small";
}
```

### Ternary Operator

```tsx
// Basic ternary
int max = x > y ? x : y;

// Nested ternary
string sign = n > 0 ? "positive" : n < 0 ? "negative" : "zero";

// In expressions
string msg = count > 0 ? "has items" : "empty";
```

### Match Expression

The `match` expression provides pattern matching against values:

```tsx
enum Status { Pending, Success, Error }

mut Status status = Status::Success;

// Match on enum
string message = match (status) {
    Status::Pending => "Loading...";
    Status::Success => "Done!";
    Status::Error => "Failed";
};
```

Arms support two forms:
- **Shorthand expression arm**: `Pattern => expression;`
- **Block arm**: `Pattern => { ... };`

When a `match` is used as a value (e.g., assigned to a variable or returned), block arms must `yield` a value:

```tsx
string label = match (status) {
    Status::Pending => { 
        System.log("pending");
        yield "Loading...";
    };
    else => "Ready";
};
```

When a `match` is used as a statement (side effects only), `yield` is optional:

```tsx
match (status) {
    Status::Pending => {
        System.log("pending");
    };
    else => {
        System.log("ready");
    };
};
```

**Literal patterns:**

```tsx
int code = 404;

string text = match (code) {
    200 => "OK";
    404 => "Not Found";
    500 => "Server Error";
    else => "Unknown";
};
```

**Pod patterns with field binding:**

```tsx
pod Point { float x; float y; }

Point p = Point{3.0, 4.0};

string desc = match (p) {
    Point{x = 0.0, y = 0.0} => "Origin";
    Point{x, y} => "Point at (${x}, ${y})";
};
```

**Default with `else`:**

```tsx
string result = match (value) {
    1 => "one";
    2 => "two";
    else => "other";
};
```

### Loops

Coi supports range-based loops and iterator-based foreach loops.

```tsx
// Range-based (start:end)
for i in 0:10 {
    sum += i;
}

// ForEach (Arrays)
for score in scores {
    total += score;
}
```

## Operators

### Arithmetic

```tsx
int sum = a + b;
int diff = a - b;
int prod = a * b;
int quot = a / b;
int mod = a % b;
```

### Comparison

```tsx
if (x == y) { }  // equal
if (x != y) { }  // not equal
if (x < y)  { }  // less than
if (x > y)  { }  // greater than
if (x <= y) { }  // less or equal
if (x >= y) { }  // greater or equal
```

### Logical

```tsx
if (a && b) { }  // and
if (a || b) { }  // or
if (!a) { }      // not
```

### Bitwise

```tsx
int and = a & b;    // bitwise AND
int or = a | b;     // bitwise OR
int xor = a ^ b;    // bitwise XOR
int not = ~a;       // bitwise NOT (complement)
int lsh = a << 2;   // left shift
int rsh = a >> 2;   // right shift
```

**Compound bitwise assignment:**

```tsx
flags &= mask;      // AND assign
flags |= flag;      // OR assign
flags ^= toggle;    // XOR assign
value <<= 1;        // left shift assign
value >>= 1;        // right shift assign
```

### Assignment

```tsx
count += 10;
count -= 5;
count *= 2;
count /= 2;
count++;
count--;
```

### Move Assignment (`:=`)

The move assignment operator `:=` transfers ownership of a value:

```tsx
string original = "Hello";
string moved := original;  // Move original into moved
// original is now invalid and cannot be used
```

## Reference and Move Semantics

Coi supports explicit reference (`&`) and move (`:`) semantics for efficient value passing.

### Reference (`&`)

Pass a reference to allow the callee to read or modify the original value (borrowing):

```tsx
def increment(mut int& value) : void {
    value++;  // Modifies the original
}

mut int count = 0;
increment(&count);  // count is now 1
```

### Move (`:`)

Transfer ownership of a value to the callee. The original variable becomes invalid:

```tsx
def consume(string text) : void {
    // Function owns the string
    System.log(text);
}

string msg = "Hello";
consume(:msg);  // Ownership transferred
// msg cannot be used anymore
```

### No-Copy Types

Some types **cannot be copied** at all - they can only be moved or referenced. These are platform types that represent browser resources:

```tsx
// Platform types that cannot be copied:
// Canvas, DOMElement, Audio, Image, WebSocket, etc.

mut Canvas canvas1 = Canvas.createCanvas("c1", 800.0, 600.0);

// ERROR: Cannot copy
Canvas canvas2 = canvas1;

// OK: Move ownership
Canvas canvas2 := canvas1;  // canvas1 is now invalid

// OK: Reference (& is part of the type declaration)
Canvas& ref = canvas1;  // Both valid

// OK: Fresh value from factory
Canvas canvas3 = Canvas.createCanvas("c3", 400.0, 300.0);
```

This restriction prevents accidental duplication of browser resources and ensures clear ownership.

### Summary

| Syntax | Meaning | Original After Call |
|--------|---------|--------------------|
| `func(value)` | Copy | Valid (unchanged) |
| `func(&value)` | Borrow (reference) | Valid (may be modified) |
| `func(:value)` | Move (transfer ownership) | Invalid |

## Functions

Functions are defined with the `def` keyword:

```tsx
def add(int a, int b) : int {
    return a + b;
}

def greet(string name) : void {
    System.log("Hello, " + name);
}

// Public function (accessible from outside component)
pub def reset() : void {
    count = 0;
}
```

### Multiple Return Values

Functions can return multiple values using tuple-style syntax:

```tsx
def getUser() : (string name, int age) {
    return ("Alice", 30);
}
```

Tuple element names in the return signature are optional:

```tsx
def getUser() : (string, int) {
    return ("Alice", 30);
}
```

Destructure them at the call site:

```tsx
(string name, int age) = getUser();
```

You can ignore values by omitting the variable name in the destructuring pattern:

```tsx
(int id, string) = getUser();  // second value is ignored
```

Tuple returns are statically typed:
- Return element count must match the function signature
- Return element types must match in order
- Destructured variables use the types you declare in the tuple pattern

`mut` is supported in destructuring when you need to reassign one of the values:

```tsx
(mut int left, int right) = getPair();
left = left + 1;
```

Without `mut`, destructured variables are immutable:

```tsx
(int left, int right) = getPair();
left++;  // compile error: left is immutable
```

## Next Steps

- [Components](components.md) — Component syntax, lifecycle, props
- [View Syntax](view-syntax.md) — JSX-like templates, conditionals, loops
- [Styling](styling.md) — Scoped and global CSS
