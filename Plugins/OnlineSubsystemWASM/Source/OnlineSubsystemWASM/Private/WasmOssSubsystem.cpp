#include "WasmOssSubsystem.h"

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformMisc.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Guid.h"
#include "Misc/SecureHash.h"
#include "Misc/Parse.h"
#include "OnlineSubsystemWASMLog.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "WasmOssLocalSave.h"
#include "WasmOssSettings.h"

namespace
{
	using FJsonWriter = TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;

	FString ToJson(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		const TSharedRef<FJsonWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Object, Writer);
		return Out;
	}

	FString Encode(const FString& Value)
	{
		return FGenericPlatformHttp::UrlEncode(Value);
	}

	FWasmOssServerInfo ServerFromJson(const FJsonObject& Json)
	{
		FWasmOssServerInfo Server;
		Server.Id = Json.GetStringField(TEXT("id"));
		Server.Name = Json.GetStringField(TEXT("name"));
		Json.TryGetStringField(TEXT("region"), Server.Region);
		Json.TryGetStringField(TEXT("map"), Server.Map);
		Json.TryGetStringField(TEXT("mode"), Server.Mode);
		Json.TryGetStringField(TEXT("version"), Server.Version);
		Json.TryGetStringField(TEXT("connectUrl"), Server.ConnectUrl);
		Json.TryGetNumberField(TEXT("players"), Server.Players);
		Json.TryGetNumberField(TEXT("maxPlayers"), Server.MaxPlayers);
		Json.TryGetStringField(TEXT("state"), Server.State);
		return Server;
	}

	FWasmOssLeaderboardEntry EntryFromJson(const FJsonObject& Json)
	{
		FWasmOssLeaderboardEntry Entry;
		Json.TryGetNumberField(TEXT("rank"), Entry.Rank);
		Json.TryGetStringField(TEXT("playerId"), Entry.PlayerId);
		Json.TryGetStringField(TEXT("displayName"), Entry.DisplayName);
		Json.TryGetNumberField(TEXT("score"), Entry.Score);
		return Entry;
	}

	/** "ws://host:port" / "wss://host:port/path" / "host:port" -> "host:port" (what ClientTravel expects). */
	FString AddressFromConnectUrl(FString Url)
	{
		int32 SchemeEnd = Url.Find(TEXT("://"));
		if (SchemeEnd != INDEX_NONE)
		{
			Url.RightChopInline(SchemeEnd + 3);
		}
		int32 Slash = INDEX_NONE;
		if (Url.FindChar(TEXT('/'), Slash))
		{
			Url.LeftInline(Slash);
		}
		return Url;
	}
}

FWasmOssLeaderboardEntry UWasmOssSubsystem::LeaderboardEntryFromJson(const FJsonObject& Json)
{
	return EntryFromJson(Json);
}

UWasmOssSubsystem* UWasmOssSubsystem::Get(const UObject* WorldContextObject)
{
	const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContextObject);
	return GameInstance ? GameInstance->GetSubsystem<UWasmOssSubsystem>() : nullptr;
}

void UWasmOssSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadLocalState();
	UE_LOG(LogOssWasm, Log, TEXT("OSS WASM ready, backend %s"), *GetBackendUrl());
}

FString UWasmOssSubsystem::GetBackendUrl() const
{
	return GetDefault<UWasmOssSettings>()->GetEffectiveBackendUrl();
}

// ---- Local state -----------------------------------------------------------------------------------------------------

void UWasmOssSubsystem::LoadLocalState()
{
	if (UWasmOssLocalSave* Saved = Cast<UWasmOssLocalSave>(UGameplayStatics::LoadGameFromSlot(UWasmOssLocalSave::SlotName, 0)))
	{
		DeviceId = Saved->DeviceId;
		SavedDisplayName = Saved->DisplayName;
	}
	if (DeviceId.Len() < 16)
	{
		DeviceId = MakeDeviceId();
		SaveLocalState();
	}
}

