import { defineConfig } from "vite";
import { resolve } from "path";
import { copyFileSync, mkdirSync, readFileSync, writeFileSync } from "fs";
import { serialProxyPlugin } from "./plugins/serial-proxy-plugin.js";
import { devProxyPlugin } from "./plugins/dev-proxy-plugin.js";

// Stamp the app version into the service worker's cache name.
//
// The service worker precaches stable-named assets — the shaders, the audio
// worklet, the emulator worker — cache-first, keyed on CACHE_VERSION. Those
// names never change between builds, so a returning browser keeps serving the
// old copy until the cache name changes. The file asked you to remember to bump
// it by hand, and 1.1.12 shipped a rewritten crt.glsl that nobody could see
// because nobody did. Deriving it from the version that is bumped every release
// removes the chance to forget.
const stampServiceWorkerVersion = () => ({
  name: "stamp-service-worker-version",
  writeBundle() {
    const versionSrc = readFileSync(
      resolve(__dirname, "src/js/config/version.js"),
      "utf8",
    );
    const version = versionSrc.match(/VERSION\s*=\s*"([^"]+)"/)?.[1];
    if (!version) {
      throw new Error("stamp-service-worker-version: no VERSION in version.js");
    }

    const swPath = resolve(__dirname, "dist/sw.js");
    const sw = readFileSync(swPath, "utf8");
    const stamped = sw.replace(
      /const CACHE_VERSION = [^;]+;/,
      `const CACHE_VERSION = "${version}";`,
    );
    if (stamped === sw) {
      throw new Error("stamp-service-worker-version: CACHE_VERSION not found");
    }
    writeFileSync(swPath, stamped);
  },
});

// Plugin to copy audio worklet and emulator worker files (can't be bundled)
const copyWorkerFiles = () => ({
  name: "copy-worker-files",
  writeBundle() {
    mkdirSync(resolve(__dirname, "dist"), { recursive: true });
    copyFileSync(
      resolve(__dirname, "src/js/audio/audio-worklet.js"),
      resolve(__dirname, "dist/audio-worklet.js"),
    );
    copyFileSync(
      resolve(__dirname, "src/js/worker/emulator-worker.js"),
      resolve(__dirname, "dist/emulator-worker.js"),
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
            "/src/js/file-explorer/disassembler.js",
            "/src/js/file-explorer/file-viewer.js",
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

  plugins: [
    serialProxyPlugin(),
    devProxyPlugin(),
    copyWorkerFiles(),
    stampServiceWorkerVersion(),
  ],
});
