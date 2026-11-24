#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "WasmOssHttp.h"
#include "WasmOssTypes.h"
#include "WasmOssSubsystem.generated.h"

class APlayerController;
class FJsonObject;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWasmOssOnLoginChanged, const FWasmOssPlayer&, Player);

/**
 * Client-side entry point to the OSSWASM backend. One per game instance.
 *
 * Blueprints: use the async nodes in the "OSS WASM" category (Login As Guest, Get Server List, Join Server, ...),
 * or the pure getters below.
 * C++: call the functions below directly; every callback runs on the game thread. To add your own backend call,
 * use CallBackend() (see docs/02-activate-and-use-oss-wasm.md, "Adding your own functions").
 */
UCLASS()
class ONLINESUBSYSTEMWASM_API UWasmOssSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	using FOnDone = TFunction<void(bool bSuccess, const FString& Error)>;
	using FOnLogin = TFunction<void(bool bSuccess, const FWasmOssPlayer& Player, const FString& Error)>;
	using FOnProviders = TFunction<void(bool bSuccess, const TArray<FString>& Providers, const FString& Error)>;
	using FOnServers = TFunction<void(bool bSuccess, const TArray<FWasmOssServerInfo>& Servers, const FString& Error)>;
	using FOnJoinTicket = TFunction<void(bool bSuccess, const FString& Address, const FString& Ticket, const FString& Error)>;
	using FOnScore = TFunction<void(bool bSuccess, const FWasmOssLeaderboardEntry& Entry, bool bImproved, const FString& Error)>;
	using FOnLeaderboard = TFunction<void(bool bSuccess, const TArray<FWasmOssLeaderboardEntry>& Entries, int32 Total, const FString& Error)>;
	using FOnLoad = TFunction<void(bool bSuccess, const FString& Data, const FString& Error)>;

	static UWasmOssSubsystem* Get(const UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// ---- Player ---------------------------------------------------------------------------------------------------

	/** Logs in as a guest. The same device gets the same player back. Empty DisplayName keeps the current one. */
	void LoginAsGuest(const FString& DisplayName, FOnLogin&& OnComplete);

	/** Names of the login providers the backend offers ("guest", "itch", ...). Guest is always there. */
	void GetLoginProviders(FOnProviders&& OnComplete);

	/**
	 * Logs in with a provider by name. "guest" = LoginAsGuest. For "itch" the backend runs the OAuth flow: this opens
	 * the itch.io page (a new tab on the web, the system browser on PC) and polls the backend until the player finished,
	 * CancelProviderLogin() is called, or the backend's time limit (10 minutes) passes. Call it straight from a button
	 * click so browsers allow the new tab.
	 */
	void LoginWithProvider(const FString& Provider, FOnLogin&& OnComplete);

	/** Stops waiting for a provider login (its callback then reports failure). */
	void CancelProviderLogin();

	UFUNCTION(BlueprintPure, Category = "OSS WASM")
	bool IsLoggedIn() const { return !PlayerToken.IsEmpty(); }

	UFUNCTION(BlueprintPure, Category = "OSS WASM")
	FWasmOssPlayer GetPlayer() const { return Player; }

	UFUNCTION(BlueprintPure, Category = "OSS WASM")
	FString GetBackendUrl() const;

	/** Fires after every successful login or rename. */
	UPROPERTY(BlueprintAssignable, Category = "OSS WASM")
	FWasmOssOnLoginChanged OnLoginChanged;

	// ---- Server list ----------------------------------------------------------------------------------------------

	/** Servers with this game's version (Project Settings > OSS WASM > Game Version). Region empty = all regions. */
	void GetServerList(const FString& Region, bool bIncludeFull, FOnServers&& OnComplete);

	/** Asks the backend for a one-time join ticket. Address is the host:port to travel to. Requires login. */
	void RequestJoinTicket(const FString& ServerId, FOnJoinTicket&& OnComplete);

	/** ClientTravel to Address, passing the ticket as a URL option the dedicated server checks. */
	bool TravelToServer(APlayerController* PlayerController, const FString& Address, const FString& Ticket) const;

	/**
	 * Cleans what a player typed ("ws://1.2.3.4", " host:7777 ", "host") into "host:port". The port defaults to 7777.
	 * Returns false (and fills Error) when nothing usable was typed.
	 */
	static bool NormalizeDirectAddress(const FString& Input, FString& OutAddress, FString& Error);

	/** Direct connect without the server list: no backend call, no join ticket. Servers accept this unless started with -WasmOssRequireTicket. */
	bool TravelToAddress(APlayerController* PlayerController, const FString& TypedAddress, FString& Error) const;

	// ---- Leaderboards ---------------------------------------------------------------------------------------------

	void SubmitScore(const FString& Board, double Score, FOnScore&& OnComplete);
	void GetLeaderboard(const FString& Board, int32 Limit, int32 Offset, FOnLeaderboard&& OnComplete);

	// ---- Cloud saves ----------------------------------------------------------------------------------------------

	/** Stores Data (any string; binary should be base64) in the player's cloud slot. Requires login. */
	void SaveToCloud(const FString& Slot, const FString& Data, FOnDone&& OnComplete);
	void LoadFromCloud(const FString& Slot, FOnLoad&& OnComplete);

	// ---- Extension point ------------------------------------------------------------------------------------------

	/**
	 * Calls any backend path (e.g. "/v1/my-endpoint") with a JSON body. bWithPlayerToken adds the
	 * Authorization header. Use this to build your own functions on top of the plugin.
	 */
	void CallBackend(const FString& Verb, const FString& Path, const FString& JsonBody, bool bWithPlayerToken, WasmOssHttp::FOnComplete&& OnComplete) const;

	/** Turns a failed response into a readable message (backend "message"/"error" field, or the transport error). */
	static FString DescribeError(const FWasmOssHttpResponse& Response);

	static TSharedPtr<FJsonObject> ParseJson(const FString& Text);

	/** Reads one leaderboard entry object as the backend returns it. */
	static FWasmOssLeaderboardEntry LeaderboardEntryFromJson(const FJsonObject& Json);

private:
	bool ApplyLoginJson(const TSharedPtr<FJsonObject>& Json);
	void PollProviderLogin();
	void FinishProviderLogin(bool bSuccess, const FString& Error);

	TFunction<void(bool, const FWasmOssPlayer&, const FString&)> ProviderCallback;
	FString ProviderName;
	FString ProviderPollId;
	double ProviderDeadline = 0.0;
	bool bProviderPollInFlight = false;
	FTSTicker::FDelegateHandle ProviderTicker;

	void LoadLocalState();
	void SaveLocalState() const;
	FString GetDeviceIdForRequests() const;
	static FString MakeDeviceId();

	FString DeviceId;
	FString PlayerToken;
	FWasmOssPlayer Player;
	FString SavedDisplayName;
};
