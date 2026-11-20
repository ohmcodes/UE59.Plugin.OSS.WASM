#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "WasmOssTypes.h"
#include "WasmOssAsyncActions.generated.h"

class APlayerController;
class USaveGame;

/**
 * Blueprint latent nodes for OSS WASM. Each node has an On Success and an On Failure pin; Error is empty on success.
 * Pattern for your own nodes: copy one of these classes and call UWasmOssSubsystem::CallBackend() in Activate().
 */
UCLASS(Abstract)
class ONLINESUBSYSTEMWASM_API UWasmOssAsyncActionBase : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

protected:
	/** The subsystem for WorldContext, or nullptr (after reporting failure through Fail()). */
	class UWasmOssSubsystem* GetSubsystemOrFail();
	virtual void Fail(const FString& Error) {}

	TWeakObjectPtr<UObject> WorldContext;
};

// ---- Login As Guest --------------------------------------------------------------------------------------------------

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FWasmOssLoginPin, const FWasmOssPlayer&, Player, const FString&, Error);

UCLASS()
class ONLINESUBSYSTEMWASM_API UWasmOssLoginAsGuestAction : public UWasmOssAsyncActionBase
{
	GENERATED_BODY()

public:
	/** Logs in as a guest (same device = same player). Leave Display Name empty to keep the current name. */
	UFUNCTION(BlueprintCallable, Category = "OSS WASM", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Login As Guest"))
	static UWasmOssLoginAsGuestAction* LoginAsGuest(UObject* WorldContextObject, const FString& DisplayName);

	UPROPERTY(BlueprintAssignable)
	FWasmOssLoginPin OnSuccess;

	UPROPERTY(BlueprintAssignable)
	FWasmOssLoginPin OnFailure;

	virtual void Activate() override;

protected:
	virtual void Fail(const FString& Error) override;

private:
	FString DisplayName;
};

// ---- Get Server List -------------------------------------------------------------------------------------------------

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FWasmOssServersPin, const TArray<FWasmOssServerInfo>&, Servers, const FString&, Error);

UCLASS()
class ONLINESUBSYSTEMWASM_API UWasmOssGetServerListAction : public UWasmOssAsyncActionBase
{
	GENERATED_BODY()

public:
	/** Dedicated servers running this game's version. Region empty = all regions. */
	UFUNCTION(BlueprintCallable, Category = "OSS WASM", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Get Server List"))
	static UWasmOssGetServerListAction* GetServerList(UObject* WorldContextObject, const FString& Region, bool bIncludeFull = true);

	UPROPERTY(BlueprintAssignable)
	FWasmOssServersPin OnSuccess;

	UPROPERTY(BlueprintAssignable)
	FWasmOssServersPin OnFailure;

	virtual void Activate() override;

protected:
	virtual void Fail(const FString& Error) override;

private:
	FString Region;
	bool bIncludeFull = true;
};

// ---- Join Server -----------------------------------------------------------------------------------------------------

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FWasmOssJoinPin, const FString&, Address, const FString&, Error);

UCLASS()
class ONLINESUBSYSTEMWASM_API UWasmOssJoinServerAction : public UWasmOssAsyncActionBase
{
	GENERATED_BODY()

public:
	/** Gets a join ticket for the server and travels the player there. Requires Login As Guest first. */
	UFUNCTION(BlueprintCallable, Category = "OSS WASM", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Join Server"))
	static UWasmOssJoinServerAction* JoinServer(UObject* WorldContextObject, APlayerController* PlayerController, const FString& ServerId);

	UPROPERTY(BlueprintAssignable)
	FWasmOssJoinPin OnSuccess;

	UPROPERTY(BlueprintAssignable)
	FWasmOssJoinPin OnFailure;

	virtual void Activate() override;

protected:
	virtual void Fail(const FString& Error) override;

private:
	TWeakObjectPtr<APlayerController> PlayerController;
	FString ServerId;
};

// ---- Submit Score ----------------------------------------------------------------------------------------------------

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FWasmOssScorePin, const FWasmOssLeaderboardEntry&, Entry, bool, bImproved, const FString&, Error);

