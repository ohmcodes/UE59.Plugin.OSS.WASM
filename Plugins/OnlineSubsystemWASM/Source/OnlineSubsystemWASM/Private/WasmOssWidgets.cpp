// Base classes for the premade widgets (guide 05). The Blueprints the developer builds only provide the layout;
// all behaviour lives here so it stays the same on every platform.

#include "WasmOssServerBrowserWidget.h"
#include "WasmOssLeaderboardWidget.h"
#include "WasmOssMainMenuWidget.h"
#include "WasmOssMenuPlayerController.h"
#include "WasmOssSubsystem.h"
#include "OnlineSubsystemWASMLog.h"

#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/EditableTextBox.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/PackageName.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "WasmOssWidgets"

namespace WasmOssWidgets
{
	static void SetTextIfBound(UTextBlock* Text, const FText& Value)
	{
		if (Text)
		{
			Text->SetText(Value);
		}
	}
}

// =====================================================================================================================
// Server row
// =====================================================================================================================

void UWasmOssServerRowWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (Button_Join)
	{
		Button_Join->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleJoinClicked);
	}
}

void UWasmOssServerRowWidget::SetServer(const FWasmOssServerInfo& InServer)
{
	Server = InServer;
	const bool bFull = Server.IsFull();
	WasmOssWidgets::SetTextIfBound(Text_Name, FText::FromString(Server.Name));
	WasmOssWidgets::SetTextIfBound(Text_Players, FText::Format(LOCTEXT("Players", "{0}/{1}"), Server.Players, Server.MaxPlayers));
	WasmOssWidgets::SetTextIfBound(Text_Map, FText::FromString(FPackageName::GetShortName(Server.Map)));
	WasmOssWidgets::SetTextIfBound(Text_Region, FText::FromString(Server.Region));
	if (Button_Join)
	{
		Button_Join->SetIsEnabled(!bFull);
	}
	OnServerSet(Server, bFull);
}

void UWasmOssServerRowWidget::HandleJoinClicked()
{
	OnJoinClicked.Broadcast(Server);
}

// =====================================================================================================================
// Server browser
// =====================================================================================================================

void UWasmOssServerBrowserWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (Button_Refresh)
	{
		Button_Refresh->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleRefreshClicked);
	}
	if (Button_Back)
	{
		Button_Back->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleBackClicked);
	}
	if (CheckBox_ShowFull)
	{
		CheckBox_ShowFull->SetIsChecked(bShowFullServers);
		CheckBox_ShowFull->OnCheckStateChanged.AddUniqueDynamic(this, &ThisClass::HandleShowFullChanged);
	}
}

void UWasmOssServerBrowserWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (bRefreshOnConstruct)
	{
		Refresh();
	}
	if (AutoRefreshSeconds > 0.f)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(RefreshTimer, FTimerDelegate::CreateUObject(this, &ThisClass::Refresh), AutoRefreshSeconds, true);
		}
	}
}

void UWasmOssServerBrowserWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
	}
	Super::NativeDestruct();
}

void UWasmOssServerBrowserWidget::SetStatus(const FText& Status, bool bIsError)
{
	WasmOssWidgets::SetTextIfBound(Text_Status, Status);
	OnStatusChanged(Status, bIsError);
}

void UWasmOssServerBrowserWidget::Refresh()
{
	UWasmOssSubsystem* Subsystem = UWasmOssSubsystem::Get(this);
	if (!Subsystem || bBusy)
	{
		return;
	}
	if (!Subsystem->IsLoggedIn())
	{
		bBusy = true;
		SetStatus(LOCTEXT("LoggingIn", "Logging in..."));
		TWeakObjectPtr<UWasmOssServerBrowserWidget> WeakThis(this);
		Subsystem->LoginAsGuest(FString(), [WeakThis](bool bOk, const FWasmOssPlayer&, const FString& Error)
		{
			if (UWasmOssServerBrowserWidget* This = WeakThis.Get())
			{
				This->bBusy = false;
				if (bOk)
				{
					This->FetchList();
				}
				else
				{
					This->SetStatus(FText::Format(LOCTEXT("LoginFailed", "Login failed: {0}"), FText::FromString(Error)), true);
				}
			}
		});
		return;
	}
	FetchList();
}