FString UWasmOssSubsystem::MakeDeviceId()
{
#if !PLATFORM_WASM
	// PC (and other native platforms): derive the id from the machine, so the player stays the same after a reinstall
	// or a deleted save folder. The engine's login id is shared by every Unreal game on this machine/user, so it is
	// hashed with the project name: the backend can't link the same person across different games.
	const FString MachineId = FPlatformMisc::GetLoginId();
	if (!MachineId.IsEmpty())
	{
		return TEXT("pc-") + FMD5::HashAnsiString(*(MachineId + TEXT("|") + FApp::GetProjectName() + TEXT("|OSSWASM")));
	}
#endif
	// Browser (and fallback): a random id kept in this site's browser storage (or the save folder).
	return TEXT("web-") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + FGuid::NewGuid().ToString(EGuidFormats::Digits);
}

void UWasmOssSubsystem::SaveLocalState() const
{
	UWasmOssLocalSave* Save = Cast<UWasmOssLocalSave>(UGameplayStatics::CreateSaveGameObject(UWasmOssLocalSave::StaticClass()));
	Save->DeviceId = DeviceId;
	Save->DisplayName = SavedDisplayName;
	UGameplayStatics::SaveGameToSlot(Save, UWasmOssLocalSave::SlotName, 0);
}

FString UWasmOssSubsystem::GetDeviceIdForRequests() const
{
	// Several packaged clients on one PC share the save folder; -WasmOssGuestSuffix=2 makes them different players.
	FString Suffix;
	if (FParse::Value(FCommandLine::Get(), TEXT("WasmOssGuestSuffix="), Suffix) && !Suffix.IsEmpty())
	{
		return DeviceId + TEXT("-") + Suffix;
	}
#if WITH_EDITOR
	// Each PIE instance shares the same save slot; give them different players so multiplayer tests see two people.
	if (GetDefault<UWasmOssSettings>()->bUniquePlayerPerPIEInstance)
	{
		if (const FWorldContext* Context = GetGameInstance()->GetWorldContext())
		{
			if (Context->WorldType == EWorldType::PIE && Context->PIEInstance > 0)
			{
				return FString::Printf(TEXT("%s-pie%d"), *DeviceId, Context->PIEInstance);
			}
		}
	}
#endif
	return DeviceId;
}

// ---- HTTP helpers ----------------------------------------------------------------------------------------------------

TSharedPtr<FJsonObject> UWasmOssSubsystem::ParseJson(const FString& Text)
{
	TSharedPtr<FJsonObject> Object;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Object))
	{
		return nullptr;
	}
	return Object;
}

FString UWasmOssSubsystem::DescribeError(const FWasmOssHttpResponse& Response)
{
	if (!Response.bConnected)
	{
		return Response.TransportError.IsEmpty() ? TEXT("Could not reach the backend") : Response.TransportError;
	}
	if (const TSharedPtr<FJsonObject> Json = ParseJson(Response.Body))
	{
		FString Message;
		if (Json->TryGetStringField(TEXT("message"), Message) || Json->TryGetStringField(TEXT("error"), Message))
		{
			return FString::Printf(TEXT("%s (HTTP %d)"), *Message, Response.Status);
		}
	}
	return FString::Printf(TEXT("HTTP %d"), Response.Status);
}

void UWasmOssSubsystem::CallBackend(const FString& Verb, const FString& Path, const FString& JsonBody, bool bWithPlayerToken,
	WasmOssHttp::FOnComplete&& OnComplete) const
{
	TMap<FString, FString> Headers;
	FString Body = JsonBody;
	if (Body.IsEmpty() && (Verb == TEXT("POST") || Verb == TEXT("PUT") || Verb == TEXT("PATCH")))
	{
		Body = TEXT("{}"); // engine HTTP adds a Content-Type anyway; send a valid JSON body
	}
	if (!Body.IsEmpty())
	{
		Headers.Add(TEXT("Content-Type"), TEXT("application/json"));
	}
	if (bWithPlayerToken && !PlayerToken.IsEmpty())
	{
		Headers.Add(TEXT("Authorization"), TEXT("Bearer ") + PlayerToken);
	}
	const FString Url = GetBackendUrl() + (Path.StartsWith(TEXT("/")) ? Path : TEXT("/") + Path);
	WasmOssHttp::Send(Verb, Url, Headers, Body, GetDefault<UWasmOssSettings>()->RequestTimeoutSeconds, MoveTemp(OnComplete));
}

