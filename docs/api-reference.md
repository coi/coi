# Platform APIs

Coi provides type-safe access to browser APIs through the [WebCC](https://github.com/io-eric/webcc) toolchain. These APIs are **automatically generated** from the WebCC schema.

## Type System

All platform types use a consistent pattern:

- **`type`**: Defines a handle type (like `Canvas`, `Image`, `DOMElement`)
- **`shared def`**: Static/factory methods called on the type itself
- **`def`**: Instance methods called on an instance

```tsx
// Static method
Image photo = Image.load("photo.png");

// Instance method
canvas.setSize(800, 600);
ctx = canvas.getContext2d();
```

### No-Copy Types

Platform types (handles to browser resources) **cannot be copied** - they can only be **moved** or **referenced**:

```tsx
// ERROR: Cannot copy a Canvas
mut Canvas canvas1 = Canvas.createCanvas("c1", 800.0, 600.0);
mut Canvas canvas2 = canvas1;  // Error!

// OK: Move ownership
mut Canvas canvas1 = Canvas.createCanvas("c1", 800.0, 600.0);
mut Canvas canvas2 := canvas1;  // canvas1 is now invalid

// OK: Reference (borrow)
mut Canvas canvas1 = Canvas.createCanvas("c1", 800.0, 600.0);
mut Canvas& canvasRef = canvas1;  // Both valid (& is part of type, not value)

// OK: Fresh value from factory method
mut Canvas canvas = Canvas.createCanvas("c1", 800.0, 600.0);  // Not a copy
```

This applies to all platform types:
- `Canvas`, `CanvasContext2D`
- `DOMElement`, `Image`, `Audio`
- `WebSocket`, `FetchRequest`
- `WebGLContext`, `WGPUContext`, etc.

Arrays of these types also cannot be copied:

```tsx
// ERROR: Cannot copy Audio[]
mut Audio[] sounds = [Audio.load("a.mp3"), Audio.load("b.mp3")];
mut Audio[] backup = sounds;  // Error!

// OK: Move the array
mut Audio[] backup := sounds;  // sounds is now invalid
```

## Canvas

2D drawing, paths, text, images, and transformations.

### Canvas Methods

| Method | Description |
|--------|-------------|
| `canvas.getContext2d()` | Get 2D rendering context |
| `canvas.setSize(width, height)` | Set canvas dimensions |

### CanvasContext2D Methods

#### Shapes
| Method | Description |
|--------|-------------|
| `fillRect(x, y, w, h)` | Fill a rectangle |
| `strokeRect(x, y, w, h)` | Stroke a rectangle outline |
| `clearRect(x, y, w, h)` | Clear a rectangular area |

#### Paths
| Method | Description |
|--------|-------------|
| `beginPath()` | Start a new path |
| `closePath()` | Close the current path |
| `moveTo(x, y)` | Move to point |
| `lineTo(x, y)` | Draw line to point |
| `arc(x, y, radius, startAngle, endAngle)` | Draw an arc |
| `arcTo(x1, y1, x2, y2, radius)` | Draw arc between points |
| `bezierCurveTo(cp1x, cp1y, cp2x, cp2y, x, y)` | Cubic bezier curve |
| `quadraticCurveTo(cpx, cpy, x, y)` | Quadratic bezier curve |
| `rect(x, y, w, h)` | Add rectangle to path |
| `ellipse(x, y, rx, ry, rotation, start, end, ccw)` | Draw ellipse |
| `fill()` | Fill the current path |
| `stroke()` | Stroke the current path |
| `clip()` | Clip to current path |

#### Styles
| Method | Description |
|--------|-------------|
| `setFillStyle(r, g, b)` | Set fill color (RGB 0-255) |
| `setFillStyleStr(color)` | Set fill color (CSS string) |
| `setStrokeStyle(r, g, b)` | Set stroke color (RGB 0-255) |
| `setStrokeStyleStr(color)` | Set stroke color (CSS string) |
| `setLineWidth(width)` | Set line width |
| `setLineCap(cap)` | Set line cap ("butt", "round", "square") |
| `setLineJoin(join)` | Set line join ("miter", "round", "bevel") |
| `setGlobalAlpha(alpha)` | Set global transparency (0.0-1.0) |
| `setShadow(blur, offX, offY, color)` | Set shadow effect |

#### Text
| Method | Description |
|--------|-------------|
| `setFont(font)` | Set font (e.g., "16px Arial") |
| `setTextAlign(align)` | Set alignment ("left", "center", "right") |
| `setTextBaseline(baseline)` | Set baseline ("top", "middle", "bottom") |
| `fillText(text, x, y)` | Draw filled text |
| `strokeText(text, x, y)` | Draw stroked text |
| `measureTextWidth(text)` | Get text width in pixels |

#### Images
| Method | Description |
|--------|-------------|
| `drawImage(image, x, y)` | Draw image at position |
| `drawImageScaled(image, x, y, w, h)` | Draw image scaled |
| `drawImageFull(img, sx, sy, sw, sh, dx, dy, dw, dh)` | Draw image with source rect |

#### Transforms
| Method | Description |
|--------|-------------|
| `save()` | Save current state |
| `restore()` | Restore saved state |
| `translate(x, y)` | Move origin |
| `rotate(angle)` | Rotate (radians) |
| `scale(x, y)` | Scale |
| `setTransform(a, b, c, d, e, f)` | Set transform matrix |
| `resetTransform()` | Reset to identity |

### Setup Example

> [!IMPORTANT]
> **Upcoming Change:** `Canvas.createCanvas()` will be removed in a future release. Use `<canvas &={canvas}>` in your view instead. See [CHANGES.md](../CHANGES.md#canvas-initialization--view-binding) for migration details.

```tsx
component CanvasApp {
    mut Canvas canvas;
    mut CanvasContext2D ctx;
    
    mount {
        canvas.setSize(800, 600);
        ctx = canvas.getContext2d();
    }
    
    view {
        <canvas &={canvas}></canvas>
    }
}
```

### Example: Bouncing Ball

```tsx
component AnimatedBall {
    mut Canvas canvas;
    mut CanvasContext2D ctx;
    mut float x = 100.0;
    mut float y = 100.0;
    mut float dx = 3.0;
    mut float dy = 2.0;

    mount {
        canvas.setSize(800, 600);
        ctx = canvas.getContext2d();
    }

    tick(float dt) {
        ctx.clearRect(0, 0, 800, 600);
        ctx.setFillStyle(66, 133, 244);
        ctx.beginPath();
        ctx.arc(x, y, 20, 0, 6.28318);
        ctx.fill();

        x += dx;
        y += dy;
        if (x < 20 || x > 780) dx = -dx;
        if (y < 20 || y > 580) dy = -dy;
    }

    view {
        <canvas &={canvas}></canvas>
    }
}
```

## Image

Load images for canvas rendering.

### Methods

| Method | Description |
|--------|-------------|
| `Image.load(string src)` | Load image from URL (static) |

### Example

```tsx
component Gallery {
    mut Canvas canvas;
    mut CanvasContext2D ctx;
    Image photo;

    mount {
        canvas.setSize(400, 300);
        ctx = canvas.getContext2d();
        photo = Image.load("photo.png");
    }

    def draw() : void {
        ctx.drawImage(photo, 0, 0);
    }

    view {
        <div>
            <canvas &={canvas}></canvas>
            <button onclick={draw}>Draw</button>
        </div>
    }
}
```

## Storage

Local storage for persisting data.

### Methods

| Method | Description |
|--------|-------------|
| `Storage.setItem(string key, string value)` | Store a key-value pair |
| `Storage.removeItem(string key)` | Remove item by key |
| `Storage.clear()` | Clear all stored items |

### Example

```tsx
component Settings {
    mut string theme = "light";

    def saveTheme() : void {
        Storage.setItem("theme", theme);
    }

    def toggleTheme() : void {
        theme = theme == "light" ? "dark" : "light";
        saveTheme();
    }

    view {
        <button onclick={toggleTheme}>Theme: {theme}</button>
    }
}
```

## Audio

Audio playback with volume, looping, and playback position.

### Methods

| Method | Description |
|--------|-------------|
| `Audio.load(string src)` | Create audio from URL (static) |
| `play()` | Start playback |
| `pause()` | Pause playback |
| `setVolume(float vol)` | Set volume (0.0 to 1.0) |
| `setLoop(int loop)` | Enable/disable looping (1/0) |
| `getCurrentTime()` | Get current playback position (seconds) |
| `getDuration()` | Get total duration (seconds) |

### Example

```tsx
component MusicPlayer {
    mut Audio music;
    mut bool playing = false;
    mut float progress = 0;

    mount {
        music = Audio.load("song.mp3");
        music.setVolume(0.8);
    }

    tick(float dt) {
        if (playing) {
            float duration = music.getDuration();
            if (duration > 0) {
                progress = (music.getCurrentTime() / duration) * 100;
            }
        }
    }

    def toggle() : void {
        if (playing) {
            music.pause();
        } else {
            music.play();
        }
        playing = !playing;
    }

    view {
        <div>
            <button onclick={toggle}>
                {playing ? "Pause" : "Play"}
            </button>
            <div style="width: {progress}%"></div>
        </div>
    }
}
```

## System

Logging, page title, time, random numbers, and URL navigation.

### Methods

| Method | Description |
|--------|-------------|
| `System.log(string msg)` | Log message to console |
| `System.warn(string msg)` | Log warning to console |
| `System.error(string msg)` | Log error to console |
| `System.setTitle(string title)` | Set page title |
| `System.reload()` | Reload the page |
| `System.openUrl(string url)` | Open URL in new tab |
| `System.navigate(string path)` | Navigate to route (client-side routing) |
| `System.getTime()` | Get time in seconds (float64) |
| `System.getDateNow()` | Get milliseconds since epoch (float64) |
| `System.random()` | Random float between 0.0 and 1.0 |
| `System.random(int seed)` | Seeded random (for reproducibility) |

### Example

```tsx
// Logging
System.log("Debug message");
System.warn("Warning!");
System.error("Error occurred");

// Page title
System.setTitle("My App");

// Time
float now = System.getTime();       // Seconds (high precision)
float epoch = System.getDateNow();  // Milliseconds since epoch

// Random numbers
float r = System.random();          // 0.0 to 1.0

// URL navigation
System.openUrl("https://example.com");  // Opens in new tab
System.navigate("/dashboard");           // Client-side navigation
```

## Input

Keyboard input handling.

### Methods

| Method | Description |
|--------|-------------|
| `Input.isKeyDown(int keyCode)` | Check if key is currently pressed |
| `Input.isKeyUp(int keyCode)` | Check if key is currently released |
| `Input.exitPointerLock()` | Exit pointer lock mode |

### Common Key Codes

| Key | Code |
|-----|------|
| Left Arrow | 37 |
| Up Arrow | 38 |
| Right Arrow | 39 |
| Down Arrow | 40 |
| Space | 32 |
| Enter | 13 |
| Escape | 27 |
| W/A/S/D | 87/65/83/68 |

### Example

```tsx
component Game {
    mut Canvas canvas;
    mut CanvasContext2D ctx;

    mut float x = 400;
    mut float y = 300;
    mut float speed = 300;
    
    float width = 800;
    float height = 600;
    float radius = 20;

    mount {
        canvas.setSize(width, height);
        ctx = canvas.getContext2d();
    }

    tick(float dt) {
        // Movement
        if (Input.isKeyDown(37)) x -= speed * dt;  // Left
        if (Input.isKeyDown(39)) x += speed * dt;  // Right
        if (Input.isKeyDown(38)) y -= speed * dt;  // Up
        if (Input.isKeyDown(40)) y += speed * dt;  // Down

        ctx.clearRect(0, 0, width, height);
        
        // Background
        ctx.setFillStyleStr("#1a1a1a");
        ctx.fillRect(0, 0, 800, 600);
        // Player
        ctx.beginPath();
        ctx.arc(x, y, radius, 0, 6.28);
        ctx.setFillStyleStr("#4ade80");
        ctx.fill();
    }


    view {
            <canvas &={canvas}></canvas>
    }
}
```

## DOMElement

Direct DOM manipulation for browser APIs and measurements.

> [!IMPORTANT]
> **Upcoming Change:** Direct DOM manipulation (`.appendChild()`, `.setInnerHtml()`, `.addClass()`) will be discouraged in favor of declarative view syntax in future releases. `DOMElement` references should primarily be used for browser APIs like `requestFullscreen()` or measurements. See [CHANGES.md](../CHANGES.md#direct-dom-manipulation--declarative-view) for details.

### Methods

| Method | Description |
|--------|-------------|
| `DOMElement.getBody()` | Get document body (static) |
| `DOMElement.getElementById(string id)` | Get element by ID (static) |
| `DOMElement.createElement(string tag)` | Create new element (static) |
| `setAttribute(string name, string value)` | Set attribute |
| `getAttribute(string name)` | Get attribute value |
| `appendChild(DOMElement child)` | Append child element |
| `insertBefore(DOMElement child, DOMElement ref)` | Insert before reference |
| `removeElement()` | Remove this element |
| `setInnerHtml(string html)` | Set inner HTML |
| `setInnerText(string text)` | Set inner text |
| `addClass(string cls)` | Add CSS class |
| `removeClass(string cls)` | Remove CSS class |
| `requestFullscreen()` | Enter fullscreen mode |
| `requestPointerLock()` | Request pointer lock |

### Example

```tsx
component App {
    mut DOMElement container;

    mount {
        DOMElement div = DOMElement.createElement("div");
        div.setInnerHtml("<p>Dynamic content</p>");
        div.addClass("dynamic");
        container.appendChild(div);
    }

    view {
        <div &={container}></div>
    }
}
```

## Fetch

HTTP requests with callback-based response handling. Returns a `FetchRequest` handle.

### Methods

| Method | Description |
|--------|-------------|
| `FetchRequest.get(url, &onSuccess=..., &onError=...)` | Make a GET request (static) |
| `FetchRequest.post(url, body, &onSuccess=..., &onError=...)` | Make a POST request (static) |

### Callback Signatures

| Callback | Signature | Description |
|----------|-----------|-------------|
| `onSuccess` | `def handler(string data) : void` | Called with response data |
| `onError` | `def handler(string error) : void` | Called on request error |

### Example

```tsx
component DataLoader {
    mut string result = "Click to load data";
    mut bool loading = false;

    def handleSuccess(string data) : void {
        loading = false;
        result = data;
    }

    def handleError(string error) : void {
        loading = false;
        result = "Error: " + error;
    }

    def loadData() : void {
        loading = true;
        result = "Loading...";
        FetchRequest.get(
            "https://api.example.com/data",
            &onSuccess = handleSuccess,
            &onError = handleError
        );
    }

    view {
        <div>
            <button onclick={loadData}>Load</button>
            <p>{result}</p>
        </div>
    }
}
```

### POST Example

```tsx
component FormSubmit {
    mut string status = "";

    def handleSuccess(string response) : void {
        status = "Submitted!";
    }

    def handleError(string error) : void {
        status = "Failed: " + error;
    }

    def submit() : void {
        status = "Submitting...";
        FetchRequest.post(
            "https://api.example.com/submit",
            "{\"name\": \"test\"}",
            &onSuccess = handleSuccess,
            &onError = handleError
        );
    }

    view {
        <div>
            <button onclick={submit}>Submit</button>
            <p>{status}</p>
        </div>
    }
}
```

## JSON

Type-safe JSON parsing with compile-time schema validation and presence tracking.

### Methods

| Method | Description |
|--------|-------------|
| `Json.parse(Type, json, &onSuccess=..., &onError=...)` | Parse JSON object into a data type (static) |
| `Json.parse(Type[], json, &onSuccess=..., &onError=...)` | Parse JSON array into a vector of data types (static) |
| `Json.stringify(value)` | Convert data type to JSON string (static) |

### Defining Data Types

JSON parsing requires a `data` definition that describes the expected structure:

```tsx
data Address {
    string street;
    string city;
    int zipcode;
}

data User {
    string name;
    int age;
    string email;
    Address address;      // Nested objects
    string[] tags;        // Arrays
    Friend[] friends;     // Arrays of objects
}
```

### Callback Signatures

**For single object parsing (`Json.parse(Type, ...)`):**

| Callback | Signature | Description |
|----------|-----------|-------------|
| `onSuccess` | `def handler(Type data, TypeMeta meta) : void` | Called with parsed data and presence info |
| `onError` | `def handler() : void` | Called when JSON structure is malformed |

**For array parsing (`Json.parse(Type[], ...)`):**

| Callback | Signature | Description |
|----------|-----------|-------------|
| `onSuccess` | `def handler(Type[] data, TypeMeta[] metas) : void` | Called with array of parsed data and meta structs |
| `onError` | `def handler() : void` | Called when JSON structure is malformed |

**Note:** `onError` is only called for invalid JSON structure (unbalanced braces, unclosed strings, etc.). Missing fields or type mismatches don't trigger errors - instead, those fields simply won't be marked as present in the meta struct.

### Meta Structs (Presence Checking)

The `onSuccess` callback receives a **meta data** that tracks which fields were present in the JSON. This handles optional/nullable fields gracefully:

```tsx
def handleUser(User u, UserMeta meta) : void {
    // Check if fields were present in JSON
    if (meta.has_name()) {
        System.log("Name: " + u.name);
    }
    
    if (meta.has_age()) {
        System.log("Age: {u.age}");
    }
    
    // Nested objects have nested meta
    if (meta.has_address()) {
        if (meta.address.has_city()) {
            System.log("City: " + u.address.city);
        }
    }
}
```

### Example: Fetch + Parse

```tsx
component UserLoader {
    mut string status = "Ready";
    mut User user;

    def handleSuccess(string data) : void {
        Json.parse(
            User,
            data,
            &onSuccess = handleParsedUser,
            &onError = handleParseError
        );
    }

    def handleParsedUser(User u, UserMeta meta) : void {
        user = u;
        status = "Loaded: " + u.name;
    }

    def handleParseError(string error) : void {
        status = "Parse error: " + error;
    }

    def handleFetchError(string error) : void {
        status = "Fetch error: " + error;
    }

    def loadUser() : void {
        status = "Loading...";
        FetchRequest.get(
            "https://api.example.com/user/1",
            &onSuccess = handleSuccess,
            &onError = handleFetchError
        );
    }

    view {
        <div>
            <button onclick={loadUser}>Load User</button>
            <p>{status}</p>
        </div>
    }
}
```

### Example: Array Parsing

Parsing JSON arrays returns a vector of data types with corresponding meta structs:

```tsx
component ShowList {
    data Show {
        string title;
        int id;
    }
    
    mut Show[] shows = [];
    mut string status = "Ready";
    
    def handleShows(Show[] parsedShows, ShowMeta[] metas) : void {
        shows = parsedShows;
        status = "Loaded {parsedShows.length()} shows";
        
        // Each element has its own meta
        for (int i = 0; i < metas.length(); i++) {
            if (metas[i].has_title()) {
                // shows[i].title was present
            }
        }
    }
    
    def handleError() : void {
        status = "Parse error";
    }
    
    mount {
        // Use template strings for clean JSON with interpolation
        string favShow = "Better Call Saul";
        string jsonData = `
        [
            {"title": "Breaking Bad", "id": 1},
            {"title": "{favShow}", "id": 2}
        ]`;
        
        Json.parse(Show[], jsonData, &onSuccess = handleShows, &onError = handleError);
    }
    
    view {
        <div>
            <p>{status}</p>
            <for show in shows>
                <div>{show.title} (ID: {show.id})</div>
            </for>
        </div>
    }
}
```

### Supported Types

| Type | JSON | Notes |
|------|------|-------|
| `string` | `"text"` | Handles escape sequences |
| `int` | `123` | 32-bit signed integer |
| `float` | `3.14` | 64-bit double |
| `bool` | `true`/`false` | |
| `Type` | `{...}` | Nested data types |
| `string[]` | `[...]` | Array of strings |
| `int[]` | `[...]` | Array of integers |
| `Type[]` | `[...]` | Array of nested objects |

### Null Handling

JSON `null` values are handled gracefully - the field is simply not marked as present in the meta struct:

```tsx
// JSON: {"name": "Alice", "age": null}
def handle(User u, UserMeta meta) : void {
    meta.has_name();  // true
    meta.has_age();   // false (was null)
}
```

## WebSocket

Real-time bidirectional communication with WebSocket servers. The handle is automatically invalidated when the connection closes or errors.

### Methods

| Method | Description |
|--------|-------------|
| `WebSocket.connect(url, &onMessage=..., &onOpen=..., &onClose=..., &onError=...)` | Create connection with callback handlers |
| `send(string msg)` | Send message (only when connected) |
| `close()` | Close connection |
| `isConnected()` | Check if WebSocket is connected (handle is valid) |

### Callback Signatures

| Callback | Signature | Description |
|----------|-----------|-------------|
| `onMessage` | `def handler(string msg) : void` | Called when message received |
| `onOpen` | `def handler : void` | Called when connection opens |
| `onClose` | `def handler : void` | Called when connection closes |
| `onError` | `def handler : void` | Called on connection error |

### Example

```tsx
component Chat {
    mut WebSocket ws;
    mut string[] messages;
    mut bool connected = false;

    def handleMessage(string msg) : void {
        messages.push(msg);
    }

    def handleOpen() : void {
        connected = true;
        System.log("Connected!");
        ws.send("Hello from Coi!");
    }

    def handleClose() : void {
        connected = false;
        System.log("Disconnected");
    }

    mount {
        ws = WebSocket.connect(
            "wss://chat.example.com",
            &onMessage = handleMessage,
            &onOpen = handleOpen,
            &onClose = handleClose
        );
    }

    def sendMessage(string text) : void {
        if (connected) {
            ws.send(text);
        }
    }

    view {
        <div>
            <p>{connected ? "🟢 Connected" : "🔴 Disconnected"}</p>
            <for msg in messages key={msg}>
                <p>{msg}</p>
            </for>
        </div>
    }
}
```

## Available APIs

| Module       | Description                                      |
|--------------|--------------------------------------------------|
| `Canvas`     | 2D drawing, paths, text, images, transformations |
| `Image`      | Image loading for canvas rendering               |
| `Audio`      | Audio playback, volume, looping, playback position |
| `Storage`    | Local storage (setItem, removeItem, clear)       |
| `System`     | Logging, page title, time, random, URL navigation |
| `Input`      | Keyboard input, pointer lock                     |
| `DOMElement` | Direct DOM manipulation                          |
| `WebGL`      | WebGL context and rendering                      |
| `WGPU`       | WebGPU support                                   |
| `FetchRequest` | HTTP GET/POST requests                         |
| `WebSocket`  | WebSocket connections                            |
| `Json`       | Type-safe JSON parsing and serialization         |

## Next Steps

- [Getting Started](getting-started.md) — Setup and first project
- [Language Guide](language-guide.md) — Types, control flow, operators
- [Components](components.md) — Component lifecycle, state management
