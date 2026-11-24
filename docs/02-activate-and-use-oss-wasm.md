# 02 – Activate and use OSS WASM

OSS WASM connects your UE 5.9 game to your own **OSSWASM backend** ([Node.OSSWASMBackend](https://github.com/ohmcodes/Node.OSSWASMBackend)):
guest login, server list, joining a server, leaderboards and cloud saves. It works in the editor, in Windows builds,
on dedicated servers, and in **web (Wasm) builds**.

> **Why the plugin has its own HTTP on the web:** in UE 5.9 the engine's HTTP module is switched off for Wasm
> (`[HTTP] bUseNullHttp=true` in `BaseWasmEngine.ini`), so every engine HTTP request fails there. The plugin calls the
> browser's `fetch()` instead on Wasm, and uses engine HTTP everywhere else. You don't need to do anything for this.

## 1. Install

1. Copy the `OnlineSubsystemWASM` folder into `<YourProject>/Plugins/` (create `Plugins` if needed).
   Your project must be a **C++ project** (Tools → New C++ Class once if it is Blueprint-only).
2. Open the project. When asked to rebuild the missing modules, choose **Yes**.
3. **Edit → Plugins**, search "OSS WASM", make sure it's enabled, restart if asked.

## 2. Configure

**Project Settings → Plugins → OSS WASM**:

| Setting | What to put |
|---|---|
| Backend Url | `http://localhost:3000` while testing; your `https://api.yourdomain.com` for release builds. No trailing slash. |
| Game Version | Your build version, e.g. `1.0`. Players only see servers with the same version. Bump it when client/server compatibility changes. |
| Request Timeout Seconds | Default 15. |
| Unique Player Per PIE Instance | On: each Play-In-Editor window logs in as a different guest, so multiplayer tests show different players. |
| Ticket Option Name | Leave as `WasmOssTicket` unless it clashes with your own URL options. |

These are saved in `Config/DefaultGame.ini`:

```ini
[/Script/OnlineSubsystemWASM.WasmOssSettings]
BackendUrl="http://localhost:3000"
GameVersion=1.0
```

If you edit the ini by hand, **keep the quotes** around the URL: Unreal reads an unquoted `//` as the start of a comment,
so `BackendUrl=http://localhost:3000` becomes `http:` and every request fails (the log shows `backend http:`).
Project Settings writes the quotes for you.

Override at launch with `-WasmOssBackendUrl=https://api.yourdomain.com` (handy for servers and tests).

> On a **web build served over https** (itch.io), Backend Url **must be https**, and the backend must allow the page's
> origin (`ALLOWED_ORIGINS`, guide 01). itch.io is allowed by default.

## 3. Blueprint nodes

Right-click in any Blueprint graph and search **OSS WASM**. Every node has **On Success** and **On Failure** pins;
`Error` is a readable message on failure.

| Node | Inputs | Outputs |
|---|---|---|
| **Login As Guest** | Display Name (empty = keep current) | Player (Id, Display Name), Error |
| **Get Server List** | Region (empty = all), Include Full | Servers (array), Error |
| **Join Server** | Player Controller, Server Id | Address, Error. Gets a one-time ticket, then travels the player to the server. |
| **Submit Score** | Board, Score | Entry (Rank, Score...), Improved, Error |
| **Get Leaderboard** | Board, Limit (1–100), Offset | Entries (array), Total, Error |
| **Save Game To Cloud** | Save Game object, Slot | Error |
| **Load Game From Cloud** | Slot | Save Game object (cast to your class), Error |
| **Call Backend** | Verb, Path, Json Body, With Player Token | Status, Response Body. For your own endpoints. |

Plain getters (from **Get Game Instance → Get Subsystem → Wasm Oss Subsystem**, or search "OSS WASM"):
**Is Logged In**, **Get Player**, **Get Backend Url**, and the **On Login Changed** event.

Login is needed before Join Server, Submit Score and the cloud save nodes. Get Server List and Get Leaderboard work without it.

**Guest identity.** No account or password is needed:

- **In a browser** the plugin keeps a random id in that site's browser storage: the same browser gets the same
  player back. Another browser, a private window or cleared site data is a new player.
- **On PC** the id is derived from the machine (hashed with your project name, so other games can't match it): the
  same PC gets the same player back, even after a reinstall or a deleted save folder.
- **New players get a random name** like `SwiftOtter4821`. Pass a Display Name to Login As Guest to rename; leave it
  empty to keep the current name. Names are cleaned by the backend (no control characters or `< >`, max 24).
- Several local test clients on one PC: start the extra ones with `-WasmOssGuestSuffix=2` (3, ...).

### Minimal test (Level Blueprint)

1. Start the backend locally (guide 01, section 1): `npm run dev` in `Node.OSSWASMBackend`.
2. In your map's Level Blueprint: **Event BeginPlay → Login As Guest** (Display Name `Tester`).
   - On Success → **Print String** with `Player.Display Name`.
   - On Failure → **Print String** with `Error`.
3. From Login's On Success → **Submit Score** (Board `test`, Score `42`) → On Success → **Get Leaderboard** (Board `test`)
   → **For Each Loop** over Entries → Print `Rank`, `Display Name`, `Score`.
4. Press Play. You should see your guest name and the leaderboard printed.
   Check the backend: `Invoke-RestMethod http://localhost:3000/v1/leaderboards/test`.

Premade widgets (server browser, leaderboard, main menu) are covered in guide 05. For full client **and dedicated server** recipes in Blueprint and C++ (server-side scores, On Player Verified, listed match state), see guide 06.

### Login with itch.io (optional)

Needs the backend set up first (backend guide 01, section 7). Blueprint: node **Login With Provider** with
`Provider` = `itch` (call it from a button click so the browser allows the new tab). It opens the itch.io page, waits
until the player finished (up to 10 minutes) and fires On Success with the player, or On Failure with the reason
(cancelled, expired, itch.io error). `guest` works as a provider name too. The premade main menu has an optional
`Button_ItchLogin` (guide 05) that appears only when **Project Settings > OSS WASM > Login Providers > Enable Itch
Login** is on and the backend lists `itch` at `/v1/auth/providers`.

C++: `Subsystem->GetLoginProviders(...)`, `Subsystem->LoginWithProvider(TEXT("itch"), callback)`,
`Subsystem->CancelProviderLogin()`.

## 4. Using it from C++

Add `"OnlineSubsystemWASM"` to your module's `PublicDependencyModuleNames`, then:

```cpp
#include "WasmOssSubsystem.h"

if (UWasmOssSubsystem* Oss = UWasmOssSubsystem::Get(this))
{
    Oss->LoginAsGuest(TEXT("Tester"), [](bool bOk, const FWasmOssPlayer& Player, const FString& Error)
    {
        UE_LOG(LogTemp, Log, TEXT("%s"), bOk ? *Player.DisplayName : *Error);
    });
}
```

All callbacks run on the game thread. Available: `LoginAsGuest`, `GetServerList`, `RequestJoinTicket`, `TravelToServer`,
`SubmitScore`, `GetLeaderboard`, `SaveToCloud`, `LoadFromCloud`, `CallBackend`.

## 5. Adding your own functions (exposed to Blueprints)

Two ways, depending on how much you need:

**A. No C++: the Call Backend node.** If you add an endpoint to your backend (e.g. `POST /v1/my-game/claim-reward`),
call it with **Call Backend** (Verb `POST`, Path `/v1/my-game/claim-reward`, Json Body `{"rewardId":"daily"}`,
With Player Token on) and parse **Response Body** with any JSON plugin.

**B. Your own async node in C++.** Copy the pattern of the plugin's nodes (`WasmOssAsyncActions.h/.cpp`):

```cpp
// MyRewardAction.h  (in your game module)
#pragma once
#include "Kismet/BlueprintAsyncActionBase.h"
#include "MyRewardAction.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMyRewardPin, int32, Coins, const FString&, Error);

UCLASS()
class UMyRewardAction : public UBlueprintAsyncActionBase
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category = "My Game", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"))
    static UMyRewardAction* ClaimDailyReward(UObject* WorldContextObject);

    UPROPERTY(BlueprintAssignable) FMyRewardPin OnSuccess;
    UPROPERTY(BlueprintAssignable) FMyRewardPin OnFailure;

    virtual void Activate() override;
private:
    TWeakObjectPtr<UObject> WorldContext;
};
```

```cpp
// MyRewardAction.cpp
#include "MyRewardAction.h"
#include "WasmOssSubsystem.h"
#include "Dom/JsonObject.h"

UMyRewardAction* UMyRewardAction::ClaimDailyReward(UObject* WorldContextObject)
{
    UMyRewardAction* Action = NewObject<UMyRewardAction>();
    Action->RegisterWithGameInstance(WorldContextObject);
    Action->WorldContext = WorldContextObject;
    return Action;
}

void UMyRewardAction::Activate()
{
    UWasmOssSubsystem* Oss = UWasmOssSubsystem::Get(WorldContext.Get());
    if (!Oss) { OnFailure.Broadcast(0, TEXT("No subsystem")); SetReadyToDestroy(); return; }

    TWeakObjectPtr<UMyRewardAction> WeakThis(this);
    Oss->CallBackend(TEXT("POST"), TEXT("/v1/my-game/claim-reward"), TEXT("{\"rewardId\":\"daily\"}"), /*bWithPlayerToken*/ true,
        [WeakThis](const FWasmOssHttpResponse& Response)
        {
            UMyRewardAction* Self = WeakThis.Get();
            if (!Self) return;
            const TSharedPtr<FJsonObject> Json = Response.IsOk() ? UWasmOssSubsystem::ParseJson(Response.Body) : nullptr;
            if (Json) { Self->OnSuccess.Broadcast(Json->GetIntegerField(TEXT("coins")), FString()); }
            else      { Self->OnFailure.Broadcast(0, UWasmOssSubsystem::DescribeError(Response)); }
            Self->SetReadyToDestroy();
        });
}
```

Your module needs `"OnlineSubsystemWASM"` and `"Json"` in its dependencies. The node then shows up as **Claim Daily Reward**.

For a simple synchronous helper (no network), a `UBlueprintFunctionLibrary` with `UFUNCTION(BlueprintPure)` or
`UFUNCTION(BlueprintCallable)` static functions that call `UWasmOssSubsystem::Get(WorldContext)` works too.

## 6. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `Could not reach the backend` in the editor | Backend not running, or wrong Backend Url. Open `<BackendUrl>/health` in a browser. |
| Web build: `Network error or blocked by CORS` | Browser console shows the reason. Add the page's origin to the backend's `ALLOWED_ORIGINS`, or fix the URL. |
| Web build on https: request blocked as "mixed content" | Backend Url must be `https://`. |
| `Not logged in` | Call **Login As Guest** first and wait for On Success. |
| Same player in every PIE window | Turn on **Unique Player Per PIE Instance**. |
| Log details | `LogOssWasm` category; `-LogCmds="LogOssWasm Verbose"` prints every request. |