void UWasmOssServerBrowserWidget::FetchList()
{
	UWasmOssSubsystem* Subsystem = UWasmOssSubsystem::Get(this);
	if (!Subsystem)
	{
		return;
	}
	bBusy = true;
	if (Servers.Num() == 0)
	{
		SetStatus(LOCTEXT("Searching", "Looking for servers..."));
	}
	const bool bIncludeFull = CheckBox_ShowFull ? CheckBox_ShowFull->IsChecked() : bShowFullServers;
	TWeakObjectPtr<UWasmOssServerBrowserWidget> WeakThis(this);
	Subsystem->GetServerList(Region, bIncludeFull, [WeakThis](bool bOk, const TArray<FWasmOssServerInfo>& NewServers, const FString& Error)
	{
		UWasmOssServerBrowserWidget* This = WeakThis.Get();
		if (!This)
		{
			return;
		}
		This->bBusy = false;
		if (!bOk)
		{
			This->SetStatus(FText::Format(LOCTEXT("ListFailed", "Could not get the server list: {0}"), FText::FromString(Error)), true);
			return;
		}
		This->Servers = NewServers;
		This->Rebuild();
		This->SetStatus(NewServers.Num() == 0
			? LOCTEXT("NoServers", "No servers online right now.")
			: FText::Format(LOCTEXT("ServerCount", "{0} {0}|plural(one=server,other=servers) online"), NewServers.Num()));
		This->OnServersRefreshed(This->Servers);
	});
}

void UWasmOssServerBrowserWidget::Rebuild()
{
	if (!List_Servers)
	{
		return;
	}
	List_Servers->ClearChildren();
	if (!RowClass)
	{
		UE_LOG(LogOssWasm, Warning, TEXT("%s: Row Class is not set, so the list stays empty (guide 05)."), *GetName());
		return;
	}
	for (const FWasmOssServerInfo& Server : Servers)
	{
		UWasmOssServerRowWidget* Row = CreateWidget<UWasmOssServerRowWidget>(this, RowClass);
		if (Row)
		{
			Row->SetServer(Server);
			Row->OnJoinClicked.AddUniqueDynamic(this, &ThisClass::HandleRowJoin);
			List_Servers->AddChild(Row);
		}
	}
}

void UWasmOssServerBrowserWidget::JoinServer(const FWasmOssServerInfo& Server)
{
	UWasmOssSubsystem* Subsystem = UWasmOssSubsystem::Get(this);
	if (!Subsystem || bBusy)
	{
		return;
	}
	if (Server.IsFull())
	{
		SetStatus(LOCTEXT("Full", "That server is full."), true);
		return;
	}
	if (!Subsystem->IsLoggedIn())
	{
		bBusy = true;
		TWeakObjectPtr<UWasmOssServerBrowserWidget> WeakThis(this);
		Subsystem->LoginAsGuest(FString(), [WeakThis, Server](bool bOk, const FWasmOssPlayer&, const FString& Error)
		{
			if (UWasmOssServerBrowserWidget* This = WeakThis.Get())
			{
				This->bBusy = false;
				if (bOk)
				{
					This->RequestTicket(Server);
				}
				else
				{
					This->SetStatus(FText::Format(LOCTEXT("LoginFailed", "Login failed: {0}"), FText::FromString(Error)), true);
				}
			}
		});
		return;
	}
	RequestTicket(Server);
}

