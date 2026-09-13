/*
 * memory-heat-map-window.js - Real-time memory access heat map visualization
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

import { machineProcessor } from "../machine/machine-profile.js";
import { BaseWindow } from "../windows/base-window.js";

// Memory region labels for main memory
const MAIN_MEMORY_REGIONS = [
  { name: "Zero Page", start: 0x0000, end: 0x00ff },
  { name: "Stack", start: 0x0100, end: 0x01ff },
  { name: "Input Buffer", start: 0x0200, end: 0x02ff },
  { name: "Vectors/Data", start: 0x0300, end: 0x03ff },
  { name: "Text Page 1", start: 0x0400, end: 0x07ff },
  { name: "Text Page 2", start: 0x0800, end: 0x0bff },
  { name: "Free RAM", start: 0x0c00, end: 0x1fff },
  { name: "HiRes Page 1", start: 0x2000, end: 0x3fff },
  { name: "HiRes Page 2", start: 0x4000, end: 0x5fff },
  { name: "Free RAM", start: 0x6000, end: 0x95ff },
  { name: "DOS 3.3", start: 0x9600, end: 0xbfff },
  { name: "I/O Space", start: 0xc000, end: 0xc0ff },
  { name: "Slot ROMs", start: 0xc100, end: 0xcfff },
  { name: "ROM/LC RAM", start: 0xd000, end: 0xffff },
];

// Memory region labels for auxiliary memory
const AUX_MEMORY_REGIONS = [
  { name: "Aux Zero Page", start: 0x0000, end: 0x00ff },
  { name: "Aux Stack", start: 0x0100, end: 0x01ff },
  { name: "Aux $0200", start: 0x0200, end: 0x03ff },
  { name: "Aux Text Page 1", start: 0x0400, end: 0x07ff },
  { name: "Aux Text Page 2", start: 0x0800, end: 0x0bff },
  { name: "Aux RAM", start: 0x0c00, end: 0x1fff },
  { name: "Aux HiRes Page 1", start: 0x2000, end: 0x3fff },
  { name: "Aux HiRes Page 2", start: 0x4000, end: 0x5fff },
  { name: "Aux RAM", start: 0x6000, end: 0xbfff },
  { name: "Aux $C000-$FFFF", start: 0xc000, end: 0xffff },
];

export class MemoryHeatMapWindow extends BaseWindow {
  constructor(wasmModule) {
    super({
      id: "memory-heatmap",
      title: "Memory Heat Map",
      defaultWidth: 580,
      defaultHeight: 435,
      minWidth: 580,
      minHeight: 435,
      maxWidth: 580,
      maxHeight: 435,

    });
    this.wasmModule = wasmModule;
    this.updateEveryNFrames = 6; // ~10fps - heavy pixel work
    this.isTracking = false;
    this.viewMode = "combined"; // combined, reads, writes
    this.decayEnabled = false;
    this.onJumpToAddress = null; // Callback for Memory Browser integration
    this.activeCanvas = "main"; // Which canvas is being hovered
  }

  renderContent() {
    return `
      <div class="heatmap-toolbar">
        <button class="heatmap-toggle-btn">Start</button>
        <button class="heatmap-clear-btn">Clear</button>
        <select class="heatmap-mode-select">
          <option value="combined">Combined</option>
          <option value="reads">Reads Only</option>
          <option value="writes">Writes Only</option>
        </select>
        <label class="heatmap-decay-label">
          <input type="checkbox" class="heatmap-decay-check" />
          Decay
        </label>
      </div>
      <div class="heatmap-dual-container">
        <div class="heatmap-panel">
          <div class="heatmap-panel-title" id="heatmap-title-main">Main RAM + ROM</div>
          <div class="heatmap-canvas-container">
            <canvas class="heatmap-canvas heatmap-canvas-main" width="256" height="256"></canvas>
          </div>
        </div>
        <div class="heatmap-panel">
          <div class="heatmap-panel-title" id="heatmap-title-aux">Auxiliary RAM</div>
          <div class="heatmap-canvas-container">
            <canvas class="heatmap-canvas heatmap-canvas-aux" width="256" height="256"></canvas>
          </div>
        </div>
      </div>
      <div class="heatmap-note" id="heatmap-note" hidden></div>
      <div class="heatmap-legend">
        <span class="heatmap-legend-item"><span class="legend-color reads"></span> Reads</span>
        <span class="heatmap-legend-item"><span class="legend-color writes"></span> Writes</span>
        <span class="heatmap-legend-item"><span class="legend-color both"></span> Both</span>
      </div>
      <div class="heatmap-info">
        <span class="heatmap-addr">$0000</span>
        <span class="heatmap-region">Zero Page</span>
        <span class="heatmap-counts">R: 0 W: 0</span>
      </div>
    `;
  }

  /**
   * Say which memory this is watching.
   *
   * A IIgs's two panels are the Mega II's banks — $E0 and $E1 — because that
   * is the MMU this is tracking, and it is the side that matters: the video,
   * the firmware's workspace and Applesoft all live there. The fast RAM on
   * the other side of the machine is not covered, and the titles say so
   * rather than letting a blank-looking map be read as an idle machine.
   */
  applyProcessor() {
    if (!this.contentElement) return;
    const banked = machineProcessor().hasBanks;
    const main = this.contentElement.querySelector("#heatmap-title-main");
    const aux = this.contentElement.querySelector("#heatmap-title-aux");
    if (main) main.textContent = banked ? "Bank $E0 + ROM" : "Main RAM + ROM";
    if (aux) aux.textContent = banked ? "Bank $E1" : "Auxiliary RAM";
    const note = this.contentElement.querySelector("#heatmap-note");
    if (note) {
      note.textContent = banked
        ? "The Mega II's side only; fast RAM is not tracked."
        : "";
      note.hidden = !banked;
    }
  }

  onMachineChanged() {
    this.applyProcessor();
  }

  onContentRendered() {
    this.applyProcessor();
    this.canvasMain = this.contentElement.querySelector(".heatmap-canvas-main");
    this.canvasAux = this.contentElement.querySelector(".heatmap-canvas-aux");
    this.ctxMain = this.canvasMain.getContext("2d");
    this.ctxAux = this.canvasAux.getContext("2d");
    this.toggleBtn = this.contentElement.querySelector(".heatmap-toggle-btn");
    this.modeSelect = this.contentElement.querySelector(".heatmap-mode-select");
    this.decayCheck = this.contentElement.querySelector(".heatmap-decay-check");
    this.addrSpan = this.contentElement.querySelector(".heatmap-addr");
    this.regionSpan = this.contentElement.querySelector(".heatmap-region");
    this.countsSpan = this.contentElement.querySelector(".heatmap-counts");

    // Pre-allocate image data for both canvases
    this.imageDataMain = this.ctxMain.createImageData(256, 256);
    this.imageDataAux = this.ctxAux.createImageData(256, 256);

    this.setupHeatmapEventListeners();

    // Apply restored state to UI elements
    this.modeSelect.value = this.viewMode;
    this.decayCheck.checked = this.decayEnabled;
  }

  setupHeatmapEventListeners() {
    // Toggle tracking
    this.toggleBtn.addEventListener("click", () => {
      this.isTracking = !this.isTracking;
      this.toggleBtn.textContent = this.isTracking ? "Stop" : "Start";
      this.toggleBtn.classList.toggle("active", this.isTracking);
      this.wasmModule._enableMemoryTracking(this.isTracking);
    });

    // Clear tracking data
    this.contentElement
      .querySelector(".heatmap-clear-btn")
      .addEventListener("click", () => {
        this.wasmModule._clearMemoryTracking();
      });

    // View mode selection
    this.modeSelect.addEventListener("change", (e) => {
      this.viewMode = e.target.value;
      if (this.onStateChange) this.onStateChange();
    });

    // Decay toggle
    this.decayCheck.addEventListener("change", (e) => {
      this.decayEnabled = e.target.checked;
      if (this.onStateChange) this.onStateChange();
    });

    // Canvas hover for main memory
    this.canvasMain.addEventListener("mousemove", (e) => {
      this.activeCanvas = "main";
      const addr = this.getAddressFromEvent(e, this.canvasMain);
      this.showAddressInfo(addr, MAIN_MEMORY_REGIONS, "Main");
    });

    // Canvas hover for aux memory
    this.canvasAux.addEventListener("mousemove", (e) => {
      this.activeCanvas = "aux";
      const addr = this.getAddressFromEvent(e, this.canvasAux);
      this.showAddressInfo(addr, AUX_MEMORY_REGIONS, "Aux");
    });

    this.canvasMain.addEventListener("mouseleave", () =>
      this.clearAddressInfo(),
    );
    this.canvasAux.addEventListener("mouseleave", () =>
      this.clearAddressInfo(),
    );

    // Click to jump to Memory Browser
    this.canvasMain.addEventListener("click", (e) => {
      const addr = this.getAddressFromEvent(e, this.canvasMain);
      if (this.onJumpToAddress) {
        this.onJumpToAddress(addr);
      }
    });

    this.canvasAux.addEventListener("click", (e) => {
      const addr = this.getAddressFromEvent(e, this.canvasAux);
      if (this.onJumpToAddress) {
        this.onJumpToAddress(addr);
      }
    });
  }

  getAddressFromEvent(e, canvas) {
    const rect = canvas.getBoundingClientRect();
    const x = Math.floor(((e.clientX - rect.left) / rect.width) * 256);
    const y = Math.floor(((e.clientY - rect.top) / rect.height) * 256);
    return y * 256 + x;
  }

  clearAddressInfo() {
    this.addrSpan.textContent = "$----";
    this.regionSpan.textContent = "";
    this.countsSpan.textContent = "";
  }

  async showAddressInfo(addr, regions, prefix) {
    if (addr < 0 || addr > 0xffff) return;

    this.addrSpan.textContent = `${prefix} $${this.formatHex(addr, 4)}`;

    // Find region
    for (const region of regions) {
      if (addr >= region.start && addr <= region.end) {
        this.regionSpan.textContent = region.name;
        break;
      }
    }

    // Get counts from WASM memory
    const [readCountsPtr, writeCountsPtr] = await this.wasmModule.batch([
      ['_getMemoryReadCounts'],
      ['_getMemoryWriteCounts'],
    ]);

    if (readCountsPtr && writeCountsPtr) {
      const [readData, writeData] = await Promise.all([
        this.wasmModule.heapRead(readCountsPtr + addr, 1),
        this.wasmModule.heapRead(writeCountsPtr + addr, 1),
      ]);
      this.countsSpan.textContent = `R: ${readData[0]} W: ${writeData[0]}`;
    }
  }

  async update(wasmModule) {
    if (!this.isVisible || !this.canvasMain || !this.canvasAux) return;

    const [readCountsPtr, writeCountsPtr, mainRAMPtr, auxRAMPtr, systemROMPtr] = await wasmModule.batch([
      ['_getMemoryReadCounts'],
      ['_getMemoryWriteCounts'],
      ['_getMainRAM'],
      ['_getAuxRAM'],
      ['_getSystemROM'],
    ]);

    if (!readCountsPtr || !writeCountsPtr) return;

    // Bulk-read all tracking and memory data
    const [readCounts, writeCounts, mainRAM, systemROM, auxRAM] = await Promise.all([
      wasmModule.heapRead(readCountsPtr, 65536),
      wasmModule.heapRead(writeCountsPtr, 65536),
      mainRAMPtr ? wasmModule.heapRead(mainRAMPtr, 0xc000) : null,
      systemROMPtr ? wasmModule.heapRead(systemROMPtr, 0x4000) : null,
      auxRAMPtr ? wasmModule.heapRead(auxRAMPtr, 65536) : null,
    ]);

    // Update main memory canvas
    this.updateCanvas(
      this.imageDataMain,
      this.ctxMain,
      readCounts,
      writeCounts,
      mainRAM,
      systemROM,
    );

    // Update aux memory canvas
    this.updateCanvasAux(
      this.imageDataAux,
      this.ctxAux,
      readCounts,
      writeCounts,
      auxRAM,
    );

    // Apply decay if enabled
    if (this.decayEnabled && this.isTracking) {
      wasmModule._decayMemoryTracking(2);
    }
  }

  /**
   * Render the memory heat map for main RAM and system ROM.
   *
   * Color encoding scheme:
   * - Background brightness: Based on memory content value (0-255 → visible gray level).
   *   This shows the actual memory contents.
   * - Read activity: Blue/cyan channel. Higher read count = brighter blue.
   * - Write activity: Red/orange channel. Higher write count = brighter red.
   * - Combined read+write: Purple/magenta blend.
   *
   * Each pixel represents one memory address. The canvas is 256x256 = 65536 pixels,
   * covering the entire 64KB address space. Addresses are laid out left-to-right,
   * top-to-bottom (address 0 at top-left, address 65535 at bottom-right).
   *
   * Memory regions:
   * - $0000-$BFFF: Main RAM (48KB)
   * - $C000-$CFFF: I/O and soft switches
   * - $D000-$FFFF: ROM or bank-switched RAM
   */
  updateCanvas(
    imageData,
    ctx,
    readCounts,
    writeCounts,
    mainRAM,
    systemROM,
  ) {
    const data = imageData.data;

    for (let addr = 0; addr < 65536; addr++) {
      const readCount = readCounts[addr];
      const writeCount = writeCounts[addr];

      // Get memory content for background brightness
      let memValue = 0;
      if (mainRAM && addr < 0xc000) {
        memValue = mainRAM[addr];
      } else if (systemROM && addr >= 0xc000) {
        memValue = systemROM[addr - 0xc000];
      }

      const pixelIndex = addr * 4;

      // Base background: visible gray based on memory content (20-60 range)
      const bgLevel = 20 + Math.floor(memValue * 0.16);

      let r, g, b;

      // Scale activity to be more visible (multiply by 3, cap at 200)
      const readIntensity = Math.min(200, readCount * 3);
      const writeIntensity = Math.min(200, writeCount * 3);

      switch (this.viewMode) {
        case "reads":
          // Apple Blue (#18ABEA) for reads
          r = Math.min(255, bgLevel + Math.floor(readIntensity * 0.1));
          g = Math.min(255, bgLevel + Math.floor(readIntensity * 0.73));
          b = Math.min(255, bgLevel + readIntensity);
          break;
        case "writes":
          // Apple Orange (#F68D35) for writes
          r = Math.min(255, bgLevel + writeIntensity);
          g = Math.min(255, bgLevel + Math.floor(writeIntensity * 0.57));
          b = Math.min(255, bgLevel + Math.floor(writeIntensity * 0.22));
          break;
        case "combined":
        default:
          if (readCount > 0 && writeCount > 0) {
            // Apple Purple (#B55DB6) for both
            r = Math.min(255, bgLevel + writeIntensity);
            g = Math.min(
              255,
              bgLevel +
                Math.floor(Math.min(readIntensity, writeIntensity) * 0.51),
            );
            b = Math.min(255, bgLevel + readIntensity);
          } else if (readCount > 0) {
            // Apple Blue (#18ABEA) for reads only
            r = Math.min(255, bgLevel + Math.floor(readIntensity * 0.1));
            g = Math.min(255, bgLevel + Math.floor(readIntensity * 0.73));
            b = Math.min(255, bgLevel + readIntensity);
          } else if (writeCount > 0) {
            // Apple Orange (#F68D35) for writes only
            r = Math.min(255, bgLevel + writeIntensity);
            g = Math.min(255, bgLevel + Math.floor(writeIntensity * 0.57));
            b = Math.min(255, bgLevel + Math.floor(writeIntensity * 0.22));
          } else {
            // No activity - gray background
            r = bgLevel;
            g = bgLevel;
            b = bgLevel;
          }
          break;
      }

      data[pixelIndex] = r;
      data[pixelIndex + 1] = g;
      data[pixelIndex + 2] = b;
      data[pixelIndex + 3] = 255;
    }

    ctx.putImageData(imageData, 0, 0);
  }

  updateCanvasAux(imageData, ctx, readCounts, writeCounts, auxRAM) {
    const data = imageData.data;

    for (let addr = 0; addr < 65536; addr++) {
      // Note: tracking counts are for the main address space
      // Aux memory accesses happen when RAMRD/RAMWRT are set
      // For now, show aux memory content with dimmed tracking overlay
      const readCount = readCounts[addr];
      const writeCount = writeCounts[addr];

      // Get aux memory content
      let memValue = 0;
      if (auxRAM) {
        memValue = auxRAM[addr];
      }

      const pixelIndex = addr * 4;

      // Base background: visible gray based on memory content (20-60 range)
      const bgLevel = 20 + Math.floor(memValue * 0.16);

      let r, g, b;

      // Scale activity (dimmed for aux since tracking is address-based)
      const readIntensity = Math.min(200, readCount * 2);
      const writeIntensity = Math.min(200, writeCount * 2);

      switch (this.viewMode) {
        case "reads":
          // Apple Blue (#18ABEA) for reads
          r = Math.min(255, bgLevel + Math.floor(readIntensity * 0.1));
          g = Math.min(255, bgLevel + Math.floor(readIntensity * 0.73));
          b = Math.min(255, bgLevel + readIntensity);
          break;
        case "writes":
          // Apple Orange (#F68D35) for writes
          r = Math.min(255, bgLevel + writeIntensity);
          g = Math.min(255, bgLevel + Math.floor(writeIntensity * 0.57));
          b = Math.min(255, bgLevel + Math.floor(writeIntensity * 0.22));
          break;
        case "combined":
        default:
          if (readCount > 0 && writeCount > 0) {
            // Apple Purple (#B55DB6) for both
            r = Math.min(255, bgLevel + writeIntensity);
            g = Math.min(
              255,
              bgLevel +
                Math.floor(Math.min(readIntensity, writeIntensity) * 0.51),
            );
            b = Math.min(255, bgLevel + readIntensity);
          } else if (readCount > 0) {
            // Apple Blue (#18ABEA) for reads only
            r = Math.min(255, bgLevel + Math.floor(readIntensity * 0.1));
            g = Math.min(255, bgLevel + Math.floor(readIntensity * 0.73));
            b = Math.min(255, bgLevel + readIntensity);
          } else if (writeCount > 0) {
            // Apple Orange (#F68D35) for writes only
            r = Math.min(255, bgLevel + writeIntensity);
            g = Math.min(255, bgLevel + Math.floor(writeIntensity * 0.57));
            b = Math.min(255, bgLevel + Math.floor(writeIntensity * 0.22));
          } else {
            // No activity - gray background
            r = bgLevel;
            g = bgLevel;
            b = bgLevel;
          }
          break;
      }

      data[pixelIndex] = r;
      data[pixelIndex + 1] = g;
      data[pixelIndex + 2] = b;
      data[pixelIndex + 3] = 255;
    }

    ctx.putImageData(imageData, 0, 0);
  }

  setJumpCallback(callback) {
    this.onJumpToAddress = callback;
  }

  getState() {
    const base = super.getState();
    base.viewMode = this.viewMode;
    base.decayEnabled = this.decayEnabled;
    return base;
  }

  restoreState(state) {
    if (state.viewMode) {
      this.viewMode = state.viewMode;
    }
    if (state.decayEnabled !== undefined) {
      this.decayEnabled = state.decayEnabled;
    }
    super.restoreState(state);
  }

  hide() {
    if (this.isTracking) {
      this.isTracking = false;
      this.toggleBtn.textContent = "Start";
      this.toggleBtn.classList.remove("active");
      this.wasmModule._enableMemoryTracking(false);
    }
    super.hide();
  }
}
