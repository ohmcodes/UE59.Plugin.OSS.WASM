# 06 – Developer guide: client and server, Blueprint and C++

This guide is for writing your game against OSS WASM. Every task is shown twice: **Blueprint** and **C++**. It
assumes the plugin is installed and configured (guide 02), a backend is running (guide 01) and you can package a
dedicated server (guide 03) and a web client (guide 04).

## 1. The model in one page

Your game is **one project**, built twice:

| Build | Runs | Talks to the backend for | Plugin object |
|---|---|---|---|
| **Client** (Wasm in the browser, or Windows) | the player's game | login, server list, join ticket, leaderboard reads, cloud saves | `UWasmOssSubsystem` |
| **Dedicated server** (WindowsServer) | the match | register / heartbeat (automatic), ticket checks (automatic), **scores**, listed mode/state | `UWasmOssServerSubsystem` |

```
Client                          Backend                        Dedicated server
Login As Guest  ───────────────▶ player token
Get Server List ───────────────▶ servers (registered by ◀────── auto register + heartbeat)
Join Server ───▶ join ticket ──▶ ClientTravel host:port?WasmOssTicket=... ─▶ PostLogin
                                 verify ticket ◀────────────────────────────── (automatic)
                                                               OnPlayerVerified (your code)
                                 score ◀─────────────────────── Submit Score For Player (Server)
```

Rules of thumb:

- **Login, browsing and joining are client code.** They run on the player's machine (in a menu, a player controller,
  a widget). The dedicated server never logs in.
- **Anything a cheater would want to fake is server code.** Scores, rewards and match results are decided by the
  dedicated server, which submits them with its own credentials. Set the backend's `CLIENT_SCORE_SUBMIT=false` once
  your server submits scores, and clients can no longer post scores at all.
- **Registration, heartbeats and ticket checks are automatic** on a dedicated server. You write no code for them,
  only command-line options (guide 03).
- `UWasmOssServerSubsystem` **only exists on dedicated servers**. On clients, in the editor and on listen servers it is
  null / "None": always check it.

### Where to put code

| You want to run code... | Blueprint | C++ |
|---|---|---|
| only on the dedicated server | Game Mode (exists only on the server), or check **Is Dedicated Server** | `AGameModeBase` subclass, or `IsRunningDedicatedServer()` |
| only on the player's machine | Widgets, a menu Player Controller, or **Is Local Controller** on the player controller | `APlayerController::IsLocalController()`, UI classes |
| on both, different behaviour | **Is Dedicated Server** → Branch | `if (IsRunningDedicatedServer()) { ... } else { ... }` |

C++ setup for everything below: in your game module's `Build.cs` add

```csharp
PublicDependencyModuleNames.AddRange(new string[] { "OnlineSubsystemWASM", "Json" });
```

and include `WasmOssSubsystem.h` (client), `WasmOssServerSubsystem.h` (server) or `WasmOssAsyncActions.h`.
All plugin callbacks run on the **game thread**; capture `TWeakObjectPtr` to your object, because a callback can
arrive after the object was destroyed (map change).

---

## 2. Client tasks

### 2.1 Log in

The player gets a guest account tied to the device/browser. Log in once, early (main menu). The premade main menu
(guide 05) already does this.

**Blueprint** (menu widget or menu Player Controller, *Event Construct* / *Event BeginPlay*):

```
Login As Guest (Display Name = "")          ← empty keeps the saved name; first time the backend picks "Guest-xxxx"
 ├─ On Success → Print/Set Text: Player.Display Name
 └─ On Failure → Print/Set Text: Error
```

To rename later call **Login As Guest** again with the new name. To react anywhere to a login, get
**Wasm Oss Subsystem** (Get Game Instance → Get Subsystem) → **Bind Event to On Login Changed**.
Getters: **Is Logged In**, **Get Player**.

**C++**

```cpp
#include "WasmOssSubsystem.h"

void AMyMenuController::BeginPlay()
{
    Super::BeginPlay();
    if (!IsLocalController()) return;

    if (UWasmOssSubsystem* Oss = UWasmOssSubsystem::Get(this))
    {
        TWeakObjectPtr<AMyMenuController> WeakThis(this);
        Oss->LoginAsGuest(FString(), [WeakThis](bool bOk, const FWasmOssPlayer& Player, const FString& Error)
        {
            if (!WeakThis.IsValid()) return;
            UE_LOG(LogTemp, Log, TEXT("Login: %s"), bOk ? *Player.DisplayName : *Error);
        });
    }
}
```

