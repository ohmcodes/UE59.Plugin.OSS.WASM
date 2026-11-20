# 03 – Package the dedicated server (and a test client)

The client and the dedicated server are **the same project**. Only the build differs:

| Build | Target | Platform | Engine |
|---|---|---|---|
| Client (test) | `<Project>` (Game) | Win64 | source build |
| Client (release) | `<Project>` (Game) | Wasm (guide 04) | source build |
| Dedicated server | `<Project>Server` (Server) | Win64 | **source build required** |

Launcher (binary) engines cannot build `Server` targets, so use a source build of 5.9 for this project.
Same project + same engine = client and server always match for replication.

The sample [UE59.OSSWASM_SampleBuildClientServer](https://github.com/ohmcodes/UE59.OSSWASM_SampleBuildClientServer)
has all of this set up, with scripts.

## 1. One-time project setup

1. **C++ project** with the plugin installed and enabled (guide 02).
2. **Server target:** add `Source/<Project>Server.Target.cs` next to `<Project>.Target.cs`:

   ```csharp
   using UnrealBuildTool;
   using System.Collections.Generic;

   public class <Project>ServerTarget : TargetRules
   {
       public <Project>ServerTarget(TargetInfo Target) : base(Target)
       {
           Type = TargetType.Server;
           DefaultBuildSettings = BuildSettingsVersion.V8;
           ExtraModuleNames.AddRange(new string[] { "<Project>" });
       }
   }
   ```

   Then right-click the `.uproject` → **Generate Visual Studio project files**.
3. **Server map:** Project Settings → Maps & Modes → **Server Default Map** = your gameplay map
   (the Third Person template defaults it to the empty `Entry` map). In `DefaultEngine.ini`:

   ```ini
   [/Script/EngineSettings.GameMapsSettings]
   ServerDefaultMap=/Game/ThirdPerson/Lvl_ThirdPerson.Lvl_ThirdPerson
   ```
4. **Plugin settings** (guide 02): `BackendUrl` and `GameVersion`. The server registers with `GameVersion`;
   clients only list servers with their own version.

## 2. Package

From the editor: **Platforms → Windows → Package Project**, with the build target set to `<Project>Server`
(and again with `<Project>` for the client).

From the command line (what the sample's `scripts/package.ps1` runs):

```powershell
& "<Engine>\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="<path>\<Project>.uproject" `
  -platform=Win64 -clientconfig=Development `
  -server -serverplatform=Win64 -serverconfig=Development `
  -build -cook -stage -pak -archive -archivedirectory="<path>\Packaged" -unattended -utf8output
```

Output: `Packaged\Windows` (client) and `Packaged\WindowsServer` (server). Add `-noclient` for the server only.
The first package compiles the engine for both builds and takes a long time; later ones are incremental.

## 3. Run the server

```powershell
.\Packaged\WindowsServer\<Project>Server.exe -log -port=7777 `
  -WasmOssBackendUrl=http://localhost:3000 `
  -WasmOssServerKey=dev-server-key `
  -WasmOssPublicAddress=127.0.0.1:7777 `
  -WasmOssServerName="Local 1"
```

| Option (or environment variable) | Meaning |
|---|---|
| `-port=` | UDP port the server listens on (default 7777). One port per server process. |
| `-WasmOssBackendUrl=` | Backend URL (overrides the project setting). |
| `-WasmOssServerKey=` / `OSSWASM_SERVER_KEY` | One of the backend's `SERVER_KEYS`. `dev-server-key` works with a backend in dev mode. |
| `-WasmOssPublicAddress=` / `OSSWASM_PUBLIC_ADDRESS` | `host:port` players connect to. Default `127.0.0.1:<port>` (same PC only). |
| `-WasmOssServerName=` / `OSSWASM_SERVER_NAME` | Name in the server list. |
| `-WasmOssRegion=` / `OSSWASM_REGION` | Region filter value (default `local`). |
| `-WasmOssMaxPlayers=` / `OSSWASM_MAX_PLAYERS` | Override the game session's max players. |
| `-WasmOssRequireTicket` / `OSSWASM_REQUIRE_TICKET=1` | Kick players who didn't join through the server list. |
| `-WasmOssNoRegister` | Don't register (LAN testing). |

In the log (`LogOssWasm`) you should see `Registered 'Local 1' as <id> at 127.0.0.1:7777`. Check it from PowerShell:

```powershell
(Invoke-RestMethod http://localhost:3000/v1/servers).servers
```

The server sends a heartbeat every ~10 s with its player count, re-registers if the backend restarts,
checks each player's join ticket (and sets their player name from it), and unregisters when it shuts down.
If it crashes, the backend drops it after 30 s.

## 4. Test with the packaged client

```powershell
.\Packaged\Windows\<Project>.exe -windowed -resx=1280 -resy=720 -log -WasmOssAutoJoin
```

`-WasmOssAutoJoin` logs in, lists servers and joins the first one with room. Or open the console (`` ` ``) and type:

| Command | Does |
|---|---|
| `wasmoss.Login Alice` | Guest login with a name |
| `wasmoss.Servers` | List servers (also on screen) |
| `wasmoss.Join 0` | Join server 0 from the list (or `wasmoss.Join <server id>`) |
| `wasmoss.AutoJoin` | All three |

Start two clients to see each other move. Clients on the same PC share a save folder and would log in as the same
guest; add `-WasmOssGuestSuffix=2` (3, ...) to the extra clients to make them different players. Run a second server with another port (`-port=7778`) to see two entries in the list.

## 5. Players on other machines

`127.0.0.1` only works on the server's own PC. For players elsewhere:

- **Native (Windows) clients** connect over UDP directly: set `-WasmOssPublicAddress=<public IP or DNS>:7777`,
  forward UDP 7777 on your router, and allow it in Windows Firewall
  (`New-NetFirewallRule -DisplayName "UE server 7777" -Direction Inbound -Protocol UDP -LocalPort 7777 -Action Allow`).
- **Web (Wasm) clients** can't send UDP. They reach the server through a WebSocket relay that turns WebSocket
  messages into UDP (guide 04). The address you register is then the one **the relay** can reach (often a LAN IP).
