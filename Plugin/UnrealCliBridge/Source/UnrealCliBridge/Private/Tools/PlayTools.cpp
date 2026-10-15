// Tools that drive a running play session: start it without blocking, send input, wait, look at the
// game viewport and query or call into actors of the play world.

#include "Containers/Ticker.h"
#include "Editor.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetDriver.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UObject/StrongObjectPtr.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedInputDeveloperSettings.h"
#include "InputMappingContext.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "ImageUtils.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputKeyEventArgs.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDeviceNull.h"
#include "Misc/Paths.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "Server/BridgeLog.h"
#include "Server/JsonUtil.h"
#include "Server/ToolRegistry.h"
#include "Tools/BlueprintHelpers.h"
#include "Tools/ToolRegistration.h"
#include "UnrealClient.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "UObject/UObjectIterator.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"

using namespace UCli;

namespace
{
	const TCHAR* NetModeName(ENetMode Mode)
	{
		switch (Mode)
		{
		case NM_DedicatedServer: return TEXT("DedicatedServer");
		case NM_ListenServer: return TEXT("ListenServer");
		case NM_Client: return TEXT("Client");
		default: return TEXT("Standalone");
		}
	}

	struct FPieWorld
	{
		FString Id;
		UWorld* World = nullptr;
		int32 Instance = 0;
	};

	/** Worlds of the play session in PIE instance order: "standalone", or "server" and "client:1", "client:2", ... */
	TArray<FPieWorld> PieWorlds()
	{
		TArray<FPieWorld> Out;
		for (const FWorldContext& C : GEngine->GetWorldContexts())
		{
			if (C.WorldType == EWorldType::PIE && C.World())
			{
				Out.Add({ FString(), C.World(), C.PIEInstance });
			}
		}
		Out.Sort([](const FPieWorld& A, const FPieWorld& B) { return A.Instance < B.Instance; });
		int32 Client = 0, Standalone = 0;
		for (FPieWorld& W : Out)
		{
			const ENetMode Mode = W.World->GetNetMode();
			if (Mode == NM_Client) W.Id = FString::Printf(TEXT("client:%d"), ++Client);
			else if (Mode == NM_Standalone) W.Id = ++Standalone == 1 ? FString(TEXT("standalone")) : FString::Printf(TEXT("standalone:%d"), Standalone);
			else W.Id = TEXT("server");
		}
		return Out;
	}

	int32 CountControllers(UWorld* World)
	{
		int32 N = 0;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It) ++N;
		return N;
	}

	/** The play world named by the optional "world" argument (default: the editor's primary play world). */
	UWorld* RequirePlayWorld(const FToolCallRef& Call)
	{
		if (!GEditor->PlayWorld)
		{
			Call->Fail(TEXT("pie.not_running"), TEXT("no play session is running"), TEXT("start one: pie_start"));
			return nullptr;
		}
		const FString Id = Call->GetString(TEXT("world"));
		if (Id.IsEmpty())
		{
			return GEditor->PlayWorld;
		}
		TArray<FString> Ids;
		for (const FPieWorld& W : PieWorlds())
		{
			if (W.Id == Id) return W.World;
			Ids.Add(W.Id);
		}
		Call->Fail(TEXT("pie.world_not_found"), FString::Printf(TEXT("the play session has no world '%s'"), *Id),
			FString::Printf(TEXT("worlds: %s"), *FString::Join(Ids, TEXT(", "))), Arr(StrArray(Ids)));
		return nullptr;
	}

	APlayerController* RequirePlayer(const FToolCallRef& Call)
	{
		UWorld* World = RequirePlayWorld(Call);
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (World && !PC)
		{
			Call->Fail(TEXT("pie.no_player"), TEXT("the play world has no player controller"));
		}
		return PC;
	}

	// Finds a play-world actor by label, name, or "player" for the player's pawn.
	AActor* FindPlayActor(const FToolCallRef& Call)
	{
		UWorld* World = RequirePlayWorld(Call);
		if (!World) return nullptr;
		const FString Id = Call->GetString(TEXT("actor"));
		if (Id == TEXT("player"))
		{
			APlayerController* PC = World->GetFirstPlayerController();
			if (PC && PC->GetPawn()) return PC->GetPawn();
		}
		if (Id == TEXT("controller"))
		{
			if (APlayerController* PC = World->GetFirstPlayerController()) return PC;
		}
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetActorLabel() == Id || It->GetName() == Id) return *It;
		}
		Call->Fail(TEXT("pie.actor_not_found"), FString::Printf(TEXT("no actor '%s' in the play world"), *Id), TEXT("list them: pie_actors (\"player\" is the player's pawn, \"controller\" its player controller)"));
		return nullptr;
	}

	/** An actor of World by label or name, else the first of a class with that name (with or without _C). */
	AActor* FindActorIn(UWorld* World, const FString& Id)
	{
		AActor* ByClass = nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetActorLabel() == Id || It->GetName() == Id) return *It;
			const FString C = It->GetClass()->GetName();
			if (!ByClass && (C == Id || C == Id + TEXT("_C"))) ByClass = *It;
		}
		return ByClass;
	}

	/** Exports Property (Name or Component.Name) of A as UE text. */
	bool ReadActorProperty(AActor* A, const FString& Path, FString& Out, FString& Error)
	{
		UObject* Target = A;
		FString Name = Path;
		FString Component, Rest;
		if (Path.Split(TEXT("."), &Component, &Rest))
		{
			for (UActorComponent* C : A->GetComponents())
			{
				if (C && C->GetName() == Component) { Target = C; Name = Rest; break; }
			}
		}
		FProperty* P = Target->GetClass()->FindPropertyByName(FName(*Name));
		if (!P)
		{
			Error = FString::Printf(TEXT("%s has no property '%s'"), *Target->GetClass()->GetName(), *Name);
			return false;
		}
		P->ExportTextItem_Direct(Out, P->ContainerPtrToValuePtr<void>(Target), nullptr, Target, PPF_None);
		return true;
	}

	/** Text equality, with numbers compared numerically ("5" == "5.000000"). */
	bool SameValue(const FString& A, const FString& B)
	{
		if (A == B) return true;
		if (A.IsNumeric() && B.IsNumeric()) return FMath::IsNearlyEqual(FCString::Atod(*A), FCString::Atod(*B), 1e-4);
		return A.Equals(B, ESearchCase::IgnoreCase);
	}

	TSharedPtr<FJsonObject> PlayVectorJson(const FVector& V)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("x"), V.X);
		O->SetNumberField(TEXT("y"), V.Y);
		O->SetNumberField(TEXT("z"), V.Z);
		return O;
	}

	TSharedPtr<FJsonObject> PlayActorJson(AActor* A)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("label"), A->GetActorLabel());
		O->SetStringField(TEXT("name"), A->GetName());
		O->SetStringField(TEXT("class"), A->GetClass()->GetName());
		O->SetObjectField(TEXT("location"), PlayVectorJson(A->GetActorLocation()));
		const FRotator R = A->GetActorRotation();
		TSharedPtr<FJsonObject> Rot = MakeShared<FJsonObject>();
		Rot->SetNumberField(TEXT("pitch"), R.Pitch);
		Rot->SetNumberField(TEXT("yaw"), R.Yaw);
		Rot->SetNumberField(TEXT("roll"), R.Roll);
		O->SetObjectField(TEXT("rotation"), Rot);
		O->SetObjectField(TEXT("velocity"), PlayVectorJson(A->GetVelocity()));
		const auto RoleName = [](ENetRole Role) { return StaticEnum<ENetRole>()->GetNameStringByValue(Role).RightChop(5); };
		O->SetStringField(TEXT("role"), RoleName(A->GetLocalRole()));
		O->SetStringField(TEXT("remoteRole"), RoleName(A->GetRemoteRole()));
		if (AActor* Owner = A->GetOwner()) O->SetStringField(TEXT("owner"), Owner->GetActorLabel());
		return O;
	}

	bool PlayActionValue(const TSharedPtr<FJsonValue>& V, FInputActionValue& Out)
	{
		if (!V.IsValid() || V->Type == EJson::Null) { Out = FInputActionValue(true); return true; }
		if (V->Type == EJson::Boolean) { Out = FInputActionValue(V->AsBool()); return true; }
		if (V->Type == EJson::Number) { Out = FInputActionValue(static_cast<float>(V->AsNumber())); return true; }
		if (V->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
			if (A.Num() == 2) { Out = FInputActionValue(FVector2D(A[0]->AsNumber(), A[1]->AsNumber())); return true; }
			if (A.Num() == 3) { Out = FInputActionValue(FVector(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber())); return true; }
		}
		return false;
	}

	FString PlayScreenshotPath(const FString& Requested)
	{
		if (!Requested.IsEmpty())
		{
			return FPaths::ConvertRelativePathToFull(FPaths::IsRelative(Requested) ? FPaths::ProjectDir() / Requested : Requested);
		}
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("UnrealCli/Screenshots") / FString::Printf(TEXT("pie-%s.png"), *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S-%s"))));
	}
}

