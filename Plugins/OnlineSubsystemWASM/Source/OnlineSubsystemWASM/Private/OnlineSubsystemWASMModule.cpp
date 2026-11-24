#include "Modules/ModuleManager.h"
#include "OnlineSubsystemWASMLog.h"
#include "WasmOssSettings.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY(LogOssWasm);

UWasmOssSettings::UWasmOssSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("OSS WASM");
}

FString UWasmOssSettings::GetEffectiveBackendUrl() const
{
	FString Url = BackendUrl;
	FParse::Value(FCommandLine::Get(), TEXT("WasmOssBackendUrl="), Url);
	Url.TrimStartAndEndInline();
	while (Url.EndsWith(TEXT("/")))
	{
		Url.LeftChopInline(1);
	}
	return Url;
}

IMPLEMENT_MODULE(FDefaultModuleImpl, OnlineSubsystemWASM)