// ---- Player ----------------------------------------------------------------------------------------------------------

void UWasmOssSubsystem::LoginAsGuest(const FString& DisplayName, FOnLogin&& OnComplete)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("deviceId"), GetDeviceIdForRequests());
	// Only send a name when the caller chose one: the backend remembers each player's name, and gives new guests a
	// random friendly one. (Re-sending the locally saved name made extra local test clients copy the first one's name.)
	if (!DisplayName.IsEmpty())
	{
		Body->SetStringField(TEXT("displayName"), DisplayName.Left(32));
	}

	TWeakObjectPtr<UWasmOssSubsystem> WeakThis(this);
	CallBackend(TEXT("POST"), TEXT("/v1/auth/guest"), ToJson(Body), false,
		[WeakThis, Callback = MoveTemp(OnComplete)](const FWasmOssHttpResponse& Response)
		{
			UWasmOssSubsystem* Self = WeakThis.Get();
			const TSharedPtr<FJsonObject> Json = Response.IsOk() ? ParseJson(Response.Body) : nullptr;
			const TSharedPtr<FJsonObject>* PlayerJson = nullptr;
			if (!Self || !Json || !Json->TryGetObjectField(TEXT("player"), PlayerJson))
			{
				const FString Error = DescribeError(Response);
				UE_LOG(LogOssWasm, Warning, TEXT("Guest login failed: %s"), *Error);
				Callback(false, FWasmOssPlayer(), Error);
				return;
			}
			Self->PlayerToken = Json->GetStringField(TEXT("token"));
			Self->Player.Id = (*PlayerJson)->GetStringField(TEXT("id"));
			Self->Player.DisplayName = (*PlayerJson)->GetStringField(TEXT("displayName"));
			Self->SavedDisplayName = Self->Player.DisplayName;
			Self->SaveLocalState();
			UE_LOG(LogOssWasm, Log, TEXT("Logged in as %s (%s)"), *Self->Player.DisplayName, *Self->Player.Id);
			Self->OnLoginChanged.Broadcast(Self->Player);
			Callback(true, Self->Player, FString());
		});
}

// ---- Server list -----------------------------------------------------------------------------------------------------

void UWasmOssSubsystem::GetServerList(const FString& Region, bool bIncludeFull, FOnServers&& OnComplete)
{
	FString Path = TEXT("/v1/servers?includeFull=") + FString(bIncludeFull ? TEXT("true") : TEXT("false"));
	const FString& Version = GetDefault<UWasmOssSettings>()->GameVersion;
	if (!Version.IsEmpty())
	{
		Path += TEXT("&version=") + Encode(Version);
	}
	if (!Region.IsEmpty())
	{
		Path += TEXT("&region=") + Encode(Region);
	}

	CallBackend(TEXT("GET"), Path, FString(), false,
		[Callback = MoveTemp(OnComplete)](const FWasmOssHttpResponse& Response)
		{
			TArray<FWasmOssServerInfo> Servers;
			const TSharedPtr<FJsonObject> Json = Response.IsOk() ? ParseJson(Response.Body) : nullptr;
			const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
			if (!Json || !Json->TryGetArrayField(TEXT("servers"), List))
			{
				Callback(false, Servers, DescribeError(Response));
				return;
			}
			for (const TSharedPtr<FJsonValue>& Value : *List)
			{
				if (const TSharedPtr<FJsonObject>* Object = nullptr; Value->TryGetObject(Object))
				{
					Servers.Add(ServerFromJson(**Object));
				}
			}
			Callback(true, Servers, FString());
		});
}

