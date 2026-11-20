# 07 – How it fits together: architecture and cross-play

## 1. Three separate pieces

| Piece | What it is | Carries gameplay? | Who needs it |
|---|---|---|---|
| **Backend** (Node, Docker) | The "lobby and services": guest login, the **server list**, join tickets, leaderboards, cloud saves | **No.** It never sees a single gameplay packet | Every client and every dedicated server |
| **Dedicated server** (WindowsServer build of your game) | The match. Runs the game and replicates it to the players | **Yes**, all of it | Every client, during a match |
| **Relay** (`ws_relay.py`, from the engine) | Turns WebSocket into UDP, because browsers can't send UDP | Yes, it forwards packets without reading them | **Browser clients only** |

The backend is **not** a relay and is not in the middle of a match. It only answers short HTTP requests: "who am I",
"which servers exist", "may this player join", "save this score".

## 2. Who talks to whom

**Before the match (HTTP to the backend):**

```
EXE client      ──HTTPS──▶ Backend   login, server list, join ticket
Browser client  ──HTTPS──▶ Backend   login, server list, join ticket   (the plugin uses fetch() in the browser)
Dedicated server ─HTTPS──▶ Backend   register, heartbeat every ~10 s, verify each player's ticket, submit scores
```

**During the match (gameplay):**

```
EXE client      ──UDP──────────────────────────────▶ Dedicated server
Browser client  ──WebSocket──▶ Relay ──UDP──────────▶ Dedicated server   (same server, same port)
```

So the order is **Client → Backend** (find a server, get a ticket), then **Client → Dedicated server** (play),
while the **Dedicated server → Backend** link runs alongside (heartbeats, ticket checks, scores).
It is not "Browser → dedicated server → backend": the client talks to the backend itself, and the server talks to
the backend on its own.

The dedicated server can't tell a browser player from an EXE player: both arrive as ordinary UDP clients. That is
why cross-play needs no extra code.

## 3. Cross-play: what has to match

Unreal lets a client into a server only if their **network version checksum** is identical. The engine builds it
from (`Engine/Source/Runtime/Core/Private/Misc/NetworkVersion.cpp`):

| Part of the checksum | Comes from | Same for EXE and Wasm? |
|---|---|---|
| Project name | your `.uproject` | yes |
| Project Version | **Project Settings → Project → Description → Project Version** | yes, if you don't change it between builds |
| NetCL | the engine build's changelist (`0` on a source build without one) | yes, **if both are built with the same engine** |
| Engine / game network protocol versions | engine code, your code (`GameNetworkVersion`) | yes, same engine and same code |

**What is _not_ in it:** the platform, the build configuration, the cook, shaders, texture formats, compression,
the web page settings. The Wasm build being "optimized" (WebGPU shaders, BC textures, Brotli, IoStore, Shipping)
changes how it renders and loads, not how it replicates. The log line shows it:

```
LogNetVersion: OSSWASM_Sample 1.0.0.0, NetCL: 0, EngineNetworkVersion: 44, GameNetworkVersion: 0 (Checksum: 1664963336)
```

The WindowsServer and the Windows client of the sample print exactly this line, and the web client joined the same
server, so its checksum matched too.

**On top of the checksum, replication must agree:** the server and every client must have the same replicated
classes and properties (C++ and Blueprints). That is automatic when they are packaged **from the same project at the
same commit**.

**The plugin's Game Version** (Project Settings → Plugins → OSS WASM) is a separate, simpler filter: the server
list only shows servers with the client's Game Version. Nothing changes it automatically; packaging for Wasm does not
touch it.

### Rules for a cross-play release

