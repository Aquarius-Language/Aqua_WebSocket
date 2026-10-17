# Aqua_WebSocket

星泉（AquariusLang）的跨平台 WebSocket 函式庫，提供伺服器與用戶端，所有函式都有繁體中文和英文兩個名稱。
A cross-platform WebSocket library for AquariusLang. It includes a server and a client, and every function has both a Traditional Chinese and an English name.

| 平台 / Platform | 伺服器 / Server | 用戶端 / Client | 引擎 / Engine |
|---|---|---|---|
| Windows x64 | ✅ | ✅ | `native/windows/aquaws.dll`（Winsock） |
| Linux x64 | ✅ | ✅ | `native/linux/libaquaws.so` |
| Android arm64-v8a / x86_64 | ✅（區域網路 / LAN） | ✅ | `native/android/<abi>/libaquaws.so` |
| 網頁 / Web | — | ✅ | `native/web/aquaws.mjs`（瀏覽器 WebSocket） |

原生引擎以 C 撰寫，獨立運作，不依賴 libc、MSVC 執行階段、NDK 或 MinGW。每個平台的二進位檔都在 50 KB 以內。
The native engine is freestanding C: it needs no libc, MSVC runtime, NDK or MinGW, and each binary is under 50 KB.

它實作 RFC 6455，包括分段訊息、ping/pong 心跳、關閉握手、UTF-8 驗證、Origin 與子協定檢查，以及訊息和連線數上限。
It implements RFC 6455, including fragmentation, ping/pong keepalive, the close handshake, UTF-8 validation, Origin and subprotocol checks, and message-size and connection limits.

## 安裝 / Installation

```toml
[dependencies]
Aqua_WebSocket = { path = "../Aqua_WebSocket", version = ">= 1.0.0, < 2.0.0" }
```

```text
變數 通訊 = 匯入("/bottles/Aqua_WebSocket/main.aqua");
```

如果專案用到網頁或 Android 的平台資產，請用 `aqua build 專案 --target web|android|windows` 或 `--platform linux` 建置。
When a project uses the web or Android platform assets, build it with `aqua build project --target web|android|windows` or `--platform linux`.

## 快速開始 / Quick start

```text
# 伺服器（桌面） / Server (desktop)
變數 通訊 = 匯入("/bottles/Aqua_WebSocket/main.aqua");
變數 伺服器 = 通訊.建立伺服器({"連接埠": 8081});
伺服器.當訊息(函式(連線, 文字) { 伺服器.廣播(文字); });
伺服器.執行(函式() { 真; });          # Run(fn): 每輪呼叫 fn，回傳 假 即停止 / return false to stop

# 用戶端（所有平台） / Client (all platforms)
變數 用戶端 = 通訊.連接並設定("ws://127.0.0.1:8081/", {"自動重連": 真});
用戶端.當開啟(函式() { 用戶端.傳送("你好"); });
用戶端.當訊息(函式(文字) { 印出(文字); });
迴圈 (變數 i = 0; i < 500; i++) { 用戶端.等待(10); }
```

事件只在你呼叫 `輪詢全部 / PollAll`、`等待 / Wait` 或 `執行 / Run` 時才會分派，所以函式庫不會在背後偷偷執行程式碼。
Events are dispatched only when you call `輪詢全部 / PollAll`, `等待 / Wait` or `執行 / Run`, so the library never runs code behind your back.

在 Processing 草圖中，請在每個影格呼叫一次 `通訊.輪詢全部(0)`。
In a Processing sketch, call `通訊.輪詢全部(0)` once per frame.

完整範例請見 `examples/chat`，它有伺服器和用戶端兩個入口。
See `examples/chat` for a complete example with a server entry and a client entry.

```text
aqua build examples/chat --platform linux -o chat.wasm
aqua run chat.wasm                      # 伺服器 / server
aqua run chat.wasm --entry client.aqua  # 用戶端 / client
```

## API

### 模組 / Module (`main.aqua`)

