#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "WasmOssLocalSave.generated.h"

/**
 * Kept on the player's device (browser storage on Wasm) so the same browser gets the same guest player back.
 * Slot name: WasmOssLocal.
 */
UCLASS()
class ONLINESUBSYSTEMWASM_API UWasmOssLocalSave : public USaveGame
{
	GENERATED_BODY()

public:
	static constexpr const TCHAR* SlotName = TEXT("WasmOssLocal");

	/** Random secret identifying this device to the backend. Treat it like a password. */
	UPROPERTY()
	FString DeviceId;

	/** Last display name the player chose. */
	UPROPERTY()
	FString DisplayName;
};
