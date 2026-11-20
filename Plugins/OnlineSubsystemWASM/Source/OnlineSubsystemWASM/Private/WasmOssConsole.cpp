// Console commands and the -WasmOssAutoJoin start option, for testing packaged builds without any UI.
//   wasmoss.Login [Name]      log in as a guest
//   wasmoss.Servers           list servers (also shown on screen)
//   wasmoss.Join <n|id>       join server number n from the last list (default 0), or by id
//   wasmoss.AutoJoin          login + list + join the first server with room
// Start a client with -WasmOssAutoJoin (web: Development page ?args=-WasmOssAutoJoin) to do AutoJoin at startup.

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OnlineSubsystemWASMLog.h"
#include "WasmOssSubsystem.h"

namespace WasmOssConsole
{
	static TArray<FWasmOssServerInfo> GLastServers;

	static void Say(const FString& Message, const FColor Color = FColor::Cyan)
	{
		UE_LOG(LogOssWasm, Display, TEXT("%s"), *Message);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, Color, TEXT("[OSS WASM] ") + Message);
		}
	}

	static UWasmOssSubsystem* Oss(UWorld* World)
	{
		UWasmOssSubsystem* Subsystem = World ? UWasmOssSubsystem::Get(World) : nullptr;
		if (!Subsystem)
		{
			Say(TEXT("No game instance / OSS WASM subsystem"), FColor::Red);
		}
		return Subsystem;
	}

	static void Join(UWorld* World, const FWasmOssServerInfo& Server)
	{
		UWasmOssSubsystem* Subsystem = Oss(World);
		if (!Subsystem)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		Say(FString::Printf(TEXT("Joining %s (%s)..."), *Server.Name, *Server.ConnectUrl));
		Subsystem->RequestJoinTicket(Server.Id, [WeakWorld](bool bOk, const FString& Address, const FString& Ticket, const FString& Error)
		{
			UWorld* W = WeakWorld.Get();
			UWasmOssSubsystem* S = W ? UWasmOssSubsystem::Get(W) : nullptr;
			if (!bOk || !S)
			{
				Say(TEXT("Join failed: ") + Error, FColor::Red);
				return;
			}
			if (!S->TravelToServer(W->GetFirstPlayerController(), Address, Ticket))
			{
				Say(TEXT("Join failed: no player controller"), FColor::Red);
			}
		});
	}

	static void ListServers(UWorld* World, TFunction<void()>&& Then = nullptr)
	{
		UWasmOssSubsystem* Subsystem = Oss(World);
		if (!Subsystem)
		{
			return;
		}
		Subsystem->GetServerList(FString(), true, [Then = MoveTemp(Then)](bool bOk, const TArray<FWasmOssServerInfo>& Servers, const FString& Error)
		{
			if (!bOk)
			{
				Say(TEXT("Server list failed: ") + Error, FColor::Red);
				return;
			}
			GLastServers = Servers;
			Say(FString::Printf(TEXT("%d server(s)"), Servers.Num()));
			for (int32 Index = 0; Index < Servers.Num(); ++Index)
			{
				const FWasmOssServerInfo& S = Servers[Index];
				Say(FString::Printf(TEXT("  [%d] %s  %s  %d/%d  %s  id=%s"), Index, *S.Name, *S.Map, S.Players, S.MaxPlayers, *S.ConnectUrl, *S.Id));
			}
			if (Then)
			{
				Then();
			}
		});
	}

	static void Login(UWorld* World, const FString& Name, TFunction<void()>&& Then = nullptr)
	{
		UWasmOssSubsystem* Subsystem = Oss(World);
		if (!Subsystem)
		{
			return;
		}
		Say(TEXT("Logging in to ") + Subsystem->GetBackendUrl());
		Subsystem->LoginAsGuest(Name, [Then = MoveTemp(Then)](bool bOk, const FWasmOssPlayer& Player, const FString& Error)
		{
			if (!bOk)
			{
				Say(TEXT("Login failed: ") + Error, FColor::Red);
				return;
			}
			Say(FString::Printf(TEXT("Logged in as %s"), *Player.DisplayName), FColor::Green);
			if (Then)
			{
				Then();
			}
		});
	}

	static void AutoJoin(UWorld* World)
	{
		TWeakObjectPtr<UWorld> WeakWorld(World);
		Login(World, FString(), [WeakWorld]()
		{
			ListServers(WeakWorld.Get(), [WeakWorld]()
			{
				for (const FWasmOssServerInfo& Server : GLastServers)
				{
					if (!Server.IsFull())
					{
						Join(WeakWorld.Get(), Server);
						return;
					}
				}
				Say(TEXT("No server with room. Start a dedicated server, then run wasmoss.AutoJoin"), FColor::Yellow);
			});
		});
	}

	static FAutoConsoleCommandWithWorldAndArgs CmdLogin(TEXT("wasmoss.Login"), TEXT("Log in as a guest: wasmoss.Login [DisplayName]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			Login(World, FString::Join(Args, TEXT(" ")));
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdServers(TEXT("wasmoss.Servers"), TEXT("List dedicated servers from the backend"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			ListServers(World);
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdJoin(TEXT("wasmoss.Join"), TEXT("Join a server: wasmoss.Join <index from wasmoss.Servers | server id>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const FString Arg = Args.Num() > 0 ? Args[0] : TEXT("0");
			const FWasmOssServerInfo* Target = nullptr;
			if (Arg.IsNumeric())
			{
				const int32 Index = FCString::Atoi(*Arg);
				Target = GLastServers.IsValidIndex(Index) ? &GLastServers[Index] : nullptr;
			}
			else
			{
				Target = GLastServers.FindByPredicate([&Arg](const FWasmOssServerInfo& S) { return S.Id == Arg; });
			}
			if (Target)
			{
				Join(World, *Target);
			}
			else if (!Arg.IsNumeric())
			{
				FWasmOssServerInfo ById;
				ById.Id = Arg;
				Join(World, ById);
			}
			else
			{
				Say(TEXT("No such server; run wasmoss.Servers first"), FColor::Red);
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdAutoJoin(TEXT("wasmoss.AutoJoin"), TEXT("Login, list servers and join the first one with room"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			AutoJoin(World);
		}));

	/** -WasmOssAutoJoin: wait until the client has a local player controller, then AutoJoin once. */
	static void StartAutoJoinIfRequested()
	{
		if (IsRunningDedicatedServer() || !FParse::Param(FCommandLine::Get(), TEXT("WasmOssAutoJoin")))
		{
			return;
		}
		FTSTicker::GetCoreTicker().AddTicker(TEXT("WasmOssAutoJoin"), 0.5f, [](float) -> bool
		{
			if (!GEngine)
			{
				return true;
			}
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (World && World->IsGameWorld() && World->GetFirstPlayerController() && UWasmOssSubsystem::Get(World))
				{
					AutoJoin(World);
					return false; // once
				}
			}
			return true;
		});
	}
}

void WasmOssStartAutoJoinIfRequested()
{
	WasmOssConsole::StartAutoJoinIfRequested();
}
