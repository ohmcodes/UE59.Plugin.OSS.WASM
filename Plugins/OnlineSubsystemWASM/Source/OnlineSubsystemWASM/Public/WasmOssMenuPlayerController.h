#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "WasmOssMenuPlayerController.generated.h"

class UUserWidget;

/**
 * Player controller for a menu level (BP_WasmOss_MenuController, guide 05): shows MenuClass with the mouse cursor and
 * UI-only input. Use it through a game mode override on your menu map only; the dedicated server's map keeps the
 * game's own controller.
 */
UCLASS()
class ONLINESUBSYSTEMWASM_API AWasmOssMenuPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	/** The menu shown on BeginPlay (e.g. WBP_WasmOss_MainMenu). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSS WASM")
	TSubclassOf<UUserWidget> MenuClass;

	UPROPERTY(BlueprintReadOnly, Category = "OSS WASM")
	TObjectPtr<UUserWidget> Menu;

protected:
	virtual void BeginPlay() override;
};
