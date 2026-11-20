#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WasmOssTypes.h"
#include "WasmOssServerBrowserWidget.h"
#include "WasmOssLeaderboardWidget.generated.h"

class UButton;
class UPanelWidget;
class UTextBlock;

/**
 * Base class for one leaderboard row (WBP_WasmOss_LeaderboardRow, guide 05).
 * Required widgets: Text_Rank, Text_Name, Text_Score.
 */
UCLASS(Abstract)
class ONLINESUBSYSTEMWASM_API UWasmOssLeaderboardRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "OSS WASM")
	void SetEntry(const FWasmOssLeaderboardEntry& InEntry, bool bInIsLocalPlayer);

	UPROPERTY(BlueprintReadOnly, Category = "OSS WASM")
	FWasmOssLeaderboardEntry Entry;

	/** True for the logged-in player's own row. */
	UPROPERTY(BlueprintReadOnly, Category = "OSS WASM")
	bool bIsLocalPlayer = false;

	/** Digits after the decimal point shown for scores. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSS WASM", meta = (ClampMin = "0", ClampMax = "6"))
	int32 ScoreDecimals = 0;

protected:
	/** Runs after SetEntry filled the widgets: highlight bIsLocalPlayer rows here. */
	UFUNCTION(BlueprintImplementableEvent, Category = "OSS WASM")
	void OnEntrySet(const FWasmOssLeaderboardEntry& InEntry, bool bInIsLocalPlayer);

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Rank;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Name;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Score;
};

/**
 * Base class for a leaderboard (WBP_WasmOss_Leaderboard, guide 05). Shows the top entries of one board.
 * Required widgets: List_Entries (any panel), Text_Status. Optional: Button_Refresh, Button_Back, Text_Title.
 */
UCLASS(Abstract)
class ONLINESUBSYSTEMWASM_API UWasmOssLeaderboardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSS WASM")
	TSubclassOf<UWasmOssLeaderboardRowWidget> RowClass;

	/** Board id, the same name your game submits scores to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSS WASM")
	FString Board = TEXT("default");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSS WASM", meta = (ClampMin = "1", ClampMax = "100"))
	int32 Limit = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSS WASM")
	bool bRefreshOnConstruct = true;

	UPROPERTY(BlueprintReadOnly, Category = "OSS WASM")
	TArray<FWasmOssLeaderboardEntry> Entries;

	UPROPERTY(BlueprintAssignable, Category = "OSS WASM")
	FWasmOssBackRequested OnBackRequested;

	UFUNCTION(BlueprintCallable, Category = "OSS WASM")
	void Refresh();

	/** Switches to another board and refreshes. */
	UFUNCTION(BlueprintCallable, Category = "OSS WASM")
	void ShowBoard(const FString& NewBoard);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;

	UFUNCTION(BlueprintImplementableEvent, Category = "OSS WASM")
	void OnStatusChanged(const FText& Status, bool bIsError);

	UFUNCTION(BlueprintImplementableEvent, Category = "OSS WASM")
	void OnLeaderboardRefreshed(const TArray<FWasmOssLeaderboardEntry>& NewEntries, int32 Total);

	UFUNCTION(BlueprintCallable, Category = "OSS WASM")
	void SetStatus(const FText& Status, bool bIsError = false);

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> List_Entries;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Status;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Title;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Refresh;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Back;

private:
	UFUNCTION()
	void HandleRefreshClicked();

	UFUNCTION()
	void HandleBackClicked();

	bool bBusy = false;
};
