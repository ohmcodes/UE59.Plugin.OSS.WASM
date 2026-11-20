#include "WasmOssHttp.h"
#include "OnlineSubsystemWASMLog.h"

#if PLATFORM_WASM

#include "Containers/AnsiString.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformTime.h"

#include <emscripten/em_js.h>
#include <emscripten/proxying.h>
#include <emscripten/threading.h>

#include <atomic>

// ---------------------------------------------------------------------------------------------------------------------
// Browser fetch() bridge. Same pattern as the engine's IoStore FetchClient.inl:
// the game thread is a pthread Worker that never runs its own JS event loop, so fetch() is started on the browser's main
// thread through an async proxy. The main thread reports into a slot in shared memory; the game thread polls it from a
// core ticker, allocates the body buffer, and asks the main thread to copy the body in. Nobody waits for anybody.
// Pointers are passed to JS as doubles (wasm64-safe).
// ---------------------------------------------------------------------------------------------------------------------

namespace WasmOssFetch
{
	/** Shared with JS: Int32Array at the slot address. */
	struct alignas(8) FSlot
	{
		std::atomic<int32> Phase;     // 0 running, 2 body ready (held by JS), 3 failed, 4 body copied
		int32 Status;                 // HTTP status
		int32 Error;                  // phase 3: 1 network/CORS error, 3 request could not be made
		std::atomic<int32> JsDone;    // 1: JS will not touch this slot again
		std::atomic<int32> BodyBytes; // phase 2: body size
		int32 Padding;
	};
	static_assert(sizeof(std::atomic<int32>) == 4, "JS reads the slot as an Int32Array");

	enum : int32 { PhaseRunning = 0, PhaseReady = 2, PhaseFailed = 3, PhaseCopied = 4 };

	struct FRequest
	{
		FSlot* Slot = nullptr;
		FAnsiString Method;
		FAnsiString Url;
		FAnsiString Headers;
		TArray<uint8> Body;
		TArray<uint8> Response;
		double Deadline = 0.0;
		bool bCopyRequested = false;
		bool bReported = false;
		WasmOssHttp::FOnComplete OnComplete;
	};
}

EM_JS(void, WasmOss_FetchStart, (double SlotPtr, double Method, double Url, double Headers, double BodyPtr, double BodyLength), {
	var Store = Module.ueWasmOssFetch || (Module.ueWasmOssFetch = {});
	var Ints = function() { return new Int32Array(HEAP8.buffer, SlotPtr, 6); };
	var Fail = function(Kind) {
		delete Store[SlotPtr];
		var I = Ints(); I[2] = Kind; Atomics.store(I, 0, 3); Atomics.store(I, 3, 1);
	};
	var Init = { method: UTF8ToString(Method), headers: [], mode: 'cors', cache: 'no-store', credentials: 'omit', redirect: 'follow' };
	UTF8ToString(Headers).split('\n').forEach(function(Line) {
		var Colon = Line.indexOf(':');
		if (Colon > 0) { Init.headers.push([Line.substring(0, Colon).trim(), Line.substring(Colon + 1).trim()]); }
	});
	if (BodyLength > 0) { Init.body = HEAPU8.slice(BodyPtr, BodyPtr + BodyLength); }
	var Url_ = UTF8ToString(Url);
	var Pending;
	try { Pending = fetch(Url_, Init); } catch (E) { console.log("OSSWASM fetch could not start " + Url_ + ": " + E); Fail(3); return; }
	Pending.then(function(Response) {
		return Response.arrayBuffer().then(function(Buffer) {
			var Body = new Uint8Array(Buffer);
			Store[SlotPtr] = Body;
			var I = Ints(); I[1] = Response.status; Atomics.store(I, 4, Body.length); Atomics.store(I, 0, 2);
		});
	}).catch(function(E) {
		console.log("OSSWASM fetch failed " + Url_ + " (network error or CORS): " + E);
		Fail(1);
	});
});

EM_JS(void, WasmOss_FetchCopy, (double SlotPtr, double Dest), {
	var Store = Module.ueWasmOssFetch || {};
	var Body = Store[SlotPtr];
	delete Store[SlotPtr];
	var I = new Int32Array(HEAP8.buffer, SlotPtr, 6);
	if (Body && Body.length > 0) { HEAPU8.set(Body, Dest); }
	Atomics.store(I, 0, 4);
	Atomics.store(I, 3, 1);
});

