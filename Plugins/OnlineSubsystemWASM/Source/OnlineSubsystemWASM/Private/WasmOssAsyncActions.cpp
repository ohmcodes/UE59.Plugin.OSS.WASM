#include "WasmOssAsyncActions.h"

#include "GameFramework/PlayerController.h"
#include "GameFramework/SaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Base64.h"
#include "WasmOssSubsystem.h"
#include "WasmOssServerSubsystem.h"

// Each action registers with the game instance (so it is not garbage collected while waiting), calls the subsystem,
// fires one pin, then SetReadyToDestroy(). Callbacks hold a weak pointer in case the game instance shuts down first.

template <typename TAction>
static TAction* MakeAction(UObject* WorldContextObject)
{
	TAction* Action = NewObject<TAction>();
	Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

UWasmOssSubsystem* UWasmOssAsyncActionBase::GetSubsystemOrFail()
{
	UWasmOssSubsystem* Subsystem = UWasmOssSubsystem::Get(WorldContext.Get());
	if (!Subsystem)
	{
		Fail(TEXT("OSS WASM subsystem not found (no game instance)"));
		SetReadyToDestroy();
	}
	return Subsystem;
}

// ---- Login As Guest --------------------------------------------------------------------------------------------------

UWasmOssLoginAsGuestAction* UWasmOssLoginAsGuestAction::LoginAsGuest(UObject* WorldContextObject, const FString& DisplayName)
{
	UWasmOssLoginAsGuestAction* Action = MakeAction<UWasmOssLoginAsGuestAction>(WorldContextObject);
	Action->WorldContext = WorldContextObject;
	Action->DisplayName = DisplayName;
	return Action;
}

void UWasmOssLoginAsGuestAction::Activate()
{
	if (UWasmOssSubsystem* Subsystem = GetSubsystemOrFail())
	{
		TWeakObjectPtr<UWasmOssLoginAsGuestAction> WeakThis(this);
		Subsystem->LoginAsGuest(DisplayName, [WeakThis](bool bSuccess, const FWasmOssPlayer& Player, const FString& Error)
		{
			if (UWasmOssLoginAsGuestAction* Self = WeakThis.Get())
			{
				(bSuccess ? Self->OnSuccess : Self->OnFailure).Broadcast(Player, Error);
				Self->SetReadyToDestroy();
			}
		});
	}
}

void UWasmOssLoginAsGuestAction::Fail(const FString& Error)
{
	OnFailure.Broadcast(FWasmOssPlayer(), Error);
}

// ---- Get Server List -------------------------------------------------------------------------------------------------

UWasmOssGetServerListAction* UWasmOssGetServerListAction::GetServerList(UObject* WorldContextObject, const FString& Region, bool bIncludeFull)
{
	UWasmOssGetServerListAction* Action = MakeAction<UWasmOssGetServerListAction>(WorldContextObject);
	Action->WorldContext = WorldContextObject;
	Action->Region = Region;
	Action->bIncludeFull = bIncludeFull;
	return Action;
}

void UWasmOssGetServerListAction::Activate()
{
	if (UWasmOssSubsystem* Subsystem = GetSubsystemOrFail())
	{
		TWeakObjectPtr<UWasmOssGetServerListAction> WeakThis(this);
		Subsystem->GetServerList(Region, bIncludeFull, [WeakThis](bool bSuccess, const TArray<FWasmOssServerInfo>& Servers, const FString& Error)
		{
			if (UWasmOssGetServerListAction* Self = WeakThis.Get())
			{
				(bSuccess ? Self->OnSuccess : Self->OnFailure).Broadcast(Servers, Error);
				Self->SetReadyToDestroy();
			}
		});
	}
}

void UWasmOssGetServerListAction::Fail(const FString& Error)
{
	OnFailure.Broadcast(TArray<FWasmOssServerInfo>(), Error);
}

// ---- Join Server -----------------------------------------------------------------------------------------------------

UWasmOssJoinServerAction* UWasmOssJoinServerAction::JoinServer(UObject* WorldContextObject, APlayerController* PlayerController, const FString& ServerId)
{
	UWasmOssJoinServerAction* Action = MakeAction<UWasmOssJoinServerAction>(WorldContextObject);
	Action->WorldContext = WorldContextObject;
	Action->PlayerController = PlayerController;
	Action->ServerId = ServerId;
	return Action;
}

void UWasmOssJoinServerAction::Activate()
{
	UWasmOssSubsystem* Subsystem = GetSubsystemOrFail();
	if (!Subsystem)
	{
		return;
	}
	if (!PlayerController.IsValid())
	{
		PlayerController = UGameplayStatics::GetPlayerController(WorldContext.Get(), 0);
	}
	TWeakObjectPtr<UWasmOssJoinServerAction> WeakThis(this);
	TWeakObjectPtr<UWasmOssSubsystem> WeakSubsystem(Subsystem);
	Subsystem->RequestJoinTicket(ServerId, [WeakThis, WeakSubsystem](bool bSuccess, const FString& Address, const FString& Ticket, const FString& Error)
	{
		UWasmOssJoinServerAction* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}
		if (!bSuccess)
		{
			Self->OnFailure.Broadcast(FString(), Error);
		}
		else if (!WeakSubsystem.IsValid() || !WeakSubsystem->TravelToServer(Self->PlayerController.Get(), Address, Ticket))
		{
			Self->OnFailure.Broadcast(Address, TEXT("No player controller to travel with"));
		}
		else
		{
			Self->OnSuccess.Broadcast(Address, FString());
		}
		Self->SetReadyToDestroy();
	});
}

