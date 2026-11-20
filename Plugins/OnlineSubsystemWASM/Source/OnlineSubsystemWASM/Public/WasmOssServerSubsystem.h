#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "WasmOssTypes.h"
#include "WasmOssServerSubsystem.generated.h"

class AGameModeBase;
class APlayerController;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FWasmOssOnPlayerVerified, APlayerController*, PlayerController, const FString&, PlayerId, const FString&, DisplayName);

/**
 * Dedicated-server side of OSS WASM. Created only on dedicated servers. Once the server is listening it:
 *  - registers with the backend's server list,
 *  - sends a heartbeat with the player count and map,
 *  - checks each joining player's ticket (optional kick when bRequireJoinTicket),
 *  - unregisters on shutdown.
 *
 * Configure with command-line options or environment variables (command line wins):
 *  -WasmOssBackendUrl=       (or Project Settings)          backend URL
 *  -WasmOssServerKey=        OSSWASM_SERVER_KEY             one of the backend's SERVER_KEYS (dev default: dev-server-key)
 *  -WasmOssPublicAddress=    OSSWASM_PUBLIC_ADDRESS         host:port players reach (default 127.0.0.1:<listen port>)
 *  -WasmOssServerName=       OSSWASM_SERVER_NAME
 *  -WasmOssRegion=           OSSWASM_REGION                 default "local"
 *  -WasmOssMaxPlayers=       OSSWASM_MAX_PLAYERS            default: the game session's MaxPlayers
 *  -WasmOssRequireTicket     OSSWASM_REQUIRE_TICKET=1       kick players without a valid join ticket
 *  -WasmOssNoRegister                                       don't register (LAN / testing)
 */
UCLASS()
class ONLINESUBSYSTEMWASM_API UWasmOssServerSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	using FOnScore = TFunction<void(bool bSuccess, const FWasmOssLeaderboardEntry& Entry, bool bImproved, const FString& Error)>;

	/** The server subsystem, or nullptr when this isn't a dedicated server. */
	static UWasmOssServerSubsystem* Get(const UObject* WorldContextObject);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "OSS WASM|Server")
	bool IsRegistered() const { return !ServerId.IsEmpty(); }

	UFUNCTION(BlueprintPure, Category = "OSS WASM|Server")
	FString GetServerId() const { return ServerId; }

	/** Backend player id of a connected player whose join ticket was verified (empty if none). */
	UFUNCTION(BlueprintPure, Category = "OSS WASM|Server")
	FString GetBackendPlayerId(const APlayerController* PlayerController) const;

	/** Fires on the server when a joining player's ticket was verified (their PlayerState name is already set). */
	UPROPERTY(BlueprintAssignable, Category = "OSS WASM|Server")
	FWasmOssOnPlayerVerified OnPlayerVerified;

	/** Game mode shown in the server list (e.g. "Deathmatch"). Sent with the next heartbeat. */
	UFUNCTION(BlueprintCallable, Category = "OSS WASM|Server")
	void SetListedGameMode(const FString& Mode);

	/**
	 * Server state shown in the server list: "open", "in_progress", "full" or "closed". Empty = automatic
	 * (open, or full when the player count reaches the maximum). Sent with the next heartbeat.
	 */
	UFUNCTION(BlueprintCallable, Category = "OSS WASM|Server")
	void SetListedState(const FString& State);

	/**
	 * Submits a score for a connected, verified player (server-authoritative scores: the client can't fake them).
	 * Fails if the player joined without a valid join ticket.
	 */
	void SubmitScoreForPlayer(const APlayerController* PlayerController, const FString& Board, double Score, FOnScore&& OnComplete);

private:
	bool Tick(float DeltaTime);
	void TryRegister();
	void SendHeartbeat();
	void OnPostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer);
	void OnLogout(AGameModeBase* GameMode, AController* Exiting);

	int32 GetPlayerCount() const;
	int32 GetMaxPlayers() const;
	FString GetMapName() const;

	FTSTicker::FDelegateHandle TickerHandle;
	FDelegateHandle PostLoginHandle;
	FDelegateHandle LogoutHandle;

	FString ServerKey;
	FString PublicAddress;
	FString ServerName;
	FString Region;
	FString ListedMode;
	FString ListedState;
	int32 MaxPlayersOverride = 0;
	bool bRequireTicket = false;
	bool bNoRegister = false;

	FString ServerId;
	FString ServerToken;
	float HeartbeatInterval = 10.f;
	double NextActionTime = 0.0;
	bool bRequestInFlight = false;

	TMap<TWeakObjectPtr<const APlayerController>, FString> VerifiedPlayers;
};
