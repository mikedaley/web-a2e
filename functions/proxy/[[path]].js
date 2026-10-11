/**
 * Cloudflare Pages Function - CORS proxy for URL-loaded media
 *
 * Serves /proxy/url/<encodeURIComponent(targetUrl)>. Fetches the target and
 * returns it with permissive CORS headers, which lets the emulator read disk
 * images hosted on servers that send no Access-Control-Allow-Origin of their
 * own. The frontend only uses this as a fallback when a direct fetch is
 * refused, so hosts that already allow cross-origin reads are never routed
 * through here.
 */

function isValidUrl(urlString) {
  try {
    const url = new URL(urlString);
    return url.protocol === "http:" || url.protocol === "https:";
  } catch {
    return false;
  }
}

export async function onRequest(context) {
  const { request, params } = context;
  const pathSegments = params.path || [];

  // CORS preflight.
  if (request.method === "OPTIONS") {
    return new Response(null, {
      status: 200,
      headers: {
        "Access-Control-Allow-Origin": "*",
        "Access-Control-Allow-Methods": "GET, OPTIONS",
        "Access-Control-Allow-Headers": "Content-Type",
      },
    });
  }

  if (request.method !== "GET") {
    return new Response("Method not allowed", { status: 405 });
  }

  const [category, ...rest] = pathSegments;
  if (category !== "url" || rest.length < 1) {
    return new Response("Invalid proxy route", { status: 404 });
  }

  const fullUrl = decodeURIComponent(rest.join("/"));
  if (!isValidUrl(fullUrl)) {
    return new Response("Invalid URL format", { status: 400 });
  }

  try {
    const upstream = await fetch(fullUrl, {
      headers: { "User-Agent": "Mozilla/5.0 (compatible; Apple2-Emulator/1.0)" },
      redirect: "follow",
    });

    if (!upstream.ok) {
      return new Response("Upstream error", { status: upstream.status });
    }

    const buffer = await upstream.arrayBuffer();

    const headers = new Headers({
      "Content-Type":
        upstream.headers.get("content-type") || "application/octet-stream",
      "Content-Length": buffer.byteLength.toString(),
      "Access-Control-Allow-Origin": "*",
      "Access-Control-Allow-Methods": "GET, OPTIONS",
      "Access-Control-Allow-Headers": "Content-Type",
      "Cache-Control": "public, max-age=86400",
    });

    return new Response(buffer, { headers });
  } catch {
    return new Response("Proxy error", { status: 502 });
  }
}