void UWasmOssServerBrowserWidget::RequestTicket(const FWasmOssServerInfo& Server)
{
	UWasmOssSubsystem* Subsystem = UWasmOssSubsystem::Get(this);
	if (!Subsystem)
	{
		return;
	}
	bBusy = true;
	SetStatus(FText::Format(LOCTEXT("Joining", "Joining {0}..."), FText::FromString(Server.Name)));
	TWeakObjectPtr<UWasmOssServerBrowserWidget> WeakThis(this);
	Subsystem->RequestJoinTicket(Server.Id, [WeakThis, Server](bool bOk, const FString& Address, const FString& Ticket, const FString& Error)
	{
		UWasmOssServerBrowserWidget* This = WeakThis.Get();
		if (!This)
		{
			return;
		}
		This->bBusy = false;
		if (!bOk)
		{
			This->SetStatus(FText::Format(LOCTEXT("JoinFailed", "Could not join: {0}"), FText::FromString(Error)), true);
			This->Refresh();
			return;
		}
		APlayerController* PlayerController = This->GetOwningPlayer();
		UWasmOssSubsystem* Inner = UWasmOssSubsystem::Get(This);
		This->OnJoining(Server);
		if (!Inner || !Inner->TravelToServer(PlayerController, Address, Ticket))
		{
			This->SetStatus(LOCTEXT("TravelFailed", "Could not travel to the server."), true);
			return;
		}
		// The game map takes over input after the travel; give it the mouse now.
		if (PlayerController)
		{
			PlayerController->SetInputMode(FInputModeGameOnly());
			PlayerController->SetShowMouseCursor(false);
		}
	});
}

void UWasmOssServerBrowserWidget::HandleRefreshClicked()
{
	Refresh();
}

void UWasmOssServerBrowserWidget::HandleBackClicked()
{
	OnBackRequested.Broadcast();
}

void UWasmOssServerBrowserWidget::HandleShowFullChanged(bool bIsChecked)
{
	bShowFullServers = bIsChecked;
	Refresh();
}

void UWasmOssServerBrowserWidget::HandleRowJoin(const FWasmOssServerInfo& Server)
{
	JoinServer(Server);
}

// =====================================================================================================================
// Leaderboard
// =====================================================================================================================

void UWasmOssLeaderboardRowWidget::SetEntry(const FWasmOssLeaderboardEntry& InEntry, bool bInIsLocalPlayer)
{
	Entry = InEntry;
	bIsLocalPlayer = bInIsLocalPlayer;
	FNumberFormattingOptions Format;
	Format.MinimumFractionalDigits = ScoreDecimals;
	Format.MaximumFractionalDigits = ScoreDecimals;
	WasmOssWidgets::SetTextIfBound(Text_Rank, FText::AsNumber(Entry.Rank));
	WasmOssWidgets::SetTextIfBound(Text_Name, FText::FromString(Entry.DisplayName));
	WasmOssWidgets::SetTextIfBound(Text_Score, FText::AsNumber(Entry.Score, &Format));
	OnEntrySet(Entry, bIsLocalPlayer);
}

void UWasmOssLeaderboardWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (Button_Refresh)
	{
		Button_Refresh->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleRefreshClicked);
	}
	if (Button_Back)
	{
		Button_Back->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleBackClicked);
	}
}

void UWasmOssLeaderboardWidget::NativeConstruct()
{
	Super::NativeConstruct();
	WasmOssWidgets::SetTextIfBound(Text_Title, FText::FromString(Board));
	if (bRefreshOnConstruct)
	{
		Refresh();
	}
}

void UWasmOssLeaderboardWidget::SetStatus(const FText& Status, bool bIsError)
{
	WasmOssWidgets::SetTextIfBound(Text_Status, Status);
	OnStatusChanged(Status, bIsError);
}

void UWasmOssLeaderboardWidget::ShowBoard(const FString& NewBoard)
{
	Board = NewBoard;
	WasmOssWidgets::SetTextIfBound(Text_Title, FText::FromString(Board));
	Refresh();
}

