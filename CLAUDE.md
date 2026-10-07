# CLAUDE.md

Guidance for Claude Code in this repository. This file holds what is needed on
every task; the design detail behind each rule lives in `docs/design/` (index
at the end). Read the relevant design doc before changing a subsystem.

## Project Overview

ApplEm: a cycle-accurate Apple II emulator (II Plus, //e, //c and IIgs). This
repository is the browser build: the C++ core compiled to WebAssembly, with a
WebGL front end in vanilla ES6 modules (Vite, no framework).

**The emulation is the `core/` submodule**
([applem-core](https://github.com/mikedaley/applem-core)), shared with the
native macOS app ([applem](https://github.com/mikedaley/applem)). Read
`core/CLAUDE.md` before changing anything in it: the core's rules, ROMs,
tests and design docs live there. A core change is made and committed in the
core repository, pushed there, and then this repository's submodule pointer
is moved to it in a commit of its own here. Never leave this repository
pointing at a core commit that is not pushed.

## Build Commands

```bash
npm install           # Install dependencies
npm run build:wasm    # Fetch the core if needed, build the WASM module (first time and after C++ changes)
npm run dev           # Start dev server at localhost:3000 (hot-reload for JS only)
npm run build         # Full production build (WASM + Vite bundle)
npm run clean         # Clean build artifacts
npm run deploy        # Deploy to the configured rsync target (see .env.deploy.example)
npm test              # JavaScript tests (Vitest)
npm run check         # check:exports + check:core-purity + check:basic-tokens + check:rom-defaults + npm test
npm run generate:basic-tokens  # Regenerate src/js/utils/basic-tokens.js from the core
```

C++ changes need `npm run build:wasm`; JS changes hot-reload. Keep build
parallelism at `-j 4`. The top-level `CMakeLists.txt` builds only the
WebAssembly module: `add_subdirectory(core)` and `src/bindings/`, with the
export list.

## Testing

- **JavaScript**: `npm test` runs `tests/js/` with `vitest.config.js` (kept
  separate from `vite.config.js`). Modules under test are pure logic in plain
  node; a new DOM dependency in one is a smell, not a reason to add jsdom.
- **Consistency checks** (`npm run check`): `check-exports.sh` (every
  `EMSCRIPTEN_KEEPALIVE` is in `EXPORTED_FUNCTIONS` in `CMakeLists.txt` and the
  reverse), the core's `check-core-purity.sh`, and the BASIC token table and
  printer ROM default checks.
- **C++**: the core's Catch2 tests build and run in the core (see
  `core/README.md`).

## Architecture

### Layout

```
core/          the shared emulation core (submodule): src/core, src/host, roms, tests
src/bindings/  wasm_interface.cpp, the WASM export glue
src/js/        browser host (kebab-case files, PascalCase classes)
  main.js, worker/, audio/, display/, disk-manager/, file-explorer/, debug/,
  help/, input/, machine/, state/, ui/, utils/, windows/, agent/, config/
functions/     Cloudflare Pages Functions (CORS proxy for URL-loaded media)
plugins/       Vite plugins (dev proxy plugin, serial proxy plugin)
public/        static assets, built WASM, shaders, the disk library
examples/      BASIC, Merlin and printer programs
docs/design/   the browser's design notes (the core's are in core/docs/design/)
wiki/          mirror of the published GitHub wiki (the published wiki wins)
```

### Rules that apply everywhere

The core's rules are in `core/CLAUDE.md`. The browser adds:

- **Host preferences are not machine state.** Speed multiplier, game port
  device, video standard, Mockingboard phase lock and mono, paste buffer: none
  is in a save state, and `reset()` keeps them.
- **Remembered per machine** in localStorage: slot layout, display settings,
  video standard, ⌘ as Open Apple, autosave.
- **Hidden, not disabled**: menu items a machine cannot use are hidden
  (`src/js/ui/machine-availability.js`).

### Browser host

The WASM module runs in a Web Worker; the main thread talks to it through
`WasmProxy` (`src/js/worker/`). Details in `docs/design/host-runtime.md`.

- Every `_fn()` on the proxy is an async RPC to the thread that runs the
  emulation, so round trips steal emulation time. **One `wasmProxy.batch()`
  per window update**; `callString` for `char*` returns; a loop of RPCs should
  become one C++ export.
- **No direct `HEAPU8`/`HEAPF32` from the main thread**: use `heapRead`,
  `heapWrite` and friends, which return typed arrays.
- `_malloc()` must be awaited; `_free()` is fire-and-forget. New exports go
  in `EXPORTED_FUNCTIONS` in `CMakeLists.txt`.
- **Audio paces the machine**: the AudioWorklet asks for a refill of one
  frame (800 samples) when below two; a free-run clock stands in while the
  `AudioContext` is suspended.
- `SharedArrayBuffer` carries the frame queue and audio ring; **the
  `postMessage` fallback must keep working.** `emulator-worker.js` is a
  classic Worker with its own copy of the frame-queue producer; keep the two
  in step.

### Video

The core makes the picture (see `core/docs/design/display.md`); the browser's
CRT shaders are in `public/shaders/`. The native app's
`native/shaders/crt.metal` ports `crt.glsl`: change one, change the other, and
compare them with `scripts/compare-crt.mjs`.

**Animated shader effects must stay within photosensitive-epilepsy limits**
(no more than three flashes a second or a 10% luminance change).

### UI and theming

- Light, dark and system themes via `ThemeManager` (`data-theme` on
  `<html>`). Accent colours derive from the six-stripe Apple palette: Green
  `#61BB46`, Yellow `#FDB827`, Orange `#F5821F`, Red `#E03A3E`, Purple
  `#963D97`, Blue `#009DDC`.
- Control styles, sizes and layout must be consistent across the app.
- **Window surfaces are opaque with no `backdrop-filter`**: use
  `--glass-bg`, `--glass-bg-solid`, `--glass-bg-header`. A blur over a canvas
  repainting at 60Hz re-blurs every window every frame.

## Design docs

| Doc | Covers |
| --- | ------ |
| `docs/design/host-runtime.md` | URL media parameters, Worker, shared memory, audio timing, free-run clock, WASM interface |
| `docs/design/agent-mcp.md` | MCP server, multi-emulator routing, sandbox, agent tools |
| `core/docs/design/*.md` | Machines, the IIgs, cards, disks, display, input, save states, debugging, the assembler |

When a change alters something a design doc describes, update that doc in the
same change.

## Deployment

`npm run deploy` and `npm run deploy:staging` run `scripts/deploy.sh`, one
rsync of `dist/` to `DEPLOY_TARGET` / `DEPLOY_STAGING_TARGET` from
`.env.deploy` (gitignored; see `.env.deploy.example`). **One SSH session
only**: the host locks out concurrent sessions, so verify over HTTPS.

An optional Cloudflare Pages deployment workflow (`.github/workflows/cloudflare-pages-deploy.yml`)
is opt-in per repository via `vars.CLOUDFLARE_PAGES_ENABLED == 'true'`. It includes CORS
proxy support for URL-loaded media (`functions/proxy/[[path]].js`) mirrored in development by `plugins/dev-proxy-plugin.js`.

## Release Process

When the user says "release":

1. Review the git log since the last release notes entry
2. Bump the version in `src/js/config/version.js`
3. Update release notes in `src/js/help/release-notes.js` (short entries)
4. Update `README.md` for new features, commands or project information
5. Update this file and the relevant `docs/design/` doc for architectural or structural changes
