## Overview

This PR lets `?disk=` / `?hd=` links load disk and hard-drive images from hosts that send **no `Access-Control-Allow-Origin`** header — the common case for archive mirrors and plain web servers like Asimov — while keeping everything working for hosts that already allow cross-origin reads.

The browser first tries a direct fetch. When it fails with the opaque `TypeError` that signals a CORS refusal, the request is automatically retried through a same-origin `/proxy/url/…` endpoint, which fetches the file server-side and returns it with permissive CORS headers. Hosts that already send CORS headers are never routed through the proxy.

## The server-side proxy is provided two ways

1. **Cloudflare Pages Function** — `functions/proxy/[[path]].js`, deployed by the optional `.github/workflows/cloudflare-pages-deploy.yml`.
2. **Vite dev plugin** — `plugins/dev-proxy-plugin.js` serves the same `/proxy/url` route during `npm run dev`, so development behaves like production.

## Important: the Cloudflare deployment is opt-in

This PR does **not** change the existing deployment. The Cloudflare Pages workflow is skipped unless the repository sets `CLOUDFLARE_PAGES_ENABLED=true`. If you keep your current VPS/static deployment, nothing here changes — you just need to provide the `/proxy/url/<encoded>` endpoint another way (e.g. an nginx reverse-proxy `location` block) for the fallback to work on your host.

To enable Cloudflare Pages on a repo:
- **Variable:** `CLOUDFLARE_PAGES_ENABLED=true`
- **Variable (optional):** `CLOUDFLARE_PAGES_PROJECT` (defaults to `web-a2e`)
- **Secrets:** `CLOUDFLARE_API_TOKEN` (Account · Cloudflare Pages:Edit), `CLOUDFLARE_ACCOUNT_ID`

## Formats

| Parameter | Device | Formats |
| --------- | ------ | ------- |
| `?disk=` | Disk II (floppy) | `.dsk` `.do` `.po` `.woz` |
| `?hd=` | SmartPort (hard drive) | `.2mg` `.hdv` |

## Testing

Verified locally (`npm run dev`) and on a live Cloudflare Pages deployment. URLs that previously failed due to CORS — including **Internet Archive** (`archive.org`) and **Asimov** (`asimov.applefritter.com`) — now load fine. For example, `?disk=https://archive.org/download/pouet_82820/IBZII.dsk` loads the full 143360-byte image via the proxy. All 91 existing Vitest tests pass.

You are welcome to try it on my fork's live deployment: **https://web-a2e.pages.dev** — append any `?disk=…` / `?hd=…` URL (including an Asimov or Internet Archive image) and it should load.

## Files changed

- `src/js/disk-manager/url-media-loader.js` — CORS fallback through `/proxy/url`
- `functions/proxy/[[path]].js` — Cloudflare Pages Function proxy
- `plugins/dev-proxy-plugin.js` — Vite dev-server proxy middleware
- `.github/workflows/cloudflare-pages-deploy.yml` — opt-in CF Pages deploy
- `vite.config.js`, `.gitignore`, `README.md`, `CLAUDE.md`
