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
| `Canvas.createCanvas(dom_id, width, height)` | Create a new canvas element |
| `canvas.getContext2d()` | Get 2D rendering context |
| `canvas.getContext2d(lowLatency, opaque)` | Same, with options: `lowLatency` lets the canvas reach the screen without waiting for the page (less pen lag), `opaque` drops transparency (a bit faster). Only the first call per canvas decides |
| `canvas.getContextWebgl()` | Get WebGL rendering context |
| `canvas.getContextWebgpu()` | Get WebGPU rendering context |
| `canvas.setSize(width, height)` | Set canvas dimensions |
| `canvas.logCanvasInfo()` | Log canvas information |

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

> **Note:** `Canvas.createCanvas()` has been removed. Use `<canvas &={canvas}>` in your view and call `canvas.setSize()` in the `mount {}` block. See [CHANGES.md](../CHANGES.md#canvas-initialization--view-binding) for migration details.

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
| `Storage.getItem(string key)` | Read a value by key (returns `""` if the key is missing) |
| `Storage.removeItem(string key)` | Remove item by key |
| `Storage.clear()` | Clear all stored items |

> **Note:** `getItem` returns an empty string (`""`) for a missing key, so use `.isEmpty()` to detect "not set".

### Example

```tsx
component Settings {
    mut string theme = "light";

    init {
        // Restore the saved theme on startup (defaults to "light").
        string saved = Storage.getItem("theme");
        if (!saved.isEmpty()) {
            theme = saved;
        }
    }

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
            <div style="width: ${progress}%"></div>
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
| `System.getDateNow()` | Get milliseconds since epoch, UTC (float64) |
| `System.getTimezoneOffsetMs()` | Local timezone offset from UTC in milliseconds (float64; add to a UTC epoch ms to get local time) |
| `System.getVisibilityState()` | Get document visibility state (`"visible"`, `"hidden"`, etc.) |
| `System.isHidden()` | Check if document is hidden (`true` hidden, `false` visible) |
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

// Page visibility
string visibility = System.getVisibilityState();
bool hidden = System.isHidden();

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

Reference handle for browser APIs and measurements.

`DOMElement` is used as a **reference handle** to interact with browser APIs that cannot be expressed declaratively in the view. Use the `&={element}` binding to capture a reference to an element.

> **Note:** Direct DOM manipulation methods (`.appendChild()`, `.setInnerHtml()`, `.addClass()`, etc.) have been removed. Define your UI structure declaratively in the `view {}` block instead. For rendering HTML strings, use the `<raw>` element (see [View Syntax](view-syntax.md#raw-html)). See [CHANGES.md](../CHANGES.md#direct-dom-manipulation--declarative-view) for migration details.

### Methods

| Method | Description |
|--------|-------------|
| `requestFullscreen()` | Enter fullscreen mode |
| `requestPointerLock()` | Request pointer lock |
| `scrollToTop()` | Scroll element to top |

### Example

```tsx
component VideoPlayer {
    mut DOMElement videoEl;

    def enterFullscreen() {
        videoEl.requestFullscreen();  // Browser API
    }

    view {
        <div>
            <video &={videoEl} src="video.mp4"></video>
            <button onclick={enterFullscreen}>Fullscreen</button>
        </div>
    }
}
```

## Fetch

HTTP requests with callback-based response handling. Returns a `FetchRequest` handle. See [Events on handles](#events-on-handles) for how callbacks work.

### Methods

| Method | Description |
|--------|-------------|
| `FetchRequest.get(url, headers="", &onSuccess=..., &onError=...)` | Make a GET request (static) |
| `FetchRequest.post(url, body, headers="", &onSuccess=..., &onError=...)` | Make a POST request (static) |
| `FetchRequest.patch(url, body, headers="", &onSuccess=..., &onError=...)` | Make a PATCH request (static) |
| `onSuccess(&handler)`, `onError(&handler)` | Bind a callback to a request after creating it |

`headers` is a JSON object string (for example: `"{\"Authorization\":\"Bearer token\",\"Content-Type\":\"application/json\"}"`). Callbacks can be passed by name (`&onError = handleError`) or by position (`&ok, &err`).

### Events

| Event | Handler parameters | When |
|-------|--------------------|------|
| `onSuccess` | `string data` | Response with a 2xx status; `data` is the body |
| `onError` | `string error` | Network failure or non-2xx status; `error` is the body or `"status text"` |

A request produces exactly one of them, after which its callbacks are released.

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
            "{\"Content-Type\":\"application/json\"}",
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

### PATCH Example

```tsx
component ProfileUpdate {
    mut string status = "";

    def ok(string response) : void {
        status = "Updated";
    }

    def fail(string error) : void {
        status = "Failed: " + error;
    }

    def updateProfile() : void {
        FetchRequest.patch(
            "https://api.example.com/profile",
            "{\"displayName\":\"Eric\"}",
            "{\"Authorization\":\"Bearer token\",\"Content-Type\":\"application/json\"}",
            &onSuccess = ok,
            &onError = fail
        );
    }

    view {
        <div>
            <button onclick={updateProfile}>Update</button>
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
| `Json.parse(Type, json)` | Parse JSON object and return a result for `match` |
| `Json.parse(Type[], json)` | Parse JSON array and return a result for `match` |
| `Json.stringify(value)` | Convert pod type to JSON string (static) |

### Defining Pod Types

JSON parsing requires a `pod` definition that describes the expected structure:

```tsx
pod Address {
    string street;
    string city;
    int zipcode;
}

pod User {
    string name;
    int age;
    string email;
    Address address;      // Nested objects
    string[] tags;        // Arrays
    Friend[] friends;     // Arrays of objects
}
```

### Result Pattern

`Json.parse(...)` is consumed through `match` using `Success(...)` and `Error(...)` arms:

```tsx
string res = match (Json.parse(User, json)) {
    Success(User data, Meta meta) => {
        // use data + field presence
        yield data.name;
    };
    Error(string message) => {
        yield message;
    };
};
```

For arrays:

```tsx
int len = match (Json.parse(User[], jsonArray)) {
    Success(User[] data, UserMeta[] metas) => data.length();
    Error(string message) => 0;
};
```

### Meta Structs (Presence Checking)

`Success(..., Meta meta)` provides presence checking for the parsed pod fields. This handles optional/nullable fields gracefully:

```tsx
match (Json.parse(User, payload)) {
    Success(User u, Meta meta) => {
        if (meta.has(User.name)) {
            System.log("Name: " + u.name);
        }
        if (meta.has(User.age)) {
            System.log("Age: ${u.age}");
        }
    };
    Error(string error) => {
        System.log("Parse error: " + error);
    };
}
```

### Example: Fetch + Parse

```tsx
component UserLoader {
    mut string status = "Ready";
    mut User user;

    def handleSuccess(string data) : void {
        match (Json.parse(User, data)) {
            Success(User u, Meta meta) => {
                user = u;
                status = "Loaded: " + u.name;
            };
            Error(string error) => {
                status = "Parse error: " + error;
            };
        };
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

Parsing JSON arrays returns a vector of pod types with corresponding meta structs:

```tsx
component ShowList {
    pod Show {
        string title;
        int id;
    }
    
    mut Show[] shows = [];
    mut string status = "Ready";
    
    mount {
        // Use template strings for clean JSON with interpolation
        string favShow = "Better Call Saul";
        string jsonData = `
        [
            {"title": "Breaking Bad", "id": 1},
            {"title": "{favShow}", "id": 2}
        ]`;

        match (Json.parse(Show[], jsonData)) {
            Success(Show[] parsedShows, ShowMeta[] metas) => {
                shows = parsedShows;
                status = "Loaded {parsedShows.size()} shows";
                // Each element has its own meta
                for i in 0:metas.size() {
                    if (metas[i].has_title()) {
                        // shows[i].title was present
                    }
                }
            };
            Error(string error) => {
                status = "Parse error: " + error;
            };
        };
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
| `Type` | `{...}` | Nested pod types |
| `string[]` | `[...]` | Array of strings |
| `int[]` | `[...]` | Array of integers |
| `Type[]` | `[...]` | Array of nested objects |

### Null Handling

JSON `null` values are handled gracefully - the field is simply not marked as present in the meta struct:

```tsx
// JSON: {"name": "Alice", "age": null}
match (Json.parse(User, payload)) {
    Success(User u, Meta meta) => {
        meta.has(User.name);  // true
        meta.has(User.age);   // false (was null)
    };
    Error(string error) => {
        yield 0;
    };
}
```

## WebSocket

Real-time bidirectional communication with WebSocket servers. An owned `WebSocket` member is closed when its component is destroyed.

### Methods

| Method | Description |
|--------|-------------|
| `WebSocket.connect(url, protocols="", &onMessage=..., &onOpen=..., &onClose=..., &onError=...)` | Create connection with callback handlers |
| `send(string msg)` | Send message (dropped unless the socket is open) |
| `close()` | Close connection |
| `isConnected()` | True while the socket is open (not connecting, closing or closed) |
| `onMessage(&h)`, `onOpen(&h)`, `onClose(&h)`, `onError(&h)` | Bind a callback after connecting |

### Events

| Event | Handler parameters | When |
|-------|--------------------|------|
| `onMessage` | `string msg` | A text message arrived |
| `onOpen` | none | The connection is open |
| `onClose` | `int code, string reason, uint8 wasClean` | The connection closed. Always the last event, also after an error; afterwards the socket's callbacks are released |
| `onError` | none | The connection failed. The browser sends `onClose` right after |

Handlers may take fewer parameters than the event provides: `def handleClose() : void` is fine for `onClose`.

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

## Storage (IndexedDB)

A key-value store that survives reloads: string keys, `uint8[]` values, no practical size limit. Results arrive on the request each call returns, so pass the callbacks to the call.

```tsx
component Notebook {
    mut Database db;
    mut string status = "";

    def opened(uint8 ok) : void {
        db.put("page/1", encodePage(), &onError = failed);
        db.get("page/1", &loaded, &failed);
    }

    def loaded(Blob value, uint8 found) : void {
        if (found == 0) return;          // no such key
        uint8[] bytes = value.take();    // take() reads and releases the blob
        decodePage(bytes);
    }

    def failed(string message) : void { status = message; }

    mount { db = Idb.open("notes", &opened); }
}
```

| Call | Result events |
|------|---------------|
| `Idb.open(name, &onOpened)` | `onOpened(uint8 ok)` |
| `db.put(key, bytes, &onDone, &onError)` | done once the write is committed |
| `db.get(key, &onValue, &onError)` | `onValue(Blob value, uint8 found)`; `found` is 0 for a missing key |
| `db.remove(key, &onDone, &onError)` | done also when the key didn't exist |
| `db.keys(prefix, &onKeys, &onError)` | `onKeys(string keys)`: sorted, one per line |
| `db.close()` | |

Calls made before `onOpened` wait for the database, and calls on one database run in order.

## Blob

Bytes held by the browser until you ask for them: an IndexedDB value, a pasted or dropped file. `Blob.create(bytes)` makes one, `size()` tells its length, `read()` copies the bytes out, `take()` copies them out and releases the blob, `free()` releases it without reading.

## Events on handles

Every browser object Coi hands you (`WebSocket`, `FetchRequest`, `Image`, a `Canvas` or any `DOMElement`...) is a *handle*, and the events the browser sends for it are methods named `on<Event>` on that handle type. They come straight from the platform schema, so a new API's events are available the moment the API is.

```tsx
mut Image logo;

def placed(int width, int height) : void { ... }
def broken() : void { ... }

mount {
    logo = Image.load("/logo.png");
    logo.onLoaded(&placed);   // LOADED carries width and height
    logo.onError(&broken);
}
```

An event with several fields can also arrive as one value. Every such event has a pod named after it, and a handler can take that instead of the separate fields:

```tsx
def onPtr(PointerEvent e) : void {
    if (e.phase == PointerPhase::Down) startStroke(e.x, e.y, e.pressure);
}

mount { surface.onPointer(&onPtr); }
```

| Pod | Fields |
|-----|--------|
| `PointerEvent` | `phase`, `pointerId`, `pointerType`, `buttons`, `x`, `y`, `pressure`, `tiltX`, `tiltY`, `time` |
| `WheelEvent` | `deltaX`, `deltaY`, `x`, `y`, `mods` |
| `ResizeEvent` | `width`, `height`, `pixelWidth`, `pixelHeight`, `dpr` |
| `CloseEvent` | `code`, `reason`, `wasClean` (WebSocket) |
| `LoadedEvent` | `width`, `height` (Image) |

Events that carry a handle (a `Blob` in an IndexedDB value, a dropped or opened file) have no pod, since a handle can't be copied: take their fields as separate parameters.

The rules:

- A callback is a method of the component, passed with `&`. It runs on the next frame after the event, with the component alive.
- A handler takes the event's pod, or its fields as separate parameters: **fewer** than the event provides is fine (the leading ones), never more or of another type. The compiler checks this.
- Functions that create a handle take the handle's callbacks as optional trailing arguments, by name (`&onError = failed`) or in event order (`&ok, &err`). `WebSocket.connect(...)` and `FetchRequest.get(...)` work this way.
- Events after which a handle is finished (a fetch's result, a socket's close) release that handle's callbacks. A destroyed component releases all of its callbacks, so a late event never reaches freed memory.

The generated definitions in `defs/web/*.d.coi` list every handle type's events with their parameters.

## Page-wide events

Some events belong to the whole page, not to one handle: pastes, key presses, the page being hidden. They are shared methods on their area's type, and every component that registers is called. Registering turns the event on, and a component's handlers go away with it.

```tsx
component Editor {
    def pasted(string text) : void { insertText(text); }
    def pastedImage(Blob image, string mime) : void { placeImage(Image.fromBlob(image, mime)); image.free(); }
    def key(KeyDownEvent k) : void {
        if ((k.mods & (Mods.Ctrl | Mods.Meta)) != 0 && k.key == "z") undo();
    }
    def hidden(VisibilityChangeEvent v) : void { if (v.hidden == 1) saveNow(); }

    mount {
        Clipboard.onPasteText(&pasted);
        Clipboard.onPasteImage(&pastedImage);
        Input.onKeyDown(&key);
        Input.preventKey(83, Mods.Ctrl);   // Ctrl+S saves the note, not the page
        System.onVisibilityChange(&hidden);
    }
}
```

| Event | Handler takes |
|-------|---------------|
| `Clipboard.onPasteText` | `string text` (pastes into text fields stay with the field) |
| `Clipboard.onPasteImage` | `Blob image, string mime` |
| `Input.onKeyDown` / `onKeyUp` | `KeyDownEvent` / `KeyUpEvent` (`keyCode`, `mods`, `repeat`, `key`) |
| `System.onVisibilityChange` | `VisibilityChangeEvent` (`hidden`, `state`): the moment to save |
| `System.onPageHide` / `onPageShow` | `uint8 persisted` |
| `System.onOnline` | `uint8 online` |

Events of a handle you create in code (`img.onLoaded(&h)`) need that handle's listener turned on where there is one (`surface.addPointerListener(flags)` before `surface.onPointer(&h)`); the view attributes (`onpointerdown=`) and page-wide events do it for you.

## More platform functions

| Call | What it does |
|------|--------------|
| `Clipboard.writeText(text)` | Copy (only in a click or key handler) |
| `FileRequest.open(accept, multiple, &onOpened = h)` | File dialog (in a click or key handler); `h(Blob data, string name, string mime, int index, int count)` per file |
| `Files.save(name, mime, bytes)` | Download |
| `FetchRequest.request(method, url, headers, bytes, &onDone = h, &onError = e)` | Any method, binary body; `h(int status, Blob body)` for every response |
| `Image.fromBlob(blob, mime, &onLoaded = h)` | Image from bytes; `img.free()` releases it |
| `PdfDocument.open(blob, &onOpened = h)`, `doc.renderPage(page, canvas, scale)` | PDF import (`Pdf.setLibrary(lib, worker)` first, see webcc's pdf docs) |
| `PdfWriter.createWriter(&onWritten = h)`, `addPage`, `moveTo`, `lineTo`, `stroke`, `drawImage`, `drawText`, `finish` | PDF export |
| `System.getDevicePixelRatio()`, `System.isOnline()` | Screen and connection |
| `el.setStyle(name, value)`, `el.focus()`, `el.blur()`, `el.getProperty(name)` | Text boxes over a canvas |
| `ws.sendBinary(bytes)`, `ws.closeWithCode(code, reason)`, `ws.getBufferedAmount()` | WebSocket extras |

## Platform enums and flags

Values the browser picks from a fixed set come in two kinds.

**Enums** are choices: exactly one applies. They are ordinary Coi enums, so `::`, `match` and the enum type checks all work:

```tsx
def onPtr(PointerPhase phase, int id, PointerType type) : void {
    if (phase == PointerPhase::Down && type == PointerType::Pen) { ... }
}

bool open = ws.getReadyState() == ReadyState::Open;
```

Comparing two different enums is a compile error, even where the numbers would match (`PointerPhase::Down` and `PointerType::Mouse` are both 0).

**Flags** are bits that combine. A flags type behaves as its integer, with each bit as a constant:

```tsx
surface.addPointerListener(PointerFlags.Capture | PointerFlags.Coalesced | PointerFlags.NoScroll);

def onKey(..., Mods mods, ...) : void {
    bool shortcut = (mods & (Mods.Ctrl | Mods.Meta)) != 0;
}
```

| Type | Kind | Values |
|------|------|--------|
| `PointerPhase` | enum | `Down`, `Move`, `Up`, `Cancel`, `Predicted` |
| `PointerType` | enum | `Mouse`, `Pen`, `Touch` |
| `ReadyState` | enum | `Connecting`, `Open`, `Closing`, `Closed` |
| `LineCap`, `LineJoin` | enum | `Butt`, `Round`, `Square` / `Miter`, `Round`, `Bevel` |
| `PointerFlags` | flags | `Capture`, `Coalesced`, `NoScroll`, `Predict` |
| `Buttons` | flags | `Primary`, `Secondary`, `Auxiliary`, `Back`, `Forward`, `Eraser` |
| `Mods` | flags | `Shift`, `Ctrl`, `Alt`, `Meta` |
| `ContextFlags` | flags | `LowLatency`, `Opaque` |

These names are taken: an enum of your own can't be called `ReadyState`. An unknown value is a compile error that lists the real ones.

## Available APIs

| Module       | Description                                      |
|--------------|--------------------------------------------------|
| `Canvas`     | 2D drawing, paths, text, images, transformations |
| `Image`      | Image loading for canvas rendering               |
| `Audio`      | Audio playback, volume, looping, playback position |
| `Storage`    | Local storage (setItem, getItem, removeItem, clear) |
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