### 2.2 List servers and join

The premade server browser (guide 05) does all of this. To build your own:

**Blueprint**

```
Get Server List (Region = "", Include Full = false)
 └─ On Success → For Each (Servers) → add a row: Name, Players / Max Players, Map, Id
Row "Join" clicked → Join Server (Player Controller = Get Owning Player, Server Id = row's Id)
 ├─ On Success → nothing to do: the player is travelling (Address = where)
 └─ On Failure → show Error ("Not logged in", server full, backend down, ...)
```

**Join Server** asks for a one-time ticket and calls ClientTravel; the dedicated server checks the ticket.
After a successful join the new map loads, so widgets of the old map are removed; set input back to *Game Only*
before or after travel if your menu set *UI Only* (the premade browser does this).

**C++**

```cpp
void UMyBrowser::Refresh()
{
    UWasmOssSubsystem* Oss = UWasmOssSubsystem::Get(this);
    TWeakObjectPtr<UMyBrowser> WeakThis(this);
    Oss->GetServerList(/*Region*/ FString(), /*bIncludeFull*/ false,
        [WeakThis](bool bOk, const TArray<FWasmOssServerInfo>& Servers, const FString& Error)
        {
            if (UMyBrowser* Self = WeakThis.Get()) { Self->ShowServers(Servers); }
        });
}

void UMyBrowser::Join(const FWasmOssServerInfo& Server)
{
    UWasmOssSubsystem* Oss = UWasmOssSubsystem::Get(this);
    TWeakObjectPtr<APlayerController> WeakPC(GetOwningPlayer());
    Oss->RequestJoinTicket(Server.Id,
        [Oss, WeakPC](bool bOk, const FString& Address, const FString& Ticket, const FString& Error)
        {
            if (bOk && WeakPC.IsValid()) { Oss->TravelToServer(WeakPC.Get(), Address, Ticket); }
        });
}
```

`FWasmOssServerInfo` fields: `Id, Name, Region, Map, Mode, Version, ConnectUrl, Players, MaxPlayers, State`
(`IsFull()` in C++). The list only contains servers with the client's **Game Version**.

### 2.3 Show a leaderboard

**Blueprint**: **Get Leaderboard** (Board, Limit 10, Offset 0) → On Success → For Each Entries →
`Rank`, `Display Name`, `Score`. Total = number of players on the board. Or use the premade leaderboard widget.

**C++**

```cpp
Oss->GetLeaderboard(TEXT("kills"), 10, 0,
    [](bool bOk, const TArray<FWasmOssLeaderboardEntry>& Entries, int32 Total, const FString& Error)
    {
        for (const FWasmOssLeaderboardEntry& E : Entries)
        {
            UE_LOG(LogTemp, Log, TEXT("%d. %s %.0f"), E.Rank, *E.DisplayName, E.Score);
        }
    });
```

Reading needs no login. **Submitting** from the client (**Submit Score** node / `Oss->SubmitScore`) is fine for
single-player and casual games; for multiplayer use the server (section 3.3).

### 2.4 Cloud saves

Saves are per player, per slot (a name you choose), and work across devices only for the same guest account (same
browser / PC).

**Blueprint**

```
Create Save Game Object (your SaveGame class) → set its variables
 → Save Game To Cloud (Save Game, Slot = "progress") → On Success / On Failure (Error)

Load Game From Cloud (Slot = "progress")
 ├─ On Success → Cast To <your SaveGame class> → read variables
 └─ On Failure → the first time there's no save yet: start fresh
```

**C++** (strings; for objects use `UGameplayStatics::SaveGameToMemory` + base64, as the nodes do):

```cpp
Oss->SaveToCloud(TEXT("settings"), TEXT("{\"volume\":0.8}"), [](bool bOk, const FString& Error) {});
Oss->LoadFromCloud(TEXT("settings"), [](bool bOk, const FString& Data, const FString& Error) {});
```

Login is required for saves.

### 2.5 Call your own backend endpoint

Add a route to the backend (`Node.OSSWASMBackend/src/routes/`), then:

**Blueprint**: **Call Backend** (Verb `POST`, Path `/v1/my-game/claim`, Json Body `{"id":"daily"}`,
With Player Token ✓) → On Success: Status, Response Body (parse with a JSON plugin, or return plain values).

