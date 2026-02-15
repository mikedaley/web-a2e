/*
 * serial-manager.js - WebSocket bridge for Super Serial Card
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

/**
 * SerialManager - Manages WebSocket connection for SSC serial I/O
 *
 * Bridges between the WASM SSC emulation and a remote host via WebSocket.
 * Each transmitted byte from the ACIA goes out over WebSocket, and each
 * received byte from WebSocket is fed into the ACIA receive buffer.
 */
export class SerialManager {
  constructor(wasmModule) {
    this.wasmModule = wasmModule;
    this.ws = null;
    this.url = null;
    this.connected = false;
    this.autoReconnect = false;
    this.reconnectTimer = null;
    this.reconnectDelay = 3000;

    // Callbacks
    this.onStatusChange = null;
    this.onError = null;
    this.onReceive = null;
  }

  /**
   * Connect to a WebSocket host
   * @param {string} url - WebSocket URL (e.g., ws://bbs.example.com:23)
   */
  connect(url) {
    if (this.ws) {
      this.disconnect();
    }

    this.url = url;

    try {
      this.ws = new WebSocket(url);
      this.ws.binaryType = "arraybuffer";

      this.ws.onopen = () => {
        this.connected = true;
        if (this.onStatusChange) {
          this.onStatusChange("connected", url);
        }
      };

      this.ws.onmessage = (event) => {
        const data = new Uint8Array(event.data);
        for (let i = 0; i < data.length; i++) {
          if (this.onReceive) {
            this.onReceive(data[i]);
          } else {
            this.wasmModule._serialReceive(data[i]);
          }
        }
      };

      this.ws.onclose = () => {
        this.connected = false;
        if (this.onStatusChange) {
          this.onStatusChange("disconnected", url);
        }
        if (this.autoReconnect) {
          this.scheduleReconnect();
        }
      };

      this.ws.onerror = (err) => {
        if (this.onError) {
          this.onError(err);
        }
      };
    } catch (err) {
      if (this.onError) {
        this.onError(err);
      }
    }
  }

  /**
   * Connect via the built-in WebSocket-to-TCP proxy
   * @param {string} host - TCP hostname
   * @param {number} port - TCP port
   */
  connectProxy(host, port) {
    const wsUrl = `ws://${location.hostname}:${location.port}/serial-proxy?host=${encodeURIComponent(host)}&port=${port}`;
    this.connect(wsUrl);
  }

  /**
   * Disconnect from the current WebSocket host
   */
  disconnect() {
    this.autoReconnect = false;
    this.clearReconnect();

    if (this.ws) {
      this.ws.onclose = null; // Prevent reconnect on intentional close
      this.ws.close();
      this.ws = null;
    }

    this.connected = false;
    if (this.onStatusChange) {
      this.onStatusChange("disconnected", this.url);
    }
  }

  /**
   * Send a byte over the WebSocket connection
   * @param {number} byte - Byte value (0-255)
   */
  sendByte(byte) {
    if (this.ws && this.connected) {
      const data = new Uint8Array([byte]);
      this.ws.send(data);
    }
  }

  /**
   * Check if currently connected
   * @returns {boolean}
   */
  isConnected() {
    return this.connected;
  }

  /**
   * Get the current connection URL
   * @returns {string|null}
   */
  getURL() {
    return this.url;
  }

  /** @private */
  scheduleReconnect() {
    this.clearReconnect();
    this.reconnectTimer = setTimeout(() => {
      if (this.url && this.autoReconnect) {
        this.connect(this.url);
      }
    }, this.reconnectDelay);
  }

  /** @private */
  clearReconnect() {
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
  }

  /**
   * Clean up resources
   */
  destroy() {
    this.disconnect();
  }
}
