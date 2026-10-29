# Aqua_WebSocket 3 / 星泉網頁通訊端

Import `AquaWebSocket` / `星泉網頁通訊端` through a Bottle dependency. Version 3 is written in AquariusLang only, on top
of the default `Network` / `網路` library: RFC 6455 framing, fragmentation, ping/pong, the closing handshake (close
codes and reasons both ways), subprotocols and TLS come from it, so the same code runs on Windows, Linux, Android and
the web, servers can serve `wss://`, and no native binaries ship with the library.

3.0 版完全以星泉撰寫，以預設「網路」函式庫為引擎：訊框、分段、ping/pong、關閉交握、子協定與 TLS 都由它處理；
同一份程式在 Windows、Linux、Android 與網頁執行，伺服器可提供 `wss://`，函式庫不再附帶原生二進位檔。

```aquarius
var ws = import("AquaWebSocket");
var server = ws.CreateServer({"host": "127.0.0.1", "port": 8081});
server.OnMessage(function(id, text) { server.Send(id, text); });
server.Run(function() { return true; });
```

```aquarius
變數 通訊 = 匯入("AquaWebSocket");
變數 用戶端 = 等待 通訊.連接非同步("ws://127.0.0.1:8081/", {"自動重連": 真});
用戶端.當訊息(函式(文字) { 印出(文字); });
用戶端.傳送("你好");
```

| Target / 平台 | Server / 伺服器 | Client / 用戶端 |
| --- | --- | --- |
| Windows, Linux, Android | `ws://`, `wss://` (certificate) | `ws://`, `wss://` |
| Web | no (browsers cannot listen) | the browser's WebSocket |

## Two styles / 兩種寫法

- **Polling (as in 2.x)**: `Wait(ms)` / `等待`, `Poll()` / `輪詢` and `Run(tick)` / `執行` dispatch callbacks while they
  are called; `ws.PollAll(ms)` / `輪詢全部` serves every server and client at once. Network work goes on between calls.
- **Async (new)**: `server.RunAsync()` / `非同步執行` and `client.RunAsync()` make callbacks run as events arrive (they
  may be async functions); the promise settles after `Stop()` / `Close()`. `await ws.ConnectAsync(url, options)` /
  `連接非同步` gives an open client in this mode, or rejects when the connection fails.

## API

Module / 模組:

| English | 繁體中文 | |
| --- | --- | --- |
| `CreateServer(options)` | `建立伺服器(選項)` | `host`/`主機` (`0.0.0.0`), `port`/`連接埠` (8081, 0 picks one), `path`/`路徑` (`""` any path), `origins`/`來源` (browser origins allowed; empty allows all), `protocols`/`子協定`, `maxMessage`/`訊息上限` (524288), `maxConnections`/`連線上限` (256), `certificate`/`憑證` + `certificatePassword`/`憑證密碼` (PKCS#12 or PEM: serves `wss://`). |
| `Mount(server, path, options)` | `掛載(伺服器, 路徑, 選項)` | A WebSocket endpoint on an existing Network server, sharing its port with HTTP routes. |
| `Connect(url)`, `ConnectWith(url, options)` | `連接`, `連接並設定` | `protocols`/`子協定`, `headers`/`標頭` and `origin`/`來源` (not in browsers), `timeout`/`逾時`, `maxMessage`/`訊息上限`, `autoReconnect`/`自動重連`, `reconnectDelay`/`重連間隔` (2000), `queueWhileConnecting`/`連線中佇列` (true). |
| `ConnectAsync(url, options)` | `連接非同步` | A promise of the open client (callbacks dispatched as events arrive). |
| `PollAll(ms)`, `CloseAll()` | `輪詢全部`, `關閉全部` | |
| `Platform()`, `SupportsServer()`, `EngineVersion()`, `Version()` | `平台`, `支援伺服器`, `引擎版本`, `版本` | |
| `PageHost()`, `PageIsSecure()`, `PageUrl(path, fallbackHost)` | `頁面主機`, `頁面安全`, `頁面網址` | On the web, the page's host, whether it is HTTPS, and the `ws(s)://` URL of the page's own server. |
| `RandomInt()`, `RandomHex(bytes)` | `隨機整數`, `隨機十六進位` | Cryptographic randomness (30-bit integers, hex text). |

Server / 伺服器: `OnOpen(fn(conn, info))`, `OnMessage(fn(conn, text))`, `OnBinary(fn(conn, bytes))`,
`OnClose(fn(conn, code, reason))`, `OnError(fn(message))` (`當連線`, `當訊息`, `當二進位`, `當關閉`, `當錯誤`);
`Send`, `SendJSON`, `SendBytes`, `SendMany`, `Broadcast`, `BroadcastExcept`, `BroadcastJSON` (`傳送`, `傳送JSON`,
`傳送位元組`, `群發`, `廣播`, `廣播除外`, `廣播JSON`); rooms `Join(conn, room)`, `Leave`, `LeaveAll`, `RoomMembers`,
`Rooms`, `BroadcastTo(room, text)`, `BroadcastToExcept` (`加入房間`, `離開房間`, `離開所有房間`, `房間成員`,
`房間列表`, `房間廣播`, `房間廣播除外`; a closed connection leaves its rooms); `Disconnect(conn, code, reason)`,
`Connections`, `ConnectionCount`, `ConnectionInfo(conn)` (`remote`, `path`, `query`, `origin`, `protocol`),
`IsOpen`, `HttpServer()` (`網路伺服器`: add HTTP routes), `Wait`, `Poll`, `Run`, `RunAsync`, `Stop`, `Close`; fields
`Port`/`連接埠`, `Address`/`位址` (`ws://host:port` or `wss://...`), `Id`/`編號`.

Client / 用戶端: `OnOpen`, `OnMessage`, `OnJSON`, `OnBinary`, `OnClose(fn(code, reason))`, `OnError`; `Send`,
`SendJSON`, `SendBytes` (queued while connecting); `State()` (`connecting`, `open`, `closed`), `IsOpen`, `Url`,
`ConnectionId`, `Protocol()` / `子協定()`, `Close()`, `CloseWith(code, reason)`, `Wait`, `Poll`, `RunAsync`; fields
`ReconnectCount`/`重連次數`, `LastError`/`最後錯誤`.

## Tests / 測試

```powershell
aqua run tests/loopback                 # one process, a server and two clients: WEBSOCKET_LOOPBACK_OK
aqua run tests/features.aqua            # RunAsync, ConnectAsync, rooms, Mount, subprotocols: WEBSOCKET_FEATURES_OK
aqua run tests/echo-server.aqua         # then, in another terminal:
aqua run tests/contract.aqua            # Unicode with NUL, bytes, close 4000 "再見": WEBSOCKET_SDK_CONTRACT_OK
aqua build tests/web_client -o client.wasm; node <AquariusLangTW>/scripts/web-run.mjs <web pack> client.wasm   # WEBSOCKET_CLIENT_OK
```

Needs an AquariusLang runtime with the Network library's promise variants, subprotocols, close codes and
`RandomBytes` (the release after 0.1.6).

## From 2.x / 從 2.x 升級

Every 2.x function keeps its names and arguments. The native `aquaws` engine and its binaries are gone (the
`AquaWebSocketNative` module no longer exists); TLS works now (`wss://` clients and servers with a certificate),
Android can run servers, close codes and reasons arrive on both sides, and connection ids are unique across servers
and clients. A 2.x program that called the native module's `Command` directly must use the public API instead.