**C++**: `Oss->CallBackend(Verb, Path, JsonBody, bWithPlayerToken, [](const FWasmOssHttpResponse& R) { ... })`;
`R.IsOk()`, `R.Status`, `R.Body`, `UWasmOssSubsystem::ParseJson(R.Body)`, `UWasmOssSubsystem::DescribeError(R)`.
To turn it into your own Blueprint node, see guide 02 section 5.

> Don't use engine HTTP (`FHttpModule`) yourself in a web build: it is switched off on Wasm. `CallBackend` works
> on every platform.

---

## 3. Dedicated server tasks

Start the server with the options from guide 03 (`-WasmOssServerKey`, `-WasmOssPublicAddress`, ...). It registers
by itself. Everything below goes into your **Game Mode** (it only exists on the server).

### 3.1 Get the server subsystem

**Blueprint**: **Get Game Instance** → **Get Subsystem** (class *Wasm Oss Server Subsystem*) → **Is Valid**.
It's invalid on clients, on listen servers and in single-process PIE (see section 5 for testing server code).

**C++**

```cpp
#include "WasmOssServerSubsystem.h"

if (UWasmOssServerSubsystem* Server = UWasmOssServerSubsystem::Get(this)) { /* dedicated server */ }
```

Getters: **Is Registered**, **Get Server Id**, **Get Backend Player Id** (Player Controller) — the backend id of a
player who joined with a valid ticket, empty otherwise.

### 3.2 React when a player is verified

When a player joins through the server list, the server checks their ticket in the background (a few ms after
*PostLogin*). On success it sets the PlayerState's name to their display name and fires **On Player Verified**.
Use it to load their data, greet them, or start tracking their score.

**Blueprint** (Game Mode, *Event BeginPlay*):

```
Get Game Instance → Get Subsystem (Wasm Oss Server Subsystem) → Is Valid?
 └─ Bind Event to On Player Verified ── Custom Event "PlayerVerified" (Player Controller, Player Id, Display Name)
                                          └─ Print String: Display Name + " joined"
```

**C++**

```cpp
// MyGameMode.h
UFUNCTION()
void HandlePlayerVerified(APlayerController* PlayerController, const FString& PlayerId, const FString& DisplayName);

// MyGameMode.cpp
void AMyGameMode::BeginPlay()
{
    Super::BeginPlay();
    if (UWasmOssServerSubsystem* Server = UWasmOssServerSubsystem::Get(this))
    {
        Server->OnPlayerVerified.AddUniqueDynamic(this, &AMyGameMode::HandlePlayerVerified);
    }
}

void AMyGameMode::HandlePlayerVerified(APlayerController* PlayerController, const FString& PlayerId, const FString& DisplayName)
{
    UE_LOG(LogTemp, Log, TEXT("%s (%s) joined"), *DisplayName, *PlayerId);
}
```

Players who connect **without** a ticket (`open 127.0.0.1:7777`, LAN) never fire this event. Start the server with
`-WasmOssRequireTicket` to kick them instead.

### 3.3 Submit scores from the server (anti-cheat)

The server knows each verified player's backend id, so it can post their score with its own token. The client
can't fake it.

**Blueprint** (e.g. when a round ends, in the Game Mode):

```
Get All Actors Of Class (PlayerController)  — or Game State → Player Array → Get Player Controller
 └─ For Each → Submit Score For Player (Server) (Player Controller, Board = "kills", Score = <their kills>)
                ├─ On Success → Entry.Rank, Improved
                └─ On Failure → Error ("Player has no verified join ticket", ...)
```

**C++**

```cpp
void AMyGameMode::ReportRoundResults()
{
    UWasmOssServerSubsystem* Server = UWasmOssServerSubsystem::Get(this);
    if (!Server) return;

    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = It->Get();
        const AMyPlayerState* PS = PC ? PC->GetPlayerState<AMyPlayerState>() : nullptr;
        if (!PS) continue;

        Server->SubmitScoreForPlayer(PC, TEXT("kills"), PS->Kills,
            [Name = PS->GetPlayerName()](bool bOk, const FWasmOssLeaderboardEntry& Entry, bool bImproved, const FString& Error)
            {
                UE_LOG(LogTemp, Log, TEXT("%s: %s"), *Name, bOk ? *FString::Printf(TEXT("rank %d"), Entry.Rank) : *Error);
            });
    }
}
```

