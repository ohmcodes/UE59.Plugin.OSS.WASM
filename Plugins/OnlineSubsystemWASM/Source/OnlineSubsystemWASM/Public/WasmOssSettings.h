#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "WasmOssSettings.generated.h"

/**
 * Project Settings > Plugins > OSS WASM. Stored in Config/DefaultGame.ini under [/Script/OnlineSubsystemWASM.WasmOssSettings].
 * Command-line overrides (useful for servers and testing): -WasmOssBackendUrl=<url>
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "OSS WASM"))
class ONLINESUBSYSTEMWASM_API UWasmOssSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UWasmOssSettings();

	/** Base URL of your OSSWASM backend, e.g. http://localhost:3000 or https://api.mygame.com (no trailing slash). */
	UPROPERTY(Config, EditAnywhere, Category = "Backend")
	FString BackendUrl = TEXT("http://localhost:3000");

	/** Your game's build version. The server list only shows servers with the same version; servers register with it. */
	UPROPERTY(Config, EditAnywhere, Category = "Backend")
	FString GameVersion = TEXT("1.0");

	/** Seconds before a backend request is reported as failed. */
	UPROPERTY(Config, EditAnywhere, Category = "Backend", meta = (ClampMin = "1", ClampMax = "120"))
	float RequestTimeoutSeconds = 15.f;

	/** In Play In Editor, give every PIE instance its own guest player so multiplayer tests see different players. */
	UPROPERTY(Config, EditAnywhere, Category = "Testing")
	bool bUniquePlayerPerPIEInstance = true;

	/** Name of the URL option that carries the join ticket to the dedicated server (?WasmOssTicket=...). */
	UPROPERTY(Config, EditAnywhere, Category = "Advanced")
	FString TicketOptionName = TEXT("WasmOssTicket");

	/** Backend URL after applying the -WasmOssBackendUrl= command-line override, without a trailing slash. */
	FString GetEffectiveBackendUrl() const;

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
};
