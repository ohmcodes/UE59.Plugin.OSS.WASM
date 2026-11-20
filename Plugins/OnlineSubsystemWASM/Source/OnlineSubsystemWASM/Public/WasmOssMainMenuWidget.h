#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WasmOssTypes.h"
#include "WasmOssMainMenuWidget.generated.h"

class UButton;
class UEditableTextBox;
class UTextBlock;
class UWidgetSwitcher;
class UWasmOssServerBrowserWidget;
class UWasmOssLeaderboardWidget;

/**
 * Base class for the main menu (WBP_WasmOss_MainMenu, guide 05): guest login with a display name, then pages for the
 * server browser and the leaderboard inside a Widget Switcher.
 * Required widgets: Switcher_Pages (page 0 = home), Text_Player.
 * Optional: TextBox_DisplayName, Button_Login, Button_Servers, Button_Leaderboard, Button_Quit,
 *           Page_ServerBrowser (a WBP_WasmOss_ServerBrowser inside the switcher), Page_Leaderboard (likewise).
 */
UCLASS(Abstract)
class ONLINESUBSYSTEMWASM_API UWasmOssMainMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Log in automatically when the menu opens (with the saved name, or a generated guest name the first time). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSS WASM")
	bool bAutoLogin = true;

	UFUNCTION(BlueprintCallable, Category = "OSS WASM")
	void Login();

	UFUNCTION(BlueprintCallable, Category = "OSS WASM")
	void ShowHome();

	UFUNCTION(BlueprintCallable, Category = "OSS WASM")
	void ShowServerBrowser();

	UFUNCTION(BlueprintCallable, Category = "OSS WASM")
	void ShowLeaderboard();

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;

	/** After every login or rename. */
	UFUNCTION(BlueprintImplementableEvent, Category = "OSS WASM")
	void OnLoggedIn(const FWasmOssPlayer& Player);

	UFUNCTION(BlueprintImplementableEvent, Category = "OSS WASM")
	void OnLoginFailed(const FString& Error);

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidgetSwitcher> Switcher_Pages;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Player;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> TextBox_DisplayName;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Login;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Servers;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Leaderboard;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Quit;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWasmOssServerBrowserWidget> Page_ServerBrowser;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWasmOssLeaderboardWidget> Page_Leaderboard;

private:
	UFUNCTION()
	void HandleLoginClicked();

	UFUNCTION()
	void HandleQuitClicked();

	UFUNCTION()
	void HandleLoginChanged(const FWasmOssPlayer& Player);

	void ShowPlayer();
	bool bLoggingIn = false;
};
