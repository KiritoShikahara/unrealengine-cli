// Gameplay Ability System and Gameplay Tag tools. GameplayAbilities is reached only through reflection (class,
// property and function names), so the bridge builds and loads in projects that do not enable the plugin.

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/ActorComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsEditorModule.h"
#include "GameplayTagsManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_IfThenElse.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Server/JsonUtil.h"
#include "Server/ToolRegistry.h"
#include "Tools/BlueprintHelpers.h"
#include "Tools/ToolRegistration.h"

using namespace UCli;

namespace
{
	UClass* GasClass(const TCHAR* Name)
	{
		return FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/GameplayAbilities.%s"), Name));
	}

	/** A GameplayAbilities class, or fails the call with gas.not_enabled. */
	UClass* RequireGasClass(const FToolCallRef& Call, const TCHAR* Name)
	{
		UClass* C = GasClass(Name);
		if (!C)
		{
			Call->Fail(TEXT("gas.not_enabled"), TEXT("the GameplayAbilities plugin is not enabled in this project"),
				TEXT("unrealengine-cli gas enable, then rebuild and restart the editor"));
		}
		return C;
	}

	/** Sets property Name of the struct or object memory Container from UE text. */
	bool SetByText(const UStruct* Type, void* Container, const FString& Name, const FString& Text, FString& Error)
	{
		FProperty* P = Type->FindPropertyByName(FName(*Name));
		if (!P)
		{
			Error = FString::Printf(TEXT("%s has no property %s"), *Type->GetName(), *Name);
			return false;
		}
		if (!P->ImportText_InContainer(*Text, Container, nullptr, PPF_None, GLog))
		{
			Error = FString::Printf(TEXT("cannot set %s.%s to %s"), *Type->GetName(), *Name, *Text);
			return false;
		}
		return true;
	}

	/** The first candidate that names a value of the enum property Name (enum names differ between engine versions). */
	FString EnumValue(const UStruct* Type, const TCHAR* Name, std::initializer_list<const TCHAR*> Candidates)
	{
		FProperty* P = Type->FindPropertyByName(Name);
		const UEnum* E = nullptr;
		if (const FByteProperty* B = CastField<FByteProperty>(P)) E = B->Enum;
		else if (const FEnumProperty* EP = CastField<FEnumProperty>(P)) E = EP->GetEnum();
		for (const TCHAR* C : Candidates)
		{
			if (E && E->GetIndexByNameString(C) != INDEX_NONE) return C;
		}
		return FString();
	}

	/** Value memory of property Name inside Container, with the property. */
	void* FieldPtr(const UStruct* Type, void* Container, const TCHAR* Name, FProperty*& Out)
	{
		Out = Type ? Type->FindPropertyByName(Name) : nullptr;
		return Out ? Out->ContainerPtrToValuePtr<void>(Container) : nullptr;
	}

	/** The class of a Blueprint path (/Game/GA_Fire), a /Script class path, or a loaded class name. */
	UClass* ResolveAssetClass(const FString& Ref)
	{
		if (UBlueprint* B = BP::Load(Ref)) return B->GeneratedClass;
		return BP::ResolveClass(Ref);
	}

	/** A loaded AttributeSet class by name (with or without the U prefix) or /Script path. */
	UClass* FindAttributeSet(const FString& SetName)
	{
		UClass* Base = GasClass(TEXT("AttributeSet"));
		for (TObjectIterator<UClass> It; It && Base; ++It)
		{
			if (!It->IsChildOf(Base) || *It == Base || It->HasAnyClassFlags(CLASS_NewerVersionExists)) continue;
			const FString N = It->GetName();
			if (N == SetName || TEXT("U") + N == SetName || It->GetPathName() == SetName || N == SetName + TEXT("_C")) return *It;
		}
		return nullptr;
	}

