#include "WasmOssServerSubsystem.h"

#include "Algo/Find.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Engine/GameInstance.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformMisc.h"
#include "IPAddress.h"
#include "Misc/App.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OnlineSubsystemWASMLog.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "WasmOssHttp.h"
#include "WasmOssSettings.h"
#include "WasmOssSubsystem.h"

namespace
{
	FString ServerOption(const TCHAR* CommandLineKey, const TCHAR* EnvName, const FString& Default = FString())
	{
		FString Value;
		if (FParse::Value(FCommandLine::Get(), CommandLineKey, Value) && !Value.IsEmpty())
		{
			return Value;
		}
		Value = FPlatformMisc::GetEnvironmentVariable(EnvName);
		return Value.IsEmpty() ? Default : Value;
	}

	FString ServerToJson(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Object, Writer);
		return Out;
	}
}

UWasmOssServerSubsystem* UWasmOssServerSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UWasmOssServerSubsystem>() : nullptr;
}

void UWasmOssServerSubsystem::SetListedGameMode(const FString& Mode)
{
	ListedMode = Mode.Left(64);
	NextActionTime = 0.0;
}

void UWasmOssServerSubsystem::SetListedState(const FString& State)
{
	static const TCHAR* Valid[] = { TEXT("open"), TEXT("in_progress"), TEXT("full"), TEXT("closed") };
	if (!State.IsEmpty() && !Algo::FindByPredicate(Valid, [&State](const TCHAR* Item) { return State == Item; }))
	{
		UE_LOG(LogOssWasm, Warning, TEXT("SetListedState: '%s' is not open, in_progress, full or closed"), *State);
		return;
	}
	ListedState = State;
	NextActionTime = 0.0;
}

void UWasmOssServerSubsystem::SubmitScoreForPlayer(const APlayerController* PlayerController, const FString& Board, double Score, FOnScore&& OnComplete)
{
	const FString PlayerId = GetBackendPlayerId(PlayerController);
	if (PlayerId.IsEmpty() || ServerId.IsEmpty())
	{
		OnComplete(false, FWasmOssLeaderboardEntry(), false,
			ServerId.IsEmpty() ? TEXT("Server is not registered with the backend") : TEXT("Player has no verified join ticket"));
		return;
	}
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetNumberField(TEXT("score"), Score);
	Body->SetStringField(TEXT("playerId"), PlayerId);
	TMap<FString, FString> Headers{ { TEXT("Content-Type"), TEXT("application/json") }, { TEXT("x-server-id"), ServerId }, { TEXT("x-server-token"), ServerToken } };
	WasmOssHttp::Send(TEXT("POST"), GetDefault<UWasmOssSettings>()->GetEffectiveBackendUrl() + TEXT("/v1/leaderboards/") + FGenericPlatformHttp::UrlEncode(Board) + TEXT("/scores"),
		Headers, ServerToJson(Body), GetDefault<UWasmOssSettings>()->RequestTimeoutSeconds,
		[Callback = MoveTemp(OnComplete)](const FWasmOssHttpResponse& Response)
		{
			const TSharedPtr<FJsonObject> Json = Response.IsOk() ? UWasmOssSubsystem::ParseJson(Response.Body) : nullptr;
			if (!Json)
			{
				Callback(false, FWasmOssLeaderboardEntry(), false, UWasmOssSubsystem::DescribeError(Response));
				return;
			}
			FWasmOssLeaderboardEntry Entry;
			const TSharedPtr<FJsonObject>* EntryJson = nullptr;
			if (Json->TryGetObjectField(TEXT("entry"), EntryJson))
			{
				Entry = UWasmOssSubsystem::LeaderboardEntryFromJson(**EntryJson);
			}
			bool bImproved = false;
			Json->TryGetBoolField(TEXT("improved"), bImproved);
			Callback(true, Entry, bImproved, FString());
		});
}

bool UWasmOssServerSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

void UWasmOssServerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ServerKey = ServerOption(TEXT("WasmOssServerKey="), TEXT("OSSWASM_SERVER_KEY"));
	if (ServerKey.IsEmpty())
	{
		ServerKey = TEXT("dev-server-key");
		UE_LOG(LogOssWasm, Warning, TEXT("No -WasmOssServerKey= / OSSWASM_SERVER_KEY: using the backend's development key"));
	}
	PublicAddress = ServerOption(TEXT("WasmOssPublicAddress="), TEXT("OSSWASM_PUBLIC_ADDRESS"));
	ServerName = ServerOption(TEXT("WasmOssServerName="), TEXT("OSSWASM_SERVER_NAME"));
	Region = ServerOption(TEXT("WasmOssRegion="), TEXT("OSSWASM_REGION"), TEXT("local"));
	MaxPlayersOverride = FCString::Atoi(*ServerOption(TEXT("WasmOssMaxPlayers="), TEXT("OSSWASM_MAX_PLAYERS"), TEXT("0")));
	bRequireTicket = FParse::Param(FCommandLine::Get(), TEXT("WasmOssRequireTicket")) || FPlatformMisc::GetEnvironmentVariable(TEXT("OSSWASM_REQUIRE_TICKET")) == TEXT("1");
	bNoRegister = FParse::Param(FCommandLine::Get(), TEXT("WasmOssNoRegister"));

	PostLoginHandle = FGameModeEvents::GameModePostLoginEvent.AddUObject(this, &UWasmOssServerSubsystem::OnPostLogin);
	LogoutHandle = FGameModeEvents::GameModeLogoutEvent.AddUObject(this, &UWasmOssServerSubsystem::OnLogout);
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UWasmOssServerSubsystem::Tick), 1.f);

	UE_LOG(LogOssWasm, Log, TEXT("OSS WASM server: backend %s, region %s%s%s"),
		*GetDefault<UWasmOssSettings>()->GetEffectiveBackendUrl(), *Region,
		bRequireTicket ? TEXT(", join tickets required") : TEXT(""), bNoRegister ? TEXT(", not registering") : TEXT(""));
}

void UWasmOssServerSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	FGameModeEvents::GameModePostLoginEvent.Remove(PostLoginHandle);
	FGameModeEvents::GameModeLogoutEvent.Remove(LogoutHandle);

	if (!ServerId.IsEmpty())
	{
		// Fire and forget; if it doesn't arrive the backend drops the server after its TTL.
		TMap<FString, FString> Headers{ { TEXT("x-server-token"), ServerToken } };
		WasmOssHttp::Send(TEXT("DELETE"), GetDefault<UWasmOssSettings>()->GetEffectiveBackendUrl() + TEXT("/v1/servers/") + ServerId,
			Headers, FString(), 3.f, [](const FWasmOssHttpResponse&) {});
		UE_LOG(LogOssWasm, Log, TEXT("Unregistered server %s"), *ServerId);
		ServerId.Reset();
	}
	Super::Deinitialize();
}

FString UWasmOssServerSubsystem::GetBackendPlayerId(const APlayerController* PlayerController) const
{
	const FString* Found = VerifiedPlayers.Find(PlayerController);
	return Found ? *Found : FString();
}

int32 UWasmOssServerSubsystem::GetPlayerCount() const
{
	const UWorld* World = GetGameInstance()->GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	return GameState ? GameState->PlayerArray.Num() : 0;
}

int32 UWasmOssServerSubsystem::GetMaxPlayers() const
{
	if (MaxPlayersOverride > 0)
	{
		return MaxPlayersOverride;
	}
	const UWorld* World = GetGameInstance()->GetWorld();
	const AGameModeBase* GameMode = World ? World->GetAuthGameMode() : nullptr;
	return (GameMode && GameMode->GameSession) ? FMath::Max(1, GameMode->GameSession->MaxPlayers) : 16;
}

FString UWasmOssServerSubsystem::GetMapName() const
{
	const UWorld* World = GetGameInstance()->GetWorld();
	return World ? UWorld::RemovePIEPrefix(World->GetMapName()) : FString();
}

bool UWasmOssServerSubsystem::Tick(float)
{
	if (bNoRegister || bRequestInFlight || FPlatformTime::Seconds() < NextActionTime)
	{
		return true;
	}
	if (ServerId.IsEmpty())
	{
		TryRegister();
	}
	else
	{
		SendHeartbeat();
	}
	return true;
}

