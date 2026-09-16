# Worker Architecture

The WASM emulator runs in a dedicated Web Worker so the main thread stays free for rendering, input and UI. This page describes the split, the RPC layer, and the shared-memory transport.

If you are writing code that talks to the emulator, the short version is: **every WASM call returns a Promise, direct heap access from the main thread is forbidden, and every round-trip you make steals emulation time.**

---

## Table of Contents

- [The Split](#the-split)
- [WasmProxy](#wasmproxy)
- [Calling Conventions](#calling-conventions)
- [Shared Memory Transport](#shared-memory-transport)
- [The postMessage Fallback](#the-postmessage-fallback)
- [Cross-Origin Isolation](#cross-origin-isolation)
- [Performance Rules](#performance-rules)

---

## The Split

```
Main Thread                    Worker Thread                AudioWorklet Thread
-----------                    -------------                -------------------
WasmProxy (ES6 Proxy)  <-msg-> emulator-worker.js           audio-worklet.js
  - WebGL renderer               - WASM module                - reads shared ring
  - Debug windows                - audio generation           - requests refill
  - Input capture                - framebuffer write            when buffer low
  - Agent tools                  - RPC handler
        ^                              |                            ^
        +---- SharedArrayBuffer: framebuffer (2 slots) + control ----+
                             audio ring buffer
```

| File | Role |
|------|------|
| `src/js/worker/wasm-proxy.js` | ES6 Proxy on the main thread; turns `_functionName()` into async RPC |
| `src/js/worker/emulator-worker.js` | The Worker itself: loads WASM, handles RPC, generates audio |
| `src/js/worker/rpc-protocol.js` | Shared message-type constants |
| `src/js/worker/shared-buffers.js` | `SharedArrayBuffer` layouts, allocation, control-block offsets |

`emulator-worker.js` is a **classic** Worker rather than a module Worker, because it uses `importScripts` to load the Emscripten glue.

## WasmProxy

`WasmProxy` intercepts property access. Reading `wasmProxy._getPC` returns a function that posts an `MSG_RPC_CALL` to the Worker and resolves when the result arrives:

```javascript
const pc = await wasmProxy._getPC();
```

Calls that only *change* state and have no return value worth waiting for -- input, control, memory writes -- are **fire-and-forget**. They post without awaiting a response:

```javascript
wasmProxy._keyDown(keyCode);   // no await needed
wasmProxy._setPaused(true);
wasmProxy._writeMemory(addr, value);
```

## Calling Conventions

| Need | Use | Notes |
|------|-----|-------|
| One value | `await wasmProxy._getPC()` | One round-trip |
| Many values | `await wasmProxy.batch([['_getPC'], ['_getA'], ...])` | **One** round-trip for all of them |
| A `char*` return | `await wasmProxy.callString(fn, ...args)` | Decoded in the Worker; one round-trip, not two |
| Read heap bytes | `await wasmProxy.heapRead(ptr, size)` | Returns a `Uint8Array` |
| Read heap words | `heapReadU32`, `heapReadF32`, `heapDataViewU32` | Typed arrays, transferred not copied |
| Write heap | `await wasmProxy.heapWrite(ptr, data)` | |
| Send a disk image | `wasmProxy.transfer(...)` | Zero-copy ownership transfer |
| Allocate | `const p = await wasmProxy._malloc(n)` | Must be awaited |
| Free | `wasmProxy._free(p)` | Fire-and-forget |

**Never** touch `HEAPU8` or `HEAPF32` from the main thread -- the heap is in the Worker. The helpers above return **typed arrays** and transfer their buffers; do not box heap data into plain `Array`s, which defeats the transfer.

`stringToUTF8()` and `UTF8ToString()` are async for the same reason.

Pause state is an exception to the round-trip rule: the Worker pushes `MSG_PAUSE_STATE` whenever it changes and the proxy caches it on `wasmProxy.isPaused`, so per-frame code reads it synchronously instead of awaiting `_isPaused()`.

## Shared Memory Transport

When `SharedArrayBuffer` is available, `main.js:setupSharedBuffers()` allocates three regions and both the framebuffer and audio bypass `postMessage` entirely.

**Framebuffer** -- double-buffered (`FB_SLOTS = 2`). The Worker writes the slot the renderer is not reading, publishes the index in `CTRL_FRAME_INDEX`, then sets `CTRL_FRAME_READY`. The main thread's `pollSharedFrame()` claims it with `Atomics.exchange`, so a frame is never uploaded twice. This replaced allocating a fresh 860KB array every frame.

The slot is **the one place that still fixes a size**: `FB_WIDTH` x `FB_HEIGHT` is 848x480, and every machine writes its own picture into it — a //e's 560x384, a IIgs's 736x448 raster. A `SharedArrayBuffer` cannot be resized once it has been handed to the Worker and the AudioWorklet, so it is allocated up front and must hold any machine's frame. `setupSharedBuffers()` checks the fit and falls back to the `postMessage` transport rather than let a frame write past the end of the slot.

**Audio ring** -- 16,384 stereo frames of interleaved L/R floats. The AudioWorklet reads it directly, so the main thread is no longer in the audio critical path; only the small refill request routes through it.

**Control block** -- an `Int32Array` of status fields (`CTRL_*` in `shared-buffers.js`):

| Field | Purpose |
|-------|---------|
| `CTRL_FRAME_READY`, `CTRL_FRAME_INDEX` | Frame handoff |
| `CTRL_IS_PAUSED` | Pause state |
| `CTRL_PC`, `CTRL_A`, `CTRL_X`, `CTRL_Y`, `CTRL_SP`, `CTRL_P` | CPU registers |
| `CTRL_BEAM_SCANLINE`, `CTRL_BEAM_HPOS`, `CTRL_BEAM_COLUMN` | Video beam position |

Only the pause and frame fields are consumed today. The register fields are groundwork for removing debug-window RPCs entirely -- a debugger that reads registers straight out of shared memory costs the emulation nothing.

## The postMessage Fallback

Without `SharedArrayBuffer` the Worker posts frames and audio samples for the main thread to relay. **This path must keep working -- do not delete it.**

It is materially worse, and it is worth understanding why. Audio paces the emulation, so putting a busy main thread in the audio path does not merely cause crackle: it causes *speed instability*. The emulator runs fast or slow depending on how contended the main thread is.

## Cross-Origin Isolation

`SharedArrayBuffer` requires the document to be cross-origin isolated, which requires two response headers:

```
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

Vite sets these for the dev server. **In production they are the deployment's responsibility.** If they are missing the app silently drops to the fallback path above -- everything works, just less smoothly, with no error to notice.

To check a deployment:

```javascript
self.crossOriginIsolated   // must be true
typeof SharedArrayBuffer   // must be "function"
```

Under `require-corp`, cross-origin subresources must be CORS-eligible or send `Cross-Origin-Resource-Policy`. Same-origin resources need nothing.

## Performance Rules

The Worker services RPCs on the same thread that runs the emulation. Every round-trip is emulation time you do not get back.

1. **One batch per window update.** `CPUDebuggerWindow.update()` is the reference example: a single 25-call `batch()`, indexed through an `UPDATE_BATCH` constant. Sequential awaits in an update loop are the classic mistake.
2. **Bulk work belongs in C++.** A loop making one RPC per iteration should become one export. `_disassembleRange` and `_getBasicHeatMapData` exist for this reason.
3. **Do not await in the render loop.** The `requestAnimationFrame` callback is synchronous by design. Awaiting anything there pushes the texture upload and draw into a microtask after a Worker round-trip, which misses vsync deadlines precisely when the Worker is busiest.
4. **Prefer pushed state to polled state.** Pause state is pushed and cached; the control block exists so more can follow.

---

## See Also

- [[Architecture-Overview]] -- how the Worker fits the wider design
- [[Audio-System]] -- the audio ring and AudioWorklet
- [[Video-Rendering]] -- what happens to a frame after it is claimed