| 中文 | English | 說明 / Description |
|---|---|---|
| `版本()` | `Version()` | 函式庫版本 / library version |
| `載入()` | `Load()` | 載入原生引擎（其他函式會自動呼叫） / load the engine (called automatically) |
| `平台()` | `Platform()` | `windows` / `linux` / `android` / `web` |
| `支援伺服器()` | `SupportsServer()` | 網頁為 假 / false on the web |
| `引擎版本()` | `EngineVersion()` | 原生引擎版本 / engine version |
| `頁面主機()` | `PageHost()` | 網頁：載入此程式的主機；其他平台為 `""` / web: host that served the page |
| `頁面安全()` | `PageIsSecure()` | 網頁是否以 HTTPS 載入 / page loaded over HTTPS |
| `建立伺服器(選項)` | `CreateServer(options)` | 見下表 / see below |
| `連接(網址)` | `Connect(url)` | `連接並設定(網址, {})` |
| `連接並設定(網址, 選項)` | `ConnectWith(url, options)` | 見下表 / see below |
| `輪詢全部(毫秒)` | `PollAll(ms)` | 等待最多 毫秒 並分派所有事件；回傳事件數 / wait up to ms and dispatch |
| `隨機整數()` / `隨機十六進位(n)` | `RandomInt()` / `RandomHex(n)` | 作業系統／瀏覽器的密碼學亂數 / OS or browser CSPRNG |
| `雙語事件(事件)` | `BilingualEvent(event)` | 原始事件轉雙語雜湊表 / raw event → bilingual hash |
| `選項值(選項, 英, 中, 預設)` | `OptionValue(...)` | 讀取雙語選項 / read a bilingual option |
| `關閉全部()` | `CloseAll()` | 關閉所有連線並釋放引擎 / close everything |

### 伺服器選項 / Server options

| 中文鍵 | English key | 預設 / Default |
|---|---|---|
| `主機` | `host` | `"0.0.0.0"` |
| `連接埠` | `port` | `8081`（`0` 表示自動選擇 / auto） |
| `路徑` | `path` | `""`（任何路徑 / any） |
| `來源` | `origins` | `[]`（全部允許；瀏覽器以外的用戶端可以不送 Origin / allow all） |
| `子協定` | `protocols` | `[]` |
| `訊息上限` | `maxMessage` | `524288` bytes |
| `連線上限` | `maxConnections` | `256` |
| `心跳間隔` | `pingInterval` | `25000` ms |

### 伺服器物件 / Server object

欄位（Fields）：`連接埠/Port`、`位址/Address`、`編號/Id`。

| 中文 | English |
|---|---|
| `當連線(函式(連線, 資訊))` | `OnOpen(fn(conn, info))`：資訊含 remote、path、query、origin、protocol |
| `當訊息(函式(連線, 文字))` | `OnMessage(fn(conn, text))` |
| `當二進位(函式(連線, 位元組))` | `OnBinary(fn(conn, bytes))`：位元組為 0–255 整數陣列 / array of 0–255 |
| `當關閉(函式(連線, 代碼, 原因))` | `OnClose(fn(conn, code, reason))` |
| `當錯誤(函式(訊息))` | `OnError(fn(message))` |
| `傳送(連線, 文字)`、`傳送JSON`、`傳送位元組` | `Send`, `SendJSON`, `SendBytes` |
| `群發(連線們, 文字)` | `SendMany(conns, text)` |
| `廣播(文字)`、`廣播JSON(值)`、`廣播除外(文字, 排除連線)` | `Broadcast`, `BroadcastJSON`, `BroadcastExcept(text, exclude)` |
| `中斷連線(連線, 代碼, 原因)` | `Disconnect(conn, code, reason)` |
| `連線列表()`、`連線數()`、`連線資訊(連線)`、`是否開啟()` | `Connections`, `ConnectionCount`, `ConnectionInfo`, `IsOpen` |
| `等待(毫秒)`、`輪詢()`、`執行(函式)`、`停止()`、`關閉()` | `Wait`, `Poll`, `Run`, `Stop`, `Close` |

沒有設定回呼的事件會累積起來，由 `等待()` 回傳。
Events that have no callback are queued and returned by `等待()`.

### 用戶端選項與物件 / Client options and object

選項（Options）：

