# OSS WASM – self-hostable online services for UE 5.9 web games

Unreal Engine 5.9 plugin (`Plugins/OnlineSubsystemWASM`) that connects your game to your own
[OSSWASM backend](https://github.com/ohmcodes/Node.OSSWASMBackend): guest login, dedicated-server list, join tickets,
leaderboards and cloud saves. Built for games packaged for the web (Wasm/WebGPU, e.g. on itch.io), and works on every
native platform too.

- **Blueprint nodes** for everything (Login As Guest, Get Server List, Join Server, Submit Score, Get Leaderboard,
  Save/Load Game To Cloud, Call Backend), plus a C++ API you can extend.
- **Works in the browser:** UE 5.9 turns engine HTTP off on Wasm; the plugin uses the browser's `fetch()` there.
- **Your infrastructure:** you run the backend (one Docker container) on your own server and domain.

This repo is also a small C++ host project (`UE59PluginOSSWASM.uproject`) used to develop the plugin.
A full sample game (Third Person, web client + dedicated server) is in
[UE59.OSSWASM_SampleBuildClientServer](https://github.com/ohmcodes/UE59.OSSWASM_SampleBuildClientServer).

## Guides
| # | Guide |
|---|---|
| 01 | [Deploy the backend](https://github.com/ohmcodes/Node.OSSWASMBackend/blob/main/docs/01-deploy-backend.md) |
| 02 | [Activate and use OSS WASM](docs/02-activate-and-use-oss-wasm.md) |
| 03 | [Package the dedicated server (and a test client)](docs/03-package-dedicated-server.md) |
| 04 | [Package the web (Wasm) client](docs/04-package-wasm-client.md) |
| 05 | [Build the premade Blueprints](docs/05-build-premade-blueprints.md) |
| 06 | [Developer guide: client and server, Blueprint and C++](docs/06-developer-guide.md) |

## Requirements
- Unreal Engine 5.9 (tested with 5.9 Preview 2 source build), C++ project.
- An OSSWASM backend (run it locally with `npm run dev` for testing).

## Building the host project from the command line
```powershell
./scripts/build.ps1                                  # Editor, Win64
./scripts/build.ps1 -Target Game -Platform Wasm      # web build (needs the Wasm platform SDK set up)
```
Engine path defaults to `D:\UE5 Source\UE59Prev2_SRC`; set `OSSWASM_ENGINE` to change it. A short summary is written to `scripts/out/*.summary.txt`.

## License
MIT
