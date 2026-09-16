# Agent Integration

The emulator exposes a control surface for AI agents over the **Model Context Protocol (MCP)** and the **AG-UI** event protocol. An agent such as Claude Code can insert disks, type BASIC, assemble 6502, read the screen, inspect memory and reconfigure slots -- everything a person can do through the UI.

---

## Table of Contents

- [Architecture](#architecture)
- [Multiple Emulators](#multiple-emulators)
- [Configuration](#configuration)
- [The Sandbox](#the-sandbox)
- [MCP Server Tools](#mcp-server-tools)
- [Frontend Tools](#frontend-tools)
- [Data Flow](#data-flow)

---

## Architecture

Two coordinated pieces:

| Piece | Where | Role |
|-------|-------|------|
| **MCP server** | `../appleii-agent/` (separate Node package) | Provides MCP tools over stdio, plus an HTTP/HTTPS server on port 3033 implementing AG-UI over SSE |
| **Agent manager** | `src/js/agent/agent-manager.js` | Browser-side AG-UI client: connects, receives tool calls over SSE, executes them, posts results back |

The browser is the client. The server never reaches into the page; it publishes tool calls that a connected emulator picks up.

## Multiple Emulators

Several browser tabs can connect at once. Each is assigned a name from a pool, stored in `sessionStorage` so it survives a server restart within the same tab.

Tools take an optional `emulator` parameter:

| Value | Behaviour |
|-------|-----------|
| `"Name"` | Target that emulator |
| `"all"` | Broadcast to every connected emulator, where the tool supports it |
| omitted, one connected | Use it |
| omitted, several connected | Use the one marked default |
| omitted, several, no default | The agent is asked to choose |

The first tab to connect becomes the default; `set_default_emulator` changes it and `list_connections` shows the current state. Double-clicking the emulator name on the sparkle button renames it inline (Unicode letters, hyphens and underscores -- no digits or spaces).

## Configuration

`.mcp.json` at the repo root configures the client:

- **Recommended:** `bunx -y @retrotech71/appleii-agent`
- **Development:** `node /path/to/appleii-agent/src/index.js`

Environment variables: `PORT` (default 3033), `HTTPS=true` for TLS, and `APPLEII_AGENT_SANDBOX` pointing at a sandbox config.

## The Sandbox

**Every file operation is gated by a sandbox config.** Without one the agent starts but all file access is blocked -- that is the intended default, not a misconfiguration.

`~/.appleii/sandbox.config`:

```
# Comments start with #
[demos]@~/AppleII/demos
[work]@/Users/me/projects/apple
```

Keys are alphanumeric with underscores and hyphens; paths are absolute or `~`-relative. Wire it up with:

```json
"env": { "APPLEII_AGENT_SANDBOX": "/path/to/sandbox.config" }
```

Tool calls then use `[key]/relative/path/file`. Path traversal (`../`) and paths outside every configured directory are rejected, and save tools default to `overwrite: false`. `reload_sandbox` picks up edits without restarting the client.

## MCP Server Tools

**Server and connection**

| Tool | Description |
|------|-------------|
| `server_control` | Start, stop or restart the agent server |
| `set_https` | Toggle HTTPS mode |
| `set_debug` | Set debug logging level |
| `get_state` | Current server and emulator state |
| `get_version` | Agent version |
| `reload_sandbox` | Reload `sandbox.config` |
| `disconnect_clients` | Disconnect all SSE clients |
| `shutdown_remote_server` | Shut down another instance on the same port |
| `list_connections` | List connected emulators |
| `set_default_emulator` | Choose the default target |

**Files into the emulator**

| Tool | Description |
|------|-------------|
| `load_disk_image` | Read a `.dsk`/`.do`/`.po`/`.nib`/`.woz` from the sandbox as base64 |
| `load_smartport_image` | Read a `.hdv`/`.po`/`.2mg` as base64 |
| `load_file` | Read any file as base64 or text |

**Files out of the emulator**

| Tool | Description |
|------|-------------|
| `get_screenshot` | Capture the screen as MCP image content the model can actually see |
| `save_to` | Save from an emulator source to a sandbox path |

`save_to` sources: `basic-editor`, `asm-editor`, `basic-memory`, `file-explorer`, `memory-range`, `screen`, `raw`.

**Generic**

`emma_command` delegates to any frontend tool over AG-UI. Window management (`showWindow`, `hideWindow`, `focusWindow`) is a frontend tool, so it goes through `emma_command` rather than being an MCP tool of its own.

## Frontend Tools

Registered in `src/js/agent/agent-tools.js`, grouped by area:

| Group | File | Examples |
|-------|------|----------|
| Emulator control | `main-tools.js` | `emulatorPower`, `emulatorCtrlReset`, `emulatorReboot`, `directLoadBinaryAt`, `directSaveBinaryRangeTo`, `captureScreenshot`, `captureScreenText` |
| BASIC | `basic-program-tools.js` | `directReadBasic`, `directWriteBasic`, `directRunBasic`, `basicProgramRun`, `basicProgramRenumber`, `basicProgramFormat`, `basicProgramLoadFile` |
| Assembler | `assembler-tools.js` | `asmAssemble`, `asmWrite`, `asmGet`/`asmSet`, `asmGetStatus`, `asmLoadFile`, `directExecuteAssemblyAt` |
| Disks | `disk-tools.js` | `driveInsertDisc`, `driveRecentsList`, `driveInsertRecent`, `drivesClearRecent` |
| SmartPort | `smartport-tools.js` | `smartportInsertImage`, `smartportRecentsList`, `smartportInsertRecent` |
| File explorer | `file-explorer-tools.js` | `listDiskFiles`, `getDiskFileContent` |
| Windows | `window-tools.js` | `showWindow`, `hideWindow`, `focusWindow` |
| Slots | `slot-tools.js` | `slotsListAll`, `slotsInstallCard`, `slotsRemoveCard`, `slotsMoveCard` |

`basicProgramLoadFile` and `asmLoadFile` are worth knowing about: they load a sandbox file into the editor **server-side**, so a long source file never passes through the model's context. Paired with `save_to`, an agent can round-trip a program it never had to read.

## Data Flow

1. The agent calls an MCP tool (say `load_disk_image`). The server reads the file and returns base64.
2. The agent calls a frontend tool (`driveInsertDisc`) via `emma_command`; AG-UI delivers the call to the browser over SSE.
3. The frontend decodes the data and drives the disk manager and WASM exports.
4. The result is posted back to `/tool-result`.

Frontend tools reach the emulator through `WasmProxy`, so they are subject to the same rules as any other main-thread code -- see [[Worker-Architecture]]. Changes to the WASM exports these tools depend on (`_readMemory`, `_setSlotCard`, `_getProDOSCatalog` and friends) need the tools updated to match.

---

## See Also

- [[Architecture-Overview]] -- where the agent layer sits
- [[Worker-Architecture]] -- how tools reach the emulator
- [[Debugger]] -- the same capabilities from the UI

## Machines other than the //e

The frontend tools and the MCP server are machine-agnostic where the core is: powering on, typing, screenshots, screen text, disk and SmartPort images, the printers and the window tools all work whichever machine is running, because the WASM interface routes them to whichever coordinator is live.

Two areas are still shaped around the Apple II family's `Emulator`, and do nothing useful while an Apple IIgs is running:

- **The BASIC and assembler tools.** They read and write Applesoft in the //e's memory map and assemble 65C02, neither of which describes a IIgs.
- **The slot tools.** A IIgs's core does not answer a request to change a slot, and a //c has no sockets, so `slotsInstallCard` and friends apply to the //e and the II Plus.

See [[Machines]] for what each machine has, and [[Apple-IIgs]] for the parts of the host that still assume an 8-bit machine.