namespace WasmOssFetch
{
	static bool RunOnMainThread(TUniqueFunction<void()>&& Function)
	{
		if (emscripten_is_main_runtime_thread())
		{
			Function();
			return true;
		}
		using FTask = TUniqueFunction<void()>;
		FTask* Task = new FTask(MoveTemp(Function));
		auto Run = [](void* Arg)
		{
			FTask* Inner = static_cast<FTask*>(Arg);
			(*Inner)();
			delete Inner;
		};
		if (!emscripten_proxy_async(emscripten_proxy_get_system_queue(), emscripten_main_runtime_thread_id(), Run, Task))
		{
			delete Task;
			return false;
		}
		return true;
	}

	static double Address(const void* Pointer)
	{
		return double(UPTRINT(Pointer));
	}

	static TArray<TUniquePtr<FRequest>> GRequests;
	static FTSTicker::FDelegateHandle GTickerHandle;

	static void Report(FRequest& Request, FWasmOssHttpResponse&& Response)
	{
		if (!Request.bReported)
		{
			Request.bReported = true;
			if (Request.OnComplete)
			{
				Request.OnComplete(Response);
			}
		}
	}

	/** Game thread, every frame while requests are pending. */
	static bool Tick(float)
	{
		const double Now = FPlatformTime::Seconds();
		// Callbacks may start new requests (adding to GRequests), so iterate over a snapshot of the current count.
		const int32 Count = GRequests.Num();
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FRequest& Request = *GRequests[Index];
			FSlot& Slot = *Request.Slot;
			const int32 Phase = Slot.Phase.load(std::memory_order_acquire);

			if (Phase == PhaseReady && !Request.bCopyRequested)
			{
				Request.bCopyRequested = true;
				Request.Response.SetNumUninitialized(Slot.BodyBytes.load(std::memory_order_acquire));
				const double SlotAddress = Address(Request.Slot);
				const double Dest = Address(Request.Response.GetData());
				if (!RunOnMainThread([SlotAddress, Dest]() { WasmOss_FetchCopy(SlotAddress, Dest); }))
				{
					UE_LOG(LogOssWasm, Error, TEXT("Could not queue the fetch copy to the main thread"));
				}
			}
			else if (Phase == PhaseCopied && !Request.bReported)
			{
				FWasmOssHttpResponse Response;
				Response.bConnected = true;
				Response.Status = Slot.Status;
				Response.Body = FString(FUTF8ToTCHAR(reinterpret_cast<const UTF8CHAR*>(Request.Response.GetData()), Request.Response.Num()));
				Report(Request, MoveTemp(Response));
			}
			else if (Phase == PhaseFailed && !Request.bReported)
			{
				FWasmOssHttpResponse Response;
				Response.TransportError = Slot.Error == 1 ? TEXT("Network error or blocked by CORS (see the browser console)") : TEXT("The request could not be started");
				Report(Request, MoveTemp(Response));
			}
			else if (!Request.bReported && Now > Request.Deadline)
			{
				FWasmOssHttpResponse Response;
				Response.TransportError = TEXT("Timed out");
				Report(Request, MoveTemp(Response)); // the slot is freed once JS is done with it
			}
		}

		// Free requests that are reported and that JS no longer touches.
		for (int32 Index = GRequests.Num() - 1; Index >= 0; --Index)
		{
			FRequest& Request = *GRequests[Index];
			if (Request.bReported && Request.Slot->JsDone.load(std::memory_order_acquire) == 1)
			{
				delete Request.Slot;
				GRequests.RemoveAtSwap(Index);
			}
			else if (Request.bReported && Request.Slot->Phase.load(std::memory_order_acquire) == PhaseReady && !Request.bCopyRequested)
			{
				// Timed out but the body arrived later: copy it anyway so JS releases its buffer and the slot.
				Request.bCopyRequested = true;
				Request.Response.SetNumUninitialized(Request.Slot->BodyBytes.load(std::memory_order_acquire));
				const double SlotAddress = Address(Request.Slot);
				const double Dest = Address(Request.Response.GetData());
				RunOnMainThread([SlotAddress, Dest]() { WasmOss_FetchCopy(SlotAddress, Dest); });
			}
		}

