/*
 * url-media-loader.js - Load disk images named by URL parameters
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

import {
  parseMediaParams,
  hasMediaParams,
  looksLikeLocalPath,
  MAX_FLOPPY_BYTES,
  MAX_HARD_DRIVE_BYTES,
} from "../utils/url-params.js";
import { showToast } from "../ui/toast.js";

/**
 * Build the path for the same-origin CORS proxy (Cloudflare Pages Function).
 *
 * The proxy lives at /proxy/url/<encodeURIComponent(target)>, matching the
 * functions/proxy/[[path]].js route. The path is relative so the app works
 * regardless of the domain it is served from.
 *
 * @param {string} url - Absolute http(s) URL to fetch through the proxy
 * @returns {string} Relative proxy URL
 */
function proxyUrl(url) {
  return `/proxy/url/${encodeURIComponent(url)}`;
}

/**
 * Confirm a proxied response really is a fetch failure worth reporting.
 *
 * A proxy that could not reach the target answers with a non-2xx status, so
 * unlike a direct fetch the failure is visible and has no CORS to blame.
 *
 * @param {Response} response
 * @returns {boolean} True when the proxy itself failed
 */
function isProxyFailure(response) {
  return response.status === 502 || response.status === 404 || response.status === 400;
}

/**
 * Fetch an image, refusing anything implausibly large.
 *
 * Credentials are omitted deliberately: the URL comes from whoever wrote the
 * link, and it must not be able to make the visitor's browser send cookies to a
 * third party. That also means the host has to serve permissive CORS headers —
 * most public file hosts do, but a plain web server generally does not. When a
 * direct fetch is refused (an opaque TypeError the browser raises for a CORS
 * refusal), the request is retried through the same-origin /proxy/url route so
 * hosts without CORS headers still work.
 *
 * @param {string} url - Absolute http(s) URL
 * @param {number} maxBytes - Reject anything larger than this
 * @returns {Promise<Uint8Array>}
 */
async function fetchImage(url, maxBytes) {
  let response;
  try {
    response = await fetch(url, {
      credentials: "omit",
      mode: "cors",
      redirect: "follow",
    });
  } catch (error) {
    // fetch rejects with an opaque TypeError for both a missing host and a
    // CORS refusal, and the browser deliberately withholds which. In practice
    // it is nearly always CORS — plenty of Apple II archives serve the file
    // happily to curl but send no Access-Control-Allow-Origin. Rather than
    // giving up (and making the host name the culprit), retry through the
    // same-origin proxy: the proxy side has no CORS, so a refusal there is a
    // real upstream problem, not the browser's security policy.
    if (error instanceof TypeError) {
      response = await fetch(proxyUrl(url), { credentials: "omit" });
      if (isProxyFailure(response)) {
        throw new Error(
          `${new URL(url).host} refused the request. It most likely does not ` +
            `allow other sites to read its files (no CORS headers); the host ` +
            `being down would look the same.`,
        );
      }
    } else {
      throw error;
    }
  }

  if (!response.ok) {
    throw new Error(`server returned ${response.status} ${response.statusText}`);
  }

  // Trust Content-Length only as an early out; it is advisory, so the decoded
  // length is checked again below.
  const declared = Number(response.headers.get("content-length"));
  if (Number.isFinite(declared) && declared > maxBytes) {
    throw new Error(`image is too large (${Math.round(declared / 1048576)}MB)`);
  }

  const data = new Uint8Array(await response.arrayBuffer());
  if (data.length === 0) throw new Error("image is empty");
  if (data.length > maxBytes) {
    throw new Error(`image is too large (${Math.round(data.length / 1048576)}MB)`);
  }

  // A wrong path usually still returns 200, because servers answer with their
  // own page rather than a 404 — this app's dev server included. Without this
  // check the HTML reaches the core and is reported as an unreadable disk
  // image, which sends the reader looking in entirely the wrong place.
  const contentType = (response.headers.get("content-type") || "").toLowerCase();
  const startsAsHtml = /^\s*<(!doctype|html)/i.test(
    String.fromCharCode(...data.slice(0, 64)),
  );
  if (contentType.startsWith("text/html") || startsAsHtml) {
    throw new Error(describeHtmlResponse(url));
  }

  return data;
}

/**
 * Explain a web page arriving where a disk image was expected.
 *
 * @param {string} url - The URL that was fetched
 * @returns {string}
 */
function describeHtmlResponse(url) {
  if (looksLikeLocalPath(url)) {
    return (
      "that looks like a file on your own computer. A web page cannot read " +
      "local files — use the Insert button or drag the file onto a drive. " +
      "URL parameters need an address the browser can fetch."
    );
  }
  return "the server returned a web page, not a disk image — check the address";
}

/**
 * Read the page URL and report which units it wants to fill.
 *
 * Called before the disk managers restore their persisted images so they can
 * skip the units a link is about to claim, rather than loading a disk only to
 * have it replaced a moment later.
 *
 * @param {Location|URL} location - Page location
 * @returns {{floppies: Array, hardDrives: Array, errors: string[]}}
 */
export function readUrlMedia(location) {
  return parseMediaParams(location.search, location.href);
}

/**
 * Fetch and insert everything the URL asked for.
 *
 * Images are inserted without being written to IndexedDB or the recents list,
 * so a shared link never evicts the disks the visitor had in their own drives —
 * loading the plain URL afterwards brings those back untouched.
 *
 * @param {Object} options
 * @param {{floppies: Array, hardDrives: Array, errors: string[]}} options.media
 * @param {Object} options.diskManager
 * @param {Object} options.hardDriveManager
 * @returns {Promise<number>} Count of images successfully inserted
 */
export async function loadUrlMedia({ media, diskManager, hardDriveManager }) {
  for (const message of media.errors) {
    console.warn(`URL parameters: ${message}`);
    showToast(message, "warning");
  }

  if (!hasMediaParams(media)) return 0;

  let loaded = 0;

  for (const item of media.floppies) {
    try {
      const data = await fetchImage(item.url, MAX_FLOPPY_BYTES);
      const inserted = await diskManager.loadDiskFromUrlData(
        item.unit,
        item.filename,
        data,
      );
      if (inserted) loaded++;
    } catch (error) {
      console.error(`Failed to load ${item.url}:`, error);
      showToast(
        `Could not load disk from URL: ${error.message}`,
        "error",
      );
    }
  }

  for (const item of media.hardDrives) {
    if (!(await hardDriveManager.isSmartPortInstalled())) {
      showToast(
        "Ignored hard drive URL — no SmartPort card is installed",
        "warning",
      );
      break;
    }
    try {
      const data = await fetchImage(item.url, MAX_HARD_DRIVE_BYTES);
      const inserted = await hardDriveManager.loadImageFromData(
        item.unit,
        item.filename,
        data,
      );
      if (inserted) loaded++;
    } catch (error) {
      console.error(`Failed to load ${item.url}:`, error);
      showToast(
        `Could not load hard drive image from URL: ${error.message}`,
        "error",
      );
    }
  }

  return loaded;
}