void UWasmOssSubsystem::RequestJoinTicket(const FString& ServerId, FOnJoinTicket&& OnComplete)
{
	if (!IsLoggedIn())
	{
		OnComplete(false, FString(), FString(), TEXT("Not logged in"));
		return;
	}
	CallBackend(TEXT("POST"), TEXT("/v1/servers/") + Encode(ServerId) + TEXT("/join"), FString(), true,
		[Callback = MoveTemp(OnComplete)](const FWasmOssHttpResponse& Response)
		{
			const TSharedPtr<FJsonObject> Json = Response.IsOk() ? ParseJson(Response.Body) : nullptr;
			FString ConnectUrl, Ticket;
			if (!Json || !Json->TryGetStringField(TEXT("connectUrl"), ConnectUrl) || !Json->TryGetStringField(TEXT("ticket"), Ticket))
			{
				Callback(false, FString(), FString(), Response.Status == 409 ? FString(TEXT("Server is full")) : DescribeError(Response));
				return;
			}
			Callback(true, AddressFromConnectUrl(ConnectUrl), Ticket, FString());
		});
}

bool UWasmOssSubsystem::TravelToServer(APlayerController* PlayerController, const FString& Address, const FString& Ticket) const
{
	if (!PlayerController || Address.IsEmpty())
	{
		return false;
	}
	const FString Option = GetDefault<UWasmOssSettings>()->TicketOptionName;
	const FString Url = Ticket.IsEmpty() ? Address : FString::Printf(TEXT("%s?%s=%s"), *Address, *Option, *Ticket);
	UE_LOG(LogOssWasm, Log, TEXT("Travelling to %s"), *Address);
	PlayerController->ClientTravel(Url, TRAVEL_Absolute);
	return true;
}

