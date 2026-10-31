/*
 * dev-proxy-plugin.js - Vite plugin serving the /proxy/url CORS proxy locally
 *
 * The deployed Cloudflare Pages site routes /proxy/url/<encoded> through a
 * Pages Function (functions/proxy/[[path]].js), which is what lets the URL
 * media loader fall back to a proxy when a host sends no CORS headers. A plain
 * `vite` dev server runs no Pages Functions, so that same fallback pointed at
 * localhost would 404. This middleware reimplements just enough of the function
 * to make `npm run dev` behave like production for the URL media feature.
 */

function isValidUrl(urlString) {
  try {
    const url = new URL(urlString);
    return url.protocol === "http:" || url.protocol === "https:";
  } catch {
    return false;
  }
}

export function devProxyPlugin() {
  return {
    name: "dev-proxy",

    configureServer(server) {
      server.middlewares.use(async (req, res, next) => {
        const prefix = "/proxy/url/";
        if (!req.url || !req.url.startsWith(prefix)) return next();

        const encoded = req.url.slice(prefix.length).split("?")[0];
        const target = decodeURIComponent(encoded);

        if (!isValidUrl(target)) {
          res.statusCode = 400;
          res.end("Invalid URL format");
          return;
        }

        try {
          const upstream = await fetch(target, {
            headers: { "User-Agent": "Mozilla/5.0 (compatible; Apple2-Emulator/1.0)" },
            redirect: "follow",
          });

          if (!upstream.ok) {
            res.statusCode = upstream.status;
            res.end("Upstream error");
            return;
          }

          const buffer = Buffer.from(await upstream.arrayBuffer());
          res.statusCode = 200;
          res.setHeader("Content-Type", upstream.headers.get("content-type") || "application/octet-stream");
          res.setHeader("Content-Length", buffer.length);
          res.setHeader("Access-Control-Allow-Origin", "*");
          res.setHeader("Cache-Control", "no-store");
          res.end(buffer);
        } catch {
          res.statusCode = 502;
          res.end("Proxy error");
        }
      });
    },
  };
}