void UWasmOssServerSubsystem::TryRegister()
{
	const UWorld* World = GetGameInstance()->GetWorld();
	UNetDriver* NetDriver = World ? World->GetNetDriver() : nullptr;
	if (!NetDriver)
	{
		return; // not listening yet; try again next tick
	}

	FString Address = PublicAddress;
	const TSharedPtr<const FInternetAddr> LocalAddr = NetDriver->GetLocalAddr();
	const int32 Port = LocalAddr.IsValid() ? LocalAddr->GetPort() : 7777;
	if (Address.IsEmpty())
	{
		Address = FString::Printf(TEXT("127.0.0.1:%d"), Port);
	}
	const FString Name = ServerName.IsEmpty() ? FString::Printf(TEXT("%s :%d"), FApp::GetProjectName(), Port) : ServerName;

	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("name"), Name.Left(64));
	Body->SetStringField(TEXT("version"), GetDefault<UWasmOssSettings>()->GameVersion);
	Body->SetStringField(TEXT("connectUrl"), Address);
	Body->SetNumberField(TEXT("maxPlayers"), GetMaxPlayers());
	Body->SetNumberField(TEXT("players"), GetPlayerCount());
	Body->SetStringField(TEXT("region"), Region);
	Body->SetStringField(TEXT("map"), GetMapName());
	if (!ListedMode.IsEmpty())
	{
		Body->SetStringField(TEXT("mode"), ListedMode);
	}

	TMap<FString, FString> Headers{ { TEXT("Content-Type"), TEXT("application/json") }, { TEXT("x-server-key"), ServerKey } };
	bRequestInFlight = true;
	TWeakObjectPtr<UWasmOssServerSubsystem> WeakThis(this);
	WasmOssHttp::Send(TEXT("POST"), GetDefault<UWasmOssSettings>()->GetEffectiveBackendUrl() + TEXT("/v1/servers/register"), Headers,
		ServerToJson(Body), GetDefault<UWasmOssSettings>()->RequestTimeoutSeconds,
		[WeakThis, Address, Name](const FWasmOssHttpResponse& Response)
		{
			UWasmOssServerSubsystem* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			Self->bRequestInFlight = false;
			const TSharedPtr<FJsonObject> Json = Response.IsOk() ? UWasmOssSubsystem::ParseJson(Response.Body) : nullptr;
			if (!Json)
			{
				UE_LOG(LogOssWasm, Warning, TEXT("Server registration failed: %s (retrying in 10 s)"), *UWasmOssSubsystem::DescribeError(Response));
				Self->NextActionTime = FPlatformTime::Seconds() + 10.0;
				return;
			}
			Self->ServerId = Json->GetStringField(TEXT("serverId"));
			Self->ServerToken = Json->GetStringField(TEXT("serverToken"));
			Self->HeartbeatInterval = FMath::Max(1.f, float(Json->GetNumberField(TEXT("heartbeatIntervalSeconds"))));
			Self->NextActionTime = FPlatformTime::Seconds() + Self->HeartbeatInterval;
			UE_LOG(LogOssWasm, Log, TEXT("Registered '%s' as %s at %s"), *Name, *Self->ServerId, *Address);
		});
}

void UWasmOssServerSubsystem::SendHeartbeat()
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	const int32 Players = GetPlayerCount();
	const int32 MaxPlayers = GetMaxPlayers();
	Body->SetNumberField(TEXT("players"), Players);
	Body->SetNumberField(TEXT("maxPlayers"), MaxPlayers);
	Body->SetStringField(TEXT("map"), GetMapName());
	Body->SetStringField(TEXT("state"), !ListedState.IsEmpty() ? ListedState : (Players >= MaxPlayers ? TEXT("full") : TEXT("open")));
	if (!ListedMode.IsEmpty())
	{
		Body->SetStringField(TEXT("mode"), ListedMode);
	}

	TMap<FString, FString> Headers{ { TEXT("Content-Type"), TEXT("application/json") }, { TEXT("x-server-token"), ServerToken } };
	bRequestInFlight = true;
	TWeakObjectPtr<UWasmOssServerSubsystem> WeakThis(this);
	WasmOssHttp::Send(TEXT("POST"), GetDefault<UWasmOssSettings>()->GetEffectiveBackendUrl() + TEXT("/v1/servers/") + ServerId + TEXT("/heartbeat"),
		Headers, ServerToJson(Body), GetDefault<UWasmOssSettings>()->RequestTimeoutSeconds,
		[WeakThis](const FWasmOssHttpResponse& Response)
		{
			UWasmOssServerSubsystem* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			Self->bRequestInFlight = false;
			if (Response.Status == 401 || Response.Status == 404)
			{
				// The backend forgot us (restart, or we missed the TTL): register again.
				UE_LOG(LogOssWasm, Warning, TEXT("Backend no longer knows server %s; registering again"), *Self->ServerId);
				Self->ServerId.Reset();
				Self->ServerToken.Reset();
				Self->NextActionTime = 0.0;
				return;
			}
			if (!Response.IsOk())
			{
				UE_LOG(LogOssWasm, Warning, TEXT("Heartbeat failed: %s"), *UWasmOssSubsystem::DescribeError(Response));
			}
			Self->NextActionTime = FPlatformTime::Seconds() + Self->HeartbeatInterval;
		});
}

