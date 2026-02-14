/*
 * serial-proxy-plugin.js - Vite plugin for WebSocket-to-TCP proxy
 *
 * Proxies browser WebSocket connections to raw TCP sockets,
 * eliminating the need for external websockify.
 */

import { createConnection } from 'net';
import { WebSocketServer } from 'ws';

export function serialProxyPlugin() {
  return {
    name: 'serial-proxy',

    configureServer(server) {
      const wss = new WebSocketServer({ noServer: true });

      wss.on('connection', (ws, req) => {
        const url = new URL(req.url, 'http://localhost');
        const host = url.searchParams.get('host');
        const port = parseInt(url.searchParams.get('port'), 10);

        if (!host || !port) {
          ws.close(1008, 'Missing host or port');
          return;
        }

        console.log(`[serial-proxy] Connecting to ${host}:${port}`);

        const tcp = createConnection({ host, port }, () => {
          console.log(`[serial-proxy] Connected to ${host}:${port}`);
        });

        tcp.on('error', (err) => {
          console.error(`[serial-proxy] TCP error: ${err.message}`);
          ws.close(1011, err.message);
        });

        tcp.on('close', () => {
          ws.close(1000);
        });

        // TCP → WebSocket
        tcp.on('data', (data) => {
          if (ws.readyState === ws.OPEN) {
            ws.send(data);
          }
        });

        // WebSocket → TCP
        ws.on('message', (data) => {
          if (!tcp.destroyed) {
            tcp.write(data);
          }
        });

        ws.on('close', () => {
          tcp.destroy();
        });

        ws.on('error', () => {
          tcp.destroy();
        });
      });

      // Hook into the HTTP server's upgrade event, before Vite's HMR handler
      return () => {
        server.httpServer?.on('upgrade', (req, socket, head) => {
          if (req.url?.startsWith('/serial-proxy')) {
            wss.handleUpgrade(req, socket, head, (ws) => {
              wss.emit('connection', ws, req);
            });
          }
        });
      };
    },
  };
}
