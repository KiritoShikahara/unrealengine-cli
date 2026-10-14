#include "Tests/BridgeTestUtil.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace UCli;
using namespace UCli::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUCliLevelActors, "UnrealCliBridge.Level.SpawnTransformFind", UCli::Test::TestFlags)
bool FUCliLevelActors::RunTest(const FString&)
{
	const FString Label = FString::Printf(TEXT("UCliSpec_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(6));
	const TSharedPtr<FJsonObject> S = Call(TEXT("actor_spawn"), FString::Printf(TEXT("{\"actorClass\":\"PointLight\",\"location\":[1,2,3],\"label\":\"%s\"}"), *Label));
	TestTrue(TEXT("spawn"), Ok(S));
	TestTrue(TEXT("transform"), Ok(Call(TEXT("actor_transform"), FString::Printf(TEXT("{\"label\":\"%s\",\"location\":[10,20,30]}"), *Label))));
	const TSharedPtr<FJsonObject> F = Data(Call(TEXT("actor_find"), FString::Printf(TEXT("{\"label\":\"%s\"}"), *Label)));
	TestEqual(TEXT("moved"), F->GetObjectField(TEXT("location"))->GetNumberField(TEXT("x")), 10.0);
	TestEqual(TEXT("missing actor"), Code(Call(TEXT("actor_find"), TEXT("{\"label\":\"NoSuchActor_UCli\"}"))), FString(TEXT("actor.not_found")));
	TestTrue(TEXT("destroy"), Ok(Call(TEXT("actor_destroy"), FString::Printf(TEXT("{\"labels\":[\"%s\"]}"), *Label))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUCliPropertyCandidates, "UnrealCliBridge.Property.SetGetAndSuggest", UCli::Test::TestFlags)
bool FUCliPropertyCandidates::RunTest(const FString&)
{
	const FString Label = FString::Printf(TEXT("UCliProp_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(6));
	Call(TEXT("actor_spawn"), FString::Printf(TEXT("{\"actorClass\":\"PointLight\",\"label\":\"%s\"}"), *Label));
	TestTrue(TEXT("set"), Ok(Call(TEXT("prop_set"), FString::Printf(TEXT("{\"path\":\"%s.LightComponent0\",\"property\":\"Intensity\",\"value\":777}"), *Label))));
	const TSharedPtr<FJsonObject> G = Data(Call(TEXT("prop_get"), FString::Printf(TEXT("{\"path\":\"%s.LightComponent0\",\"property\":\"Intensity\"}"), *Label)));
	TestEqual(TEXT("value"), G->GetNumberField(TEXT("value")), 777.0);
	const TSharedPtr<FJsonObject> Bad = Call(TEXT("prop_get"), FString::Printf(TEXT("{\"path\":\"%s.LightComponent0\",\"property\":\"Intensty\"}"), *Label));
	TestEqual(TEXT("not found"), Code(Bad), FString(TEXT("prop.not_found")));
	TestTrue(TEXT("suggests"), Bad->GetObjectField(TEXT("error"))->GetStringField(TEXT("hint")).Contains(TEXT("intensity")));
	Call(TEXT("actor_destroy"), FString::Printf(TEXT("{\"labels\":[\"%s\"]}"), *Label));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUCliAssetLifecycle, "UnrealCliBridge.Asset.CreateDuplicateDelete", UCli::Test::TestFlags)
bool FUCliAssetLifecycle::RunTest(const FString&)
{
	const FString A = UniquePath(TEXT("M_Spec"));
	const FString B = A + TEXT("_Copy");
	TestTrue(TEXT("create"), Ok(Call(TEXT("asset_create"), FString::Printf(TEXT("{\"path\":\"%s\",\"class\":\"Material\"}"), *A))));
	TestEqual(TEXT("exists"), Code(Call(TEXT("asset_create"), FString::Printf(TEXT("{\"path\":\"%s\",\"class\":\"Material\"}"), *A))), FString(TEXT("asset.exists")));
	TestTrue(TEXT("duplicate"), Ok(Call(TEXT("asset_duplicate"), FString::Printf(TEXT("{\"path\":\"%s\",\"newPath\":\"%s\"}"), *A, *B))));
	TestTrue(TEXT("delete"), Ok(Call(TEXT("asset_delete"), FString::Printf(TEXT("{\"path\":\"%s\",\"force\":true}"), *B))));
	return true;
}

#endif