void UWasmOssLeaderboardWidget::Refresh()
{
	UWasmOssSubsystem* Subsystem = UWasmOssSubsystem::Get(this);
	if (!Subsystem || bBusy)
	{
		return;
	}
	bBusy = true;
	SetStatus(LOCTEXT("LoadingBoard", "Loading..."));
	TWeakObjectPtr<UWasmOssLeaderboardWidget> WeakThis(this);
	Subsystem->GetLeaderboard(Board, Limit, 0, [WeakThis](bool bOk, const TArray<FWasmOssLeaderboardEntry>& NewEntries, int32 Total, const FString& Error)
	{
		UWasmOssLeaderboardWidget* This = WeakThis.Get();
		if (!This)
		{
			return;
		}
		This->bBusy = false;
		if (!bOk)
		{
			This->SetStatus(FText::Format(LOCTEXT("BoardFailed", "Could not load the leaderboard: {0}"), FText::FromString(Error)), true);
			return;
		}
		This->Entries = NewEntries;
		if (This->List_Entries)
		{
			This->List_Entries->ClearChildren();
			const UWasmOssSubsystem* Inner = UWasmOssSubsystem::Get(This);
			const FString LocalId = Inner ? Inner->GetPlayer().Id : FString();
			if (!This->RowClass)
			{
				UE_LOG(LogOssWasm, Warning, TEXT("%s: Row Class is not set, so the list stays empty (guide 05)."), *This->GetName());
			}
			for (const FWasmOssLeaderboardEntry& Entry : NewEntries)
			{
				UWasmOssLeaderboardRowWidget* Row = This->RowClass ? CreateWidget<UWasmOssLeaderboardRowWidget>(This, This->RowClass) : nullptr;
				if (Row)
				{
					Row->SetEntry(Entry, !LocalId.IsEmpty() && Entry.PlayerId == LocalId);
					This->List_Entries->AddChild(Row);
				}
			}
		}
		This->SetStatus(NewEntries.Num() == 0 ? LOCTEXT("NoScores", "No scores yet.") : FText::GetEmpty());
		This->OnLeaderboardRefreshed(This->Entries, Total);
	});
}

void UWasmOssLeaderboardWidget::HandleRefreshClicked()
{
	Refresh();
}

void UWasmOssLeaderboardWidget::HandleBackClicked()
{
	OnBackRequested.Broadcast();
}

// =====================================================================================================================
// Main menu
// =====================================================================================================================

void UWasmOssMainMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (Button_Login)
	{
		Button_Login->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleLoginClicked);
	}
	if (Button_Servers)
	{
		Button_Servers->OnClicked.AddUniqueDynamic(this, &ThisClass::ShowServerBrowser);
	}
	if (Button_Leaderboard)
	{
		Button_Leaderboard->OnClicked.AddUniqueDynamic(this, &ThisClass::ShowLeaderboard);
	}
	if (Button_Quit)
	{
#if PLATFORM_WASM
		Button_Quit->SetVisibility(ESlateVisibility::Collapsed); // a web page can't quit; the player closes the tab
#else
		Button_Quit->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleQuitClicked);
#endif
	}
	if (Page_ServerBrowser)
	{
		Page_ServerBrowser->OnBackRequested.AddUniqueDynamic(this, &ThisClass::ShowHome);
	}
	if (Page_Leaderboard)
	{
		Page_Leaderboard->OnBackRequested.AddUniqueDynamic(this, &ThisClass::ShowHome);
	}
	if (UWasmOssSubsystem* Subsystem = UWasmOssSubsystem::Get(this))
	{
		Subsystem->OnLoginChanged.AddUniqueDynamic(this, &ThisClass::HandleLoginChanged);
	}
}

void UWasmOssMainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ShowHome();
	ShowPlayer();
	const UWasmOssSubsystem* Subsystem = UWasmOssSubsystem::Get(this);
	if (bAutoLogin && Subsystem && !Subsystem->IsLoggedIn())
	{
		Login();
	}
}

