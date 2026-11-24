# 05 – Build the premade Blueprints

The plugin ships C++ base classes that hold all the behaviour: login, refresh, join (ticket + travel), the
leaderboard and page switching. You build the Blueprints on top of them; they only add the **layout**. A widget the
base class needs is found **by its name**, so the names below must match exactly (the editor tells you when a
required one is missing: "A required widget binding ... was not found").

| Blueprint | Parent class | What it is |
|---|---|---|
| `WBP_WasmOss_ServerRow` | `WasmOssServerRowWidget` | One server in the list |
| `WBP_WasmOss_ServerBrowser` | `WasmOssServerBrowserWidget` | Server list with refresh and join |
| `WBP_WasmOss_LeaderboardRow` | `WasmOssLeaderboardRowWidget` | One leaderboard line |
| `WBP_WasmOss_Leaderboard` | `WasmOssLeaderboardWidget` | Top scores of one board |
| `WBP_WasmOss_MainMenu` | `WasmOssMainMenuWidget` | Login + pages for the browser and the leaderboard |
| `BP_WasmOss_MenuController` | `WasmOssMenuPlayerController` | Shows the menu with a mouse cursor |
| `BP_WasmOss_MenuGameMode` | `GameModeBase` | Game mode for the menu map |
| `Lvl_WasmOss_MainMenu` | (level) | Where the client starts |

No game instance Blueprint is needed: the plugin's subsystem lives in every game instance already.

## 0. Where to save them

Save everything in the **plugin's** content folder so it travels with the plugin:

1. Content Browser → **Settings** (gear) → tick **Show Plugin Content**.
2. Open the plugin's content folder (**Plugins → OnlineSubsystemWASM Content**, shown as "OSS WASM" in some views).
   The folders `Widgets` and `Maps` are already there.

In the sample project the plugin folder is a link to the plugin repo, so these files land in
`UE59.Plugin.OSS.WASM/Plugins/OnlineSubsystemWASM/Content/` and get committed with the plugin.

To create each widget: right-click → **User Interface → Widget Blueprint** → in "Pick Parent Class" expand
**All Classes**, search the parent class from the table, select it, name the asset.

Every `Text_*`, `Button_*`, `List_*` widget must have **Is Variable** ticked (it is by default for most; check the
checkbox at the top of the Details panel).

## 1. WBP_WasmOss_ServerRow

Hierarchy (Designer → Palette, drag in; rename in the Hierarchy panel with F2):

```
Border                      (Padding 8, Brush Color dark grey, optional)
└─ Horizontal Box
   ├─ Text_Name     (Text Block)   Slot: Size = Fill 1.0
   ├─ Text_Map      (Text Block)   Slot: Size = Fill 0.6        optional
   ├─ Text_Region   (Text Block)   Slot: Size = Auto, Padding 8 optional
   ├─ Text_Players  (Text Block)   Slot: Size = Auto, Padding 8 optional
   └─ Button_Join   (Button)
      └─ Text Block "Join"
```

Required: `Text_Name`, `Button_Join`. In the Designer toolbar set the preview size to **Desired on Screen** so the
row doesn't fill the whole canvas. No graph needed.

Optional styling: Graph → right-click → **Event On Server Set** gives you the server and `Is Full`; for example set the
row's color when it is full. (The Join button is already disabled for full servers.)

## 2. WBP_WasmOss_ServerBrowser

```
Canvas Panel
└─ Border  (Anchors: center, Size 900 x 600, Alignment 0.5/0.5, Padding 16)
   └─ Vertical Box
      ├─ Horizontal Box
      │  ├─ Text Block "Servers"          (font size 28)  Slot: Fill
      │  ├─ CheckBox_ShowFull (Check Box)  + a Text Block "Show full" next to it   optional
      │  ├─ Button_Refresh  (Button) └─ Text Block "Refresh"                        optional
      │  └─ Button_Back     (Button) └─ Text Block "Back"                           optional
      ├─ List_Servers  (Scroll Box)        Slot: Size = Fill 1.0
      ├─ Horizontal Box                    (direct connect, optional)
      │  ├─ TextBox_Address (Editable Text Box)  Hint Text "IP or host, e.g. 192.168.1.20:7777"   Slot: Fill
      │  └─ Button_Connect  (Button) └─ Text Block "Connect"
      └─ Text_Status   (Text Block)
```

