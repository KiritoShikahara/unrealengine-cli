#include "Editor.h"
#include "Landscape.h"
#include "LandscapeEdit.h"
#include "LandscapeInfo.h"
#include "LandscapeProxy.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraSystem.h"
#include "Server/JsonUtil.h"
#include "Server/ToolRegistry.h"
#include "Tools/BlueprintHelpers.h"
#include "Tools/ToolRegistration.h"
#include "EngineUtils.h"

using namespace UCli;

namespace
{
	UNiagaraSystem* RequireSystem(const FToolCallRef& Call)
	{
		UNiagaraSystem* S = LoadObject<UNiagaraSystem>(nullptr, *BP::ObjectPath(Call->GetString(TEXT("system"))), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!S)
		{
			Call->Fail(TEXT("niagara.not_found"), FString::Printf(TEXT("no Niagara System at '%s'"), *Call->GetString(TEXT("system"))), TEXT("create one: niagara_system_create"));
		}
		return S;
	}

	TSharedPtr<FJsonObject> EmittersJson(UNiagaraSystem* S)
	{
		TArray<FString> Names;
		for (const FNiagaraEmitterHandle& H : S->GetEmitterHandles())
		{
			Names.Add(H.GetName().ToString());
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("system"), S->GetPathName());
		O->SetArrayField(TEXT("emitters"), StrArray(Names));
		return O;
	}

	ALandscape* FindLandscape(const FString& Label)
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		for (TActorIterator<ALandscape> It(World); It; ++It)
		{
			if (Label.IsEmpty() || It->GetActorLabel() == Label)
			{
				return *It;
			}
		}
		return nullptr;
	}
}

