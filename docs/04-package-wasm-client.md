# 04 – Package the web (Wasm) client

The web client is the same project as the dedicated server (guide 03), built for the **Wasm** platform
(WebAssembly + WebGPU). Epic's own docs for the platform are in the engine:
`Engine/Platforms/Wasm/Docs/GettingStarted.md` (read sections 2–4 and 7) and the agent skill
`Engine/Platforms/Wasm/Skills/web-deploy/SKILL.md`. This guide is the short path for an OSS WASM game.

## How a browser player reaches your dedicated server

```
Browser (game)  --fetch (HTTPS)------------------->  OSSWASM backend   (login, server list, join ticket)
Browser (game)  --WebSocket ws(s)://relay/udp?to=-->  ws_relay  --UDP-->  dedicated server (normal IpNetDriver)
```

- Browsers can't send UDP. The engine's web socket layer sends each UDP packet as a WebSocket message to a small relay,
  `Engine/Platforms/Wasm/Build/Tools/web/ws_relay.py`, which forwards it as real UDP. The dedicated server needs nothing
  special: it sees the browser as an ordinary UDP client.
- The address a server registers (`-WasmOssPublicAddress`) must therefore be reachable **from the relay**.
- Engine HTTP is switched off on the web. The plugin makes its backend calls with the browser's `fetch()` instead, so
  every Blueprint node works the same in the browser. Your backend must allow the page's origin (CORS, guide 01; the
  defaults already allow `localhost:*` and itch.io).

Web and Windows players can be on the same server at the same time (cross-play). What must match between the builds,
and why the web build's optimizations don't affect it: guide 07.

## 1. One-time setup

1. **emscripten 6.0.10**, exactly (the engine accepts no other version):

   ```powershell
   git clone https://github.com/emscripten-core/emsdk.git D:\emsdk
   cd D:\emsdk
   .\emsdk.bat install 6.0.10
   .\emsdk.bat activate 6.0.10
   [Environment]::SetEnvironmentVariable("WASM_EMSDK_ROOT", "D:\emsdk", "User")
   ```

   Restart the editor and any terminals. Check:
   `<Engine>\Engine\Build\BatchFiles\RunUBT.bat -Mode=ValidatePlatforms -Platforms=Wasm` prints `Wasm VALID`.
2. **Keep `OnlineSubsystemUtils` and `OnlineSubsystemNull` enabled** (they are by default). `OnlineSubsystemUtils` holds
   the `IpNetDriver` that joins a server through the relay.
3. Don't enable `AllToolsets` (the Wasm cook fails on its game feature plugins).
4. Optional web settings: **Project Settings → Platforms → Wasm** (page title, `StartMode=Click`, renderer tiers).

## 2. Package

From the editor: **Platforms → Wasm → Package Project**. From the command line (the sample's
`scripts/package.ps1 -What Client -ClientPlatform Wasm` runs this):

```powershell
& "<Engine>\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="<path>\<Project>.uproject" `
  -platform=Wasm -clientconfig=Development -build -cook -iterate -stage -pak -iostore -package `
  -archive -archivedirectory="<path>\Packaged" -unattended -utf8output `
  -AdditionalCookerOptions="-unattended -nopause -nosplash -nullrhi"
```

- Output: `Packaged\Wasm` (the whole web site) and `Packaged\Wasm-Debug` (symbols; never upload it).
- The first web build compiles the engine for Wasm and takes a long time; the link needs a lot of memory.
- Use `-clientconfig=Shipping` for anything public. Development builds contain source paths, but they accept `?args=`
  in the page URL, which is handy for testing.

## 3. Test on localhost

You need four things running on the same PC: the backend, a dedicated server (guide 03), the relay and a web server for
the page. The sample has a script for the last two:

```powershell
cd <sample>; .\scripts\run-server.ps1          # dedicated server, registers 127.0.0.1:7777
cd <sample>; .\scripts\serve-web.ps1           # relay on ws://localhost:8889 + page on http://localhost:8080
```

Without the script:

```powershell
$py = "<Engine>\Engine\Binaries\ThirdParty\Python3\Win64\python.exe"
& $py -I "<Engine>\Engine\Platforms\Wasm\Build\Tools\web\ws_relay.py"                       # relay, 127.0.0.1:8889
& $py -I "<Engine>\Engine\Platforms\Wasm\Build\Tools\web\serve.py" --root <path>\Packaged\Wasm --port 8080
```

Open `http://localhost:8080/index.html` in **Chrome or Edge** (never the file from disk: the game needs the headers
`serve.py` sends). Then join with your UI, or:

| Way | How |
|---|---|
| Automatic (Development builds) | `http://localhost:8080/index.html?args=-WasmOssAutoJoin` |
| Second player in another tab | `...?args=-WasmOssAutoJoin%20-WasmOssGuestSuffix=2` (tabs share the browser's save data, so they'd be the same guest otherwise) |
| Console | press `` ` `` and type `wasmoss.AutoJoin`, or `wasmoss.Login`, `wasmoss.Servers`, `wasmoss.Join 0` |

Press F12 → Console to see the engine log (`LogOssWasm` lines). The relay logs every connection.

By default the game looks for the relay at **the page's host, port 8889**, and the relay only accepts pages from its own
machine and relays only to loopback and private (LAN) addresses.

## 4. Going online (itch.io)

The page on itch.io is HTTPS, so everything it talks to must be HTTPS / WSS with a valid certificate:

1. **Backend** on HTTPS (guide 01, Nginx Proxy Manager or Cloudflare Tunnel). Set the plugin's `BackendUrl` to it.
2. **Relay** on WSS, next to your dedicated servers, allowed to reach them and to accept the itch.io page:

   ```powershell
   & $py -I ws_relay.py --bind 0.0.0.0 --port 8889 --allow <server network, e.g. 192.168.1.0/24> `
     --origin https://html-classic.itch.zone --tls-cert <cert.pem> --tls-key <key.pem>
   ```

   (or terminate TLS in Nginx Proxy Manager / Cloudflare Tunnel and forward WebSockets to the relay).
3. Tell the game where the relay is: **Project Settings → Platforms → Wasm → Page Args**, append
   `-WebRelay=relay.example.com:443`, and keep the default `-TaskGraphUseDynamicThreadCreation=1 -oversubscriptionratio=1.25 -NoLogThread`.
4. Servers register the address **the relay** reaches (`-WasmOssPublicAddress=192.168.1.20:7777` if the relay is on the
   same LAN).
5. Package **Shipping**, zip the contents of `Packaged\Wasm`, upload as an HTML project on itch.io, and turn on
   **Edit game → Embed options → SharedArrayBuffer support** (check it stayed on after saving).

Phase 6 of the project verifies this path end to end; until then treat section 4 as the plan.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `Network error or blocked by CORS` from a node | Backend down, wrong `BackendUrl`, or the page's origin isn't in `ALLOWED_ORIGINS`. The browser console names the cause. |
| `WEB SOCKETS relay error` / `relay closed` | Relay not running or not at page host :8889 → start it or set `-WebRelay=`. Relay logs `refused`: address outside `--allow`. `refused_origin`: add `--origin`. |
| Joins time out but the list works | The registered server address isn't reachable from the relay (e.g. `127.0.0.1` with the relay on another machine). |
| Blank page, `SharedArrayBuffer is not defined` | Opened from disk, or the host doesn't send COOP/COEP. Use `serve.py`; on itch.io enable SharedArrayBuffer support. |
| `Platform Wasm is not a valid platform` | emsdk 6.0.10 missing or `WASM_EMSDK_ROOT` not seen. Restart the editor/terminal after setting it. |
