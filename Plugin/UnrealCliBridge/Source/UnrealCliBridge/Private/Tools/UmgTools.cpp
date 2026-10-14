#include "Animation/MovieScene2DTransformSection.h"
#include "Animation/MovieScene2DTransformTrack.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "MovieScene.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Server/JsonUtil.h"
#include "Server/ToolRegistry.h"
#include "Tools/AssetCreation.h"
#include "Tools/BlueprintHelpers.h"
#include "Tools/ToolRegistration.h"
#include "WidgetBlueprint.h"

using namespace UCli;

namespace
{
	UWidgetBlueprint* RequireWBP(const FToolCallRef& Call)
	{
		const FString Path = Call->GetString(TEXT("path"));
		UWidgetBlueprint* W = LoadObject<UWidgetBlueprint>(nullptr, *BP::ObjectPath(Path), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!W)
		{
			Call->Fail(TEXT("umg.not_found"), FString::Printf(TEXT("no Widget Blueprint at '%s'"), *Path), TEXT("create one: widget_create"));
		}
		return W;
	}

	UClass* WidgetClass(const FString& Name)
	{
		UClass* C = BP::ResolveClass(Name);
		if (!C)
		{
			C = LoadObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/UMG.%s"), *Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
		return C && C->IsChildOf(UWidget::StaticClass()) ? C : nullptr;
	}

	TSharedPtr<FJsonObject> WidgetJson(UWidget* W)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), W->GetName());
		O->SetStringField(TEXT("class"), W->GetClass()->GetName());
		O->SetStringField(TEXT("parent"), W->GetParent() ? W->GetParent()->GetName() : FString());
		O->SetBoolField(TEXT("isVariable"), W->bIsVariable);
		if (W->Slot)
		{
			O->SetStringField(TEXT("slot"), W->Slot->GetClass()->GetName());
		}
		return O;
	}

	bool SetReflected(UObject* Target, const FString& Name, const FString& Value, FString& Error)
	{
		FProperty* P = Target->GetClass()->FindPropertyByName(*Name);
		if (!P)
		{
			Error = FString::Printf(TEXT("%s has no property '%s'"), *Target->GetClass()->GetName(), *Name);
			return false;
		}
		Target->Modify();
		void* Ptr = P->ContainerPtrToValuePtr<void>(Target);
		if (FTextProperty* TP = CastField<FTextProperty>(P))
		{
			TP->SetPropertyValue(Ptr, FText::FromString(Value));
		}
		else if (!P->ImportText_Direct(*Value, Ptr, Target, PPF_None))
		{
			Error = FString::Printf(TEXT("'%s' is not a valid %s value"), *Value, *P->GetCPPType());
			return false;
		}
		FPropertyChangedEvent Ev(P);
		Target->PostEditChangeProperty(Ev);
		return true;
	}

	bool AnchorPreset(const FString& Name, FAnchors& Out)
	{
		static const TMap<FString, FVector4> Presets = {
			{TEXT("TopLeft"), FVector4(0, 0, 0, 0)}, {TEXT("TopCenter"), FVector4(.5, 0, .5, 0)}, {TEXT("TopRight"), FVector4(1, 0, 1, 0)},
			{TEXT("CenterLeft"), FVector4(0, .5, 0, .5)}, {TEXT("Center"), FVector4(.5, .5, .5, .5)}, {TEXT("CenterRight"), FVector4(1, .5, 1, .5)},
			{TEXT("BottomLeft"), FVector4(0, 1, 0, 1)}, {TEXT("BottomCenter"), FVector4(.5, 1, .5, 1)}, {TEXT("BottomRight"), FVector4(1, 1, 1, 1)},
			{TEXT("TopFill"), FVector4(0, 0, 1, 0)}, {TEXT("CenterFill"), FVector4(0, .5, 1, .5)}, {TEXT("BottomFill"), FVector4(0, 1, 1, 1)},
			{TEXT("LeftFill"), FVector4(0, 0, 0, 1)}, {TEXT("RightFill"), FVector4(1, 0, 1, 1)}, {TEXT("Fill"), FVector4(0, 0, 1, 1)}};
		if (const FVector4* V = Presets.Find(Name))
		{
			Out = FAnchors(V->X, V->Y, V->Z, V->W);
			return true;
		}
		return false;
	}

	bool Vec2(const FToolCallRef& Call, const TCHAR* Key, FVector2D& Out)
	{
		const TArray<TSharedPtr<FJsonValue>> A = Call->GetArray(Key);
		if (A.Num() != 2) return false;
		Out = FVector2D(A[0]->AsNumber(), A[1]->AsNumber());
		return true;
	}

	FString SlotSummary(UPanelSlot* Slot)
	{
		FString Out;
		for (const TCHAR* Name : {TEXT("LayoutData"), TEXT("bAutoSize"), TEXT("ZOrder"), TEXT("Padding"), TEXT("HorizontalAlignment"), TEXT("VerticalAlignment"), TEXT("Size")})
		{
			if (FProperty* P = Slot->GetClass()->FindPropertyByName(Name))
			{
				FString Text;
				P->ExportTextItem_Direct(Text, P->ContainerPtrToValuePtr<void>(Slot), nullptr, Slot, PPF_None);
				Out += FString::Printf(TEXT("%s%s=%s"), Out.IsEmpty() ? TEXT("") : TEXT(" "), Name, *Text);
			}
		}
		return Out;
	}

	void AddKeys(FMovieSceneFloatChannel& Channel, const TArray<TPair<double, float>>& Keys, FFrameRate Tick)
	{
		for (const TPair<double, float>& K : Keys)
		{
			Channel.AddCubicKey((K.Key * Tick).RoundToFrame(), K.Value);
		}
	}
}