void UCli::RegisterNiagaraLandscapeTools(FToolRegistry& R)
{
	R.Add(TEXT("emitter_add"), TEXT("niagara"), TEXT("Add an emitter asset (e.g. a template under /Niagara/DefaultAssets/Templates/Emitters) to a Niagara System."),
		TEXT(R"({"type":"object","properties":{"system":{"type":"string"},"emitter":{"type":"string"}},"required":["system","emitter"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UNiagaraSystem* S = RequireSystem(Call);
			if (!S) return;
			UNiagaraEmitter* E = LoadObject<UNiagaraEmitter>(nullptr, *BP::ObjectPath(Call->GetString(TEXT("emitter"))), nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!E)
			{
				Call->Fail(TEXT("niagara.emitter_not_found"), FString::Printf(TEXT("no emitter at '%s'"), *Call->GetString(TEXT("emitter"))), TEXT("search: asset_search class=NiagaraEmitter pathPrefix=/Niagara"));
				return;
			}
			S->Modify();
			FNiagaraEditorUtilities::AddEmitterToSystem(*S, *E, E->GetExposedVersion().VersionGuid);
			S->RequestCompile(false);
			S->MarkPackageDirty();
			Call->Succeed(EmittersJson(S));
		});

	R.Add(TEXT("niagara_emitters_list"), TEXT("niagara"), TEXT("Emitters of a Niagara System."),
		TEXT(R"({"type":"object","properties":{"system":{"type":"string"}},"required":["system"]})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			UNiagaraSystem* S = RequireSystem(Call);
			if (!S) return;
			Call->Succeed(EmittersJson(S));
		});

	R.Add(TEXT("landscape_create"), TEXT("landscape"), TEXT("Create a flat landscape: components x components, each 63 quads, at location with scale."),
		TEXT(R"({"type":"object","properties":{"components":{"type":"integer"},"location":{"type":"array"},"scale":{"type":"array"},"label":{"type":"string"},"material":{"type":"string"}}})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			const int32 Components = FMath::Clamp(static_cast<int32>(Call->GetNumber(TEXT("components"), 2)), 1, 32);
			const int32 Quads = 63;
			const int32 Size = Components * Quads + 1;
			auto Vec = [&Call](const TCHAR* Key, const FVector& Default)
			{
				const TArray<TSharedPtr<FJsonValue>> A = Call->GetArray(Key);
				return A.Num() == 3 ? FVector(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber()) : Default;
			};
			UWorld* World = GEditor->GetEditorWorldContext().World();
			ALandscape* L = World->SpawnActor<ALandscape>(Vec(TEXT("location"), FVector::ZeroVector), FRotator::ZeroRotator);
			L->SetActorScale3D(Vec(TEXT("scale"), FVector(100, 100, 100)));
			if (Call->Has(TEXT("material")))
			{
				L->LandscapeMaterial = LoadObject<UMaterialInterface>(nullptr, *BP::ObjectPath(Call->GetString(TEXT("material"))));
			}
			TArray<uint16> Heights;
			Heights.Init(32768, Size * Size);
			TMap<FGuid, TArray<uint16>> HeightLayers;
			HeightLayers.Add(FGuid(), MoveTemp(Heights));
			TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayers;
			MaterialLayers.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());
			L->Import(FGuid::NewGuid(), 0, 0, Size - 1, Size - 1, 1, Quads, HeightLayers, nullptr, MaterialLayers, ELandscapeImportAlphamapType::Additive, TArrayView<const FLandscapeLayer>());
			if (Call->Has(TEXT("label")))
			{
				L->SetActorLabel(Call->GetString(TEXT("label")));
			}
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("label"), L->GetActorLabel());
			O->SetNumberField(TEXT("size"), Size);
			O->SetNumberField(TEXT("components"), L->LandscapeComponents.Num());
			Call->Succeed(O);
		});

	R.Add(TEXT("landscape_sculpt"), TEXT("landscape"), TEXT("Raise (positive height, cm) or lower a circular area of a landscape with smooth falloff."),
		TEXT(R"({"type":"object","properties":{"label":{"type":"string"},"center":{"type":"array"},"radius":{"type":"number"},"height":{"type":"number"}},"required":["center","radius","height"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			ALandscape* L = FindLandscape(Call->GetString(TEXT("label")));
			ULandscapeInfo* Info = L ? L->GetLandscapeInfo() : nullptr;
			const TArray<TSharedPtr<FJsonValue>> C = Call->GetArray(TEXT("center"));
			if (!Info || C.Num() < 2)
			{
				Call->Fail(TEXT("landscape.not_found"), TEXT("no landscape (or center is not [x, y])"), TEXT("create one: landscape_create"));
				return;
			}
			const FTransform T = L->GetActorTransform();
			const FVector Local = T.InverseTransformPosition(FVector(C[0]->AsNumber(), C[1]->AsNumber(), 0));
			const double RadiusQ = Call->GetNumber(TEXT("radius")) / T.GetScale3D().X;
			const double DeltaUnits = Call->GetNumber(TEXT("height")) / (T.GetScale3D().Z / 128.0);
			int32 MinX, MinY, MaxX, MaxY;
			if (!Info->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
			{
				Call->Fail(TEXT("landscape.empty"), TEXT("the landscape has no components"));
				return;
			}
			const int32 X1 = FMath::Clamp(FMath::FloorToInt(Local.X - RadiusQ), MinX, MaxX);
			const int32 Y1 = FMath::Clamp(FMath::FloorToInt(Local.Y - RadiusQ), MinY, MaxY);
			const int32 X2 = FMath::Clamp(FMath::CeilToInt(Local.X + RadiusQ), MinX, MaxX);
			const int32 Y2 = FMath::Clamp(FMath::CeilToInt(Local.Y + RadiusQ), MinY, MaxY);
			FLandscapeEditDataInterface Edit(Info);
			TArray<uint16> Data;
			Data.SetNumZeroed((X2 - X1 + 1) * (Y2 - Y1 + 1));
			int32 GX1 = X1, GY1 = Y1, GX2 = X2, GY2 = Y2;
			Edit.GetHeightData(GX1, GY1, GX2, GY2, Data.GetData(), 0);
			int32 Changed = 0;
			for (int32 Y = Y1; Y <= Y2; ++Y)
			{
				for (int32 X = X1; X <= X2; ++X)
				{
					const double D = FVector2D::Distance(FVector2D(X, Y), FVector2D(Local.X, Local.Y));
					if (D > RadiusQ) continue;
					const double Falloff = 0.5 * (1.0 + FMath::Cos(PI * D / FMath::Max(RadiusQ, 1.0)));
					uint16& H = Data[(Y - Y1) * (X2 - X1 + 1) + (X - X1)];
					H = static_cast<uint16>(FMath::Clamp(H + DeltaUnits * Falloff, 0.0, 65535.0));
					++Changed;
				}
			}
			Edit.SetHeightData(X1, Y1, X2, Y2, Data.GetData(), 0, true);
			Edit.Flush();
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("vertices"), Changed);
			Call->Succeed(O);
		});

	R.Add(TEXT("landscape_info"), TEXT("landscape"), TEXT("Landscapes in the level with size, component count and material."),
		TEXT(R"({"type":"object","properties":{}})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			TArray<TSharedPtr<FJsonValue>> Items;
			UWorld* World = GEditor->GetEditorWorldContext().World();
			for (TActorIterator<ALandscape> It(World); It; ++It)
			{
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetStringField(TEXT("label"), It->GetActorLabel());
				O->SetNumberField(TEXT("components"), It->LandscapeComponents.Num());
				O->SetStringField(TEXT("material"), It->LandscapeMaterial ? It->LandscapeMaterial->GetPathName() : FString());
				int32 MinX = 0, MinY = 0, MaxX = 0, MaxY = 0;
				if (ULandscapeInfo* Info = It->GetLandscapeInfo())
				{
					Info->GetLandscapeExtent(MinX, MinY, MaxX, MaxY);
				}
				O->SetNumberField(TEXT("sizeX"), MaxX - MinX + 1);
				O->SetNumberField(TEXT("sizeY"), MaxY - MinY + 1);
				Items.Add(Obj(O));
			}
			Call->Succeed(Arr(Items));
		});
}
