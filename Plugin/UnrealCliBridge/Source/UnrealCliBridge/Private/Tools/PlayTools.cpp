// Tools that drive a running play session: start it without blocking, send input, wait, look at the
// game viewport and query or call into actors of the play world.

#include "Containers/Ticker.h"
#include "Editor.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
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
#include "Server/BridgeLog.h"
#include "Server/JsonUtil.h"
#include "Server/ToolRegistry.h"
#include "Tools/BlueprintHelpers.h"
#include "Tools/ToolRegistration.h"
#include "UnrealClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"

using namespace UCli;

namespace
{
	UWorld* RequirePlayWorld(const FToolCallRef& Call)
	{
		if (!GEditor->PlayWorld)
		{
			Call->Fail(TEXT("pie.not_running"), TEXT("no play session is running"), TEXT("start one: pie_start"));
		}
		return GEditor->PlayWorld;
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
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetActorLabel() == Id || It->GetName() == Id) return *It;
		}
		Call->Fail(TEXT("pie.actor_not_found"), FString::Printf(TEXT("no actor '%s' in the play world"), *Id), TEXT("list them: pie_actors (\"player\" is the player's pawn)"));
		return nullptr;
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

void UCli::RegisterPlayTools(FToolRegistry& R)
{
	R.Add(TEXT("pie_start"), TEXT("runtime"), TEXT("Start Play In Editor and return once the play world exists; drive it with pie_input / pie_wait / pie_screenshot, end it with pie_stop."),
		TEXT(R"({"type":"object","properties":{}})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			if (GEditor->PlayWorld || GEditor->IsPlaySessionRequestQueued())
			{
				Call->Fail(TEXT("pie.already_running"), TEXT("a play session is already running"), TEXT("stop it: pie_stop"));
				return;
			}
			FRequestPlaySessionParams Params;
			Params.WorldType = EPlaySessionWorldType::PlayInEditor;
			GEditor->RequestPlaySession(Params);
			const double Deadline = FPlatformTime::Seconds() + 60.0;
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Call, Deadline](float)
			{
				UWorld* World = GEditor->PlayWorld;
				if (World && World->GetFirstPlayerController())
				{
					TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
					O->SetStringField(TEXT("world"), World->GetPathName());
					APawn* Pawn = World->GetFirstPlayerController()->GetPawn();
					O->SetStringField(TEXT("pawn"), Pawn ? Pawn->GetName() : FString());
					Call->Succeed(O);
					return false;
				}
				if (FPlatformTime::Seconds() > Deadline)
				{
					Call->Fail(TEXT("pie.start_failed"), TEXT("the play session did not start within 60 s"), TEXT("check the log: unrealengine-cli logs --severity warning"));
					return false;
				}
				return true;
			}), 0.05f);
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
		TEXT(R"({"type":"object","properties":{"key":{"type":"string"},"action":{"type":"string"},"value":{},"seconds":{"type":"number"}}})"), EToolFlags::None,
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
		TEXT(R"({"type":"object","properties":{"seconds":{"type":"number"},"untilLog":{"type":"string"}}})"), EToolFlags::None,
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
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"}}})"), EToolFlags::None,
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
		TEXT(R"({"type":"object","properties":{"classFilter":{"type":"string"},"nameFilter":{"type":"string"},"limit":{"type":"integer"}}})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			UWorld* World = RequirePlayWorld(Call);
			if (!World) return;
			const FString ClassFilter = Call->GetString(TEXT("classFilter"));
			const FString NameFilter = Call->GetString(TEXT("nameFilter"));
			const int32 Limit = static_cast<int32>(Call->GetNumber(TEXT("limit"), 200));
			APlayerController* PC = World->GetFirstPlayerController();
			APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;
			TArray<TSharedPtr<FJsonValue>> Items;
			for (TActorIterator<AActor> It(World); It && Items.Num() < Limit; ++It)
			{
				if (!ClassFilter.IsEmpty() && !It->GetClass()->GetName().Contains(ClassFilter)) continue;
				if (!NameFilter.IsEmpty() && !It->GetActorLabel().Contains(NameFilter) && !It->GetName().Contains(NameFilter)) continue;
				TSharedPtr<FJsonObject> O = PlayActorJson(*It);
				if (*It == PlayerPawn) O->SetBoolField(TEXT("player"), true);
				Items.Add(Obj(O));
			}
			Call->Succeed(Arr(Items));
		});

	R.Add(TEXT("pie_get"), TEXT("runtime"), TEXT("Read properties of a play-world actor (or one of its components): actor is a label, name or \"player\"; without properties returns its transform and velocity."),
		TEXT(R"({"type":"object","properties":{"actor":{"type":"string"},"component":{"type":"string"},"properties":{"type":"array","items":{"type":"string"}}},"required":["actor"]})"), EToolFlags::None,
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
		TEXT(R"({"type":"object","properties":{"actor":{"type":"string"},"command":{"type":"string"}},"required":["actor","command"]})"), EToolFlags::None,
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

	R.Add(TEXT("pie_exec"), TEXT("runtime"), TEXT("Run a console command in the play world (e.g. \"slomo 0.5\", \"show collision\", \"ce MyEvent\")."),
		TEXT(R"({"type":"object","properties":{"command":{"type":"string"}},"required":["command"]})"), EToolFlags::None,
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