void UWasmOssJoinServerAction::Fail(const FString& Error)
{
	OnFailure.Broadcast(FString(), Error);
}

// ---- Submit Score ----------------------------------------------------------------------------------------------------

UWasmOssSubmitScoreAction* UWasmOssSubmitScoreAction::SubmitScore(UObject* WorldContextObject, const FString& Board, double Score)
{
	UWasmOssSubmitScoreAction* Action = MakeAction<UWasmOssSubmitScoreAction>(WorldContextObject);
	Action->WorldContext = WorldContextObject;
	Action->Board = Board;
	Action->Score = Score;
	return Action;
}

void UWasmOssSubmitScoreAction::Activate()
{
	if (UWasmOssSubsystem* Subsystem = GetSubsystemOrFail())
	{
		TWeakObjectPtr<UWasmOssSubmitScoreAction> WeakThis(this);
		Subsystem->SubmitScore(Board, Score, [WeakThis](bool bSuccess, const FWasmOssLeaderboardEntry& Entry, bool bImproved, const FString& Error)
		{
			if (UWasmOssSubmitScoreAction* Self = WeakThis.Get())
			{
				(bSuccess ? Self->OnSuccess : Self->OnFailure).Broadcast(Entry, bImproved, Error);
				Self->SetReadyToDestroy();
			}
		});
	}
}

void UWasmOssSubmitScoreAction::Fail(const FString& Error)
{
	OnFailure.Broadcast(FWasmOssLeaderboardEntry(), false, Error);
}

// ---- Submit Score For Player (dedicated server) ----------------------------------------------------------------------

UWasmOssServerSubmitScoreAction* UWasmOssServerSubmitScoreAction::SubmitScoreForPlayer(UObject* WorldContextObject, APlayerController* PlayerController, const FString& Board, double Score)
{
	UWasmOssServerSubmitScoreAction* Action = MakeAction<UWasmOssServerSubmitScoreAction>(WorldContextObject);
	Action->WorldContext = WorldContextObject;
	Action->PlayerController = PlayerController;
	Action->Board = Board;
	Action->Score = Score;
	return Action;
}

void UWasmOssServerSubmitScoreAction::Activate()
{
	UWasmOssServerSubsystem* Server = UWasmOssServerSubsystem::Get(WorldContext.Get());
	if (!Server)
	{
		OnFailure.Broadcast(FWasmOssLeaderboardEntry(), false, TEXT("Submit Score For Player only works on a dedicated server"));
		SetReadyToDestroy();
		return;
	}
	TWeakObjectPtr<UWasmOssServerSubmitScoreAction> WeakThis(this);
	Server->SubmitScoreForPlayer(PlayerController.Get(), Board, Score, [WeakThis](bool bSuccess, const FWasmOssLeaderboardEntry& Entry, bool bImproved, const FString& Error)
	{
		if (UWasmOssServerSubmitScoreAction* Self = WeakThis.Get())
		{
			(bSuccess ? Self->OnSuccess : Self->OnFailure).Broadcast(Entry, bImproved, Error);
			Self->SetReadyToDestroy();
		}
	});
}

// ---- Get Leaderboard -------------------------------------------------------------------------------------------------

UWasmOssGetLeaderboardAction* UWasmOssGetLeaderboardAction::GetLeaderboard(UObject* WorldContextObject, const FString& Board, int32 Limit, int32 Offset)
{
	UWasmOssGetLeaderboardAction* Action = MakeAction<UWasmOssGetLeaderboardAction>(WorldContextObject);
	Action->WorldContext = WorldContextObject;
	Action->Board = Board;
	Action->Limit = Limit;
	Action->Offset = Offset;
	return Action;
}

void UWasmOssGetLeaderboardAction::Activate()
{
	if (UWasmOssSubsystem* Subsystem = GetSubsystemOrFail())
	{
		TWeakObjectPtr<UWasmOssGetLeaderboardAction> WeakThis(this);
		Subsystem->GetLeaderboard(Board, Limit, Offset, [WeakThis](bool bSuccess, const TArray<FWasmOssLeaderboardEntry>& Entries, int32 Total, const FString& Error)
		{
			if (UWasmOssGetLeaderboardAction* Self = WeakThis.Get())
			{
				(bSuccess ? Self->OnSuccess : Self->OnFailure).Broadcast(Entries, Total, Error);
				Self->SetReadyToDestroy();
			}
		});
	}
}