		if (GRequests.Num() == 0)
		{
			GTickerHandle.Reset();
			return false;
		}
		return true;
	}

	static void Send(const FString& Verb, const FString& Url, const TMap<FString, FString>& Headers, const FString& Body,
		float TimeoutSeconds, WasmOssHttp::FOnComplete&& OnComplete)
	{
		TUniquePtr<FRequest> Request = MakeUnique<FRequest>();
		Request->Slot = new FSlot();
		Request->Slot->Phase.store(PhaseRunning);
		Request->Slot->Status = 0;
		Request->Slot->Error = 0;
		Request->Slot->JsDone.store(0);
		Request->Slot->BodyBytes.store(0);
		Request->Method = FAnsiString(TCHAR_TO_UTF8(*Verb));
		Request->Url = FAnsiString(TCHAR_TO_UTF8(*Url));
		FString HeaderText;
		for (const TPair<FString, FString>& Header : Headers)
		{
			HeaderText += Header.Key + TEXT(": ") + Header.Value + TEXT("\n");
		}
		Request->Headers = FAnsiString(TCHAR_TO_UTF8(*HeaderText));
		if (!Body.IsEmpty())
		{
			FTCHARToUTF8 Utf8(*Body);
			Request->Body.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		}
		Request->Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
		Request->OnComplete = MoveTemp(OnComplete);

		// The strings and body live in Request until it is freed, which is after JS has finished with the slot.
		const double SlotAddress = Address(Request->Slot);
		const double MethodAddress = Address(*Request->Method);
		const double UrlAddress = Address(*Request->Url);
		const double HeadersAddress = Address(*Request->Headers);
		const double BodyAddress = Address(Request->Body.GetData());
		const double BodyLength = double(Request->Body.Num());

		FRequest& Ref = *Request;
		GRequests.Add(MoveTemp(Request));
		if (!GTickerHandle.IsValid())
		{
			GTickerHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("WasmOssFetch"), 0.f, &Tick);
		}

		if (!RunOnMainThread([=]() { WasmOss_FetchStart(SlotAddress, MethodAddress, UrlAddress, HeadersAddress, BodyAddress, BodyLength); }))
		{
			// Never started: JS will not touch the slot.
			Ref.Slot->JsDone.store(1);
			FWasmOssHttpResponse Response;
			Response.TransportError = TEXT("Could not queue fetch() on the browser main thread");
			Report(Ref, MoveTemp(Response));
		}
	}
}

#else // !PLATFORM_WASM

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

#endif

void WasmOssHttp::Send(const FString& Verb, const FString& Url, const TMap<FString, FString>& Headers, const FString& Body,
	float TimeoutSeconds, FOnComplete&& OnComplete)
{
	UE_LOG(LogOssWasm, Verbose, TEXT("%s %s"), *Verb, *Url);

#if PLATFORM_WASM
	WasmOssFetch::Send(Verb, Url, Headers, Body, TimeoutSeconds, MoveTemp(OnComplete));
#else
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetVerb(Verb);
	Request->SetURL(Url);
	for (const TPair<FString, FString>& Header : Headers)
	{
		Request->SetHeader(Header.Key, Header.Value);
	}
	if (!Body.IsEmpty())
	{
		Request->SetContentAsString(Body);
	}
	Request->SetTimeout(TimeoutSeconds);
	Request->OnProcessRequestComplete().BindLambda(
		[Callback = MoveTemp(OnComplete)](FHttpRequestPtr, FHttpResponsePtr HttpResponse, bool bConnectedSuccessfully)
		{
			FWasmOssHttpResponse Response;
			Response.bConnected = bConnectedSuccessfully && HttpResponse.IsValid();
			if (Response.bConnected)
			{
				Response.Status = HttpResponse->GetResponseCode();
				Response.Body = HttpResponse->GetContentAsString();
			}
			else
			{
				Response.TransportError = TEXT("Could not reach the backend");
			}
			if (Callback)
			{
				Callback(Response);
			}
		});
	Request->ProcessRequest();
#endif
}
