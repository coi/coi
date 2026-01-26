# Language Guide

Coi is a statically-typed language. This guide covers the core language features.

## Naming Conventions

Coi enforces naming conventions to distinguish between different constructs:

| Construct | Convention | Example | Enforced |
|-----------|------------|---------|----------|
| Components | `UpperCase` | `component Counter` | ✓ Yes |
| Data types | `UpperCase` | `data User` | ✓ Yes |
| Enums | `UpperCase` | `enum Mode` | ✓ Yes |
| Methods | `lowerCase` | `def handleClick()` | ✓ Yes |
| Variables | `lowerCase` | `mut int count` | Recommended |

**Enforced conventions** will cause compile errors:

```tsx
// ✓ Correct
data User { string name; }
enum Status { Active, Inactive }
def handleClick() : void { }

// ✗ Compile errors
data user { }      // Error: Data type name must start with uppercase
enum status { }    // Error: Enum type name must start with uppercase  
def HandleClick()  // Error: Method name must start with lowercase
```

**Why this matters:** When you write `Name(...)`, Coi treats it as component/type construction. Writing `name(...)` is a function call. This distinction enables clean JSX-like syntax without ambiguity.

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

## Data Types

Data types are simple value types (like structs in other languages) that group related fields together. Unlike platform types (Canvas, Audio, etc.), data types are **copyable** and can be freely passed around.

### Declaring Data Types

Data types can be declared globally or inside components:

```tsx
// Global data type
data User {
    string name;
    int age;
    string email;
}

// Inside a component
component App {
    data Config {
        string host;
        int port;
        bool secure;
    }
    
    mut Config config;
}
```

### Field Rules

- **No modifiers**: Data fields cannot use `pub` or `mut` modifiers
- **Value types only**: Data fields cannot contain no-copy types like `Canvas`, `Audio`, `WebSocket`, etc.
- **Any primitive or data type**: Can use `int`, `float`, `string`, `bool`, arrays, and other data types

```tsx
data Point {
    float x;
    float y;
}

data Rectangle {
    Point topLeft;     // Nested data type - OK
    Point bottomRight;
    string label;
}

data BadData {
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

Data types are value types and support both copying and moving:

```tsx
mut User user1 = User{"Bob", 30, "bob@example.com"};

// Copy (both variables are valid)
mut User user2 = user1;

// Move (user3 becomes invalid after move)
mut User user3 = User{"Charlie", 35, "charlie@example.com"};
mut User user4 := user3;  // user3 is now invalid
```

### References

Data types support references like any other type:

```tsx
def updateUser(mut User& u) : void {
    u.age = u.age + 1;
}

mut User user = User{"Dave", 28, "dave@example.com"};
updateUser(&user);  // Pass by reference
```

### Arrays of Data Types

You can create arrays of data types:

```tsx
data Item {
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

### Reactivity

Data types participate in Coi's reactivity system. When you modify a field of a data type, the entire object is marked as modified:

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

## Next Steps

- [Components](components.md) — Component syntax, lifecycle, props
- [View Syntax](view-syntax.md) — JSX-like templates, conditionals, loops
- [Styling](styling.md) — Scoped and global CSS
