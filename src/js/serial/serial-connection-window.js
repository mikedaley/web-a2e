/*
 * serial-connection-window.js - Serial port connection settings window
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

import { BaseWindow } from "../windows/base-window.js";

const STORAGE_KEY = "a2e-serial-connection";

export class SerialConnectionWindow extends BaseWindow {
  constructor(modem) {
    super({
      id: "serial-connection",
      title: "Serial Port",
      minWidth: 320,
      minHeight: 200,
      defaultWidth: 320,
      defaultHeight: 200,
      resizeDirections: [],
    });

    this.modem = modem;
  }

  create() {
    super.create();

    const saved = this.loadSettings();

    this.contentElement.innerHTML = `
      <div class="serial-connection-content">
        <div class="serial-field-row">
          <label class="serial-label" for="serial-host">Host</label>
          <input type="text" id="serial-host" class="serial-input" value="${saved.host}" placeholder="bbs.example.com" spellcheck="false" />
        </div>
        <div class="serial-field-row">
          <label class="serial-label" for="serial-port">Port</label>
          <input type="number" id="serial-port" class="serial-input serial-port-input" value="${saved.port}" min="1" max="65535" />
        </div>
        <div class="serial-status-row">
          <span class="serial-status-dot disconnected" id="serial-status-dot"></span>
          <span class="serial-status-text" id="serial-status-text">Disconnected</span>
        </div>
        <button class="serial-connect-btn" id="serial-connect-btn">Connect</button>
      </div>
    `;

    this.hostInput = this.contentElement.querySelector("#serial-host");
    this.portInput = this.contentElement.querySelector("#serial-port");
    this.statusDot = this.contentElement.querySelector("#serial-status-dot");
    this.statusText = this.contentElement.querySelector("#serial-status-text");
    this.connectBtn = this.contentElement.querySelector("#serial-connect-btn");

    this.connectBtn.addEventListener("click", () => this.toggleConnection());

    // Stop keyboard events from reaching the emulator
    this.contentElement.addEventListener("keydown", (e) => e.stopPropagation());
    this.contentElement.addEventListener("keyup", (e) => e.stopPropagation());

    // Wire modem status callbacks
    this.modem.onStatusChange = (status) => {
      this.updateStatus(status === "connected");
    };

    this.updateStatus(this.modem.isConnected());
  }

  toggleConnection() {
    if (this.modem.isConnected()) {
      this.modem.hangup();
    } else {
      const host = this.hostInput.value.trim();
      const port = parseInt(this.portInput.value, 10);
      if (!host || !port) return;

      this.saveSettings(host, port);
      this.modem.dial(`${host}:${port}`);
      this.statusText.textContent = "Connecting...";
    }
  }

  updateStatus(connected) {
    this.statusDot.classList.toggle("connected", connected);
    this.statusDot.classList.toggle("disconnected", !connected);
    this.statusText.textContent = connected ? "Connected" : "Disconnected";
    this.connectBtn.textContent = connected ? "Disconnect" : "Connect";
    this.hostInput.disabled = connected;
    this.portInput.disabled = connected;
  }

  loadSettings() {
    try {
      const data = JSON.parse(localStorage.getItem(STORAGE_KEY));
      return { host: data?.host || "", port: data?.port || 23 };
    } catch {
      return { host: "", port: 23 };
    }
  }

  saveSettings(host, port) {
    localStorage.setItem(STORAGE_KEY, JSON.stringify({ host, port }));
  }
}