Each board keeps a player's **best** (highest) score; `Improved` is false when the new score didn't beat it.
Then set `CLIENT_SCORE_SUBMIT=false` on the backend (guide 01).

### 3.4 Show the match state in the server list

By default a server is listed as `open`, or `full` when the player count reaches the maximum. Set your own:

**Blueprint**: server subsystem → **Set Listed Game Mode** ("Deathmatch") and **Set Listed State**
(`open`, `in_progress`, `full`, `closed`; empty string = automatic again).

**C++**

```cpp
Server->SetListedGameMode(TEXT("Deathmatch"));
Server->SetListedState(TEXT("in_progress"));   // match running
// ... after the match
Server->SetListedState(FString());             // back to automatic open/full
```

Clients see it in `FWasmOssServerInfo::Mode` / `State` (the next heartbeat sends it, within a second).
`in_progress` and `closed` servers stay in the list, so filter them in your browser if players shouldn't join them.

---

## 4. A complete minimal setup

**Client side**
1. Menu map with the premade main menu (guide 05), or your own widget using 2.1 and 2.2.
2. A leaderboard widget (premade or 2.3) on the menu.

**Server side** (`AMyGameMode`, C++ or Blueprint)
1. *BeginPlay*: `SetListedGameMode("Deathmatch")`, bind `OnPlayerVerified` (3.2).
2. Match start: `SetListedState("in_progress")`. Match end: `SubmitScoreForPlayer` for everyone (3.3), then
   `SetListedState("")`.

**Run**: backend → dedicated server(s) → clients (guide 03 for Windows, guide 04 for the browser).

## 5. Testing checklist

| Test | How |
|---|---|
| Client code in the editor | PIE (Standalone or New Editor Window). Each PIE window is a different guest (setting *Unique Player Per PIE Instance*). |
| Server code | Package the server (guide 03). In PIE the server subsystem exists only in a separate server process: *Net Mode: Play As Client* with **Run Under One Process** off (Editor Preferences → Level Editor → Play → Multiplayer Options). |
| Join through the list | Packaged server + packaged client, or `-WasmOssAutoJoin` on the client. |
| Web specifics (fetch, relay) | Web build served by `serve-web.ps1` (guide 04). |
| What the backend has | `Invoke-RestMethod http://localhost:3000/v1/servers`, `/v1/leaderboards/<board>` |
| Logs | Category `LogOssWasm`; `-LogCmds="LogOssWasm Verbose"` prints every request. In the browser: F12 → Console. |

## 6. API reference (quick)

**Client – `UWasmOssSubsystem`** (`UWasmOssSubsystem::Get(WorldContext)`; Blueprint: async nodes in *OSS WASM*)

| C++ | Blueprint | Needs login |
|---|---|---|
| `LoginAsGuest(Name, cb)` | Login As Guest | – |
| `IsLoggedIn()`, `GetPlayer()`, `GetBackendUrl()`, `OnLoginChanged` | same names | – |
| `GetServerList(Region, bIncludeFull, cb)` | Get Server List | no |
| `RequestJoinTicket(ServerId, cb)` + `TravelToServer(PC, Address, Ticket)` | Join Server | yes |
| `SubmitScore(Board, Score, cb)` | Submit Score | yes |
| `GetLeaderboard(Board, Limit, Offset, cb)` | Get Leaderboard | no |
| `SaveToCloud(Slot, Data, cb)` / `LoadFromCloud(Slot, cb)` | Save Game To Cloud / Load Game From Cloud | yes |
| `CallBackend(Verb, Path, Json, bWithToken, cb)` | Call Backend | when the route needs it |

**Server – `UWasmOssServerSubsystem`** (`UWasmOssServerSubsystem::Get(WorldContext)`, null off dedicated servers)

| C++ | Blueprint |
|---|---|
| `IsRegistered()`, `GetServerId()`, `GetBackendPlayerId(PC)` | same names |
| `OnPlayerVerified` (PC, PlayerId, DisplayName) | Bind Event to On Player Verified |
| `SubmitScoreForPlayer(PC, Board, Score, cb)` | Submit Score For Player (Server) |
| `SetListedGameMode(Mode)`, `SetListedState(State)` | Set Listed Game Mode, Set Listed State |

Server options (command line / environment): guide 03. Backend settings: guide 01.
