#pragma once

#include "CoreMinimal.h"
#include "WasmOssTypes.generated.h"

/** The logged-in guest player. */
USTRUCT(BlueprintType)
struct ONLINESUBSYSTEMWASM_API FWasmOssPlayer
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OSS WASM")
	FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "OSS WASM")
	FString DisplayName;

	bool IsValid() const { return !Id.IsEmpty(); }
};

/** One dedicated server from the backend's server list. */
USTRUCT(BlueprintType)
struct ONLINESUBSYSTEMWASM_API FWasmOssServerInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OSS WASM")
	FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "OSS WASM")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "OSS WASM")
	FString Region;

	UPROPERTY(BlueprintReadOnly, Category = "OSS WASM")
	FString Map;

	UPROPERTY(BlueprintReadOnly, Category = "OSS WASM")
	FString Mode;

	UPROPERTY(BlueprintReadOnly, Category = "OSS WASM")
	FString Version;

	/** Where the server says players connect. */
	UPROPERTY(BlueprintReadOnly, Category = "OSS WASM")
	FString ConnectUrl;

	UPROPERTY(BlueprintReadOnly, Category = "OSS WASM")
	int32 Players = 0;

	UPROPERTY(BlueprintReadOnly, Category = "OSS WASM")
	int32 MaxPlayers = 0;

	/** open, in_progress, full or closed. */
	UPROPERTY(BlueprintReadOnly, Category = "OSS WASM")
	FString State;

	bool IsFull() const { return Players >= MaxPlayers || State == TEXT("full"); }
};

/** One leaderboard row. */
USTRUCT(BlueprintType)
struct ONLINESUBSYSTEMWASM_API FWasmOssLeaderboardEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OSS WASM")
	int32 Rank = 0;

	UPROPERTY(BlueprintReadOnly, Category = "OSS WASM")
	FString PlayerId;

	UPROPERTY(BlueprintReadOnly, Category = "OSS WASM")
	FString DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "OSS WASM")
	double Score = 0.0;
};
