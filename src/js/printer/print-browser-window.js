/*
 * print-browser-window.js - Browse, preview, export, reprint, and delete the
 * pages captured by the virtual printer.
 *
 * Pages are auto-saved to IndexedDB by printer-window.js (printer-page-store.js)
 * so output survives a tab close. This window is a read/manage view over that
 * store: a thumbnail list grouped by print job on the left, a preview on the
 * right. Selecting a job header previews the whole continuous paper track and
 * reprints the entire job; selecting a single page previews and exports just
 * that page. Reprint reuses the printer window's full-bleed page sizing
 * (print-utils.js). Styled with the shared control tokens so it matches the
 * Printer window's look and feel.
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 *  Shawn Bullock <shawn@agenticexpert.ai>
 */

import { BaseWindow } from "../windows/base-window.js";
import { getAllPages, deletePage, deleteJob } from "./printer-page-store.js";
import { printPagesViaIframe, printImageViaIframe } from "./print-utils.js";
import { makeZipStore } from "./zip-store.js";
import { showConfirm } from "../ui/confirm.js";

// Decode a PNG data URL to its raw bytes (for zipping stored pages).
function dataUrlToBytes(dataUrl) {
  const bin = atob(dataUrl.split(",")[1]);
  const bytes = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
  return bytes;
}

function loadImage(src) {
  return new Promise((resolve, reject) => {
    const img = new Image();
    img.onload  = () => resolve(img);
    img.onerror = reject;
    img.src = src;
  });
}


function saveBlob(blob, filename) {
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = filename;
  a.click();
  URL.revokeObjectURL(url);
}

export class PrintBrowserWindow extends BaseWindow {
  constructor(printerWindow = null) {
    super({
      id: "print-browser",
      title: "Print Browser",
      minWidth: 420,
      minHeight: 320,
      defaultWidth: 780,
      defaultHeight: 560,
    });
    this._printerWindow = printerWindow;   // for "Send to Printer" (load job back)
    this._pages = [];
    this._sel   = { kind: null };   // { kind:'job', jobId } | { kind:'page', id }
  }

  renderContent() {
    return `
      <div class="pb-root">
        <div class="pb-toolbar">
          <button id="pb-refresh" class="pb-btn">Refresh</button>
          <span id="pb-count" class="pb-count"></span>
        </div>
        <div class="pb-body">
          <div id="pb-list" class="pb-list"></div>
          <div id="pb-preview" class="pb-preview">
            <div class="pb-empty">No page selected</div>
          </div>
        </div>
      </div>`;
  }

  onContentRendered() {
    this._list    = this.contentElement.querySelector("#pb-list");
    this._preview = this.contentElement.querySelector("#pb-preview");
    this._count   = this.contentElement.querySelector("#pb-count");

    this.contentElement.querySelector("#pb-refresh")
      .addEventListener("click", () => this._refresh());

    // Keep keystrokes inside the window from leaking to the emulator.
    this.contentElement.addEventListener("keydown", (e) => e.stopPropagation());
    this.contentElement.addEventListener("keyup",   (e) => e.stopPropagation());

    this._refresh();
  }

  // Reload from the store every time the window is shown so freshly printed
  // pages appear without a manual refresh.
  show() {
    super.show();
    this._refresh();
  }

  async _refresh() {
    this._pages = await getAllPages();
    if (this._count) {
      const n = this._pages.length;
      this._count.textContent = n ? `${n} page${n === 1 ? "" : "s"}` : "no pages";
    }
    this._renderList();
    this._renderPreview();
  }

  // Group pages by jobId, newest job first.
  _groupJobs() {
    const jobs = [];
    for (const page of this._pages) {
      let job = jobs.find((j) => j.jobId === page.jobId);
      if (!job) {
        job = { jobId: page.jobId, pages: [] };
        jobs.push(job);
      }
      job.pages.push(page);
    }
    jobs.sort((a, b) => b.jobId - a.jobId);
    return jobs;
  }