Required: `List_Servers` (Scroll Box or Vertical Box), `Text_Status`.
Optional: `TextBox_Address` + `Button_Connect` = **direct connect**: the player types an IP or host name (port defaults
to 7777; `ws://` or `http://` prefixes are ignored) and presses Connect or Enter. No backend call and no join ticket,
so it works for a LAN, a friend's PC-hosted server or an unlisted server. A server started with `-WasmOssRequireTicket`
refuses direct connections. In a browser the address must be reachable from the web relay.

**Class Defaults** (toolbar button) → category *OSS WASM*:

| Property | Set to |
|---|---|
| Row Class | `WBP_WasmOss_ServerRow` (**required**, otherwise the list stays empty) |
| Region | empty = all regions |
| Refresh On Construct | on |
| Auto Refresh Seconds | 10 (0 = only with the Refresh button) |
| Show Full Servers | on |

That's all: clicking a row's Join logs in if needed, gets a join ticket and travels to the server. **Nothing connects
by itself**; the player always picks a server or types an address.
Optional events: **On Status Changed** (to color errors), **On Servers Refreshed**, **On Joining** (play a sound, show
a loading screen; also fires for direct connect). Blueprint function **Connect To Address** does a direct connect from
your own button.

## 3. WBP_WasmOss_LeaderboardRow

```
Horizontal Box
├─ Text_Rank   (Text Block)  Slot: Auto, Padding right 12
├─ Text_Name   (Text Block)  Slot: Fill 1.0
└─ Text_Score  (Text Block)  Slot: Auto
```

Class Defaults: **Score Decimals** (0 for integer scores). Optional: **Event On Entry Set** → if `Is Local Player`,
set the text color of the three texts to highlight your own row.

## 4. WBP_WasmOss_Leaderboard

```
Canvas Panel
└─ Border (center, 600 x 600)
   └─ Vertical Box
      ├─ Horizontal Box
      │  ├─ Text_Title     (Text Block, size 28)  Slot: Fill      optional (shows the board name)
      │  ├─ Button_Refresh (Button) └─ "Refresh"                   optional
      │  └─ Button_Back    (Button) └─ "Back"                      optional
      ├─ List_Entries (Scroll Box)  Slot: Fill 1.0
      └─ Text_Status  (Text Block)
```

Class Defaults: **Row Class** = `WBP_WasmOss_LeaderboardRow`, **Board** = the board your game submits to
(e.g. `default`), **Limit** = 10.

Submitting scores is done by your game with the **Submit Score** node (guide 02), for example when a round ends.
To switch boards at runtime call **Show Board**.

## 5. WBP_WasmOss_MainMenu

The menu is one widget with a **Widget Switcher**. Page 0 is the home page; the browser and the leaderboard are
pages 1 and 2, placed as child widgets.

```
Canvas Panel
└─ Switcher_Pages (Widget Switcher)   Anchors: full screen (fill), offsets 0
   ├─ [0] Vertical Box  (home; center it with a Size Box or Overlay if you like)
   │  ├─ Text Block "My Game"                       (title)
   │  ├─ Text_Player        (Text Block)            "Playing as ..."
   │  ├─ Horizontal Box
   │  │  ├─ TextBox_DisplayName (Editable Text Box)  Hint Text "Your name"   optional
   │  │  └─ Button_Login (Button) └─ "Set name"                               optional
   │  ├─ Button_ItchLogin   (Button) └─ "Log in with itch.io"  (shown only when enabled, see below) optional
   │  ├─ Button_Servers     (Button) └─ "Play online"                         optional
   │  ├─ Button_Leaderboard (Button) └─ "Leaderboard"                          optional
   │  └─ Button_Quit        (Button) └─ "Quit"   (hidden automatically on the web) optional
   ├─ [1] Page_ServerBrowser  (drag WBP_WasmOss_ServerBrowser from Palette → User Created)
   └─ [2] Page_Leaderboard    (drag WBP_WasmOss_Leaderboard from Palette → User Created)
```