void UWasmOssMainMenuWidget::Login()
{
	UWasmOssSubsystem* Subsystem = UWasmOssSubsystem::Get(this);
	if (!Subsystem || bLoggingIn)
	{
		return;
	}
	bLoggingIn = true;
	if (Button_Login)
	{
		Button_Login->SetIsEnabled(false);
	}
	WasmOssWidgets::SetTextIfBound(Text_Player, LOCTEXT("MenuLoggingIn", "Logging in..."));
	const FString Name = TextBox_DisplayName ? TextBox_DisplayName->GetText().ToString().TrimStartAndEnd() : FString();
	TWeakObjectPtr<UWasmOssMainMenuWidget> WeakThis(this);
	Subsystem->LoginAsGuest(Name, [WeakThis](bool bOk, const FWasmOssPlayer& Player, const FString& Error)
	{
		UWasmOssMainMenuWidget* This = WeakThis.Get();
		if (!This)
		{
			return;
		}
		This->bLoggingIn = false;
		if (This->Button_Login)
		{
			This->Button_Login->SetIsEnabled(true);
		}
		if (bOk)
		{
			This->ShowPlayer(); // OnLoggedIn fires through OnLoginChanged
		}
		else
		{
			WasmOssWidgets::SetTextIfBound(This->Text_Player, FText::Format(LOCTEXT("MenuLoginFailed", "Login failed: {0}"), FText::FromString(Error)));
			This->OnLoginFailed(Error);
		}
	});
}

void UWasmOssMainMenuWidget::ShowPlayer()
{
	const UWasmOssSubsystem* Subsystem = UWasmOssSubsystem::Get(this);
	if (!Subsystem || !Subsystem->IsLoggedIn())
	{
		WasmOssWidgets::SetTextIfBound(Text_Player, LOCTEXT("NotLoggedIn", "Not logged in"));
		return;
	}
	const FWasmOssPlayer Player = Subsystem->GetPlayer();
	WasmOssWidgets::SetTextIfBound(Text_Player, FText::Format(LOCTEXT("PlayingAs", "Playing as {0}"), FText::FromString(Player.DisplayName)));
	if (TextBox_DisplayName && TextBox_DisplayName->GetText().IsEmpty())
	{
		TextBox_DisplayName->SetText(FText::FromString(Player.DisplayName));
	}
}

void UWasmOssMainMenuWidget::ShowHome()
{
	if (Switcher_Pages)
	{
		Switcher_Pages->SetActiveWidgetIndex(0);
	}
}

void UWasmOssMainMenuWidget::ShowServerBrowser()
{
	if (Switcher_Pages && Page_ServerBrowser)
	{
		Switcher_Pages->SetActiveWidget(Page_ServerBrowser);
		Page_ServerBrowser->Refresh();
	}
}

void UWasmOssMainMenuWidget::ShowLeaderboard()
{
	if (Switcher_Pages && Page_Leaderboard)
	{
		Switcher_Pages->SetActiveWidget(Page_Leaderboard);
		Page_Leaderboard->Refresh();
	}
}

void UWasmOssMainMenuWidget::HandleLoginClicked()
{
	Login();
}

void UWasmOssMainMenuWidget::HandleQuitClicked()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

void UWasmOssMainMenuWidget::HandleLoginChanged(const FWasmOssPlayer& Player)
{
	ShowPlayer();
	OnLoggedIn(Player);
}

// =====================================================================================================================
// Menu player controller
// =====================================================================================================================

void AWasmOssMenuPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController() || !MenuClass)
	{
		return;
	}
	Menu = CreateWidget<UUserWidget>(this, MenuClass);
	if (!Menu)
	{
		return;
	}
	Menu->AddToViewport();
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(Menu->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	SetShowMouseCursor(true);
}

#undef LOCTEXT_NAMESPACE
