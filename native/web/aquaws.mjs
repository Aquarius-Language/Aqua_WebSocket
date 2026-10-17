// Aqua_WebSocket browser adapter (Aquarius DynamicLibrary ES-module ABI 1).
// Mirrors the JSON contract of the native engine using the browser WebSocket
// API. Browsers cannot accept connections, so listening reports Unsupported.
// MIT License, Copyright (c) 2026 Temple Lin.
const VERSION = '1.0.0';
const encoder = new TextEncoder();
const decoder = new TextDecoder('utf-8');

function fromBase64(text) {
  const s = atob(text);
  const out = new Uint8Array(s.length);
  for (let i = 0; i < s.length; i++) out[i] = s.charCodeAt(i);
  return out;
}

// Poll replies must fit the DynamicLibrary 1 MiB call limit. Like the native engine, take
// whole events until the next one would overflow; the rest stay queued ("pending") for the
// next poll. A backgrounded tab stops polling and can queue far more than 1 MiB.
const CALL_LIMIT = 1024 * 1024;
function utf8Length(text) {
  let n = 0;
  for (let i = 0; i < text.length; i++) {
    const c = text.charCodeAt(i);
    n += c < 0x80 ? 1 : c < 0x800 ? 2 : (c >= 0xd800 && c <= 0xdfff) ? 2 : 3;
  }
  return n;
}
function takeEvents(events, max, dropped) {
  const parts = [];
  let used = 64, taken = 0;
  while (taken < events.length && parts.length < max) {
    const json = JSON.stringify(events[taken]);
    const size = utf8Length(json) + 1;
    if (used + size > CALL_LIMIT) {
      if (!parts.length) { parts.push(JSON.stringify(dropped)); taken++; }
      break;
    }
    parts.push(json); used += size; taken++;
  }
  events.splice(0, taken);
  return encoder.encode(`{"events":[${parts.join(',')}],"pending":${events.length}}`);
}

export function aqua_library_abi() { return 1; }