| 中文鍵 | English key | 說明 / Description |
|---|---|---|
| `自動重連` | `autoReconnect` | 斷線後自動重連 / reconnect after a drop |
| `重連間隔` | `reconnectDelay` | 預設 2000 ms / default 2000 ms |
| `連線中佇列` | `queueWhileConnecting` | 預設 真；開啟前送出的訊息會先排隊 / default true: queue messages sent before open |
| `子協定` | `protocols` | 要求的子協定 / requested subprotocols |
| `來源` | `origin` | Origin 標頭（原生） / Origin header (native) |
| `標頭` | `headers` | 額外的握手標頭（原生） / extra handshake headers (native) |
| `逾時` | `timeout` | 預設 10000 ms / default 10000 ms |
| `訊息上限` | `maxMessage` | 單則訊息上限 / message-size limit |
| `心跳間隔` | `pingInterval` | 心跳間隔 / keepalive interval |

方法（Methods）：

| 中文 | English |
|---|---|
| `當開啟`、`當訊息`、`當JSON`、`當二進位`、`當關閉(函式(代碼, 原因))`、`當錯誤` | `OnOpen`, `OnMessage`, `OnJSON`, `OnBinary`, `OnClose`, `OnError` |
| `傳送(文字)`、`傳送JSON(值)`、`傳送位元組(位元組)` | `Send`, `SendJSON`, `SendBytes` |
| `狀態()`、`已連線()`、`網址()`、`連線編號()` | `State`（connecting/open/closed）, `IsOpen`, `Url`, `ConnectionId` |
| `等待(毫秒)`、`輪詢()`、`關閉()`、`關閉並說明(代碼, 原因)` | `Wait`, `Poll`, `Close`, `CloseWith` |

欄位（Fields）：`重連次數/ReconnectCount`、`最後錯誤/LastError`。

## 限制 / Limitations

- 原生引擎不含 TLS：在桌面和 Android 上請使用 `ws://`。需要 `wss://` 時，請在前面放一個負責 TLS 的反向代理。網頁版會使用瀏覽器內建的 TLS。
  The native engine has no TLS. Use `ws://` on desktop and Android, and put a TLS-terminating reverse proxy in front when you need `wss://`. The web build uses the browser's TLS.
- 原生引擎不內建 DNS 查詢。遇到主機名稱時，函式庫會透過星泉的 `網路.解析主機` 解析，並優先選用 IPv4。
  The native engine has no DNS resolver built in. Host names are resolved through Aquarius `網路.解析主機`, preferring IPv4.
- 不支援 permessage-deflate 壓縮。
  The permessage-deflate extension is not supported.

## 原生引擎 / Native engine

- 原始碼：`native/src/`（`aq_plat.c` 平台層、`aq_json.c`、`aquaws.c`）
  Source: `native/src/` (`aq_plat.c` platform layer, `aq_json.c`, `aquaws.c`)
- 介面採用星泉 DynamicLibrary ABI 1：每個匯出函式都接收並回傳 UTF-8 JSON。網頁轉接層 `aquaws.mjs` 遵守同一份 JSON 契約。
  Interface: Aquarius DynamicLibrary ABI 1, where every export takes and returns UTF-8 JSON. The web adapter `aquaws.mjs` follows the same JSON contract.
- 重新建置需要 LLVM 15 以上版本的 clang、lld、llvm-dlltool 和 llvm-strip，然後執行 `native/build/build.sh`。Windows 使用者可在 Git Bash 或 WSL 中執行同一個腳本。
  To rebuild, install clang, lld, llvm-dlltool and llvm-strip (LLVM 15+) and run `native/build/build.sh`. Windows users can run the same script from Git Bash or WSL.
- 測試：`native/tests/`
  Tests: `native/tests/`
  - `test_ws_self.c`：Linux 迴路測試 / Linux loopback
  - `test_ws_win.c`：Windows 或 wine
  - `test_ws_static.c`：arm64 搭配 qemu / arm64 under qemu
  - `ws_browser.mjs`：Chromium 用戶端對原生伺服器 / Chromium client against the native server

## 測試 / Tests

- `tests/loopback`：桌面伺服器與用戶端，輸出 `WEBSOCKET_LOOPBACK_OK`。
  Desktop server and client; prints `WEBSOCKET_LOOPBACK_OK`.
- `tests/web_client`：網頁用戶端，輸出 `WEBSOCKET_CLIENT_OK`。
  Web client; prints `WEBSOCKET_CLIENT_OK`.

## 授權 / License

MIT，見 `LICENSE`。
MIT; see `LICENSE`.