Required: `Switcher_Pages`, `Text_Player`. The names `Page_ServerBrowser` and `Page_Leaderboard` connect the
buttons to the pages, and the pages' Back buttons return to page 0. Keep the home page **first** in the switcher.

Class Defaults: **Auto Login** on (logs in as a guest when the menu opens; the backend picks a guest name the first
time, and the name is remembered on this device/browser). Typing a name and pressing **Set name** renames the player.

Optional events: **On Logged In**, **On Login Failed**.

**itch.io login** (optional): add `Button_ItchLogin`. It stays hidden unless **Project Settings > OSS WASM > Login
Providers > Enable Itch Login** is on and the backend has `ITCH_CLIENT_ID` (backend guide 01, section 7). Clicking it
opens itch.io in a new tab; the player approves and returns to the game, which logs in by itself. Clicking the button
again while waiting cancels. Test it after the backend is online (itch needs a real https redirect URI).

## 6. BP_WasmOss_MenuController and BP_WasmOss_MenuGameMode

1. Right-click → **Blueprint Class** → All Classes → `WasmOssMenuPlayerController` → `BP_WasmOss_MenuController`.
   Class Defaults → **Menu Class** = `WBP_WasmOss_MainMenu`.
2. Right-click → **Blueprint Class** → **Game Mode Base** → `BP_WasmOss_MenuGameMode`. Class Defaults:
   - **Player Controller Class** = `BP_WasmOss_MenuController`
   - **Default Pawn Class** = `None` (nothing to walk around with in a menu)
   - **HUD Class** = `HUD` (default)

## 7. The menu map

1. **File → New Level → Empty Level**, save as `Lvl_WasmOss_MainMenu` (in `Maps`). It may stay empty (black
   background), or add a camera/backdrop.
2. **Window → World Settings → GameMode Override** = `BP_WasmOss_MenuGameMode`.
3. **Project Settings → Maps & Modes**:
   - **Game Default Map** = `Lvl_WasmOss_MainMenu` (where clients start)
   - **Server Default Map** stays your gameplay map (`Lvl_ThirdPerson`): the dedicated server never opens the menu.
4. **Project Settings → Packaging → List of maps to include** (Advanced): add both maps, so the cook includes the
   menu map (it isn't referenced by anything else).

After joining, the client loads the server's map with the server's game mode, so the menu disappears by itself. If a
join fails or the server kicks the player, the engine returns to the Game Default Map: the menu again.

## 8. Test

1. Backend running (`npm run dev`), dedicated server running (guide 03, `run-server.ps1`).
2. In the editor open `Lvl_WasmOss_MainMenu` → Play (**Standalone Game** is closest to a packaged client).
   You should see "Playing as Guest-xxxx", then **Play online** lists `Local 7777`; **Join** puts you on the server.
3. Package again (Windows: `package.ps1 -What Client`, web: `-ClientPlatform Wasm`) and test the same in the browser
   (guide 04). The menu replaces the old auto-join (removed). Also test **direct connect**: type `127.0.0.1:7777` and press Enter.

| Problem | Fix |
|---|---|
| "A required widget binding ... was not found" when compiling | Rename the widget exactly as in the tables (case matters) and tick Is Variable. |
| A binding exists but has the wrong type | `List_*` must be a panel (Scroll Box / Vertical Box), `Text_*` a Text Block, `Button_*` a Button, `TextBox_DisplayName` an Editable Text Box, `Switcher_Pages` a Widget Switcher. |
| List stays empty, log says "Row Class is not set" | Set Row Class in the browser's / leaderboard's Class Defaults. |
| Status says "Could not get the server list" | Backend not running, or `BackendUrl` wrong (keep the quotes in the ini, guide 02). |
| "No servers online" though the server runs | The server's `GameVersion` differs from the client's, or it registered with another backend. |
| Menu shows but buttons don't react | The menu map's GameMode Override isn't `BP_WasmOss_MenuGameMode`, so the cursor/input mode is never set. |