export function aqua_create_library() {
  let nextId = 1;
  const contexts = new Map();   // ctx -> {events: [], conns: Map}
  const reply = value => encoder.encode(JSON.stringify(value));
  const parse = bytes => { try { return JSON.parse(decoder.decode(bytes)); } catch { return null; } };
  const handler = fn => input => {
    const request = parse(input);
    if (!request || typeof request !== 'object') return reply({ error: 'invalid JSON request' });
    try { return reply(fn(request)); } catch (error) { return reply({ error: String(error && error.message || error) }); }
  };
  const ctxOf = request => {
    const ctx = contexts.get(request.ctx);
    if (!ctx) throw new Error('unknown context');
    return ctx;
  };
  const push = (ctx, event) => { ctx.events.push(event); const wake = ctx.wake; ctx.wake = null; if (wake) wake(); };
  // Yield one macrotask so the browser can deliver socket events even when the
  // Aquarius program polls in a tight loop (library calls otherwise resume on microtasks).
  const channel = typeof MessageChannel === 'function' ? new MessageChannel() : null;
  const yieldTask = () => new Promise(resolve => {
    if (!channel) { setTimeout(resolve, 0); return; }
    channel.port1.onmessage = () => resolve();
    channel.port2.postMessage(0);
  });
  const waitForEvents = (ctx, timeout) => new Promise(resolve => {
    const timer = setTimeout(() => { ctx.wake = null; resolve(); }, timeout);
    ctx.wake = () => { clearTimeout(timer); resolve(); };
  });

  const api = {
    aqws_open: handler(() => {
      const id = nextId++;
      contexts.set(id, { events: [], conns: new Map(), wake: null });
      // The page location lets apps reach the server that served them (e.g. ws://<page host>:port).
      const page = typeof location === 'object' && location ? { host: location.hostname || '', port: location.port || '', secure: location.protocol === 'https:' } : { host: '', port: '', secure: false };
      return { ctx: id, version: VERSION, platform: 'web', server: false, tls: true, page };
    }),
    aqws_close_ctx: handler(request => {
      const ctx = contexts.get(request.ctx);
      if (!ctx) return { ok: false };
      for (const entry of ctx.conns.values()) { try { entry.socket.close(1001, 'going away'); } catch {} }
      contexts.delete(request.ctx);
      return { ok: true };
    }),
    aqws_listen: handler(() => ({ error: 'Unsupported / 網頁瀏覽器無法建立 WebSocket 伺服器；請在桌面平台執行伺服器' })),
    aqws_close_server: handler(() => ({ ok: false })),
    aqws_broadcast: handler(() => ({ sent: 0 })),
    aqws_connect: handler(request => {
      const ctx = ctxOf(request);
      const id = nextId++;
      let socket;
      try {
        socket = Array.isArray(request.protocols) && request.protocols.length
          ? new WebSocket(String(request.url), request.protocols.map(String))
          : new WebSocket(String(request.url));
      } catch (error) {
        return { error: 'invalid WebSocket URL: ' + (error && error.message || error) };
      }
      socket.binaryType = 'arraybuffer';
      const entry = { socket, state: 'connecting', remote: String(request.url) };
      ctx.conns.set(id, entry);
      const timeout = Number(request.timeout) > 0 ? Number(request.timeout) : 10000;
      const timer = setTimeout(() => {
        if (entry.state === 'connecting') {
          push(ctx, { type: 'error', conn: id, server: 0, message: 'connection timed out' });
          try { socket.close(); } catch {}
        }
      }, timeout);
      socket.onopen = () => {
        clearTimeout(timer);
        entry.state = 'open';
        push(ctx, { type: 'open', conn: id, server: 0, remote: entry.remote, protocol: socket.protocol || '' });
      };
      socket.onmessage = event => {
        if (typeof event.data === 'string') push(ctx, { type: 'message', conn: id, server: 0, text: event.data });
        else push(ctx, { type: 'binary', conn: id, server: 0, bytes: Array.from(new Uint8Array(event.data)) });
      };
      socket.onerror = () => {
        if (entry.state === 'connecting') push(ctx, { type: 'error', conn: id, server: 0, message: 'connection refused or unreachable' });
      };
      socket.onclose = event => {
        clearTimeout(timer);
        entry.state = 'closed';
        push(ctx, { type: 'close', conn: id, server: 0, code: event.code || 1006, reason: event.reason || '', clean: !!event.wasClean });
        ctx.conns.delete(id);
      };
      return { conn: id };
    }),
    aqws_send: handler(request => {
      const ctx = ctxOf(request);
      const ids = Array.isArray(request.conns) ? request.conns : [request.conn];
      let payload;
      if (typeof request.text === 'string') payload = request.text;
      else if (Array.isArray(request.bytes)) payload = Uint8Array.from(request.bytes, v => Number(v) & 255);
      else if (typeof request.binary === 'string') payload = fromBase64(request.binary);
      else return { error: 'send needs text, bytes or binary' };
      let sent = 0;
      for (const id of ids) {
        const entry = ctx.conns.get(id);
        if (entry && entry.socket.readyState === WebSocket.OPEN) { entry.socket.send(payload); sent++; }
      }
      return { sent };
    }),
    aqws_close: handler(request => {
      const ctx = ctxOf(request);
      const entry = ctx.conns.get(request.conn);
      if (!entry) return { ok: false };
      let code = Number(request.code) || 1000;
      if (code !== 1000 && (code < 3000 || code > 4999)) code = 1000;
      try { entry.socket.close(code, String(request.reason || '').slice(0, 120)); } catch { entry.socket.close(); }
      return { ok: true };
    }),
    aqws_poll: async input => {
      const request = parse(input);
      if (!request || typeof request !== 'object') return reply({ error: 'invalid JSON request' });
      const ctx = contexts.get(request.ctx);
      if (!ctx) return reply({ error: 'unknown context' });
      const timeout = Math.max(0, Math.min(1000, Number(request.timeout) || 0));
      if (!ctx.events.length) await (timeout > 0 ? waitForEvents(ctx, timeout) : yieldTask());
      const max = Number(request.max) > 0 ? Number(request.max) : 10000;
      return takeEvents(ctx.events, max, { type: 'error', conn: 0, server: 0, message: 'event exceeded the 1 MiB call limit and was dropped' });
    },
    aqws_info: handler(request => {
      const ctx = ctxOf(request);
      const connections = [];
      for (const [id, entry] of ctx.conns) connections.push({ conn: id, server: 0, state: entry.state, buffered: entry.socket.bufferedAmount, remote: entry.remote });
      return { servers: [], connections };
    }),
    aqws_random: handler(request => {
      const n = Math.max(1, Math.min(256, Number(request.bytes) || 16));
      const bytes = crypto.getRandomValues(new Uint8Array(n));
      const word = crypto.getRandomValues(new Uint32Array(1))[0] & 0x3fffffff;
      return { hex: [...bytes].map(b => b.toString(16).padStart(2, '0')).join(''), int: word };
    }),
    aqua_dispose() {
      for (const ctx of contexts.values()) for (const entry of ctx.conns.values()) { try { entry.socket.close(1001, 'going away'); } catch {} }
      contexts.clear();
    }
  };
  return api;
}