UCLASS()
class ONLINESUBSYSTEMWASM_API UWasmOssSubmitScoreAction : public UWasmOssAsyncActionBase
{
	GENERATED_BODY()

public:
	/** Posts a score; the board keeps each player's best (highest). Improved is false when it did not beat the best. */
	UFUNCTION(BlueprintCallable, Category = "OSS WASM", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Submit Score"))
	static UWasmOssSubmitScoreAction* SubmitScore(UObject* WorldContextObject, const FString& Board, double Score);

	UPROPERTY(BlueprintAssignable)
	FWasmOssScorePin OnSuccess;

	UPROPERTY(BlueprintAssignable)
	FWasmOssScorePin OnFailure;

	virtual void Activate() override;

protected:
	virtual void Fail(const FString& Error) override;

private:
	FString Board;
	double Score = 0.0;
};

// ---- Submit Score For Player (dedicated server) ----------------------------------------------------------------------

UCLASS()
class ONLINESUBSYSTEMWASM_API UWasmOssServerSubmitScoreAction : public UWasmOssAsyncActionBase
{
	GENERATED_BODY()

public:
	/**
	 * Dedicated server only: submits a score for a connected player who joined with a valid join ticket.
	 * Server-authoritative, so clients can't fake it. Fails on clients and for players without a ticket.
	 */
	UFUNCTION(BlueprintCallable, Category = "OSS WASM|Server", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Submit Score For Player (Server)"))
	static UWasmOssServerSubmitScoreAction* SubmitScoreForPlayer(UObject* WorldContextObject, APlayerController* PlayerController, const FString& Board, double Score);

	UPROPERTY(BlueprintAssignable)
	FWasmOssScorePin OnSuccess;

	UPROPERTY(BlueprintAssignable)
	FWasmOssScorePin OnFailure;

	virtual void Activate() override;

private:
	TWeakObjectPtr<APlayerController> PlayerController;
	FString Board;
	double Score = 0.0;
};

// ---- Get Leaderboard -------------------------------------------------------------------------------------------------

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FWasmOssLeaderboardPin, const TArray<FWasmOssLeaderboardEntry>&, Entries, int32, Total, const FString&, Error);

UCLASS()
class ONLINESUBSYSTEMWASM_API UWasmOssGetLeaderboardAction : public UWasmOssAsyncActionBase
{
	GENERATED_BODY()

public:
	/** Top scores of a board, best first. Limit is 1-100. */
	UFUNCTION(BlueprintCallable, Category = "OSS WASM", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Get Leaderboard"))
	static UWasmOssGetLeaderboardAction* GetLeaderboard(UObject* WorldContextObject, const FString& Board, int32 Limit = 10, int32 Offset = 0);

	UPROPERTY(BlueprintAssignable)
	FWasmOssLeaderboardPin OnSuccess;

	UPROPERTY(BlueprintAssignable)
	FWasmOssLeaderboardPin OnFailure;

	virtual void Activate() override;

protected:
	virtual void Fail(const FString& Error) override;

private:
	FString Board;
	int32 Limit = 10;
	int32 Offset = 0;
};

// ---- Save Game To Cloud / Load Game From Cloud -----------------------------------------------------------------------

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWasmOssErrorPin, const FString&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FWasmOssLoadPin, USaveGame*, SaveGame, const FString&, Error);

UCLASS()
class ONLINESUBSYSTEMWASM_API UWasmOssSaveGameToCloudAction : public UWasmOssAsyncActionBase
{
	GENERATED_BODY()

public:
	/** Uploads a Save Game object to the player's cloud slot. Requires Login As Guest first. */
	UFUNCTION(BlueprintCallable, Category = "OSS WASM", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Save Game To Cloud"))
	static UWasmOssSaveGameToCloudAction* SaveGameToCloud(UObject* WorldContextObject, USaveGame* SaveGame, const FString& Slot);

	UPROPERTY(BlueprintAssignable)
	FWasmOssErrorPin OnSuccess;

	UPROPERTY(BlueprintAssignable)
	FWasmOssErrorPin OnFailure;

	virtual void Activate() override;

protected:
	virtual void Fail(const FString& Error) override;

private:
	UPROPERTY()
	TObjectPtr<USaveGame> SaveGame;
	FString Slot;
};

UCLASS()
class ONLINESUBSYSTEMWASM_API UWasmOssLoadGameFromCloudAction : public UWasmOssAsyncActionBase
{
	GENERATED_BODY()

public:
	/** Downloads the player's cloud slot as a Save Game object (cast it to your save class). */
	UFUNCTION(BlueprintCallable, Category = "OSS WASM", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Load Game From Cloud"))
	static UWasmOssLoadGameFromCloudAction* LoadGameFromCloud(UObject* WorldContextObject, const FString& Slot);

	UPROPERTY(BlueprintAssignable)
	FWasmOssLoadPin OnSuccess;

	UPROPERTY(BlueprintAssignable)
	FWasmOssLoadPin OnFailure;

	virtual void Activate() override;

protected:
	virtual void Fail(const FString& Error) override;

private:
	FString Slot;
};

// ---- Call Backend (custom endpoints) ---------------------------------------------------------------------------------

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FWasmOssCallPin, int32, Status, const FString&, ResponseBody);

UCLASS()
class ONLINESUBSYSTEMWASM_API UWasmOssCallBackendAction : public UWasmOssAsyncActionBase
{
	GENERATED_BODY()

public:
	/**
	 * Calls any path on your backend (e.g. "/v1/my-endpoint") with a JSON body. On Success fires for HTTP 2xx,
	 * On Failure otherwise (Status 0 = no response). Use it to reach endpoints you add to the backend yourself.
	 */
	UFUNCTION(BlueprintCallable, Category = "OSS WASM", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Call Backend"))
	static UWasmOssCallBackendAction* CallBackend(UObject* WorldContextObject, const FString& Verb = TEXT("GET"), const FString& Path = TEXT("/health"), const FString& JsonBody = TEXT(""), bool bWithPlayerToken = true);

	UPROPERTY(BlueprintAssignable)
	FWasmOssCallPin OnSuccess;

	UPROPERTY(BlueprintAssignable)
	FWasmOssCallPin OnFailure;

	virtual void Activate() override;

protected:
	virtual void Fail(const FString& Error) override;

private:
	FString Verb;
	FString Path;
	FString JsonBody;
	bool bWithPlayerToken = true;
};
