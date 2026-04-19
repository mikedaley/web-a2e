// Service Worker for Apple //e Emulator
// Enables offline functionality by caching app assets

// IMPORTANT: Bump this version when WASM or core JS files change
const CACHE_VERSION = 3.5;
const CACHE_NAME = `a2e-cache-v${CACHE_VERSION}`;

// Files that should always be fetched fresh (network-first)
const NETWORK_FIRST_FILES = ["/a2e.js", "/a2e.wasm"];

// Assets to cache on install
const PRECACHE_ASSETS = [
  "/",
  "/index.html",
  "/manifest.json",
  "/a2e.js",
  "/a2e.wasm",
  "/audio-worklet.js",
  "/css/base.css",
  "/css/layout.css",
  "/css/monitor.css",
  "/css/disk-drives.css",
  "/css/controls.css",
  "/css/modals.css",
  "/css/debug-windows.css",
  "/css/file-explorer.css",
  "/css/documentation.css",
  "/css/responsive.css",
  "/assets/drive-open.png",
];

// Install event - cache core assets
self.addEventListener("install", (event) => {
  event.waitUntil(
    caches
      .open(CACHE_NAME)
      .then((cache) => {
        console.log("[SW] Precaching app assets");
        return cache.addAll(PRECACHE_ASSETS);
      })
      .then(() => {
        // Activate immediately without waiting
        return self.skipWaiting();
      })
      .catch((error) => {
        console.error("[SW] Precache failed:", error);
      }),
  );
});

// Activate event - clean up old caches
self.addEventListener("activate", (event) => {
  event.waitUntil(
    caches
      .keys()
      .then((cacheNames) => {
        return Promise.all(
          cacheNames
            .filter((name) => name !== CACHE_NAME)
            .map((name) => {
              console.log("[SW] Deleting old cache:", name);
              return caches.delete(name);
            }),
        );
      })
      .then(() => {
        // Take control of all pages immediately
        return self.clients.claim();
      }),
  );
});

// Check if URL should use network-first strategy
function isNetworkFirst(url) {
  // Explicit network-first files
  if (
    NETWORK_FIRST_FILES.some(
      (file) => url.pathname === file || url.pathname.endsWith(file),
    )
  ) {
    return true;
  }
  // Also use network-first for Vite JS bundles (they have hashed names)
  if (url.pathname.includes("/assets/") && url.pathname.endsWith(".js")) {
    return true;
  }
  return false;
}

// Fetch event - serve from cache, fall back to network
self.addEventListener("fetch", (event) => {
  const url = new URL(event.request.url);

  // Skip non-GET requests
  if (event.request.method !== "GET") {
    return;
  }

  // Skip cross-origin requests (fonts, etc.)
  if (url.origin !== self.location.origin) {
    return;
  }

  // Use network-first for critical files (WASM, core JS)
  if (isNetworkFirst(url)) {
    event.respondWith(
      fetch(event.request)
        .then((networkResponse) => {
          if (networkResponse && networkResponse.status === 200) {
            // Update cache with fresh response
            const responseToCache = networkResponse.clone();
            caches.open(CACHE_NAME).then((cache) => {
              cache.put(event.request, responseToCache);
            });
          }
          return networkResponse;
        })
        .catch(() => {
          // Network failed, try cache as fallback
          return caches.match(event.request);
        }),
    );
    return;
  }

  // Cache-first for other assets
  event.respondWith(
    caches.match(event.request).then((cachedResponse) => {
      if (cachedResponse) {
        // Return cached response
        return cachedResponse;
      }

      // Not in cache - fetch from network
      return fetch(event.request)
        .then((networkResponse) => {
          // Don't cache non-successful responses
          if (!networkResponse || networkResponse.status !== 200) {
            return networkResponse;
          }

          // Cache the new response for future use
          // Clone because response can only be consumed once
          const responseToCache = networkResponse.clone();

          caches.open(CACHE_NAME).then((cache) => {
            // Cache JS bundles and other assets dynamically
            if (shouldCache(event.request.url)) {
              cache.put(event.request, responseToCache);
            }
          });

          return networkResponse;
        })
        .catch((error) => {
          console.error("[SW] Fetch failed:", error);
          // Could return an offline fallback page here
          throw error;
        });
    }),
  );
});

// Determine if a URL should be cached dynamically
function shouldCache(url) {
  // Cache JS bundles (Vite generates hashed names)
  if (url.includes("/assets/") && url.endsWith(".js")) {
    return true;
  }
  // Cache images
  if (url.match(/\.(png|jpg|jpeg|gif|svg|webp)$/)) {
    return true;
  }
  // Cache WASM
  if (url.endsWith(".wasm")) {
    return true;
  }
  return false;
}

// Listen for messages from the main app
self.addEventListener("message", (event) => {
  if (event.data === "skipWaiting") {
    self.skipWaiting();
  }
});
