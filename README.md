# Aqua_WebSocket 2 / 星泉網頁通訊端

Import `AquaWebSocket` / `星泉網頁通訊端` through a version 2 Bottle dependency. The existing bilingual server, client, reconnect, queued-send, JSON, binary, broadcast and polling APIs are retained. The native C engine registers `AquaWebSocketNative` through `<aqua/aqua.h>`; the web uses C for connection state, request parsing, queues, timeouts and JSON, calling only the browser WebSocket API through AquariusLang's unified network glue.

```aquarius
var ws = import("AquaWebSocket");
var server = ws.CreateServer({"host":"127.0.0.1", "port":8081});
server.OnMessage(function(id,text) { server.Send(id,text); });
server.Run(function() { return true; });
```

Windows, Linux and Android provide the native client/server engine. Browser applications provide clients, including `wss`; they cannot listen for connections. The native engine's TLS capability remains unchanged (`false`). Use `SupportsServer()` / `支援伺服器()` and `Platform()` / `平台()` to inspect capabilities.

Build native parts with the current Aquarius SDK and its pinned LLVM toolchains:

```powershell
aqua library build . --targets windows-x64,web,android-arm64-v8a,android-x86_64
aqua run tests/loopback/main.aqua
```

Build `linux-x64` in Linux/WSL. Native outputs live in `native/<target>/`, and Bottle deploys them. The only production native export is `aqua_library_entry`; no DynamicLibrary loader, application-level buffer ABI or JavaScript library is used.

The web part requires the current AquariusLang unified network glue with subprotocol and page-location Web API calls. Run `tests/echo-server.aqua` on a native target, then `tests/contract.aqua` natively or in a browser. The contract verifies Unicode and embedded NULs, binary bytes, close codes/reasons and secure random output. Successful runs print `WEBSOCKET_SDK_CONTRACT_OK`; loopback prints `WEBSOCKET_LOOPBACK_OK`.

The old private DynamicLibrary C harnesses under `native/tests/` describe the retired entry points; the supported public SDK integration programs are under `tests/`.