bool UWasmOssSubsystem::NormalizeDirectAddress(const FString& Input, FString& OutAddress, FString& Error)
{
	FString Text = Input.TrimStartAndEnd();
	const int32 SchemeEnd = Text.Find(TEXT("://"));
	if (SchemeEnd != INDEX_NONE)
	{
		Text = Text.Mid(SchemeEnd + 3);
	}
	Text.Split(TEXT("/"), &Text, nullptr); // drop any path
	Text.TrimStartAndEndInline();
	if (Text.IsEmpty() || Text.Contains(TEXT(" ")) || Text.Contains(TEXT("?")))
	{
		Error = TEXT("Type an address like 192.168.1.20:7777");
		return false;
	}
	FString Host = Text;
	FString PortText;
	if (Text.Split(TEXT(":"), &Host, &PortText, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
	{
		if (Host.IsEmpty() || PortText.IsEmpty() || !PortText.IsNumeric() || FCString::Atoi(*PortText) < 1 || FCString::Atoi(*PortText) > 65535)
		{
			Error = TEXT("The port must be a number from 1 to 65535");
			return false;
		}
	}
	else
	{
		PortText = TEXT("7777");
	}
	OutAddress = Host + TEXT(":") + PortText;
	return true;
}

bool UWasmOssSubsystem::TravelToAddress(APlayerController* PlayerController, const FString& TypedAddress, FString& Error) const
{
	FString Address;
	if (!NormalizeDirectAddress(TypedAddress, Address, Error))
	{
		return false;
	}
	if (!PlayerController)
	{
		Error = TEXT("No player controller");
		return false;
	}
	return TravelToServer(PlayerController, Address, FString());
}

// ---- Leaderboards ----------------------------------------------------------------------------------------------------

void UWasmOssSubsystem::SubmitScore(const FString& Board, double Score, FOnScore&& OnComplete)
{
	if (!IsLoggedIn())
	{
		OnComplete(false, FWasmOssLeaderboardEntry(), false, TEXT("Not logged in"));
		return;
	}
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetNumberField(TEXT("score"), Score);
	CallBackend(TEXT("POST"), TEXT("/v1/leaderboards/") + Encode(Board) + TEXT("/scores"), ToJson(Body), true,
		[Callback = MoveTemp(OnComplete)](const FWasmOssHttpResponse& Response)
		{
			const TSharedPtr<FJsonObject> Json = Response.IsOk() ? ParseJson(Response.Body) : nullptr;
			if (!Json)
			{
				Callback(false, FWasmOssLeaderboardEntry(), false, DescribeError(Response));
				return;
			}
			FWasmOssLeaderboardEntry Entry;
			const TSharedPtr<FJsonObject>* EntryJson = nullptr;
			if (Json->TryGetObjectField(TEXT("entry"), EntryJson))
			{
				Entry = EntryFromJson(**EntryJson);
			}
			Callback(true, Entry, Json->GetBoolField(TEXT("improved")), FString());
		});
}

void UWasmOssSubsystem::GetLeaderboard(const FString& Board, int32 Limit, int32 Offset, FOnLeaderboard&& OnComplete)
{
	const FString Path = FString::Printf(TEXT("/v1/leaderboards/%s?limit=%d&offset=%d"), *Encode(Board),
		FMath::Clamp(Limit, 1, 100), FMath::Max(Offset, 0));
	CallBackend(TEXT("GET"), Path, FString(), false,
		[Callback = MoveTemp(OnComplete)](const FWasmOssHttpResponse& Response)
		{
			TArray<FWasmOssLeaderboardEntry> Entries;
			const TSharedPtr<FJsonObject> Json = Response.IsOk() ? ParseJson(Response.Body) : nullptr;
			const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
			if (!Json || !Json->TryGetArrayField(TEXT("entries"), List))
			{
				Callback(false, Entries, 0, DescribeError(Response));
				return;
			}
			for (const TSharedPtr<FJsonValue>& Value : *List)
			{
				if (const TSharedPtr<FJsonObject>* Object = nullptr; Value->TryGetObject(Object))
				{
					Entries.Add(EntryFromJson(**Object));
				}
			}
			int32 Total = Entries.Num();
			Json->TryGetNumberField(TEXT("total"), Total);
			Callback(true, Entries, Total, FString());
		});
}

// ---- Cloud saves -----------------------------------------------------------------------------------------------------

void UWasmOssSubsystem::SaveToCloud(const FString& Slot, const FString& Data, FOnDone&& OnComplete)
{
	if (!IsLoggedIn())
	{
		OnComplete(false, TEXT("Not logged in"));
		return;
	}
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("data"), Data);
	CallBackend(TEXT("PUT"), TEXT("/v1/saves/") + Encode(Slot), ToJson(Body), true,
		[Callback = MoveTemp(OnComplete)](const FWasmOssHttpResponse& Response)
		{
			Callback(Response.IsOk(), Response.IsOk() ? FString() : DescribeError(Response));
		});
}

void UWasmOssSubsystem::LoadFromCloud(const FString& Slot, FOnLoad&& OnComplete)
{
	if (!IsLoggedIn())
	{
		OnComplete(false, FString(), TEXT("Not logged in"));
		return;
	}
	CallBackend(TEXT("GET"), TEXT("/v1/saves/") + Encode(Slot), FString(), true,
		[Callback = MoveTemp(OnComplete)](const FWasmOssHttpResponse& Response)
		{
			const TSharedPtr<FJsonObject> Json = Response.IsOk() ? ParseJson(Response.Body) : nullptr;
			FString Data;
			if (!Json || !Json->TryGetStringField(TEXT("data"), Data))
			{
				Callback(false, FString(), Response.Status == 404 ? FString(TEXT("No save in this slot")) : DescribeError(Response));
				return;
			}
			Callback(true, Data, FString());
		});
}
