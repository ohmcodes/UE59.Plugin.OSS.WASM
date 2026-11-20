#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WasmOssTypes.h"
#include "WasmOssServerBrowserWidget.generated.h"

class UButton;
class UCheckBox;
class UPanelWidget;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWasmOssServerRowJoinClicked, const FWasmOssServerInfo&, Server);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FWasmOssBackRequested);

/**
 * Base class for one row of the server browser (WBP_WasmOss_ServerRow, guide 05).
 * Required widgets: Text_Name, Button_Join. Optional: Text_Players, Text_Map, Text_Region.
 */
UCLASS(Abstract)
class ONLINESUBSYSTEMWASM_API UWasmOssServerRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Fills the row. The browser calls this; call it yourself if you build your own list. */
	UFUNCTION(BlueprintCallable, Category = "OSS WASM")
	void SetServer(const FWasmOssServerInfo& InServer);

	UPROPERTY(BlueprintReadOnly, Category = "OSS WASM")
	FWasmOssServerInfo Server;

	/** Fired when Button_Join is clicked. */
	UPROPERTY(BlueprintAssignable, Category = "OSS WASM")
	FWasmOssServerRowJoinClicked OnJoinClicked;

protected:
	virtual void NativeOnInitialized() override;

	/** Runs after SetServer filled the widgets: add your own styling here (full servers, your map names, ...). */
	UFUNCTION(BlueprintImplementableEvent, Category = "OSS WASM")
	void OnServerSet(const FWasmOssServerInfo& InServer, bool bIsFull);

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Name;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Join;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Players;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Map;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Region;

private:
	UFUNCTION()
	void HandleJoinClicked();
};

/**
 * Base class for the server browser (WBP_WasmOss_ServerBrowser, guide 05). Lists the servers of this game version,
 * logs in as a guest first if needed, and joins the chosen server (join ticket + travel).
 * Required widgets: List_Servers (any panel: Scroll Box, Vertical Box), Text_Status.
 * Optional: Button_Refresh, CheckBox_ShowFull, Button_Back.
 */
UCLASS(Abstract)
class ONLINESUBSYSTEMWASM_API UWasmOssServerBrowserWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Row widget created for each server. Must be a child of UWasmOssServerRowWidget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSS WASM")
	TSubclassOf<UWasmOssServerRowWidget> RowClass;

	/** Region filter; empty lists every region. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSS WASM")
	FString Region;

	/** Refresh when the widget is shown. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSS WASM")
	bool bRefreshOnConstruct = true;

	/** Refresh every N seconds while shown (0 = only on demand). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSS WASM", meta = (ClampMin = "0"))
	float AutoRefreshSeconds = 10.f;

	/** Show full servers (their Join button is disabled). CheckBox_ShowFull overrides this when present. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSS WASM")
	bool bShowFullServers = true;

	/** Last list received. */
	UPROPERTY(BlueprintReadOnly, Category = "OSS WASM")
	TArray<FWasmOssServerInfo> Servers;

	/** Fired when Button_Back is clicked (the main menu uses it to go back). */
	UPROPERTY(BlueprintAssignable, Category = "OSS WASM")
	FWasmOssBackRequested OnBackRequested;

	UFUNCTION(BlueprintCallable, Category = "OSS WASM")
	void Refresh();

	/** Gets a join ticket for Server and travels there. Logs in first if needed. */
	UFUNCTION(BlueprintCallable, Category = "OSS WASM")
	void JoinServer(const FWasmOssServerInfo& Server);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** Every status message (also written to Text_Status). */
	UFUNCTION(BlueprintImplementableEvent, Category = "OSS WASM")
	void OnStatusChanged(const FText& Status, bool bIsError);

	/** After a refresh filled the list. */
	UFUNCTION(BlueprintImplementableEvent, Category = "OSS WASM")
	void OnServersRefreshed(const TArray<FWasmOssServerInfo>& NewServers);

	/** Just before travelling to a server (hide your menus here if they aren't removed by the map change). */
	UFUNCTION(BlueprintImplementableEvent, Category = "OSS WASM")
	void OnJoining(const FWasmOssServerInfo& Server);

	UFUNCTION(BlueprintCallable, Category = "OSS WASM")
	void SetStatus(const FText& Status, bool bIsError = false);

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> List_Servers;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Status;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Refresh;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> CheckBox_ShowFull;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Back;

private:
	UFUNCTION()
	void HandleRefreshClicked();

	UFUNCTION()
	void HandleBackClicked();

	UFUNCTION()
	void HandleShowFullChanged(bool bIsChecked);

	UFUNCTION()
	void HandleRowJoin(const FWasmOssServerInfo& Server);

	void FetchList();
	void Rebuild();
	void RequestTicket(const FWasmOssServerInfo& Server);

	FTimerHandle RefreshTimer;
	bool bBusy = false;
};