	/** "Set.Attribute" (Set: class name with or without the U prefix, or a /Script path) as FGameplayAttribute text. */
	bool AttributeText(const FString& Ref, FString& Out, FString& Error)
	{
		FString SetName, Attr;
		if (!Ref.Split(TEXT("."), &SetName, &Attr, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
		{
			Error = FString::Printf(TEXT("'%s' is not Set.Attribute"), *Ref);
			return false;
		}
		UClass* Set = FindAttributeSet(SetName);
		if (!Set)
		{
			Error = FString::Printf(TEXT("no AttributeSet class '%s' is loaded"), *SetName);
			return false;
		}
		FProperty* P = Set->FindPropertyByName(FName(*Attr));
		if (!P)
		{
			Error = FString::Printf(TEXT("%s has no attribute %s"), *Set->GetName(), *Attr);
			return false;
		}
		Out = FString::Printf(TEXT("(AttributeName=\"%s\",Attribute=\"%s\",AttributeOwner=\"%s\")"), *Attr, *P->GetPathName(), *Set->GetPathName());
		return true;
	}

	/** Tag container text, failing on tags that are not registered. */
	bool TagsText(const TArray<FString>& Tags, FString& Out, FString& Error)
	{
		TArray<FString> Parts;
		for (const FString& T : Tags)
		{
			if (!UGameplayTagsManager::Get().RequestGameplayTag(FName(*T), false).IsValid())
			{
				Error = FString::Printf(TEXT("gameplay tag '%s' does not exist"), *T);
				return false;
			}
			Parts.Add(FString::Printf(TEXT("(TagName=\"%s\")"), *T));
		}
		Out = FString::Printf(TEXT("(GameplayTags=(%s))"), *FString::Join(Parts, TEXT(",")));
		return true;
	}

	TArray<FString> StringList(const FToolCallRef& Call, const TCHAR* Field)
	{
		TArray<FString> Out;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Call->Args->TryGetArrayField(Field, Arr))
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr) Out.Add(V->AsString());
		}
		return Out;
	}

	/** Creates a compiled Blueprint of Parent at Path, or fails the call. */
	UBlueprint* NewBlueprint(const FToolCallRef& Call, const FString& Path, UClass* Parent)
	{
		FText Reason;
		if (!FPackageName::IsValidLongPackageName(Path, false, &Reason))
		{
			Call->Fail(TEXT("args.invalid"), FString::Printf(TEXT("'%s' is not a valid package path: %s"), *Path, *Reason.ToString()), TEXT("use a path like /Game/GAS/GE_Damage"));
			return nullptr;
		}
		if (BP::Load(Path) || FPackageName::DoesPackageExist(Path))
		{
			Call->Fail(TEXT("asset.exists"), FString::Printf(TEXT("'%s' already exists"), *Path));
			return nullptr;
		}
		UPackage* Package = CreatePackage(*Path);
		UBlueprint* B = FKismetEditorUtilities::CreateBlueprint(Parent, Package, FName(*FPackageName::GetShortName(Path)), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
		FAssetRegistryModule::AssetCreated(B);
		FKismetEditorUtilities::CompileBlueprint(B);
		return B;
	}

	/** Compiles and saves B; adds compile results to O. False (call failed) when it does not compile. */
	bool CompileAndSave(const FToolCallRef& Call, UBlueprint* B, const TSharedPtr<FJsonObject>& O)
	{
		B->GetOutermost()->MarkPackageDirty();
		const TSharedPtr<FJsonObject> C = BP::CompileJson(B);
		O->SetObjectField(TEXT("compile"), C);
		if (!C->GetBoolField(TEXT("succeeded")))
		{
			Call->Fail(TEXT("blueprint.compile_errors"), TEXT("the generated Blueprint does not compile"), TEXT("see details"), Obj(C));
			return false;
		}
		UEditorLoadingAndSavingUtils::SavePackages({ B->GetOutermost() }, false);
		O->SetStringField(TEXT("path"), B->GetOutermost()->GetName());
		return true;
	}

	UEdGraphNode* AddCall(UEdGraph* Graph, UClass* Owner, const TCHAR* Function, int32 X, int32 Y)
	{
		UFunction* F = Owner->FindFunctionByName(Function);
		if (!F) return nullptr;
		FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
		UK2Node_CallFunction* N = Creator.CreateNode();
		N->SetFromFunction(F);
		N->NodePosX = X;
		N->NodePosY = Y;
		Creator.Finalize();
		return N;
	}

	/** Starting value of an attribute property on the set's class defaults (FGameplayAttributeData or float). */
	bool DefaultValue(UObject* SetCDO, FProperty* P, double& Out)
	{
		if (const FStructProperty* SP = CastField<FStructProperty>(P); SP && SP->Struct->GetFName() == TEXT("GameplayAttributeData"))
		{
			FProperty* BaseP = nullptr;
			void* Base = FieldPtr(SP->Struct, SP->ContainerPtrToValuePtr<void>(SetCDO), TEXT("BaseValue"), BaseP);
			if (!Base || !CastField<FFloatProperty>(BaseP)) return false;
			Out = *static_cast<float*>(Base);
			return true;
		}
		if (const FNumericProperty* NP = CastField<FNumericProperty>(P); NP && NP->IsFloatingPoint())
		{
			Out = NP->GetFloatingPointPropertyValue(NP->ContainerPtrToValuePtr<void>(SetCDO));
			return true;
		}
		return false;
	}

	/** DataTable of FAttributeMetaData rows (<Set>.<Attribute>) holding the set's default values, next to the Blueprint. */
	UDataTable* StartingTable(UBlueprint* B, UClass* Set, FString& Error)
	{
		UScriptStruct* Row = FindObject<UScriptStruct>(nullptr, TEXT("/Script/GameplayAbilities.AttributeMetaData"));
		if (!Row)
		{
			Error = TEXT("GameplayAbilities has no AttributeMetaData struct");
			return nullptr;
		}
		UClass* Base = GasClass(TEXT("AttributeSet"));
		FString Csv = TEXT("---,BaseValue,MinValue,MaxValue,DerivedAttributeInfo,bCanStack\n");
		UObject* CDO = Set->GetDefaultObject();
		for (TFieldIterator<FProperty> It(Set); It; ++It)
		{
			double Value = 0;
			if (It->GetOwnerClass() == Base || !DefaultValue(CDO, *It, Value)) continue;
			Csv += FString::Printf(TEXT("%s.%s,%s,0,0,,False\n"), *It->GetOwnerVariant().GetName(), *It->GetName(), *FString::SanitizeFloat(Value));
		}
		const FString Path = FString::Printf(TEXT("%s/DT_%s_%s"), *FPackageName::GetLongPackagePath(B->GetOutermost()->GetName()), *B->GetName(), *Set->GetName());
		UPackage* Package = CreatePackage(*Path);
		UDataTable* Table = FindObject<UDataTable>(Package, *FPackageName::GetShortName(Path));
		if (!Table)
		{
			Table = NewObject<UDataTable>(Package, FName(*FPackageName::GetShortName(Path)), RF_Public | RF_Standalone | RF_Transactional);
			FAssetRegistryModule::AssetCreated(Table);
		}
		Table->RowStruct = Row;
		const TArray<FString> Problems = Table->CreateTableFromCSVString(Csv);
		if (Problems.Num())
		{
			Error = FString::Join(Problems, TEXT("; "));
			return nullptr;
		}
		Package->MarkPackageDirty();
		UEditorLoadingAndSavingUtils::SavePackages({ Package }, false);
		return Table;
	}

	UActorComponent* FindAsc(AActor* A)
	{
		UClass* Asc = GasClass(TEXT("AbilitySystemComponent"));
		for (UActorComponent* C : A ? A->GetComponents() : TSet<UActorComponent*>())
		{
			if (Asc && C && C->IsA(Asc)) return C;
		}
		return nullptr;
	}

	/** Calls a function of Target by name; Args are its parameters as UE text. */
	bool CallByName(UObject* Target, const TCHAR* Name, const TMap<FString, FString>& Args, FString& ReturnText, FString& Error)
	{
		UFunction* F = Target->FindFunction(Name);
		if (!F)
		{
			Error = FString::Printf(TEXT("%s has no function %s"), *Target->GetClass()->GetName(), Name);
			return false;
		}
		uint8* Params = static_cast<uint8*>(FMemory::Malloc(FMath::Max<int32>(F->ParmsSize, 1), F->GetMinAlignment()));
		FMemory::Memzero(Params, F->ParmsSize);
		for (TFieldIterator<FProperty> It(F); It && It->HasAnyPropertyFlags(CPF_Parm); ++It) It->InitializeValue_InContainer(Params);
		bool bOk = true;
		for (const TPair<FString, FString>& A : Args)
		{
			if (!SetByText(F, Params, A.Key, A.Value, Error)) { bOk = false; break; }
		}
		if (bOk)
		{
			Target->ProcessEvent(F, Params);
			if (FProperty* R = F->GetReturnProperty())
			{
				R->ExportTextItem_Direct(ReturnText, R->ContainerPtrToValuePtr<void>(Params), nullptr, nullptr, PPF_None);
			}
		}
		for (TFieldIterator<FProperty> It(F); It && It->HasAnyPropertyFlags(CPF_Parm); ++It) It->DestroyValue_InContainer(Params);
		FMemory::Free(Params);
		return bOk;
	}

	/** Attributes, owned tags, granted abilities and active effects of an AbilitySystemComponent. */
	TSharedPtr<FJsonObject> AscState(UActorComponent* Asc)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		UClass* SetBase = GasClass(TEXT("AttributeSet"));
		TSharedPtr<FJsonObject> Attributes = MakeShared<FJsonObject>();
		FProperty* P = nullptr;
		if (void* Sets = FieldPtr(Asc->GetClass(), Asc, TEXT("SpawnedAttributes"), P))
		{
			const FArrayProperty* AP = CastField<FArrayProperty>(P);
			const FObjectPropertyBase* Inner = AP ? CastField<FObjectPropertyBase>(AP->Inner) : nullptr;
			FScriptArrayHelper H(AP, Sets);
			for (int32 i = 0; Inner && i < H.Num(); ++i)
			{
				UObject* Set = Inner->GetObjectPropertyValue(H.GetRawPtr(i));
				if (!Set) continue;
				for (TFieldIterator<FProperty> It(Set->GetClass()); It; ++It)
				{
					const FString Key = Set->GetClass()->GetName() + TEXT(".") + It->GetName();
					if (const FStructProperty* SP = CastField<FStructProperty>(*It); SP && SP->Struct->GetFName() == TEXT("GameplayAttributeData"))
					{
						void* Data = SP->ContainerPtrToValuePtr<void>(Set);
						FProperty* BaseP = nullptr;
						FProperty* CurP = nullptr;
						void* Base = FieldPtr(SP->Struct, Data, TEXT("BaseValue"), BaseP);
						void* Cur = FieldPtr(SP->Struct, Data, TEXT("CurrentValue"), CurP);
						TSharedPtr<FJsonObject> V = MakeShared<FJsonObject>();
						if (Base && CastField<FFloatProperty>(BaseP)) V->SetNumberField(TEXT("base"), *static_cast<float*>(Base));
						if (Cur && CastField<FFloatProperty>(CurP)) V->SetNumberField(TEXT("current"), *static_cast<float*>(Cur));
						Attributes->SetObjectField(Key, V);
					}
					else if (const FFloatProperty* FP = CastField<FFloatProperty>(*It); FP && SetBase && It->GetOwnerClass() && It->GetOwnerClass() != SetBase && It->GetOwnerClass()->IsChildOf(SetBase))
					{
						TSharedPtr<FJsonObject> V = MakeShared<FJsonObject>();
						V->SetNumberField(TEXT("current"), FP->GetPropertyValue_InContainer(Set));
						Attributes->SetObjectField(Key, V);
					}
				}
			}
		}
		O->SetObjectField(TEXT("attributes"), Attributes);

		TArray<FString> Tags;
		if (IGameplayTagAssetInterface* TI = Cast<IGameplayTagAssetInterface>(Asc))
		{
			FGameplayTagContainer C;
			TI->GetOwnedGameplayTags(C);
			for (const FGameplayTag& T : C) Tags.Add(T.ToString());
		}
		O->SetArrayField(TEXT("tags"), StrArray(Tags));

		TArray<TSharedPtr<FJsonValue>> Abilities;
		if (void* Container = FieldPtr(Asc->GetClass(), Asc, TEXT("ActivatableAbilities"), P))
		{
			const FStructProperty* SP = CastField<FStructProperty>(P);
			FProperty* ItemsP = nullptr;
			void* Items = SP ? FieldPtr(SP->Struct, Container, TEXT("Items"), ItemsP) : nullptr;
			if (const FArrayProperty* AP = CastField<FArrayProperty>(ItemsP); AP && Items)
			{
				const UStruct* Spec = CastField<FStructProperty>(AP->Inner)->Struct;
				FScriptArrayHelper H(AP, Items);
				for (int32 i = 0; i < H.Num(); ++i)
				{
					FProperty* AbP = nullptr;
					FProperty* ActP = nullptr;
					void* Ab = FieldPtr(Spec, H.GetRawPtr(i), TEXT("Ability"), AbP);
					void* Act = FieldPtr(Spec, H.GetRawPtr(i), TEXT("ActiveCount"), ActP);
					const FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(AbP);
					UObject* Ability = OP && Ab ? OP->GetObjectPropertyValue(Ab) : nullptr;
					TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
					E->SetStringField(TEXT("ability"), Ability ? Ability->GetClass()->GetPathName() : FString());
					if (const FByteProperty* BP = CastField<FByteProperty>(ActP); BP && Act) E->SetNumberField(TEXT("activeCount"), *static_cast<uint8*>(Act));
					Abilities.Add(Obj(E));
				}
			}
		}
		O->SetArrayField(TEXT("abilities"), Abilities);

		TArray<FString> Effects;
		if (void* Container = FieldPtr(Asc->GetClass(), Asc, TEXT("ActiveGameplayEffects"), P))
		{
			const FStructProperty* SP = CastField<FStructProperty>(P);
			FProperty* ListP = nullptr;
			void* List = SP ? FieldPtr(SP->Struct, Container, TEXT("GameplayEffects_Internal"), ListP) : nullptr;
			if (const FArrayProperty* AP = CastField<FArrayProperty>(ListP); AP && List)
			{
				const UStruct* Active = CastField<FStructProperty>(AP->Inner)->Struct;
				FScriptArrayHelper H(AP, List);
				for (int32 i = 0; i < H.Num(); ++i)
				{
					FProperty* SpecP = nullptr;
					void* Spec = FieldPtr(Active, H.GetRawPtr(i), TEXT("Spec"), SpecP);
					const FStructProperty* SpecS = CastField<FStructProperty>(SpecP);
					FProperty* DefP = nullptr;
					void* Def = SpecS && Spec ? FieldPtr(SpecS->Struct, Spec, TEXT("Def"), DefP) : nullptr;
					const FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(DefP);
					UObject* Effect = OP && Def ? OP->GetObjectPropertyValue(Def) : nullptr;
					if (Effect) Effects.Add(Effect->GetClass()->GetPathName());
				}
			}
		}
		O->SetArrayField(TEXT("activeEffects"), StrArray(Effects));
		return O;
	}
}

