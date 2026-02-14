import { defineConfig } from "vite";
import { resolve } from "path";
import { copyFileSync, mkdirSync } from "fs";
import { serialProxyPlugin } from "./plugins/serial-proxy-plugin.js";

// Plugin to copy audio worklet file (can't be bundled)
const copyAudioWorklet = () => ({
  name: "copy-audio-worklet",
  writeBundle() {
    mkdirSync(resolve(__dirname, "dist"), { recursive: true });
    copyFileSync(
      resolve(__dirname, "src/js/audio/audio-worklet.js"),
      resolve(__dirname, "dist/audio-worklet.js"),
    );
  },
});

export default defineConfig({
  root: "public",
  publicDir: "../public",

  server: {
    port: 3000,
    open: true,
    headers: {
      // Required for SharedArrayBuffer (if needed for AudioWorklet)
      "Cross-Origin-Opener-Policy": "same-origin",
      "Cross-Origin-Embedder-Policy": "require-corp",
      "Cache-Control": "no-store",
    },
  },

  build: {
    outDir: "../dist",
    emptyOutDir: true,
    rollupOptions: {
      input: {
        main: resolve(__dirname, "public/index.html"),
      },
      output: {
        manualChunks: {
          debug: [
            "/src/js/debug/cpu-debugger-window.js",
            "/src/js/debug/memory-browser-window.js",
            "/src/js/debug/memory-heat-map-window.js",
            "/src/js/debug/memory-map-window.js",
            "/src/js/debug/stack-viewer-window.js",
            "/src/js/debug/zero-page-watch-window.js",
            "/src/js/debug/soft-switch-window.js",
            "/src/js/debug/mockingboard-window.js",
            "/src/js/debug/mouse-card-window.js",
            "/src/js/debug/basic-program-window.js",
            "/src/js/debug/rule-builder-window.js",
            "/src/js/debug/assembler-editor-window.js",
          ],
          display: [
            "/src/js/display/index.js",
            "/src/js/display/webgl-renderer.js",
            "/src/js/display/display-settings-window.js",
            "/src/js/display/screen-window.js",
          ],
          "disk-manager": [
            "/src/js/disk-manager/index.js",
            "/src/js/disk-manager/disk-operations.js",
            "/src/js/disk-manager/disk-persistence.js",
            "/src/js/disk-manager/disk-surface-renderer.js",
            "/src/js/disk-manager/disk-drives-window.js",
            "/src/js/disk-manager/drive-sounds.js",
          ],
          "file-explorer": [
            "/src/js/file-explorer/index.js",
            "/src/js/file-explorer/dos33.js",
            "/src/js/file-explorer/prodos.js",
            "/src/js/file-explorer/disassembler.js",
            "/src/js/file-explorer/file-viewer.js",
            "/src/js/file-explorer/utils.js",
          ],
        },
      },
    },
  },

  resolve: {
    alias: {
      "/src": resolve(__dirname, "src"),
    },
  },

  // Handle WASM files
  assetsInclude: ["**/*.wasm"],

  optimizeDeps: {
    exclude: ["a2e.js"],
  },

  plugins: [serialProxyPlugin(), copyAudioWorklet()],
});
