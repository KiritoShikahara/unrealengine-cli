#include "Tests/BridgeTestUtil.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace UCli;
using namespace UCli::Test;

namespace
{
	TSharedPtr<FJsonObject> FindByName(const TArray<TSharedPtr<FJsonValue>>& Items, const FString& Name)
	{
		for (const TSharedPtr<FJsonValue>& V : Items)
		{
			if (V->AsObject()->GetStringField(TEXT("name")) == Name) return V->AsObject();
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUCliNiagaraStack, "UnrealCliBridge.Niagara.ModulesSwitchesRenderer", UCli::Test::TestFlags)
bool FUCliNiagaraStack::RunTest(const FString&)
{
	const FString Sys = UniquePath(TEXT("NS_Stack"));
	TestTrue(TEXT("system"), Ok(Call(TEXT("niagara_system_create"), FString::Printf(TEXT("{\"path\":\"%s\"}"), *Sys))));
	TestTrue(TEXT("emitter"), Ok(Call(TEXT("emitter_add"), FString::Printf(TEXT("{\"system\":\"%s\",\"emitter\":\"/Niagara/DefaultAssets/Templates/Emitters/Fountain\"}"), *Sys))));
	const FString Base = FString::Printf(TEXT("\"system\":\"%s\",\"emitter\":\"Fountain\""), *Sys);

	const TSharedPtr<FJsonObject> List = Call(TEXT("niagara_modules_list"), FString::Printf(TEXT("{%s,\"stage\":\"ParticleSpawn\"}"), *Base));
	TestTrue(TEXT("list"), Ok(List));
	if (!Ok(List)) return false;
	const TSharedPtr<FJsonObject> D = Data(List);
	const TArray<TSharedPtr<FJsonValue>>& Stages = D->GetArrayField(TEXT("stages"));
	TestEqual(TEXT("one stage"), Stages.Num(), 1);
	if (Stages.Num() != 1) return false;
	const TArray<TSharedPtr<FJsonValue>>& Modules = Stages[0]->AsObject()->GetArrayField(TEXT("modules"));
	const TSharedPtr<FJsonObject> Shape = FindByName(Modules, TEXT("ShapeLocation"));
	TestTrue(TEXT("ShapeLocation listed"), Shape.IsValid());
	if (!Shape) return false;
	const TSharedPtr<FJsonObject> Primitive = FindByName(Shape->GetArrayField(TEXT("inputs")), TEXT("Shape Primitive"));
	TestTrue(TEXT("Shape Primitive listed"), Primitive.IsValid());
	if (Primitive)
	{
		TestEqual(TEXT("is a static switch"), Primitive->GetStringField(TEXT("kind")), FString(TEXT("staticSwitch")));
		TestTrue(TEXT("has options"), Primitive->GetArrayField(TEXT("options")).Num() > 1);
	}
	TestTrue(TEXT("renderers listed"), D->GetArrayField(TEXT("renderers")).Num() > 0);

	TestTrue(TEXT("set static switch"), Ok(Call(TEXT("niagara_input_set"), FString::Printf(TEXT("{%s,\"stage\":\"ParticleSpawn\",\"module\":\"ShapeLocation\",\"input\":\"Shape Primitive\",\"value\":\"Cylinder\"}"), *Base))));
	const TSharedPtr<FJsonObject> After = FindByName(FindByName(Data(Call(TEXT("niagara_modules_list"), FString::Printf(TEXT("{%s,\"stage\":\"ParticleSpawn\"}"), *Base)))->GetArrayField(TEXT("stages"))[0]->AsObject()->GetArrayField(TEXT("modules")), TEXT("ShapeLocation"))->GetArrayField(TEXT("inputs")), TEXT("Shape Primitive"));
	TestEqual(TEXT("switch value"), After.IsValid() ? After->GetStringField(TEXT("value")) : FString(), FString(TEXT("Cylinder")));
	TestEqual(TEXT("bad enum"), Code(Call(TEXT("niagara_input_set"), FString::Printf(TEXT("{%s,\"stage\":\"ParticleSpawn\",\"module\":\"ShapeLocation\",\"input\":\"Shape Primitive\",\"value\":\"Banana\"}"), *Base))), FString(TEXT("niagara.bad_value")));

	const TSharedPtr<FJsonObject> Off = Call(TEXT("niagara_module_enable"), FString::Printf(TEXT("{%s,\"stage\":\"ParticleUpdate\",\"module\":\"GravityForce\",\"enabled\":false}"), *Base));
	TestTrue(TEXT("disable module"), Ok(Off));
	TestFalse(TEXT("disabled"), Ok(Off) && Data(Off)->GetBoolField(TEXT("enabled")));

	const TSharedPtr<FJsonObject> Mat = Call(TEXT("niagara_renderer_set"), FString::Printf(TEXT("{%s,\"renderer\":\"SpriteRenderer\",\"property\":\"Material\",\"value\":\"/Engine/EngineMaterials/DefaultMaterial\"}"), *Base));
	TestTrue(TEXT("renderer material"), Ok(Mat));
	TestTrue(TEXT("material value"), Ok(Mat) && Data(Mat)->GetStringField(TEXT("value")).Contains(TEXT("DefaultMaterial")));
	TestEqual(TEXT("wrong asset type"), Code(Call(TEXT("niagara_renderer_set"), FString::Printf(TEXT("{%s,\"property\":\"Material\",\"value\":\"%s\"}"), *Base, *Sys))), FString(TEXT("niagara.bad_value")));
	return true;
}

#endif