void UCli::RegisterGasTools(FToolRegistry& R)
{
	R.Add(TEXT("gas_effect_create"), TEXT("gas"), TEXT("Create a Gameplay Effect Blueprint: duration instant (default), infinite or seconds; period seconds; modifiers [{attribute: Set.Attribute, op: add|multiply|divide|override, magnitude}]; stacking source|target with stackLimit; grantedTags (must exist, see tags_add). Compiled and saved."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"duration":{"type":"string"},"period":{"type":"number"},"modifiers":{"type":"array","items":{"type":"object","properties":{"attribute":{"type":"string"},"op":{"type":"string","enum":["add","multiply","divide","override"]},"magnitude":{"type":"number"}},"required":["attribute","magnitude"]}},"stacking":{"type":"string","enum":["none","source","target"]},"stackLimit":{"type":"integer"},"grantedTags":{"type":"array","items":{"type":"string"}}},"required":["path"]})"),
		EToolFlags::Mutating | EToolFlags::NoUndo,
		[](const FToolCallRef& Call)
		{
			UClass* GE = RequireGasClass(Call, TEXT("GameplayEffect"));
			if (!GE) return;
			// Validate everything before creating the asset.
			struct FMod { FString Attribute, Op, Magnitude; };
			TArray<FMod> Mods;
			const TArray<TSharedPtr<FJsonValue>>* ModArr = nullptr;
			FString Error;
			if (Call->Args->TryGetArrayField(TEXT("modifiers"), ModArr))
			{
				for (const TSharedPtr<FJsonValue>& V : *ModArr)
				{
					const TSharedPtr<FJsonObject> M = V->AsObject();
					FMod Mod;
					if (!M || !AttributeText(M->GetStringField(TEXT("attribute")), Mod.Attribute, Error))
					{
						Call->Fail(TEXT("gas.bad_attribute"), Error.IsEmpty() ? TEXT("a modifier is not an object") : Error, TEXT("attributes are Set.Attribute, e.g. MyAttributeSet.Health; see gas_state for an actor's sets"));
						return;
					}
					const FString Op = M->HasField(TEXT("op")) ? M->GetStringField(TEXT("op")) : TEXT("add");
					const UStruct* Info = FindObject<UScriptStruct>(nullptr, TEXT("/Script/GameplayAbilities.GameplayModifierInfo"));
					Mod.Op = Op == TEXT("multiply") ? EnumValue(Info, TEXT("ModifierOp"), { TEXT("MultiplyAdditive"), TEXT("Multiplicitive") })
						: Op == TEXT("divide") ? EnumValue(Info, TEXT("ModifierOp"), { TEXT("DivideAdditive"), TEXT("Division") })
						: Op == TEXT("override") ? EnumValue(Info, TEXT("ModifierOp"), { TEXT("Override") })
						: EnumValue(Info, TEXT("ModifierOp"), { TEXT("AddBase"), TEXT("Additive") });
					if (Mod.Op.IsEmpty())
					{
						Call->Fail(TEXT("args.invalid"), FString::Printf(TEXT("unknown op '%s'"), *Op), TEXT("add, multiply, divide or override"));
						return;
					}
					Mod.Magnitude = FString::Printf(TEXT("(MagnitudeCalculationType=ScalableFloat,ScalableFloatMagnitude=(Value=%f))"), M->GetNumberField(TEXT("magnitude")));
					Mods.Add(Mod);
				}
			}
			FString GrantedText;
			const TArray<FString> Granted = StringList(Call, TEXT("grantedTags"));
			if (Granted.Num() && !TagsText(Granted, GrantedText, Error))
			{
				Call->Fail(TEXT("tags.not_found"), Error, TEXT("add it first: unrealengine-cli tags add <tag>"));
				return;
			}
			const FString Duration = Call->GetString(TEXT("duration"), TEXT("instant")).ToLower();
			const bool bTimed = Duration != TEXT("instant") && Duration != TEXT("infinite");
			if (bTimed && !Duration.IsNumeric())
			{
				Call->Fail(TEXT("args.invalid"), FString::Printf(TEXT("duration '%s' is not instant, infinite or seconds"), *Duration));
				return;
			}

			UBlueprint* B = NewBlueprint(Call, Call->GetString(TEXT("path")), GE);
			if (!B) return;
			UObject* CDO = B->GeneratedClass->GetDefaultObject();
			UClass* C = CDO->GetClass();
			bool bOk = SetByText(C, CDO, TEXT("DurationPolicy"), bTimed ? TEXT("HasDuration") : Duration == TEXT("infinite") ? TEXT("Infinite") : TEXT("Instant"), Error);
			if (bOk && bTimed)
			{
				bOk = SetByText(C, CDO, TEXT("DurationMagnitude"), FString::Printf(TEXT("(MagnitudeCalculationType=ScalableFloat,ScalableFloatMagnitude=(Value=%s))"), *Duration), Error);
			}
			if (bOk && Call->Has(TEXT("period")))
			{
				bOk = SetByText(C, CDO, TEXT("Period"), FString::Printf(TEXT("(Value=%f)"), Call->GetNumber(TEXT("period"))), Error);
			}
			FProperty* ModsP = nullptr;
			if (bOk && Mods.Num())
			{
				void* ModsPtr = FieldPtr(C, CDO, TEXT("Modifiers"), ModsP);
				const FArrayProperty* AP = CastField<FArrayProperty>(ModsP);
				const UStruct* Info = AP ? CastField<FStructProperty>(AP->Inner)->Struct : nullptr;
				FScriptArrayHelper H(AP, ModsPtr);
				for (const FMod& M : Mods)
				{
					if (!Info) { bOk = false; Error = TEXT("GameplayEffect has no Modifiers array"); break; }
					void* Elem = H.GetRawPtr(H.AddValue());
					bOk = SetByText(Info, Elem, TEXT("Attribute"), M.Attribute, Error)
						&& SetByText(Info, Elem, TEXT("ModifierOp"), M.Op, Error)
						&& SetByText(Info, Elem, TEXT("ModifierMagnitude"), M.Magnitude, Error);
					if (!bOk) break;
				}
			}
			const FString Stacking = Call->GetString(TEXT("stacking"), TEXT("none"));
			if (bOk && Stacking != TEXT("none"))
			{
				bOk = SetByText(C, CDO, TEXT("StackingType"), Stacking == TEXT("source") ? TEXT("AggregateBySource") : TEXT("AggregateByTarget"), Error)
					&& SetByText(C, CDO, TEXT("StackLimitCount"), FString::FromInt((int32)Call->GetNumber(TEXT("stackLimit"), 1)), Error);
			}
			if (bOk && Granted.Num())
			{
				UClass* TagComp = GasClass(TEXT("TargetTagsGameplayEffectComponent"));
				FProperty* CompsP = nullptr;
				void* Comps = FieldPtr(C, CDO, TEXT("GEComponents"), CompsP);
				const FArrayProperty* AP = CastField<FArrayProperty>(CompsP);
				if (!TagComp || !AP)
				{
					bOk = false;
					Error = TEXT("this engine has no TargetTagsGameplayEffectComponent");
				}
				else
				{
					UObject* Comp = NewObject<UObject>(CDO, TagComp, NAME_None, CDO->GetMaskedFlags(RF_PropagateToSubObjects) | RF_Transactional);
					bOk = SetByText(TagComp, Comp, TEXT("InheritableGrantedTagsContainer"), FString::Printf(TEXT("(CombinedTags=%s,Added=%s)"), *GrantedText, *GrantedText), Error);
					FScriptArrayHelper H(AP, Comps);
					CastField<FObjectPropertyBase>(AP->Inner)->SetObjectPropertyValue(H.GetRawPtr(H.AddValue()), Comp);
				}
			}
			if (!bOk)
			{
				Call->Fail(TEXT("gas.effect_failed"), Error, TEXT("the asset was created but not saved; fix the arguments, delete it (asset delete) and retry"));
				return;
			}
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("duration"), Duration);
			O->SetNumberField(TEXT("modifiers"), Mods.Num());
			O->SetArrayField(TEXT("grantedTags"), StrArray(Granted));
			if (!CompileAndSave(Call, B, O)) return;
			Call->Succeed(O);
		});

	R.Add(TEXT("gas_ability_create"), TEXT("gas"), TEXT("Create a Gameplay Ability Blueprint. effect: a Gameplay Effect applied to the owner when the ability activates (the graph is ActivateAbility -> CommitAbility -> ApplyGameplayEffectToOwner -> EndAbility); cost / cooldown: Gameplay Effects; tags: ability tags. Compiled and saved."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"effect":{"type":"string"},"cost":{"type":"string"},"cooldown":{"type":"string"},"tags":{"type":"array","items":{"type":"string"}}},"required":["path"]})"),
		EToolFlags::Mutating | EToolFlags::NoUndo,
		[](const FToolCallRef& Call)
		{
			UClass* GA = RequireGasClass(Call, TEXT("GameplayAbility"));
			if (!GA) return;
			UClass* GE = GasClass(TEXT("GameplayEffect"));
			TMap<FString, UClass*> Effects;
			for (const TCHAR* Field : { TEXT("effect"), TEXT("cost"), TEXT("cooldown") })
			{
				if (!Call->Has(Field)) continue;
				UClass* E = ResolveAssetClass(Call->GetString(Field));
				if (!E || !E->IsChildOf(GE))
				{
					Call->Fail(TEXT("gas.bad_effect"), FString::Printf(TEXT("%s '%s' is not a Gameplay Effect"), Field, *Call->GetString(Field)), TEXT("create one: unrealengine-cli gas effect <path>"));
					return;
				}
				Effects.Add(Field, E);
			}
			FString TagText, Error;
			const TArray<FString> Tags = StringList(Call, TEXT("tags"));
			if (Tags.Num() && !TagsText(Tags, TagText, Error))
			{
				Call->Fail(TEXT("tags.not_found"), Error, TEXT("add it first: unrealengine-cli tags add <tag>"));
				return;
			}
			UBlueprint* B = NewBlueprint(Call, Call->GetString(TEXT("path")), GA);
			if (!B) return;
			UObject* CDO = B->GeneratedClass->GetDefaultObject();
			bool bOk = true;
			if (UClass** Cost = Effects.Find(TEXT("cost"))) bOk &= SetByText(CDO->GetClass(), CDO, TEXT("CostGameplayEffectClass"), (*Cost)->GetPathName(), Error);
			if (UClass** Cd = Effects.Find(TEXT("cooldown"))) bOk &= SetByText(CDO->GetClass(), CDO, TEXT("CooldownGameplayEffectClass"), (*Cd)->GetPathName(), Error);
			if (Tags.Num()) bOk &= SetByText(CDO->GetClass(), CDO, TEXT("AbilityTags"), TagText, Error);
			if (bOk && Effects.Contains(TEXT("effect")))
			{
				UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(B);
				const UEdGraphSchema_K2* K2 = GetDefault<UEdGraphSchema_K2>();
				FGraphNodeCreator<UK2Node_Event> EventCreator(*Graph);
				UK2Node_Event* Ev = EventCreator.CreateNode();
				Ev->EventReference.SetExternalMember(TEXT("K2_ActivateAbility"), GA);
				Ev->bOverrideFunction = true;
				Ev->NodePosX = 0;
				Ev->NodePosY = 0;
				EventCreator.Finalize();
				UEdGraphNode* Commit = AddCall(Graph, GA, TEXT("K2_CommitAbility"), 300, 0);
				FGraphNodeCreator<UK2Node_IfThenElse> BranchCreator(*Graph);
				UK2Node_IfThenElse* Branch = BranchCreator.CreateNode();
				Branch->NodePosX = 600;
				BranchCreator.Finalize();
				UEdGraphNode* Apply = AddCall(Graph, GA, TEXT("BP_ApplyGameplayEffectToOwner"), 900, 0);
				UEdGraphNode* End = AddCall(Graph, GA, TEXT("K2_EndAbility"), 1300, 0);
				if (!Commit || !Apply || !End)
				{
					bOk = false;
					Error = TEXT("GameplayAbility lacks K2_CommitAbility, BP_ApplyGameplayEffectToOwner or K2_EndAbility");
				}
				else
				{
					bOk = K2->TryCreateConnection(Ev->FindPin(UEdGraphSchema_K2::PN_Then), Commit->FindPin(UEdGraphSchema_K2::PN_Execute))
						&& K2->TryCreateConnection(Commit->FindPin(UEdGraphSchema_K2::PN_Then), Branch->GetExecPin())
						&& K2->TryCreateConnection(Commit->FindPin(UEdGraphSchema_K2::PN_ReturnValue), Branch->GetConditionPin())
						&& K2->TryCreateConnection(Branch->GetThenPin(), Apply->FindPin(UEdGraphSchema_K2::PN_Execute))
						&& K2->TryCreateConnection(Apply->FindPin(UEdGraphSchema_K2::PN_Then), End->FindPin(UEdGraphSchema_K2::PN_Execute))
						&& K2->TryCreateConnection(Branch->GetElsePin(), End->FindPin(UEdGraphSchema_K2::PN_Execute));
					if (UEdGraphPin* ClassPin = Apply->FindPin(TEXT("GameplayEffectClass")))
					{
						K2->TrySetDefaultObject(*ClassPin, Effects[TEXT("effect")]);
					}
					if (!bOk) Error = TEXT("could not wire the ability graph");
				}
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(B);
			}
			if (!bOk)
			{
				Call->Fail(TEXT("gas.ability_failed"), Error, TEXT("the asset was created but not saved; delete it (asset delete) and retry"));
				return;
			}
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetBoolField(TEXT("appliesEffect"), Effects.Contains(TEXT("effect")));
			O->SetArrayField(TEXT("tags"), StrArray(Tags));
			if (!CompileAndSave(Call, B, O)) return;
			Call->Succeed(O);
		});

	R.Add(TEXT("gas_cue_create"), TEXT("gas"), TEXT("Create a Gameplay Cue notify Blueprint for a GameplayCue.* tag (kind static, default, or actor). Compiled and saved; implement OnExecute / OnActive in its graph."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"tag":{"type":"string"},"kind":{"type":"string","enum":["static","actor"]}},"required":["path","tag"]})"),
		EToolFlags::Mutating | EToolFlags::NoUndo,
		[](const FToolCallRef& Call)
		{
			UClass* Parent = RequireGasClass(Call, Call->GetString(TEXT("kind")) == TEXT("actor") ? TEXT("GameplayCueNotify_Actor") : TEXT("GameplayCueNotify_Static"));
			if (!Parent) return;
			const FString Tag = Call->GetString(TEXT("tag"));
			if (!Tag.StartsWith(TEXT("GameplayCue.")) || !UGameplayTagsManager::Get().RequestGameplayTag(FName(*Tag), false).IsValid())
			{
				Call->Fail(TEXT("tags.not_found"), FString::Printf(TEXT("'%s' is not an existing GameplayCue.* tag"), *Tag), FString::Printf(TEXT("add it first: unrealengine-cli tags add %s"), Tag.StartsWith(TEXT("GameplayCue.")) ? *Tag : *(TEXT("GameplayCue.") + Tag)));
				return;
			}
			UBlueprint* B = NewBlueprint(Call, Call->GetString(TEXT("path")), Parent);
			if (!B) return;
			UObject* CDO = B->GeneratedClass->GetDefaultObject();
			FString Error;
			if (!SetByText(CDO->GetClass(), CDO, TEXT("GameplayCueTag"), FString::Printf(TEXT("(TagName=\"%s\")"), *Tag), Error))
			{
				Call->Fail(TEXT("gas.cue_failed"), Error);
				return;
			}
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("tag"), Tag);
			if (!CompileAndSave(Call, B, O)) return;
			Call->Succeed(O);
		});

	R.Add(TEXT("gas_component_add"), TEXT("gas"), TEXT("Give a Blueprint actor an AbilitySystemComponent (reused when it already has one) whose DefaultStartingData creates the given AttributeSets (class names) at start, with a DT_<Blueprint>_<Set> table of starting values taken from the set's defaults (edit it to change them). Compiled and saved."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"attributeSets":{"type":"array","items":{"type":"string"}},"name":{"type":"string"}},"required":["path"]})"),
		EToolFlags::Mutating | EToolFlags::NoUndo,
		[](const FToolCallRef& Call)
		{
			UClass* AscClass = RequireGasClass(Call, TEXT("AbilitySystemComponent"));
			if (!AscClass) return;
			UBlueprint* B = BP::Load(Call->GetString(TEXT("path")));
			if (!B || !B->SimpleConstructionScript)
			{
				Call->Fail(TEXT("blueprint.not_found"), FString::Printf(TEXT("no actor Blueprint at '%s'"), *Call->GetString(TEXT("path"))));
				return;
			}
			TArray<FString> Sets, Tables;
			FString Error;
			for (const FString& S : StringList(Call, TEXT("attributeSets")))
			{
				UClass* C = FindAttributeSet(S);
				if (!C)
				{
					Call->Fail(TEXT("gas.bad_attribute"), FString::Printf(TEXT("no AttributeSet class '%s' is loaded"), *S), TEXT("generate one: unrealengine-cli gas attribute-set, then code apply"));
					return;
				}
				// DefaultStartingData only creates a set together with a table of starting values: write one from the set's defaults.
				UDataTable* Table = StartingTable(B, C, Error);
				if (!Table)
				{
					Call->Fail(TEXT("gas.component_failed"), Error);
					return;
				}
				Tables.Add(Table->GetPathName());
				Sets.Add(FString::Printf(TEXT("(Attributes=\"%s\",DefaultStartingTable=\"%s\")"), *C->GetPathName(), *Table->GetPathName()));
			}
			if (B->GeneratedClass)
			{
				if (AActor* CDO = Cast<AActor>(B->GeneratedClass->GetDefaultObject()))
				{
					for (UActorComponent* C : CDO->GetComponents())
					{
						if (C && C->IsA(AscClass))
						{
							Call->Fail(TEXT("gas.native_component"), TEXT("the parent class already creates an AbilitySystemComponent in C++"), TEXT("set DefaultStartingData in the C++ constructor instead"));
							return;
						}
					}
				}
			}
			USCS_Node* Node = nullptr;
			for (USCS_Node* N : B->SimpleConstructionScript->GetAllNodes())
			{
				if (N && N->ComponentTemplate && N->ComponentTemplate->IsA(AscClass)) { Node = N; break; }
			}
			const bool bCreated = Node == nullptr;
			if (!Node)
			{
				Node = B->SimpleConstructionScript->CreateNode(AscClass, FName(*Call->GetString(TEXT("name"), TEXT("AbilitySystem"))));
				B->SimpleConstructionScript->AddNode(Node);
			}
			FString OldText, NewText;
			if (Sets.Num())
			{
				UActorComponent* Template = Node->ComponentTemplate;
				FProperty* Prop = Template->GetClass()->FindPropertyByName(TEXT("DefaultStartingData"));
				if (Prop) Prop->ExportTextItem_InContainer(OldText, Template, nullptr, nullptr, PPF_None);
				if (!SetByText(Template->GetClass(), Template, TEXT("DefaultStartingData"), FString::Printf(TEXT("(%s)"), *FString::Join(Sets, TEXT(","))), Error))
				{
					Call->Fail(TEXT("gas.component_failed"), Error);
					return;
				}
				// Like the details panel: placed instances that still had the old default take the new one.
				Prop->ExportTextItem_InContainer(NewText, Template, nullptr, nullptr, PPF_None);
				TArray<UObject*> Instances;
				Template->GetArchetypeInstances(Instances);
				for (UObject* I : Instances)
				{
					FString Current;
					Prop->ExportTextItem_InContainer(Current, I, nullptr, nullptr, PPF_None);
					if (Current == OldText)
					{
						I->Modify();
						Prop->ImportText_InContainer(*NewText, I, I, PPF_None);
					}
				}
			}
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(B);
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("component"), Node->GetVariableName().ToString());
			O->SetBoolField(TEXT("created"), bCreated);
			O->SetNumberField(TEXT("attributeSets"), Sets.Num());
			O->SetArrayField(TEXT("startingTables"), StrArray(Tables));
			O->SetStringField(TEXT("defaultStartingData"), NewText);
			if (!CompileAndSave(Call, B, O)) return;
			Call->Succeed(O);
		});

	R.Add(TEXT("gas_runtime"), TEXT("gas"), TEXT("Drive an actor's AbilitySystemComponent in the play world: give (ability), activate (ability), apply (effect to self), state. Every action returns the state: attributes (base/current), owned tags, granted abilities, active effects."),
		TEXT(R"({"type":"object","properties":{"action":{"type":"string","enum":["give","activate","apply","state"]},"actor":{"type":"string"},"ability":{"type":"string"},"effect":{"type":"string"},"level":{"type":"number"},"world":{"type":"string"}},"required":["action","actor"]})"),
		EToolFlags::Mutating | EToolFlags::NoUndo,
		[](const FToolCallRef& Call)
		{
			if (!RequireGasClass(Call, TEXT("AbilitySystemComponent"))) return;
			AActor* A = ResolvePlayActor(Call);
			if (!A) return;
			UActorComponent* Asc = FindAsc(A);
			if (!Asc)
			{
				Call->Fail(TEXT("gas.no_component"), FString::Printf(TEXT("%s has no AbilitySystemComponent"), *A->GetActorLabel()),
					TEXT("add one: unrealengine-cli blueprint add-component <bp> AbilitySystemComponent, and list attribute sets in its DefaultStartingData"));
				return;
			}
			const FString Action = Call->GetString(TEXT("action"));
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			FString Ret, Error;
			const FString Level = FString::SanitizeFloat(Call->GetNumber(TEXT("level"), 1.0));
			if (Action == TEXT("give") || Action == TEXT("activate") || Action == TEXT("apply"))
			{
				const bool bEffect = Action == TEXT("apply");
				const FString Ref = Call->GetString(bEffect ? TEXT("effect") : TEXT("ability"));
				UClass* Base = GasClass(bEffect ? TEXT("GameplayEffect") : TEXT("GameplayAbility"));
				UClass* Cls = ResolveAssetClass(Ref);
				if (!Cls || !Cls->IsChildOf(Base))
				{
					Call->Fail(bEffect ? TEXT("gas.bad_effect") : TEXT("gas.bad_ability"), FString::Printf(TEXT("'%s' is not a %s"), *Ref, *Base->GetName()));
					return;
				}
				TMap<FString, FString> Args;
				const TCHAR* Fn;
				if (Action == TEXT("give"))
				{
					Fn = TEXT("K2_GiveAbility");
					Args = { { TEXT("AbilityClass"), Cls->GetPathName() }, { TEXT("Level"), FString::FromInt((int32)Call->GetNumber(TEXT("level"), 1.0)) }, { TEXT("InputID"), TEXT("-1") } };
				}
				else if (Action == TEXT("activate"))
				{
					Fn = TEXT("TryActivateAbilityByClass");
					Args = { { TEXT("InAbilityToActivate"), Cls->GetPathName() }, { TEXT("bAllowRemoteActivation"), TEXT("true") } };
				}
				else
				{
					Fn = TEXT("BP_ApplyGameplayEffectToSelf");
					Args = { { TEXT("GameplayEffectClass"), Cls->GetPathName() }, { TEXT("Level"), Level } };
				}
				if (!CallByName(Asc, Fn, Args, Ret, Error))
				{
					Call->Fail(TEXT("gas.call_failed"), Error);
					return;
				}
				if (Action == TEXT("activate"))
				{
					O->SetBoolField(TEXT("activated"), Ret == TEXT("True") || Ret == TEXT("true"));
					if (!O->GetBoolField(TEXT("activated"))) O->SetStringField(TEXT("hint"), TEXT("not activated: give it first (gas give), or its cost, cooldown or tags block it"));
				}
				else
				{
					O->SetStringField(TEXT("handle"), Ret);
				}
			}
			else if (Action != TEXT("state"))
			{
				Call->Fail(TEXT("args.invalid"), FString::Printf(TEXT("unknown action '%s'"), *Action), TEXT("give, activate, apply or state"));
				return;
			}
			O->SetStringField(TEXT("actor"), A->GetActorLabel());
			O->SetObjectField(TEXT("state"), AscState(Asc));
			Call->Succeed(O);
		});

	R.Add(TEXT("tags_list"), TEXT("tags"), TEXT("Registered Gameplay Tags (optionally only those containing filter)."),
		TEXT(R"({"type":"object","properties":{"filter":{"type":"string"}}})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			FGameplayTagContainer All;
			UGameplayTagsManager::Get().RequestAllGameplayTags(All, true);
			const FString Filter = Call->GetString(TEXT("filter"));
			TArray<FString> Names;
			for (const FGameplayTag& T : All)
			{
				if (Filter.IsEmpty() || T.ToString().Contains(Filter)) Names.Add(T.ToString());
			}
			Names.Sort();
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("count"), Names.Num());
			O->SetArrayField(TEXT("tags"), StrArray(Names));
			Call->Succeed(O);
		});

	R.Add(TEXT("tags_add"), TEXT("tags"), TEXT("Add a Gameplay Tag to Config/DefaultGameplayTags.ini (parents are implied). No-op when it exists."),
		TEXT(R"({"type":"object","properties":{"tag":{"type":"string"},"comment":{"type":"string"}},"required":["tag"]})"), EToolFlags::Mutating | EToolFlags::NoUndo,
		[](const FToolCallRef& Call)
		{
			const FString Tag = Call->GetString(TEXT("tag"));
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("tag"), Tag);
			if (UGameplayTagsManager::Get().IsDictionaryTag(FName(*Tag)))
			{
				O->SetBoolField(TEXT("created"), false);
				Call->Succeed(O);
				return;
			}
			FText Why;
			if (!UGameplayTagsManager::Get().IsValidGameplayTagString(Tag, &Why))
			{
				Call->Fail(TEXT("tags.invalid"), FString::Printf(TEXT("'%s' is not a valid tag: %s"), *Tag, *Why.ToString()));
				return;
			}
			if (!IGameplayTagsEditorModule::Get().AddNewGameplayTagToINI(Tag, Call->GetString(TEXT("comment"))))
			{
				Call->Fail(TEXT("tags.add_failed"), FString::Printf(TEXT("could not add '%s'"), *Tag), TEXT("check the log: unrealengine-cli logs --severity warning"));
				return;
			}
			O->SetBoolField(TEXT("created"), true);
			Call->Succeed(O);
		});

	R.Add(TEXT("tags_rename"), TEXT("tags"), TEXT("Rename a Gameplay Tag (and its children); writes a redirect so existing references keep working."),
		TEXT(R"({"type":"object","properties":{"from":{"type":"string"},"to":{"type":"string"}},"required":["from","to"]})"), EToolFlags::Mutating | EToolFlags::NoUndo,
		[](const FToolCallRef& Call)
		{
			const FString From = Call->GetString(TEXT("from"));
			const FString To = Call->GetString(TEXT("to"));
			if (!UGameplayTagsManager::Get().IsDictionaryTag(FName(*From)))
			{
				Call->Fail(TEXT("tags.not_found"), FString::Printf(TEXT("tag '%s' does not exist"), *From), TEXT("see: unrealengine-cli tags list"));
				return;
			}
			if (!IGameplayTagsEditorModule::Get().RenameTagInINI(From, To))
			{
				Call->Fail(TEXT("tags.rename_failed"), FString::Printf(TEXT("could not rename '%s' (tags defined outside the project ini cannot be renamed here)"), *From));
				return;
			}
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("from"), From);
			O->SetStringField(TEXT("to"), To);
			Call->Succeed(O);
		});

	R.Add(TEXT("tags_remove"), TEXT("tags"), TEXT("Remove a Gameplay Tag from the project ini. Refused while assets still reference it."),
		TEXT(R"({"type":"object","properties":{"tag":{"type":"string"}},"required":["tag"]})"), EToolFlags::Mutating | EToolFlags::NoUndo,
		[](const FToolCallRef& Call)
		{
			const FString Tag = Call->GetString(TEXT("tag"));
			TSharedPtr<FGameplayTagNode> Node = UGameplayTagsManager::Get().FindTagNode(FName(*Tag));
			if (!Node.IsValid())
			{
				Call->Fail(TEXT("tags.not_found"), FString::Printf(TEXT("tag '%s' does not exist"), *Tag), TEXT("see: unrealengine-cli tags list"));
				return;
			}
			if (!IGameplayTagsEditorModule::Get().DeleteTagFromINI(Node))
			{
				Call->Fail(TEXT("tags.remove_failed"), FString::Printf(TEXT("could not remove '%s': it is referenced, has children, or is not defined in the project ini"), *Tag), TEXT("rename it instead: unrealengine-cli tags rename"));
				return;
			}
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("removed"), Tag);
			Call->Succeed(O);
		});
}