1. Package the WindowsServer, the Windows client and the web client **from the same commit, with the same engine**.
2. Bump **both** versions together whenever gameplay code or replicated data changes:
   - Project Settings → Description → **Project Version** (the engine's network check), and
   - Project Settings → OSS WASM → **Game Version** (the server list filter).
   Then old clients don't see new servers in the list, and if they try anyway the engine refuses them cleanly
   ("version mismatch") instead of letting them desync.
3. Don't mix engines: a client from the Launcher (binary) engine and a server from a source build usually have
   **different NetCL** values, so they won't connect. Use the source build for all three builds.
4. Development and Shipping builds can play together (configuration isn't in the checksum), as long as your own code
   doesn't replicate different things per configuration (e.g. replicated properties inside `#if !UE_BUILD_SHIPPING`).
5. Platform-specific `.ini` overrides (`Platforms/Wasm/Config/...`) are fine for rendering and performance. Don't put
   gameplay values there (speeds, damage, rules): the server uses its own values and the client would mispredict.

### Advanced: allowing compatible versions

`FNetworkVersion::IsNetworkCompatibleOverride` and `GetLocalNetworkVersionOverride` (C++) let a game decide the check
itself, for example to allow 1.0.1 clients on a 1.0.0 server. You rarely need this; bumping both versions is safer.

## 4. Going online: one address, two paths

A dedicated server registers **one** `connectUrl` (`-WasmOssPublicAddress`). Both paths use it:

- EXE clients connect to it **directly** from the internet → it must be the public IP/domain, with the UDP port
  forwarded.
- Browser clients reach it **through the relay** → the relay must be allowed to send UDP to that address
  (`--allow <public IP>/32`). If the relay runs in the same home network as the server, the router must support
  connecting to its own public IP from inside (NAT loopback / hairpin NAT); many do, some don't.

On localhost everything is `127.0.0.1`, so this doesn't show. If your router lacks NAT loopback, put the relay on the
server PC itself (the relay then reaches the server on `127.0.0.1` / the LAN IP while EXE clients use the public
address). A planned improvement is a second, relay-only address per server so each path gets the address that works
for it; see the plugin's progress notes.

## 5. Platforms

| Client | Reaches the server by | Status |
|---|---|---|
| Windows (EXE) | UDP | verified |
| Browser (Wasm: Chrome, Edge) | WebSocket → relay → UDP | verified |
| Browser on phones, Firefox, Safari | same as above | should work where the engine's web platform runs; not tested |
| Mac / Linux / Android builds | UDP | should work (same netcode, same plugin HTTP path); not built or tested |

The dedicated server is always the WindowsServer build (guide 03). No client can host: browsers can't open listening
sockets, and the plugin's server list only lists dedicated servers.

## 6. Why not just use the Steam or EOS online subsystem?

OSS WASM exists so a **web (Wasm) build** can have online features and join dedicated servers. Steam's and Epic's
online subsystems can't do that.

### Why they don't work in a browser build

- **Native SDKs only.** UE's Steam subsystem wraps the Steamworks SDK and the EOS subsystem wraps the EOS SDK. Both are
  native libraries (Windows, Mac, Linux, consoles, mobile); neither is built for WebAssembly. The engine's own web
  docs say online subsystems that need native sockets don't run on the web. (Epic offers web APIs for some EOS
  services; whether those could be used from a browser hasn't been checked, but UE's EOS plugin itself doesn't run
  there.)
- **Native networking.** Steam joins through its relay network (SteamSockets), EOS through its own P2P net driver.
  A browser only has HTTP and WebSockets.
- **Their logins don't fit a browser game.** Steam needs the Steam client running and signed in; EOS needs an Epic
  account (or another platform account linked through Epic). An itch.io player in a browser tab has neither, so
  OSS WASM uses guest accounts tied to the browser or PC.

### What OSS WASM gives you that they don't

- Runs in the browser: HTTP through the browser's `fetch()`, gameplay through the WebSocket relay.
- **Self-hosted**: your backend, your domain, your data; no platform approval, store page or fees.
- One system for PC and browser players together, on the same dedicated servers and leaderboards (section 3).

### What Steam / EOS give that OSS WASM doesn't (yet)

- Real player accounts, friends, invites, presence.
- Voice chat, achievements, stats, anti-cheat.
- NAT traversal and global relays (hosting from home is easier; OSS WASM needs a public address or port forwarding).
- Matchmaking, and scale and uptime run by Valve or Epic.

### Two notes

- **Not a formal `IOnlineSubsystem` (yet).** Steam and EOS implement Unreal's standard online interfaces (Identity,
  Sessions, Leaderboards, ...), so game code written against those can switch between them. OSS WASM is its own game
  instance subsystem plus Blueprint nodes. Implementing the standard interfaces is a possible later step, useful only
  if you want existing session/identity code or marketplace plugins to work with it unchanged.
- **You can use both.** A Steam release can keep Steam for achievements and friends while using OSS WASM for the server
  list and leaderboards, so Steam players and browser players share the same servers and boards. (Not tested yet.)

**In short:** Steam and EOS are platform services for native games. OSS WASM is a small, self-hosted service built for
web games (itch.io and similar), which also works in PC builds, so both can play together.
