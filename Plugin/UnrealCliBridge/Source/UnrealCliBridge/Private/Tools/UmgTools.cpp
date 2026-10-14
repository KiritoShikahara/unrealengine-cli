#include "Blueprint/WidgetTree.h"
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
