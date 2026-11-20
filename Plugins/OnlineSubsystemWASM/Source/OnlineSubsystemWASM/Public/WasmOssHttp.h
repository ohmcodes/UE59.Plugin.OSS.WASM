#pragma once

#include "CoreMinimal.h"

/** Result of one backend request. */
struct ONLINESUBSYSTEMWASM_API FWasmOssHttpResponse
{
	/** False when no HTTP response arrived (network error, CORS refusal, timeout). */
	bool bConnected = false;
	int32 Status = 0;
	FString Body;
	/** Why the request failed without a response (for logs). */
	FString TransportError;

	bool IsOk() const { return bConnected && Status >= 200 && Status < 300; }
};

/**
 * Minimal HTTP client used by the plugin.
 * - Native platforms (editor, Windows client, dedicated server): engine HTTP module.
 * - Wasm: the browser's fetch(), because the engine's HTTP module is a null module there ([HTTP] bUseNullHttp=true).
 *   fetch() runs on the browser's main thread; the game thread polls for the result each tick, as the engine's own
 *   IoStore fetch client does. Requests from an https page must use https URLs, and the backend must allow the page's origin (CORS).
 * The completion callback always runs on the game thread.
 */
namespace WasmOssHttp
{
	using FOnComplete = TFunction<void(const FWasmOssHttpResponse&)>;

	ONLINESUBSYSTEMWASM_API void Send(
		const FString& Verb,
		const FString& Url,
		const TMap<FString, FString>& Headers,
		const FString& Body,
		float TimeoutSeconds,
		FOnComplete&& OnComplete);
}