  _renderList() {
    if (!this._list) return;
    if (!this._pages.length) {
      this._list.innerHTML = `<div class="pb-empty">Nothing printed yet</div>`;
      return;
    }
    this._list.innerHTML = "";
    for (const job of this._groupJobs()) {
      const first = job.pages[0];
      const section = document.createElement("div");
      section.className = "pb-job";

      const jobSel = this._sel.kind === "job" && this._sel.jobId === job.jobId;
      const head = document.createElement("div");
      head.className = "pb-job-head" + (jobSel ? " sel" : "");
      const when = new Date(first.savedAt).toLocaleString();
      head.innerHTML =
        `<span class="pb-job-title">${this._esc(first.model)} · ${when} · ` +
        `${job.pages.length}pg</span>`;
      head.title = "Preview / print the whole job";
      head.addEventListener("click", () => {
        this._sel = { kind: "job", jobId: job.jobId };
        this._renderList();
        this._renderPreview();
      });
      section.appendChild(head);

      const thumbs = document.createElement("div");
      thumbs.className = "pb-thumbs";
      // Count the pages this job actually has rather than trusting the count
      // stamped on each record: a print longer than the live paper window is
      // saved a page at a time as it scrolls, so the early records were written
      // when the job was still short and their stored count is out of date.
      const jobPages = job.pages.length;
      for (const page of job.pages) {
        const pageSel = this._sel.kind === "page" && this._sel.id === page.id;
        const card = document.createElement("div");
        card.className = "pb-thumb" + (pageSel ? " sel" : "");
        card.innerHTML =
          `<img src="${page.pngDataUrl}" alt="page ${page.pageIndex + 1}"/>` +
          `<span class="pb-thumb-cap">Page ${page.pageIndex + 1} / ${jobPages}<br>` +
          `${this._esc(page.ribbon)} · ${this._pageSizeLabel(page)}</span>`;
        card.addEventListener("click", () => {
          this._sel = { kind: "page", id: page.id };
          this._renderList();
          this._renderPreview();
        });
        thumbs.appendChild(card);
      }
      section.appendChild(thumbs);
      this._list.appendChild(section);
    }
  }

  _renderPreview() {
    if (!this._preview) return;
    if (this._sel.kind === "job") {
      const job = this._groupJobs().find((j) => j.jobId === this._sel.jobId);
      if (job) return this._renderJobPreview(job);
    } else if (this._sel.kind === "page") {
      const page = this._pages.find((p) => p.id === this._sel.id);
      if (page) return this._renderPagePreview(page);
    }
    this._sel = { kind: null };
    this._preview.innerHTML = `<div class="pb-empty">Select a job header to preview the whole run, or a page to preview it on its own</div>`;
  }

  // 72-dpi display width for a page PNG (canvas px → physical inches → 72dpi screen px).
  _imgDisplayW(page) { return Math.round((page.width || 960) * 72 / this._pagePpi(page)); }

  // Stored-PNG raster density. Newer records carry it (pages are stored above
  // logical res for print quality); records from before the field are 120/in.
  _pagePpi(page) { return page.pxPerInch || 120; }

  // Whole job: the continuous paper track (every page stacked) + job actions.
  _renderJobPreview(job) {
    const first = job.pages[0];
    const when  = new Date(first.savedAt).toLocaleString();
    const imgs  = job.pages
      .map((p) => `<img src="${p.pngDataUrl}" style="width:${this._imgDisplayW(p)}px" alt="page ${p.pageIndex + 1}"/>`).join("");
    this._preview.innerHTML = `
      <div class="pb-actions">
        <button id="pb-print" class="pb-btn">Print Job…</button>
        <button id="pb-zip" class="pb-btn">Export PNG</button>
        <button id="pb-send" class="pb-btn">Back to ${this._esc(first.model)} ${first.ribbon === "color" ? "Color" : "B/W"}</button>
        <button id="pb-del-job" class="pb-btn pb-btn-danger pb-spacer">Delete Job</button>
      </div>
      <div class="pb-meta">
        ${this._esc(first.model)} · ${job.pages.length} page${job.pages.length === 1 ? "" : "s"} ·
        ${this._esc(first.ribbon)} ribbon · ${this._pageSizeLabel(first)} · ${when}
      </div>
      <div class="pb-stage">${imgs}</div>`;
    this._preview.querySelector("#pb-print").addEventListener("click", () => this._printJob(job));
    this._preview.querySelector("#pb-zip").addEventListener("click", () => this._exportJob(job));
    this._preview.querySelector("#pb-send").addEventListener("click", () => this._sendToPrinter(job));
    this._preview.querySelector("#pb-del-job").addEventListener("click", () => this._deleteJob(job.jobId));
  }

  // How many pages this page's job holds, counted from the records rather than
  // read off the record: a long print is saved a page at a time as the paper
  // scrolls, so an early record's stored count was right only at the time.
  _jobPageCount(page) {
    return this._pages.filter((p) => p.jobId === page.jobId).length || 1;
  }