UWorld* UCli::ResolvePlayWorld(const FToolCallRef& Call)
{
	return RequirePlayWorld(Call);
}

AActor* UCli::ResolvePlayActor(const FToolCallRef& Call)
{
	return FindPlayActor(Call);
}

void UCli::RegisterPlayTools(FToolRegistry& R)
{
	R.Add(TEXT("pie_start"), TEXT("runtime"), TEXT("Start Play In Editor and return once every player exists; drive it with pie_input / pie_wait / pie_screenshot, end it with pie_stop. players (1-4) and netMode (standalone; listen = listen server + clients; client = dedicated server + clients) start a multiplayer session in the editor process; pick a world with the world argument of the other pie tools (see pie_worlds)."),
		TEXT(R"({"type":"object","properties":{"players":{"type":"integer","minimum":1,"maximum":4},"netMode":{"type":"string","enum":["standalone","listen","client"]}}})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			if (GEditor->PlayWorld || GEditor->IsPlaySessionRequestQueued())
			{
				Call->Fail(TEXT("pie.already_running"), TEXT("a play session is already running"), TEXT("stop it: pie_stop"));
				return;
			}
			const int32 Players = FMath::Clamp(static_cast<int32>(Call->GetNumber(TEXT("players"), 1)), 1, 4);
			const FString Mode = Call->GetString(TEXT("netMode"), TEXT("standalone"));
			FRequestPlaySessionParams Params;
			Params.WorldType = EPlaySessionWorldType::PlayInEditor;
			// A copy of the play settings: the user's Multiplayer Options stay as they are.
			TSharedRef<TStrongObjectPtr<ULevelEditorPlaySettings>> Settings = MakeShared<TStrongObjectPtr<ULevelEditorPlaySettings>>(
				DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(), GetTransientPackage()));
			(*Settings)->SetPlayNetMode(Mode == TEXT("listen") ? PIE_ListenServer : Mode == TEXT("client") ? PIE_Client : PIE_Standalone);
			(*Settings)->SetPlayNumberOfClients(Players);
			(*Settings)->SetRunUnderOneProcess(true);
			Params.EditorPlaySettings = Settings->Get();
			GEditor->RequestPlaySession(Params);
			// A dedicated server has a world of its own; a listen server's host is one of the players.
			const int32 Expected = Mode == TEXT("client") ? Players + 1 : Players;
			const bool bNetworked = Mode == TEXT("listen") || Mode == TEXT("client");
			const double Deadline = FPlatformTime::Seconds() + 90.0;
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Call, Settings, Expected, Players, bNetworked, Deadline](float)
			{
				const TArray<FPieWorld> Worlds = PieWorlds();
				bool bReady = GEditor->PlayWorld && Worlds.Num() == Expected;
				for (const FPieWorld& W : Worlds)
				{
					const ENetMode NM = W.World->GetNetMode();
					if (NM == NM_DedicatedServer || NM == NM_ListenServer)
					{
						// Every client has joined once the server has a controller per player.
						bReady &= !bNetworked || CountControllers(W.World) >= Players;
					}
					else
					{
						APlayerController* PC = W.World->GetFirstPlayerController();
						bReady &= PC && PC->GetPawn();
					}
				}
				if (bReady)
				{
					TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
					O->SetStringField(TEXT("world"), GEditor->PlayWorld->GetPathName());
					APlayerController* First = GEditor->PlayWorld->GetFirstPlayerController();
					O->SetStringField(TEXT("pawn"), First && First->GetPawn() ? First->GetPawn()->GetName() : FString());
					TArray<FString> Ids;
					for (const FPieWorld& W : Worlds) Ids.Add(W.Id);
					O->SetArrayField(TEXT("worlds"), StrArray(Ids));
					Call->Succeed(O);
					return false;
				}
				if (FPlatformTime::Seconds() > Deadline)
				{
					Call->Fail(TEXT("pie.start_failed"), FString::Printf(TEXT("the play session did not start within 90 s (%d of %d worlds ready)"), Worlds.Num(), Expected),
						TEXT("check the log: unrealengine-cli logs --severity warning"));
					return false;
				}
				return true;
			}), 0.05f);
		});

	R.Add(TEXT("pie_worlds"), TEXT("runtime"), TEXT("Worlds of the running play session: id (standalone, server, client:1, ...; pass it as world to the other pie tools), netMode and the number of player controllers."),
		TEXT(R"({"type":"object","properties":{}})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			if (!RequirePlayWorld(Call)) return;
			TArray<TSharedPtr<FJsonValue>> Items;
			for (const FPieWorld& W : PieWorlds())
			{
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetStringField(TEXT("id"), W.Id);
				O->SetStringField(TEXT("netMode"), NetModeName(W.World->GetNetMode()));
				O->SetNumberField(TEXT("pieInstance"), W.Instance);
				O->SetStringField(TEXT("map"), W.World->GetMapName());
				O->SetNumberField(TEXT("playerControllers"), CountControllers(W.World));
				O->SetBoolField(TEXT("primary"), W.World == GEditor->PlayWorld);
				Items.Add(Obj(O));
			}
			Call->Succeed(Arr(Items));
		});

	R.Add(TEXT("net_assert"), TEXT("net"), TEXT("Wait until an actor's property has the same value in every world of the play session (or the expect value in all of them); fails with net.mismatch and each world's last value after timeoutSeconds (default 5). actor: label or name (level actors have the same one in every world) or a class name; property: Name or Component.Name."),
		TEXT(R"({"type":"object","properties":{"actor":{"type":"string"},"property":{"type":"string"},"expect":{"type":"string"},"timeoutSeconds":{"type":"number"}},"required":["actor","property"]})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			if (!RequirePlayWorld(Call)) return;
			const FString Actor = Call->GetString(TEXT("actor"));
			const FString Property = Call->GetString(TEXT("property"));
			const bool bExpect = Call->Has(TEXT("expect"));
			const FString Expect = Call->GetString(TEXT("expect"));
			const double Limit = FMath::Max(0.0, Call->GetNumber(TEXT("timeoutSeconds"), 5.0));
			const double Start = FPlatformTime::Seconds();
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Call, Actor, Property, bExpect, Expect, Limit, Start](float)
			{
				if (!GEditor->PlayWorld)
				{
					Call->Fail(TEXT("pie.not_running"), TEXT("the play session ended"));
					return false;
				}
				TArray<TSharedPtr<FJsonValue>> Worlds;
				TArray<FString> Values;
				bool bAllFound = true;
				FString Problem;
				for (const FPieWorld& W : PieWorlds())
				{
					TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
					O->SetStringField(TEXT("id"), W.Id);
					FString Value;
					AActor* A = FindActorIn(W.World, Actor);
					if (!A)
					{
						bAllFound = false;
						O->SetField(TEXT("value"), MakeShared<FJsonValueNull>());
						O->SetStringField(TEXT("problem"), TEXT("actor not found (not replicated or not relevant to this world?)"));
					}
					else if (!ReadActorProperty(A, Property, Value, Problem))
					{
						Call->Fail(TEXT("prop.not_found"), Problem, TEXT("list properties: prop_list"));
						return false;
					}
					else
					{
						O->SetStringField(TEXT("value"), Value);
						Values.Add(Value);
					}
					Worlds.Add(Obj(O));
				}
				bool bOk = bAllFound && Values.Num() > 0;
				for (const FString& V : Values)
				{
					bOk &= SameValue(V, bExpect ? Expect : Values[0]);
				}
				const double Waited = FPlatformTime::Seconds() - Start;
				if (bOk)
				{
					TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
					O->SetBoolField(TEXT("ok"), true);
					O->SetArrayField(TEXT("worlds"), Worlds);
					O->SetNumberField(TEXT("waitedSeconds"), Waited);
					Call->Succeed(O);
					return false;
				}
				if (Waited >= Limit || Call->bCancelled)
				{
					TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
					D->SetArrayField(TEXT("worlds"), Worlds);
					if (bExpect) D->SetStringField(TEXT("expect"), Expect);
					Call->Fail(TEXT("net.mismatch"), FString::Printf(TEXT("%s.%s %s after %.1f s"), *Actor, *Property,
						bExpect ? *FString::Printf(TEXT("is not %s in every world"), *Expect) : TEXT("differs between worlds"), Waited),
						TEXT("is the actor replicated (bReplicates) and the variable replicated? changes made on a client do not reach the server; use a Server RPC"), Obj(D));
					return false;
				}
				return true;
			}), 0.02f);
		});

	R.Add(TEXT("net_emulate"), TEXT("net"), TEXT("Simulate network conditions in the running play session: latencyMs (outgoing lag), jitterMs, lossPercent on the server, the clients or all worlds; off=true removes them. Ends with the session."),
		TEXT(R"({"type":"object","properties":{"latencyMs":{"type":"integer"},"jitterMs":{"type":"integer"},"lossPercent":{"type":"integer"},"target":{"type":"string","enum":["all","server","clients"]},"off":{"type":"boolean"}}})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			if (!RequirePlayWorld(Call)) return;
#if DO_ENABLE_NET_TEST
			FPacketSimulationSettings S;
			if (!Call->GetBool(TEXT("off")))
			{
				S.PktLag = static_cast<int32>(Call->GetNumber(TEXT("latencyMs"), 0));
				S.PktLagVariance = static_cast<int32>(Call->GetNumber(TEXT("jitterMs"), 0));
				S.PktLoss = FMath::Clamp(static_cast<int32>(Call->GetNumber(TEXT("lossPercent"), 0)), 0, 100);
			}
			const FString Target = Call->GetString(TEXT("target"), TEXT("all"));
			TArray<FString> Applied;
			for (const FPieWorld& W : PieWorlds())
			{
				const bool bServer = W.World->GetNetMode() == NM_ListenServer || W.World->GetNetMode() == NM_DedicatedServer;
				if ((Target == TEXT("server") && !bServer) || (Target == TEXT("clients") && bServer)) continue;
				if (UNetDriver* Driver = W.World->GetNetDriver())
				{
					Driver->SetPacketSimulationSettings(S);
					Applied.Add(W.Id);
				}
			}
			if (Applied.IsEmpty())
			{
				Call->Fail(TEXT("net.not_networked"), TEXT("no world of the play session has a network connection"), TEXT("start a multiplayer session: pie_start players=2 netMode=listen"));
				return;
			}
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("latencyMs"), S.PktLag);
			O->SetNumberField(TEXT("jitterMs"), S.PktLagVariance);
			O->SetNumberField(TEXT("lossPercent"), S.PktLoss);
			O->SetArrayField(TEXT("worlds"), StrArray(Applied));
			Call->Succeed(O);
#else
			Call->Fail(TEXT("engine.unsupported"), TEXT("network emulation is compiled out of this build"));
#endif
		});

	R.Add(TEXT("input_default_context_add"), TEXT("input"), TEXT("Register an Input Mapping Context in Project Settings > Enhanced Input > Default Mapping Contexts so every player gets it at start (no Blueprint code needed); remove=true takes it out. Saved to DefaultInput.ini."),
		TEXT(R"({"type":"object","properties":{"context":{"type":"string"},"priority":{"type":"integer"},"remove":{"type":"boolean"}},"required":["context"]})"),
		EToolFlags::Mutating | EToolFlags::NoUndo,
		[](const FToolCallRef& Call)
		{
			UInputMappingContext* Ctx = LoadObject<UInputMappingContext>(nullptr, *BP::ObjectPath(Call->GetString(TEXT("context"))), nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!Ctx)
			{
				Call->Fail(TEXT("input.context_not_found"), FString::Printf(TEXT("no Input Mapping Context at '%s'"), *Call->GetString(TEXT("context"))), TEXT("create one: mapping_context_create"));
				return;
			}
			UEnhancedInputDeveloperSettings* S = GetMutableDefault<UEnhancedInputDeveloperSettings>();
			S->Modify();
			const int32 Priority = static_cast<int32>(Call->GetNumber(TEXT("priority"), 0));
			FDefaultContextSetting* Existing = S->DefaultMappingContexts.FindByPredicate([Ctx](const FDefaultContextSetting& D) { return D.InputMappingContext.ToSoftObjectPath() == FSoftObjectPath(Ctx); });
			if (Call->GetBool(TEXT("remove")))
			{
				S->DefaultMappingContexts.RemoveAll([Ctx](const FDefaultContextSetting& D) { return D.InputMappingContext.ToSoftObjectPath() == FSoftObjectPath(Ctx); });
			}
			else if (Existing)
			{
				Existing->Priority = Priority;
			}
			else
			{
				FDefaultContextSetting D;
				D.InputMappingContext = Ctx;
				D.Priority = Priority;
				S->DefaultMappingContexts.Add(D);
			}
			if (!Call->GetBool(TEXT("remove"))) S->bEnableDefaultMappingContexts = true;
			S->TryUpdateDefaultConfigFile();
			TArray<FString> Names;
			for (const FDefaultContextSetting& D : S->DefaultMappingContexts) Names.Add(D.InputMappingContext.ToString());
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetArrayField(TEXT("defaultContexts"), StrArray(Names));
			Call->Succeed(O);
		});

	R.Add(TEXT("pie_input"), TEXT("runtime"), TEXT("Send input to the running play session: a key (W, SpaceBar, LeftMouseButton, Gamepad_LeftX, ...) or an Enhanced Input action asset with a value (bool, number, [x,y] or [x,y,z]). Held for seconds (0 = one press/injection); replies after release."),
		TEXT(R"({"type":"object","properties":{"world":{"type":"string","description":"server, client:1, ... (see pie_worlds); default: the primary play world"},"key":{"type":"string"},"action":{"type":"string"},"value":{},"seconds":{"type":"number"}}})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			APlayerController* PC = RequirePlayer(Call);
			if (!PC) return;
			const double Seconds = FMath::Max(0.0, Call->GetNumber(TEXT("seconds"), 0.1));
			TWeakObjectPtr<APlayerController> WeakPC(PC);
			if (Call->Has(TEXT("action")))
			{
				UInputAction* Action = LoadObject<UInputAction>(nullptr, *BP::ObjectPath(Call->GetString(TEXT("action"))), nullptr, LOAD_NoWarn | LOAD_Quiet);
				ULocalPlayer* LP = PC->GetLocalPlayer();
				UEnhancedInputLocalPlayerSubsystem* Sub = LP ? LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
				FInputActionValue Value;
				if (!Action || !Sub || !PlayActionValue(Call->Args->TryGetField(TEXT("value")), Value))
				{
					Call->Fail(TEXT("pie.input_invalid"), FString::Printf(TEXT("no Input Action at '%s' (or no Enhanced Input subsystem, or value is not bool/number/[x,y]/[x,y,z])"), *Call->GetString(TEXT("action"))), TEXT("search: asset_search class=InputAction"));
					return;
				}
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetStringField(TEXT("action"), Action->GetPathName());
				O->SetNumberField(TEXT("seconds"), Seconds);
				if (Seconds <= 0.0)
				{
					Sub->InjectInputForAction(Action, Value, {}, {});
					Call->Succeed(O);
					return;
				}
				Sub->StartContinuousInputInjectionForAction(Action, Value, {}, {});
				TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> WeakSub(Sub);
				TWeakObjectPtr<UInputAction> WeakAction(Action);
				const double End = FPlatformTime::Seconds() + Seconds;
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Call, O, WeakSub, WeakAction, End](float)
				{
					if (GEditor->PlayWorld && WeakSub.IsValid() && FPlatformTime::Seconds() < End) return true;
					if (WeakSub.IsValid() && WeakAction.IsValid()) WeakSub->StopContinuousInputInjectionForAction(WeakAction.Get());
					Call->Succeed(O);
					return false;
				}), 0.0f);
				return;
			}
			const FKey Key(*Call->GetString(TEXT("key")));
			if (!Key.IsValid())
			{
				Call->Fail(TEXT("pie.input_invalid"), FString::Printf(TEXT("'%s' is not a key name"), *Call->GetString(TEXT("key"))), TEXT("pass key (e.g. W, SpaceBar, LeftMouseButton, Gamepad_LeftX) or action (Input Action asset path)"));
				return;
			}
			const bool bAxis = Key.IsAxis1D() || Key.IsAxis2D() || Key.IsAxis3D();
			const float Amount = static_cast<float>(Call->GetNumber(TEXT("value"), 1.0));
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("key"), Key.GetFName().ToString());
			O->SetNumberField(TEXT("seconds"), Seconds);
			if (!bAxis)
			{
				PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Pressed, 1.f));
			}
			const double End = FPlatformTime::Seconds() + Seconds;
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Call, O, WeakPC, Key, bAxis, Amount, End](float)
			{
				APlayerController* P = WeakPC.Get();
				const bool bHolding = P && GEditor->PlayWorld && FPlatformTime::Seconds() < End;
				if (bHolding)
				{
					if (bAxis) P->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Axis, Amount));
					return true;
				}
				if (P)
				{
					P->InputKey(FInputKeyEventArgs::CreateSimulated(Key, bAxis ? IE_Axis : IE_Released, 0.f));
				}
				Call->Succeed(O);
				return false;
			}), 0.0f);
		});

	R.Add(TEXT("pie_wait"), TEXT("runtime"), TEXT("Let the play session run for seconds, or until a log line contains untilLog (whichever comes first)."),
		TEXT(R"({"type":"object","properties":{"world":{"type":"string","description":"server, client:1, ... (see pie_worlds); default: the primary play world"},"seconds":{"type":"number"},"untilLog":{"type":"string"}}})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			if (!RequirePlayWorld(Call)) return;
			const double Seconds = FMath::Max(0.0, Call->GetNumber(TEXT("seconds"), 1.0));
			const FString Until = Call->GetString(TEXT("untilLog"));
			const int64 LogStart = FBridgeLog::Get().NextSeq();
			const double Start = FPlatformTime::Seconds();
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Call, Seconds, Until, LogStart, Start](float)
			{
				auto Done = [&](const TCHAR* Reason, const FString& Line)
				{
					TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
					O->SetStringField(TEXT("endedReason"), Reason);
					O->SetNumberField(TEXT("waited"), FPlatformTime::Seconds() - Start);
					if (!Line.IsEmpty()) O->SetStringField(TEXT("line"), Line);
					Call->Succeed(O);
					return false;
				};
				if (!GEditor->PlayWorld) return Done(TEXT("ended"), FString());
				if (!Until.IsEmpty())
				{
					for (const FBridgeLogLine& L : FBridgeLog::Get().Read(LogStart, 0, ELogVerbosity::VeryVerbose, FString()))
					{
						if (L.Text.Contains(Until)) return Done(TEXT("log"), L.Text);
					}
				}
				if (FPlatformTime::Seconds() - Start >= Seconds) return Done(TEXT("duration"), FString());
				return true;
			}), 0.05f);
		});

	R.Add(TEXT("pie_screenshot"), TEXT("runtime"), TEXT("Capture the play session's game viewport to a PNG (default under Saved/UnrealCli/Screenshots) and return its path; blank=true means a single flat colour (unlit level or camera sees nothing). View the image to check what the player sees."),
		TEXT(R"({"type":"object","properties":{"world":{"type":"string","description":"server, client:1, ... (see pie_worlds); default: the primary play world"},"path":{"type":"string"}}})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			UWorld* World = RequirePlayWorld(Call);
			if (!World) return;
			UGameViewportClient* GVC = World->GetGameViewport();
			FViewport* VP = GVC ? GVC->Viewport : nullptr;
			TArray<FColor> Pixels;
			FIntPoint Size(0, 0);
			FString Source = TEXT("slate");
			// The Slate capture of the viewport widget includes UMG on top of the scene; ReadPixels has the scene only.
			TSharedPtr<SViewport> ViewportWidget = GVC ? GVC->GetGameViewportWidget() : nullptr;
			FIntVector SlateSize(0, 0, 0);
			if (ViewportWidget.IsValid() && FSlateApplication::IsInitialized() && FSlateApplication::Get().TakeScreenshot(ViewportWidget.ToSharedRef(), Pixels, SlateSize) && Pixels.Num() > 0)
			{
				Size = FIntPoint(SlateSize.X, SlateSize.Y);
			}
			else if (VP && VP->ReadPixels(Pixels) && Pixels.Num() > 0)
			{
				Size = VP->GetSizeXY();
				Source = TEXT("viewport");
			}
			if (Pixels.Num() == 0 || Pixels.Num() != Size.X * Size.Y)
			{
				Call->Fail(TEXT("pie.screenshot_failed"), TEXT("could not read the game viewport (an editor started with --null-rhi has none)"), TEXT("open the editor with rendering: unrealengine-cli editor open (or --headless)"));
				return;
			}
			int32 MinLuma = 255, MaxLuma = 0;
			for (FColor& C : Pixels)
			{
				C.A = 255;
				const int32 Luma = (C.R * 3 + C.G * 6 + C.B) / 10;
				MinLuma = FMath::Min(MinLuma, Luma);
				MaxLuma = FMath::Max(MaxLuma, Luma);
			}
			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(Size.X, Size.Y, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
			const FString Path = PlayScreenshotPath(Call->GetString(TEXT("path")));
			if (!FFileHelper::SaveArrayToFile(Png, *Path))
			{
				Call->Fail(TEXT("io.write"), FString::Printf(TEXT("could not write %s"), *Path));
				return;
			}
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("path"), Path);
			O->SetNumberField(TEXT("width"), Size.X);
			O->SetNumberField(TEXT("height"), Size.Y);
			// One flat colour usually means nothing is lit or the camera sees nothing.
			O->SetBoolField(TEXT("blank"), MaxLuma - MinLuma < 8);
			O->SetStringField(TEXT("source"), Source);
			Call->Succeed(O);
		});

	R.Add(TEXT("pie_actors"), TEXT("runtime"), TEXT("Actors of the play world (filter by class or label/name substring) with location, rotation and velocity; the player's pawn is flagged."),
		TEXT(R"({"type":"object","properties":{"world":{"type":"string","description":"server, client:1, ... (see pie_worlds); default: the primary play world"},"classFilter":{"type":"string"},"nameFilter":{"type":"string"},"limit":{"type":"integer"}}})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			UWorld* World = RequirePlayWorld(Call);
			if (!World) return;
			const FString ClassFilter = Call->GetString(TEXT("classFilter"));
			const FString NameFilter = Call->GetString(TEXT("nameFilter"));
			const int32 Limit = static_cast<int32>(Call->GetNumber(TEXT("limit"), 200));
			APlayerController* PC = World->GetFirstPlayerController();
			APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;
			// An exact class name (with or without the Blueprint _C suffix) wins over substring matches,
			// so "BP_Coin" does not also return BP_CoinGM.
			// Decided by class existence, not by live actors: destroyed pickups must not turn the filter into a substring.
			const bool bExact = !ClassFilter.IsEmpty()
				&& (FindFirstObject<UClass>(*ClassFilter, EFindFirstObjectOptions::NativeFirst) || FindFirstObject<UClass>(*(ClassFilter + TEXT("_C")), EFindFirstObjectOptions::NativeFirst));
			TArray<TSharedPtr<FJsonValue>> Items;
			for (TActorIterator<AActor> It(World); It && Items.Num() < Limit; ++It)
			{
				const FString N = It->GetClass()->GetName();
				if (!ClassFilter.IsEmpty() && (bExact ? !(N.Equals(ClassFilter, ESearchCase::IgnoreCase) || N.Equals(ClassFilter + TEXT("_C"), ESearchCase::IgnoreCase)) : !N.Contains(ClassFilter))) continue;
				if (!NameFilter.IsEmpty() && !It->GetActorLabel().Contains(NameFilter) && !It->GetName().Contains(NameFilter)) continue;
				TSharedPtr<FJsonObject> O = PlayActorJson(*It);
				if (*It == PlayerPawn) O->SetBoolField(TEXT("player"), true);
				Items.Add(Obj(O));
			}
			Call->Succeed(Arr(Items));
		});

	R.Add(TEXT("pie_get"), TEXT("runtime"), TEXT("Read properties of a play-world actor (or one of its components): actor is a label, name, \"player\" (the pawn) or \"controller\" (its player controller); without properties returns its transform and velocity."),
		TEXT(R"({"type":"object","properties":{"world":{"type":"string","description":"server, client:1, ... (see pie_worlds); default: the primary play world"},"actor":{"type":"string"},"component":{"type":"string"},"properties":{"type":"array","items":{"type":"string"}}},"required":["actor"]})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			AActor* A = FindPlayActor(Call);
			if (!A) return;
			UObject* Target = A;
			if (Call->Has(TEXT("component")))
			{
				const FString Name = Call->GetString(TEXT("component"));
				Target = nullptr;
				for (UActorComponent* C : A->GetComponents())
				{
					if (C && C->GetName() == Name) { Target = C; break; }
				}
				if (!Target)
				{
					Call->Fail(TEXT("pie.component_not_found"), FString::Printf(TEXT("actor has no component '%s'"), *Name));
					return;
				}
			}
			TSharedPtr<FJsonObject> O = PlayActorJson(A);
			TSharedPtr<FJsonObject> Values = MakeShared<FJsonObject>();
			for (const FString& Name : Call->GetStringArray(TEXT("properties")))
			{
				FProperty* P = Target->GetClass()->FindPropertyByName(FName(*Name));
				if (!P)
				{
					Call->Fail(TEXT("prop.not_found"), FString::Printf(TEXT("%s has no property '%s'"), *Target->GetClass()->GetName(), *Name));
					return;
				}
				FString Text;
				P->ExportTextItem_Direct(Text, P->ContainerPtrToValuePtr<void>(Target), nullptr, Target, PPF_None);
				Values->SetStringField(Name, Text);
			}
			O->SetObjectField(TEXT("properties"), Values);
			Call->Succeed(O);
		});

	R.Add(TEXT("pie_call"), TEXT("runtime"), TEXT("Call a function or custom event on a play-world actor with space-separated arguments, e.g. actor=player command=\"SetActorHiddenInGame true\" or command=\"TakeHit 25\"."),
		TEXT(R"({"type":"object","properties":{"world":{"type":"string","description":"server, client:1, ... (see pie_worlds); default: the primary play world"},"actor":{"type":"string"},"command":{"type":"string"}},"required":["actor","command"]})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			AActor* A = FindPlayActor(Call);
			if (!A) return;
			const FString Command = Call->GetString(TEXT("command"));
			FString FunctionName;
			Command.Split(TEXT(" "), &FunctionName, nullptr);
			if (FunctionName.IsEmpty()) FunctionName = Command;
			if (!A->FindFunction(FName(*FunctionName)))
			{
				Call->Fail(TEXT("pie.function_not_found"), FString::Printf(TEXT("%s has no function '%s'"), *A->GetClass()->GetName(), *FunctionName));
				return;
			}
			FOutputDeviceNull Ar;
			if (!A->CallFunctionByNameWithArguments(*Command, Ar, nullptr, true))
			{
				Call->Fail(TEXT("pie.call_failed"), FString::Printf(TEXT("could not call '%s' (check the argument count and types)"), *Command));
				return;
			}
			Call->Succeed(PlayActorJson(A));
		});

	R.Add(TEXT("pie_widgets"), TEXT("runtime"), TEXT("UI on screen in the play session: user widgets in the viewport with their child widgets (name, class, visible, text for text blocks)."),
		TEXT(R"({"type":"object","properties":{"world":{"type":"string","description":"server, client:1, ... (see pie_worlds); default: the primary play world"}}})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			UWorld* World = RequirePlayWorld(Call);
			if (!World) return;
			TArray<TSharedPtr<FJsonValue>> Items;
			for (TObjectIterator<UUserWidget> It; It; ++It)
			{
				UUserWidget* W = *It;
				if (W->GetWorld() != World || !W->IsInViewport() || !W->WidgetTree) continue;
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetStringField(TEXT("name"), W->GetName());
				O->SetStringField(TEXT("class"), W->GetClass()->GetName());
				TArray<TSharedPtr<FJsonValue>> Children;
				W->WidgetTree->ForEachWidget([&Children](UWidget* C)
				{
					TSharedPtr<FJsonObject> CO = MakeShared<FJsonObject>();
					CO->SetStringField(TEXT("name"), C->GetName());
					CO->SetStringField(TEXT("class"), C->GetClass()->GetName());
					CO->SetBoolField(TEXT("visible"), C->IsVisible());
					if (const UTextBlock* T = Cast<UTextBlock>(C)) CO->SetStringField(TEXT("text"), T->GetText().ToString());
					if (const UButton* B = Cast<UButton>(C)) CO->SetBoolField(TEXT("enabled"), B->GetIsEnabled());
					Children.Add(Obj(CO));
				});
				O->SetArrayField(TEXT("widgets"), Children);
				Items.Add(Obj(O));
			}
			Call->Succeed(Arr(Items));
		});

	R.Add(TEXT("pie_click"), TEXT("runtime"), TEXT("Click a button of the on-screen UI in the play session by widget name (fires its OnClicked; see pie_widgets)."),
		TEXT(R"({"type":"object","properties":{"world":{"type":"string","description":"server, client:1, ... (see pie_worlds); default: the primary play world"},"widget":{"type":"string"}},"required":["widget"]})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			UWorld* World = RequirePlayWorld(Call);
			if (!World) return;
			const FString Name = Call->GetString(TEXT("widget"));
			TArray<FString> Buttons;
			for (TObjectIterator<UUserWidget> It; It; ++It)
			{
				UUserWidget* W = *It;
				if (W->GetWorld() != World || !W->IsInViewport() || !W->WidgetTree) continue;
				UButton* Hit = nullptr;
				W->WidgetTree->ForEachWidget([&](UWidget* C)
				{
					if (UButton* B = Cast<UButton>(C))
					{
						if (!Hit && B->GetName() == Name) Hit = B;
						Buttons.AddUnique(B->GetName());
					}
				});
				if (Hit)
				{
					if (!Hit->GetIsEnabled() || !Hit->IsVisible())
					{
						Call->Fail(TEXT("pie.widget_disabled"), FString::Printf(TEXT("button '%s' is hidden or disabled"), *Name));
						return;
					}
					// OnClicked handlers run synchronously: return what they logged.
					const int64 LogStart = FBridgeLog::Get().NextSeq();
					Hit->OnClicked.Broadcast();
					TArray<FString> Lines;
					for (const FBridgeLogLine& L : FBridgeLog::Get().Read(LogStart, 50, ELogVerbosity::Log, FString())) Lines.Add(L.Text);
					TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
					O->SetStringField(TEXT("clicked"), Name);
					O->SetArrayField(TEXT("log"), StrArray(Lines));
					O->SetStringField(TEXT("in"), W->GetClass()->GetName());
					Call->Succeed(O);
					return;
				}
			}
			Call->Fail(TEXT("pie.widget_not_found"), FString::Printf(TEXT("no button '%s' on screen"), *Name), FString::Printf(TEXT("buttons: %s"), *FString::Join(Buttons, TEXT(", "))));
		});

	R.Add(TEXT("online_session"), TEXT("online"), TEXT("Session in a play world through the online subsystem (Null in PIE): create (sessionName, maxPlayers, lan), find (lists results with their index), join (index of the last find in that world; travel=true then moves the player to it), destroy. Waits up to timeoutSeconds (default 10) and fails with online.session_failed."),
		TEXT(R"({"type":"object","properties":{"action":{"type":"string","enum":["create","find","join","destroy"]},"world":{"type":"string","description":"standalone, standalone:2, server, client:1, ... - see pie_worlds"},"sessionName":{"type":"string"},"maxPlayers":{"type":"integer"},"lan":{"type":"boolean"},"index":{"type":"integer"},"travel":{"type":"boolean"},"timeoutSeconds":{"type":"number"}},"required":["action"]})"), EToolFlags::Mutating | EToolFlags::NoUndo,
		[](const FToolCallRef& Call)
		{
			UWorld* World = RequirePlayWorld(Call);
			if (!World) return;
			IOnlineSubsystem* Oss = Online::GetSubsystem(World);
			IOnlineSessionPtr Sessions = Oss ? Oss->GetSessionInterface() : nullptr;
			if (!Sessions.IsValid())
			{
				Call->Fail(TEXT("online.no_subsystem"), TEXT("the play world has no online session interface"), TEXT("configure one: unrealengine-cli online configure --subsystem null, then restart the editor"));
				return;
			}
			const FString Action = Call->GetString(TEXT("action"));
			const FName Name(Call->Has(TEXT("sessionName")) ? *Call->GetString(TEXT("sessionName")) : TEXT("GameSession"));
			const bool bLan = !Call->Has(TEXT("lan")) || Call->GetBool(TEXT("lan"));
			const double Limit = FMath::Max(0.5, Call->GetNumber(TEXT("timeoutSeconds"), 10.0));
			const FString WorldKey = World->GetPathName();
			static TMap<FString, TSharedPtr<FOnlineSessionSearch>> LastSearch;

			struct FState { bool bDone = false; bool bOk = false; FString Detail; FDelegateHandle Handle; };
			TSharedRef<FState> S = MakeShared<FState>();
			TSharedPtr<FOnlineSessionSearch> Search;
			bool bStarted = false;
			if (Action == TEXT("create"))
			{
				FOnlineSessionSettings Settings;
				Settings.NumPublicConnections = FMath::Max(1, (int32)Call->GetNumber(TEXT("maxPlayers"), 4));
				Settings.bIsLANMatch = bLan;
				Settings.bShouldAdvertise = true;
				Settings.bAllowJoinInProgress = true;
				Settings.bUsesPresence = !bLan;
				Settings.bUseLobbiesIfAvailable = !bLan;
				S->Handle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(FOnCreateSessionCompleteDelegate::CreateLambda([S](FName, bool bOk) { S->bDone = true; S->bOk = bOk; }));
				bStarted = Sessions->CreateSession(0, Name, Settings);
				if (!bStarted) Sessions->ClearOnCreateSessionCompleteDelegate_Handle(S->Handle);
			}
			else if (Action == TEXT("find"))
			{
				Search = MakeShared<FOnlineSessionSearch>();
				Search->bIsLanQuery = bLan;
				Search->MaxSearchResults = 50;
				S->Handle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(FOnFindSessionsCompleteDelegate::CreateLambda([S](bool bOk) { S->bDone = true; S->bOk = bOk; }));
				bStarted = Sessions->FindSessions(0, Search.ToSharedRef());
				if (!bStarted) Sessions->ClearOnFindSessionsCompleteDelegate_Handle(S->Handle);
			}
			else if (Action == TEXT("join"))
			{
				const TSharedPtr<FOnlineSessionSearch>* Found = LastSearch.Find(WorldKey);
				const int32 Index = (int32)Call->GetNumber(TEXT("index"), 0);
				if (!Found || !(*Found).IsValid() || !(*Found)->SearchResults.IsValidIndex(Index))
				{
					Call->Fail(TEXT("online.no_result"), FString::Printf(TEXT("no find result %d in this world"), Index), TEXT("run online session find in the same world first"));
					return;
				}
				S->Handle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(FOnJoinSessionCompleteDelegate::CreateLambda([S](FName, EOnJoinSessionCompleteResult::Type Result)
				{
					S->bDone = true;
					S->bOk = Result == EOnJoinSessionCompleteResult::Success;
					S->Detail = LexToString(Result);
				}));
				bStarted = Sessions->JoinSession(0, Name, (*Found)->SearchResults[Index]);
				if (!bStarted) Sessions->ClearOnJoinSessionCompleteDelegate_Handle(S->Handle);
			}
			else if (Action == TEXT("destroy"))
			{
				bStarted = Sessions->DestroySession(Name, FOnDestroySessionCompleteDelegate::CreateLambda([S](FName, bool bOk) { S->bDone = true; S->bOk = bOk; }));
			}
			else
			{
				Call->Fail(TEXT("tool.invalid_args"), FString::Printf(TEXT("unknown action '%s'"), *Action), TEXT("use create, find, join or destroy"));
				return;
			}
			if (!bStarted)
			{
				Call->Fail(TEXT("online.session_failed"), FString::Printf(TEXT("%s of session '%s' was refused by the %s subsystem"), *Action, *Name.ToString(), *Oss->GetSubsystemName().ToString()),
					Action == TEXT("create") ? TEXT("does a session with this name already exist? destroy it first") : TEXT("check the log: unrealengine-cli logs --severity warning"));
				return;
			}
			const bool bTravel = Call->GetBool(TEXT("travel"));
			const double Start = FPlatformTime::Seconds();
			TWeakObjectPtr<UWorld> WeakWorld(World);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Call, S, Sessions, Search, Action, Name, Limit, Start, WorldKey, bTravel, WeakWorld, Oss](float)
			{
				const double Waited = FPlatformTime::Seconds() - Start;
				if (!S->bDone && Waited < Limit && !Call->bCancelled) return true;
				if (Action == TEXT("create")) Sessions->ClearOnCreateSessionCompleteDelegate_Handle(S->Handle);
				else if (Action == TEXT("find")) Sessions->ClearOnFindSessionsCompleteDelegate_Handle(S->Handle);
				else if (Action == TEXT("join")) Sessions->ClearOnJoinSessionCompleteDelegate_Handle(S->Handle);
				if (!S->bDone || !S->bOk)
				{
					Call->Fail(TEXT("online.session_failed"), S->bDone
						? FString::Printf(TEXT("%s of session '%s' failed%s"), *Action, *Name.ToString(), S->Detail.IsEmpty() ? TEXT("") : *(TEXT(": ") + S->Detail))
						: FString::Printf(TEXT("%s of session '%s' did not finish within %.1f s"), *Action, *Name.ToString(), Limit),
						TEXT("check the log: unrealengine-cli logs --severity warning"));
					return false;
				}
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetStringField(TEXT("action"), Action);
				O->SetStringField(TEXT("sessionName"), Name.ToString());
				O->SetStringField(TEXT("subsystem"), Oss->GetSubsystemName().ToString());
				O->SetNumberField(TEXT("seconds"), Waited);
				if (Action == TEXT("find"))
				{
					LastSearch.Add(WorldKey, Search);
					TArray<TSharedPtr<FJsonValue>> Results;
					for (int32 i = 0; i < Search->SearchResults.Num(); ++i)
					{
						const FOnlineSessionSearchResult& Res = Search->SearchResults[i];
						TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
						E->SetNumberField(TEXT("index"), i);
						E->SetStringField(TEXT("id"), Res.GetSessionIdStr());
						E->SetStringField(TEXT("owner"), Res.Session.OwningUserName);
						E->SetNumberField(TEXT("pingMs"), Res.PingInMs);
						E->SetNumberField(TEXT("maxPlayers"), Res.Session.SessionSettings.NumPublicConnections);
						E->SetNumberField(TEXT("openSlots"), Res.Session.NumOpenPublicConnections);
						Results.Add(Obj(E));
					}
					O->SetArrayField(TEXT("results"), Results);
				}
				else if (Action != TEXT("destroy"))
				{
					FString Connect;
					if (Sessions->GetResolvedConnectString(Name, Connect)) O->SetStringField(TEXT("connectString"), Connect);
					if (const FNamedOnlineSession* Named = Sessions->GetNamedSession(Name))
					{
						O->SetStringField(TEXT("id"), Named->GetSessionIdStr());
						O->SetNumberField(TEXT("maxPlayers"), Named->SessionSettings.NumPublicConnections);
						O->SetBoolField(TEXT("lan"), Named->SessionSettings.bIsLANMatch);
					}
					if (Action == TEXT("join") && bTravel && !Connect.IsEmpty())
					{
						APlayerController* PC = WeakWorld.IsValid() ? WeakWorld->GetFirstPlayerController() : nullptr;
						if (PC) PC->ClientTravel(Connect, TRAVEL_Absolute);
						O->SetBoolField(TEXT("travelling"), PC != nullptr);
					}
				}
				Call->Succeed(O);
				return false;
			}), 0.02f);
		});

	R.Add(TEXT("pie_exec"), TEXT("runtime"), TEXT("Run a console command in the play world (e.g. \"slomo 0.5\", \"show collision\", \"ce MyEvent\")."),
		TEXT(R"({"type":"object","properties":{"world":{"type":"string","description":"server, client:1, ... (see pie_worlds); default: the primary play world"},"command":{"type":"string"}},"required":["command"]})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			APlayerController* PC = RequirePlayer(Call);
			if (!PC) return;
			const FString Result = PC->ConsoleCommand(Call->GetString(TEXT("command")), true);
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("output"), Result);
			Call->Succeed(O);
		});
}