void UWasmOssGetLeaderboardAction::Fail(const FString& Error)
{
	OnFailure.Broadcast(TArray<FWasmOssLeaderboardEntry>(), 0, Error);
}

// ---- Save Game To Cloud ----------------------------------------------------------------------------------------------

UWasmOssSaveGameToCloudAction* UWasmOssSaveGameToCloudAction::SaveGameToCloud(UObject* WorldContextObject, USaveGame* SaveGame, const FString& Slot)
{
	UWasmOssSaveGameToCloudAction* Action = MakeAction<UWasmOssSaveGameToCloudAction>(WorldContextObject);
	Action->WorldContext = WorldContextObject;
	Action->SaveGame = SaveGame;
	Action->Slot = Slot;
	return Action;
}

void UWasmOssSaveGameToCloudAction::Activate()
{
	UWasmOssSubsystem* Subsystem = GetSubsystemOrFail();
	if (!Subsystem)
	{
		return;
	}
	TArray<uint8> Bytes;
	if (!SaveGame || !UGameplayStatics::SaveGameToMemory(SaveGame, Bytes))
	{
		Fail(TEXT("Save Game object is empty or could not be serialized"));
		SetReadyToDestroy();
		return;
	}
	TWeakObjectPtr<UWasmOssSaveGameToCloudAction> WeakThis(this);
	Subsystem->SaveToCloud(Slot, FBase64::Encode(Bytes), [WeakThis](bool bSuccess, const FString& Error)
	{
		if (UWasmOssSaveGameToCloudAction* Self = WeakThis.Get())
		{
			(bSuccess ? Self->OnSuccess : Self->OnFailure).Broadcast(Error);
			Self->SetReadyToDestroy();
		}
	});
}

void UWasmOssSaveGameToCloudAction::Fail(const FString& Error)
{
	OnFailure.Broadcast(Error);
}

// ---- Load Game From Cloud --------------------------------------------------------------------------------------------

UWasmOssLoadGameFromCloudAction* UWasmOssLoadGameFromCloudAction::LoadGameFromCloud(UObject* WorldContextObject, const FString& Slot)
{
	UWasmOssLoadGameFromCloudAction* Action = MakeAction<UWasmOssLoadGameFromCloudAction>(WorldContextObject);
	Action->WorldContext = WorldContextObject;
	Action->Slot = Slot;
	return Action;
}

void UWasmOssLoadGameFromCloudAction::Activate()
{
	if (UWasmOssSubsystem* Subsystem = GetSubsystemOrFail())
	{
		TWeakObjectPtr<UWasmOssLoadGameFromCloudAction> WeakThis(this);
		Subsystem->LoadFromCloud(Slot, [WeakThis](bool bSuccess, const FString& Data, const FString& Error)
		{
			UWasmOssLoadGameFromCloudAction* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			TArray<uint8> Bytes;
			USaveGame* Loaded = nullptr;
			if (bSuccess && FBase64::Decode(Data, Bytes))
			{
				Loaded = UGameplayStatics::LoadGameFromMemory(Bytes);
			}
			if (Loaded)
			{
				Self->OnSuccess.Broadcast(Loaded, FString());
			}
			else
			{
				Self->OnFailure.Broadcast(nullptr, bSuccess ? FString(TEXT("Cloud data is not a Save Game")) : Error);
			}
			Self->SetReadyToDestroy();
		});
	}
}

void UWasmOssLoadGameFromCloudAction::Fail(const FString& Error)
{
	OnFailure.Broadcast(nullptr, Error);
}

// ---- Call Backend ----------------------------------------------------------------------------------------------------

UWasmOssCallBackendAction* UWasmOssCallBackendAction::CallBackend(UObject* WorldContextObject, const FString& Verb, const FString& Path,
	const FString& JsonBody, bool bWithPlayerToken)
{
	UWasmOssCallBackendAction* Action = MakeAction<UWasmOssCallBackendAction>(WorldContextObject);
	Action->WorldContext = WorldContextObject;
	Action->Verb = Verb.ToUpper();
	Action->Path = Path;
	Action->JsonBody = JsonBody;
	Action->bWithPlayerToken = bWithPlayerToken;
	return Action;
}

void UWasmOssCallBackendAction::Activate()
{
	if (UWasmOssSubsystem* Subsystem = GetSubsystemOrFail())
	{
		TWeakObjectPtr<UWasmOssCallBackendAction> WeakThis(this);
		Subsystem->CallBackend(Verb, Path, JsonBody, bWithPlayerToken, [WeakThis](const FWasmOssHttpResponse& Response)
		{
			if (UWasmOssCallBackendAction* Self = WeakThis.Get())
			{
				const FString Body = Response.bConnected ? Response.Body : Response.TransportError;
				(Response.IsOk() ? Self->OnSuccess : Self->OnFailure).Broadcast(Response.Status, Body);
				Self->SetReadyToDestroy();
			}
		});
	}
}

void UWasmOssCallBackendAction::Fail(const FString& Error)
{
	OnFailure.Broadcast(0, Error);
}