  // Single page: just that sheet + per-page actions (export is page-only).
  _renderPagePreview(page) {
    const when = new Date(page.savedAt).toLocaleString();
    this._preview.innerHTML = `
      <div class="pb-actions">
        <button id="pb-print" class="pb-btn">Print Page…</button>
        <button id="pb-png" class="pb-btn">Export PNG</button>
        <button id="pb-del" class="pb-btn pb-btn-danger pb-spacer">Delete Page</button>
      </div>
      <div class="pb-meta">
        ${this._esc(page.model)} · page ${page.pageIndex + 1} of ${this._jobPageCount(page)} ·
        ${this._esc(page.ribbon)} ribbon · ${this._pageSizeLabel(page)} · ${when}
      </div>
      <div class="pb-stage"><img src="${page.pngDataUrl}" style="width:${this._imgDisplayW(page)}px" alt="printed page"/></div>`;
    this._preview.querySelector("#pb-print").addEventListener("click", () => this._printPage(page));
    this._preview.querySelector("#pb-png").addEventListener("click", () => this._exportPage(page));
    this._preview.querySelector("#pb-del").addEventListener("click", () => this._deletePage(page));
  }

  // Whole job, full-bleed, one sheet per page (same layout as the printer
  // window's PDF export).
  _printJob(job) {
    const first = job.pages[0];
    printPagesViaIframe(
      job.pages.map((p) => p.pngDataUrl),
      first.width / this._pagePpi(first),
      first.formInches,
    );
  }

  _printPage(page) {
    printImageViaIframe(page.pngDataUrl, page.width / this._pagePpi(page), page.formInches);
  }

  // Re-load the whole job onto the virtual printer's paper — re-preview it there,
  // or carry on printing onto it. The printer window restores the model/ribbon/
  // form and the head position the job left off at, and re-adopts the job's id.
  _sendToPrinter(job) {
    this._printerWindow?.loadJobToPaper?.(job);
  }

  // Whole job → one PNG per page (page-01.png, page-02.png, …) plus a joined
  // full.png strip, zipped — the same export the printer window produces for a
  // multi-page run. A single-page job exports as a plain PNG (no zip), matching
  // the printer window's behaviour.
  async _exportJob(job) {
    if (job.pages.length === 1) {
      const p = job.pages[0];
      return saveBlob(new Blob([dataUrlToBytes(p.pngDataUrl)], { type: "image/png" }),
        `print-${job.jobId}.png`);
    }
    const pad   = (n) => String(n).padStart(2, "0");
    const files = job.pages.map((p, i) =>
      ({ name: `page-${pad(i + 1)}.png`, data: dataUrlToBytes(p.pngDataUrl) }));

    // full.png — every page stacked into one tall strip (printshop/banner use).
    const w = job.pages[0].width;
    const h = job.pages[0].height;
    const full = document.createElement("canvas");
    full.width  = w;
    full.height = h * job.pages.length;
    const ctx = full.getContext("2d");
    ctx.fillStyle = "#ffffff";
    ctx.fillRect(0, 0, full.width, full.height);
    for (let i = 0; i < job.pages.length; i++) {
      ctx.drawImage(await loadImage(job.pages[i].pngDataUrl), 0, i * h);
    }
    const fullBytes = await new Promise((resolve) =>
      full.toBlob(async (b) => resolve(new Uint8Array(await b.arrayBuffer())), "image/png"));
    files.push({ name: "full.png", data: fullBytes });

    saveBlob(makeZipStore(files), `print-${job.jobId}.zip`);
  }

  _exportPage(page) {
    const a = document.createElement("a");
    a.href = page.pngDataUrl;
    a.download = `print-${page.jobId}-p${String(page.pageIndex + 1).padStart(2, "0")}.png`;
    a.click();
  }

  async _deletePage(page) {
    if (!(await showConfirm(`Delete page ${page.pageIndex + 1} of this print job?`, "Delete"))) return;
    await deletePage(page.id);
    if (this._sel.kind === "page" && this._sel.id === page.id) this._sel = { kind: null };
    this._refresh();
  }

  async _deleteJob(jobId) {
    const job = this._groupJobs().find((j) => j.jobId === jobId);
    const n   = job ? job.pages.length : 0;
    if (!(await showConfirm(`Delete this print job (${n} page${n === 1 ? "" : "s"})?`, "Delete"))) return;
    await deleteJob(jobId);
    if (this._sel.kind === "job" && this._sel.jobId === jobId) this._sel = { kind: null };
    this._refresh();
  }

  _pageSizeLabel(rec) {
    const h = rec.formInches?.toFixed(1) ?? "?";
    const w = rec.paperWidthInch != null ? rec.paperWidthInch.toFixed(1) : null;
    return w ? `${w}×${h}"` : `${h}" form`;
  }

  _esc(str) {
    return String(str ?? "")
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;");
  }
}