void UCli::RegisterUmgTools(FToolRegistry& R)
{
	R.Add(TEXT("widget_create"), TEXT("umg"), TEXT("Create a Widget Blueprint (UserWidget) with an optional root panel class (default CanvasPanel)."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"root":{"type":"string"}},"required":["path"]})"),
		EToolFlags::Mutating | EToolFlags::NoUndo,
		[](const FToolCallRef& Call)
		{
			FString Error;
			UWidgetBlueprint* W = Cast<UWidgetBlueprint>(CreateAssetOfClass(Call->GetString(TEXT("path")), UWidgetBlueprint::StaticClass(), Error));
			if (!W)
			{
				Call->Fail(Error.Contains(TEXT("exists")) ? TEXT("asset.exists") : TEXT("asset.create_failed"), Error);
				return;
			}
			if (!W->WidgetTree->RootWidget)
			{
				UClass* RootClass = WidgetClass(Call->GetString(TEXT("root"), TEXT("CanvasPanel")));
				if (RootClass)
				{
					W->WidgetTree->RootWidget = W->WidgetTree->ConstructWidget<UWidget>(RootClass, *RootClass->GetName());
					W->OnVariableAdded(W->WidgetTree->RootWidget->GetFName());
				}
			}
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(W);
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("path"), W->GetPathName());
			O->SetStringField(TEXT("root"), W->WidgetTree->RootWidget ? W->WidgetTree->RootWidget->GetName() : FString());
			Call->Succeed(O);
		});

	R.Add(TEXT("widget_tree_get"), TEXT("umg"), TEXT("Widgets of a Widget Blueprint with parent, slot and variable flags."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			UWidgetBlueprint* W = RequireWBP(Call);
			if (!W) return;
			TArray<TSharedPtr<FJsonValue>> Items;
			W->WidgetTree->ForEachWidget([&Items](UWidget* X) { Items.Add(Obj(WidgetJson(X))); });
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("root"), W->WidgetTree->RootWidget ? W->WidgetTree->RootWidget->GetName() : FString());
			O->SetArrayField(TEXT("widgets"), Items);
			Call->Succeed(O);
		});

	R.Add(TEXT("widget_add"), TEXT("umg"), TEXT("Add a widget (TextBlock, Button, Image, VerticalBox, ...) under a panel (default: root)."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"class":{"type":"string"},"name":{"type":"string"},"parent":{"type":"string"},"isVariable":{"type":"boolean"}},"required":["path","class","name"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UWidgetBlueprint* W = RequireWBP(Call);
			if (!W) return;
			UClass* C = WidgetClass(Call->GetString(TEXT("class")));
			if (!C)
			{
				Call->Fail(TEXT("umg.bad_class"), FString::Printf(TEXT("'%s' is not a widget class"), *Call->GetString(TEXT("class"))));
				return;
			}
			const FName Name(*Call->GetString(TEXT("name")));
			if (W->WidgetTree->FindWidget(Name))
			{
				Call->Fail(TEXT("umg.name_taken"), FString::Printf(TEXT("a widget named '%s' exists"), *Name.ToString()));
				return;
			}
			W->Modify();
			W->WidgetTree->Modify();
			UWidget* Parent = Call->Has(TEXT("parent")) ? W->WidgetTree->FindWidget(FName(*Call->GetString(TEXT("parent")))) : W->WidgetTree->RootWidget.Get();
			UWidget* New = W->WidgetTree->ConstructWidget<UWidget>(C, Name);
			New->bIsVariable = Call->GetBool(TEXT("isVariable"), true);
			if (!W->WidgetTree->RootWidget)
			{
				W->WidgetTree->RootWidget = New;
			}
			else if (UPanelWidget* Panel = Cast<UPanelWidget>(Parent))
			{
				if (!Panel->CanAddMoreChildren())
				{
					Call->Fail(TEXT("umg.parent_full"), FString::Printf(TEXT("'%s' cannot take more children"), *Panel->GetName()));
					return;
				}
				Panel->AddChild(New);
			}
			else
			{
				Call->Fail(TEXT("umg.bad_parent"), TEXT("parent is missing or is not a panel widget"));
				return;
			}
			// Registers the widget's variable GUID like the designer does (the widget compiler expects one).
			W->OnVariableAdded(New->GetFName());
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(W);
			Call->Succeed(WidgetJson(New));
		});

	R.Add(TEXT("widget_set_prop"), TEXT("umg"), TEXT("Set a widget property by reflection (UE text form); prefix 'Slot.' targets the slot (e.g. Slot.Padding)."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"widget":{"type":"string"},"property":{"type":"string"},"value":{"type":"string"}},"required":["path","widget","property","value"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UWidgetBlueprint* W = RequireWBP(Call);
			if (!W) return;
			UWidget* X = W->WidgetTree->FindWidget(FName(*Call->GetString(TEXT("widget"))));
			if (!X)
			{
				Call->Fail(TEXT("umg.widget_not_found"), FString::Printf(TEXT("no widget '%s'"), *Call->GetString(TEXT("widget"))), TEXT("list: widget_tree_get"));
				return;
			}
			FString Prop = Call->GetString(TEXT("property"));
			UObject* Target = X;
			if (Prop.StartsWith(TEXT("Slot.")))
			{
				Target = X->Slot;
				Prop.RightChopInline(5);
				if (!Target)
				{
					Call->Fail(TEXT("umg.no_slot"), TEXT("the widget has no slot (is it the root?)"));
					return;
				}
			}
			FString Error;
			if (!SetReflected(Target, Prop, Call->GetString(TEXT("value")), Error))
			{
				Call->Fail(TEXT("prop.bad_value"), Error);
				return;
			}
			FBlueprintEditorUtils::MarkBlueprintAsModified(W);
			Call->Succeed(WidgetJson(X));
		});

	R.Add(TEXT("widget_layout"), TEXT("umg"), TEXT("Lay out a widget in its parent. Canvas children: anchor (TopLeft, TopCenter, TopRight, CenterLeft, Center, CenterRight, BottomLeft, BottomCenter, BottomRight, TopFill, BottomFill, LeftFill, RightFill, Fill or [minX,minY,maxX,maxY]), position [x,y], size [w,h], alignment [x,y] (0..1 pivot), autoSize, zOrder. Box/overlay children: padding (number or [l,t,r,b]), hAlign (Left|Center|Right|Fill), vAlign (Top|Center|Bottom|Fill), fill (share of the remaining space)."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"widget":{"type":"string"},"anchor":{},"position":{"type":"array"},"size":{"type":"array"},"alignment":{"type":"array"},"autoSize":{"type":"boolean"},"zOrder":{"type":"integer"},"padding":{},"hAlign":{"type":"string","enum":["Left","Center","Right","Fill"]},"vAlign":{"type":"string","enum":["Top","Center","Bottom","Fill"]},"fill":{"type":"number"}},"required":["path","widget"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UWidgetBlueprint* W = RequireWBP(Call);
			if (!W) return;
			UWidget* X = W->WidgetTree->FindWidget(FName(*Call->GetString(TEXT("widget"))));
			if (!X || !X->Slot)
			{
				Call->Fail(X ? TEXT("umg.no_slot") : TEXT("umg.widget_not_found"), X ? TEXT("the root widget has no slot to lay out") : FString::Printf(TEXT("no widget '%s'"), *Call->GetString(TEXT("widget"))), TEXT("list: widget_tree_get"));
				return;
			}
			UPanelSlot* Slot = X->Slot;
			Slot->Modify();
			FString Error;
			if (UCanvasPanelSlot* Canvas = Cast<UCanvasPanelSlot>(Slot))
			{
				const TSharedPtr<FJsonValue> AnchorV = Call->Args->TryGetField(TEXT("anchor"));
				if (AnchorV.IsValid())
				{
					FAnchors A;
					const TArray<TSharedPtr<FJsonValue>>* Arr4 = nullptr;
					if (AnchorV->TryGetArray(Arr4) && Arr4->Num() == 4)
						A = FAnchors((*Arr4)[0]->AsNumber(), (*Arr4)[1]->AsNumber(), (*Arr4)[2]->AsNumber(), (*Arr4)[3]->AsNumber());
					else if (!AnchorPreset(AnchorV->AsString(), A))
					{
						Call->Fail(TEXT("args.invalid"), FString::Printf(TEXT("unknown anchor '%s'"), *AnchorV->AsString()), TEXT("presets: TopLeft, TopCenter, TopRight, CenterLeft, Center, CenterRight, BottomLeft, BottomCenter, BottomRight, TopFill, BottomFill, LeftFill, RightFill, Fill"));
						return;
					}
					Canvas->SetAnchors(A);
				}
				FVector2D V;
				if (Vec2(Call, TEXT("alignment"), V)) Canvas->SetAlignment(V);
				if (Vec2(Call, TEXT("position"), V)) Canvas->SetPosition(V);
				if (Vec2(Call, TEXT("size"), V)) Canvas->SetSize(V);
				if (Call->Has(TEXT("autoSize"))) Canvas->SetAutoSize(Call->GetBool(TEXT("autoSize")));
				if (Call->Has(TEXT("zOrder"))) Canvas->SetZOrder(static_cast<int32>(Call->GetNumber(TEXT("zOrder"))));
			}
			else
			{
				for (const TCHAR* CanvasOnly : {TEXT("anchor"), TEXT("position"), TEXT("size"), TEXT("alignment"), TEXT("autoSize"), TEXT("zOrder")})
				{
					if (Call->Has(CanvasOnly))
					{
						Call->Fail(TEXT("args.invalid"), FString::Printf(TEXT("'%s' applies to Canvas Panel children; this widget is in a %s"), CanvasOnly, *Slot->GetClass()->GetName()), TEXT("use padding, hAlign, vAlign, fill"));
						return;
					}
				}
			}
			if (Call->Has(TEXT("padding")))
			{
				const TSharedPtr<FJsonValue> P = Call->Args->TryGetField(TEXT("padding"));
				const TArray<TSharedPtr<FJsonValue>>* Arr4 = nullptr;
				const FString Text = P->TryGetArray(Arr4) && Arr4->Num() == 4
					? FString::Printf(TEXT("(Left=%g,Top=%g,Right=%g,Bottom=%g)"), (*Arr4)[0]->AsNumber(), (*Arr4)[1]->AsNumber(), (*Arr4)[2]->AsNumber(), (*Arr4)[3]->AsNumber())
					: FString::Printf(TEXT("(Left=%g,Top=%g,Right=%g,Bottom=%g)"), P->AsNumber(), P->AsNumber(), P->AsNumber(), P->AsNumber());
				if (!SetReflected(Slot, TEXT("Padding"), Text, Error)) { Call->Fail(TEXT("args.invalid"), Error); return; }
			}
			if (Call->Has(TEXT("hAlign")) && !SetReflected(Slot, TEXT("HorizontalAlignment"), TEXT("HAlign_") + Call->GetString(TEXT("hAlign")), Error)) { Call->Fail(TEXT("args.invalid"), Error); return; }
			if (Call->Has(TEXT("vAlign")) && !SetReflected(Slot, TEXT("VerticalAlignment"), TEXT("VAlign_") + Call->GetString(TEXT("vAlign")), Error)) { Call->Fail(TEXT("args.invalid"), Error); return; }
			if (Call->Has(TEXT("fill")) && !SetReflected(Slot, TEXT("Size"), FString::Printf(TEXT("(SizeRule=Fill,Value=%g)"), Call->GetNumber(TEXT("fill"))), Error)) { Call->Fail(TEXT("args.invalid"), Error); return; }
			FBlueprintEditorUtils::MarkBlueprintAsModified(W);
			TSharedPtr<FJsonObject> O = WidgetJson(X);
			O->SetStringField(TEXT("layout"), SlotSummary(Slot));
			Call->Succeed(O);
		});

	R.Add(TEXT("widget_animation_add"), TEXT("umg"), TEXT("Add (or extend) a widget animation: track opacity | translation | scale | angle on a widget with keys [[seconds, value]] (translation/scale values are [x,y]). After bp_compile the animation is a variable; play it with PlayAnimation."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"name":{"type":"string"},"widget":{"type":"string"},"track":{"type":"string","enum":["opacity","translation","scale","angle"]},"keys":{"type":"array"}},"required":["path","name","widget","track","keys"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UWidgetBlueprint* W = RequireWBP(Call);
			if (!W) return;
			UWidget* X = W->WidgetTree->FindWidget(FName(*Call->GetString(TEXT("widget"))));
			if (!X)
			{
				Call->Fail(TEXT("umg.widget_not_found"), FString::Printf(TEXT("no widget '%s'"), *Call->GetString(TEXT("widget"))), TEXT("list: widget_tree_get"));
				return;
			}
			const FString Track = Call->GetString(TEXT("track"));
			const bool bVector = Track == TEXT("translation") || Track == TEXT("scale");
			TArray<TPair<double, float>> KeysA, KeysB;
			double End = 0.0;
			for (const TSharedPtr<FJsonValue>& K : Call->GetArray(TEXT("keys")))
			{
				const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* XY = nullptr;
				if (!K->TryGetArray(Pair) || Pair->Num() != 2 || (bVector && (!(*Pair)[1]->TryGetArray(XY) || XY->Num() != 2)))
				{
					Call->Fail(TEXT("args.invalid"), bVector ? TEXT("keys are [[seconds, [x, y]], ...]") : TEXT("keys are [[seconds, value], ...]"));
					return;
				}
				const double T = (*Pair)[0]->AsNumber();
				End = FMath::Max(End, T);
				KeysA.Emplace(T, static_cast<float>(bVector ? (*XY)[0]->AsNumber() : (*Pair)[1]->AsNumber()));
				if (bVector) KeysB.Emplace(T, static_cast<float>((*XY)[1]->AsNumber()));
			}
			W->Modify();
			const FName AnimName(*Call->GetString(TEXT("name")));
			UWidgetAnimation* Anim = nullptr;
			for (UWidgetAnimation* A : W->Animations)
			{
				if (A && A->GetFName() == AnimName) Anim = A;
			}
			if (!Anim)
			{
				Anim = NewObject<UWidgetAnimation>(W, AnimName, RF_Transactional);
				Anim->MovieScene = NewObject<UMovieScene>(Anim, AnimName, RF_Transactional);
				Anim->MovieScene->SetDisplayRate(FFrameRate(20, 1));
				W->Animations.Add(Anim);
				W->OnVariableAdded(Anim->GetFName());
			}
			UMovieScene* Scene = Anim->MovieScene;
			Scene->Modify();
			FGuid Guid;
			for (const FWidgetAnimationBinding& B : Anim->AnimationBindings)
			{
				if (B.WidgetName == X->GetFName()) Guid = B.AnimationGuid;
			}
			if (!Guid.IsValid())
			{
				Guid = Scene->AddPossessable(X->GetName(), X->GetClass());
				FWidgetAnimationBinding Binding;
				Binding.WidgetName = X->GetFName();
				Binding.AnimationGuid = Guid;
				Binding.bIsRootWidget = X == W->WidgetTree->RootWidget;
				Anim->AnimationBindings.Add(Binding);
			}
			const FFrameRate Tick = Scene->GetTickResolution();
			if (Track == TEXT("opacity"))
			{
				UMovieSceneFloatTrack* T = Scene->AddTrack<UMovieSceneFloatTrack>(Guid);
				T->SetPropertyNameAndPath(TEXT("RenderOpacity"), TEXT("RenderOpacity"));
				UMovieSceneFloatSection* S = CastChecked<UMovieSceneFloatSection>(T->CreateNewSection());
				S->SetRange(TRange<FFrameNumber>::All());
				T->AddSection(*S);
				AddKeys(S->GetChannel(), KeysA, Tick);
			}
			else
			{
				UMovieScene2DTransformTrack* T = Scene->FindTrack<UMovieScene2DTransformTrack>(Guid);
				UMovieScene2DTransformSection* S = nullptr;
				if (!T)
				{
					T = Scene->AddTrack<UMovieScene2DTransformTrack>(Guid);
					T->SetPropertyNameAndPath(TEXT("RenderTransform"), TEXT("RenderTransform"));
				}
				S = T->GetAllSections().Num() > 0 ? Cast<UMovieScene2DTransformSection>(T->GetAllSections()[0]) : nullptr;
				if (!S)
				{
					S = CastChecked<UMovieScene2DTransformSection>(T->CreateNewSection());
					S->SetRange(TRange<FFrameNumber>::All());
					S->Scale[0].SetDefault(1.f);
					S->Scale[1].SetDefault(1.f);
					T->AddSection(*S);
				}
				S->Modify();
				if (Track == TEXT("translation")) { AddKeys(S->Translation[0], KeysA, Tick); AddKeys(S->Translation[1], KeysB, Tick); }
				else if (Track == TEXT("scale")) { AddKeys(S->Scale[0], KeysA, Tick); AddKeys(S->Scale[1], KeysB, Tick); }
				else AddKeys(S->Rotation, KeysA, Tick);
			}
			const FFrameNumber EndFrame = (End * Tick).CeilToFrame();
			const TRange<FFrameNumber> Current = Scene->GetPlaybackRange();
			const int32 Duration = FMath::Max(EndFrame.Value, Current.HasUpperBound() ? Current.GetUpperBoundValue().Value : 0);
			Scene->SetPlaybackRange(0, FMath::Max(Duration, 1));
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(W);
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("animation"), AnimName.ToString());
			O->SetStringField(TEXT("widget"), X->GetName());
			O->SetStringField(TEXT("track"), Track);
			O->SetNumberField(TEXT("seconds"), static_cast<double>(Duration) / Tick.AsDecimal());
			Call->Succeed(O);
		});

	R.Add(TEXT("widget_bind"), TEXT("umg"), TEXT("Create a bound event node for a widget delegate (e.g. Button OnClicked) in the event graph."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"widget":{"type":"string"},"event":{"type":"string"}},"required":["path","widget","event"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UWidgetBlueprint* W = RequireWBP(Call);
			if (!W) return;
			const FName WidgetName(*Call->GetString(TEXT("widget")));
			UWidget* X = W->WidgetTree->FindWidget(WidgetName);
			if (!X)
			{
				Call->Fail(TEXT("umg.widget_not_found"), FString::Printf(TEXT("no widget '%s'"), *WidgetName.ToString()));
				return;
			}
			const FName Event(*Call->GetString(TEXT("event")));
			if (!FindFProperty<FMulticastDelegateProperty>(X->GetClass(), Event))
			{
				Call->Fail(TEXT("umg.event_not_found"), FString::Printf(TEXT("%s has no event '%s'"), *X->GetClass()->GetName(), *Event.ToString()));
				return;
			}
			W->Modify();
			X->bIsVariable = true;
			FKismetEditorUtilities::CompileBlueprint(W);
			FObjectProperty* Prop = FindFProperty<FObjectProperty>(W->SkeletonGeneratedClass, WidgetName);
			if (!Prop)
			{
				Call->Fail(TEXT("umg.bind_failed"), TEXT("the widget variable was not generated"));
				return;
			}
			FKismetEditorUtilities::CreateNewBoundEventForClass(X->GetClass(), Event, W, Prop);
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("widget"), WidgetName.ToString());
			O->SetStringField(TEXT("event"), Event.ToString());
			Call->Succeed(O);
		});
}