void UWasmOssServerSubsystem::OnPostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer)
{
	if (!NewPlayer || GameMode->GetGameInstance() != GetGameInstance())
	{
		return;
	}
	NextActionTime = 0.0; // report the new player count soon

	// RequestURL is "<map>?Name=...?WasmOssTicket=...": ParseOption only reads from the first '?'.
	const UNetConnection* Connection = NewPlayer->GetNetConnection();
	FString Options;
	if (Connection)
	{
		int32 Question = INDEX_NONE;
		if (Connection->RequestURL.FindChar(TEXT('?'), Question))
		{
			Options = Connection->RequestURL.RightChop(Question);
		}
	}
	const FString Ticket = UGameplayStatics::ParseOption(Options, GetDefault<UWasmOssSettings>()->TicketOptionName);

	if (Ticket.IsEmpty() || ServerId.IsEmpty())
	{
		UE_LOG(LogOssWasm, Log, TEXT("%s joined without a join ticket%s"), *GetNameSafe(NewPlayer),
			ServerId.IsEmpty() ? TEXT(" (server not registered yet)") : TEXT(""));
		if (bRequireTicket)
		{
			UE_LOG(LogOssWasm, Warning, TEXT("Kicking %s: no join ticket"), *GetNameSafe(NewPlayer));
			if (GameMode->GameSession)
			{
				GameMode->GameSession->KickPlayer(NewPlayer, FText::FromString(TEXT("Join through the server list")));
			}
		}
		return;
	}

	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("ticket"), Ticket);
	TMap<FString, FString> Headers{ { TEXT("Content-Type"), TEXT("application/json") }, { TEXT("x-server-token"), ServerToken } };
	TWeakObjectPtr<UWasmOssServerSubsystem> WeakThis(this);
	TWeakObjectPtr<APlayerController> WeakPlayer(NewPlayer);
	WasmOssHttp::Send(TEXT("POST"), GetDefault<UWasmOssSettings>()->GetEffectiveBackendUrl() + TEXT("/v1/servers/") + ServerId + TEXT("/verify-ticket"),
		Headers, ServerToJson(Body), GetDefault<UWasmOssSettings>()->RequestTimeoutSeconds,
		[WeakThis, WeakPlayer](const FWasmOssHttpResponse& Response)
		{
			UWasmOssServerSubsystem* Self = WeakThis.Get();
			APlayerController* Player = WeakPlayer.Get();
			if (!Self || !Player)
			{
				return;
			}
			const TSharedPtr<FJsonObject> Json = Response.IsOk() ? UWasmOssSubsystem::ParseJson(Response.Body) : nullptr;
			bool bValid = false;
			if (Json && Json->TryGetBoolField(TEXT("valid"), bValid) && bValid)
			{
				const FString PlayerId = Json->GetStringField(TEXT("playerId"));
				const FString DisplayName = Json->GetStringField(TEXT("displayName"));
				Self->VerifiedPlayers.Add(Player, PlayerId);
				if (Player->PlayerState)
				{
					Player->PlayerState->SetPlayerName(DisplayName);
				}
				UE_LOG(LogOssWasm, Log, TEXT("Player %s verified (%s)"), *DisplayName, *PlayerId);
				Self->OnPlayerVerified.Broadcast(Player, PlayerId, DisplayName);
				return;
			}
			FString Reason = TEXT("invalid");
			if (Json)
			{
				Json->TryGetStringField(TEXT("reason"), Reason);
			}
			UE_LOG(LogOssWasm, Warning, TEXT("Join ticket rejected for %s: %s"), *GetNameSafe(Player), *Reason);
			if (Self->bRequireTicket)
			{
				if (AGameModeBase* Mode = Player->GetWorld() ? Player->GetWorld()->GetAuthGameMode() : nullptr; Mode && Mode->GameSession)
				{
					Mode->GameSession->KickPlayer(Player, FText::FromString(TEXT("Invalid join ticket")));
				}
			}
		});
}

void UWasmOssServerSubsystem::OnLogout(AGameModeBase* GameMode, AController* Exiting)
{
	if (const APlayerController* Player = Cast<APlayerController>(Exiting))
	{
		VerifiedPlayers.Remove(Player);
	}
	NextActionTime = 0.0;
}
