#include "Tests/BridgeTestUtil.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace UCli;
using namespace UCli::Test;

namespace
{
	FString P(const FString& Path, const FString& Extra = FString())
	{
		return FString::Printf(TEXT("{\"path\":\"%s\"%s}"), *Path, *Extra);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUCliBlueprintPrintString, "UnrealCliBridge.Blueprint.BeginPlayPrintString", UCli::Test::TestFlags)
bool FUCliBlueprintPrintString::RunTest(const FString&)
{
	const FString Path = UniquePath(TEXT("BP_Hello"));
	TestTrue(TEXT("create"), Ok(Call(TEXT("bp_create"), P(Path, TEXT(",\"parent\":\"Actor\"")))));
	TestEqual(TEXT("duplicate refused"), Code(Call(TEXT("bp_create"), P(Path))), FString(TEXT("asset.exists")));

	const TSharedPtr<FJsonObject> Ev = Call(TEXT("bp_add_node"), P(Path, TEXT(",\"type\":\"Event\",\"memberName\":\"BeginPlay\"")));
	TestTrue(TEXT("event"), Ok(Ev));
	const FString EvId = Data(Ev)->GetStringField(TEXT("guid"));
	const TSharedPtr<FJsonObject> Pr = Call(TEXT("bp_add_node"), P(Path, TEXT(",\"type\":\"CallFunction\",\"memberName\":\"PrintString\"")));
	TestTrue(TEXT("print"), Ok(Pr));
	const FString PrId = Data(Pr)->GetStringField(TEXT("guid"));

	TestTrue(TEXT("connect"), Ok(Call(TEXT("bp_connect"), P(Path, FString::Printf(TEXT(",\"from\":\"%s.then\",\"to\":\"%s.execute\""), *EvId, *PrId)))));
	TestTrue(TEXT("set pin"), Ok(Call(TEXT("bp_set_pin"), P(Path, FString::Printf(TEXT(",\"node\":\"%s\",\"pin\":\"InString\",\"value\":\"Hello\""), *PrId)))));
	TestEqual(TEXT("bad pin"), Code(Call(TEXT("bp_connect"), P(Path, FString::Printf(TEXT(",\"from\":\"%s.nope\",\"to\":\"%s.execute\""), *EvId, *PrId)))), FString(TEXT("blueprint.pin_not_found")));

	const TSharedPtr<FJsonObject> G = Call(TEXT("bp_graph_get"), P(Path));
	bool bLinked = false;
	for (const TSharedPtr<FJsonValue>& N : Data(G)->GetArrayField(TEXT("nodes")))
	{
		for (const TSharedPtr<FJsonValue>& Pin : N->AsObject()->GetArrayField(TEXT("pins")))
		{
			bLinked |= Pin->AsObject()->HasField(TEXT("linkedTo"));
		}
	}
	TestTrue(TEXT("graph shows link"), bLinked);

	const TSharedPtr<FJsonObject> C = Call(TEXT("bp_compile"), P(Path));
	TestTrue(TEXT("compiles"), Data(C)->GetBoolField(TEXT("succeeded")));
	TestEqual(TEXT("no errors"), static_cast<int32>(Data(C)->GetNumberField(TEXT("errors"))), 0);

	const TSharedPtr<FJsonObject> Again = Call(TEXT("bp_add_node"), P(Path, TEXT(",\"type\":\"Event\",\"memberName\":\"BeginPlay\"")));
	TestTrue(TEXT("existing event reused"), Data(Again)->GetBoolField(TEXT("existing")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUCliBlueprintEventGraphPage, "UnrealCliBridge.Blueprint.EventGraphPage", UCli::Test::TestFlags)
bool FUCliBlueprintEventGraphPage::RunTest(const FString&)
{
	const FString Bpi = UniquePath(TEXT("BPI_Notify"));
	TestTrue(TEXT("interface"), Ok(Call(TEXT("bp_create"), P(Bpi, TEXT(",\"kind\":\"interface\"")))));
	TestTrue(TEXT("interface fn"), Ok(Call(TEXT("bp_add_function"), P(Bpi, TEXT(",\"name\":\"Notified\"")))));
	Call(TEXT("bp_compile"), P(Bpi));

	const FString Path = UniquePath(TEXT("BP_Pages"));
	TestTrue(TEXT("create"), Ok(Call(TEXT("bp_create"), P(Path, TEXT(",\"parent\":\"Actor\"")))));
	TestTrue(TEXT("implement"), Ok(Call(TEXT("bp_implement_interface"), P(Path, FString::Printf(TEXT(",\"interface\":\"%s\""), *Bpi)))));
	TestTrue(TEXT("add graph"), Ok(Call(TEXT("bp_add_graph"), P(Path, TEXT(",\"name\":\"InterfaceGraph\"")))));
	TestEqual(TEXT("duplicate graph"), Code(Call(TEXT("bp_add_graph"), P(Path, TEXT(",\"name\":\"InterfaceGraph\"")))), FString(TEXT("blueprint.graph_exists")));
	TestTrue(TEXT("event in page"), Ok(Call(TEXT("bp_add_node"), P(Path, FString::Printf(TEXT(",\"graph\":\"InterfaceGraph\",\"type\":\"Event\",\"memberName\":\"Notified\",\"memberParent\":\"%s\""), *Bpi)))));
	bool bListed = false;
	const TSharedPtr<FJsonObject> Info = Data(Call(TEXT("bp_get"), P(Path)));
	for (const TSharedPtr<FJsonValue>& G : Info->GetArrayField(TEXT("graphs")))
	{
		bListed |= G->AsObject()->GetStringField(TEXT("name")) == TEXT("InterfaceGraph");
	}
	TestTrue(TEXT("listed"), bListed);
	TestTrue(TEXT("compiles"), Data(Call(TEXT("bp_compile"), P(Path)))->GetBoolField(TEXT("succeeded")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUCliBlueprintVariables, "UnrealCliBridge.Blueprint.Variables", UCli::Test::TestFlags)
bool FUCliBlueprintVariables::RunTest(const FString&)
{
	const FString Path = UniquePath(TEXT("BP_Vars"));
	Call(TEXT("bp_create"), P(Path));
	TestTrue(TEXT("add int"), Ok(Call(TEXT("bp_add_variable"), P(Path, TEXT(",\"name\":\"Health\",\"type\":\"int\",\"defaultValue\":\"100\",\"instanceEditable\":true")))));
	TestTrue(TEXT("add array"), Ok(Call(TEXT("bp_add_variable"), P(Path, TEXT(",\"name\":\"Points\",\"type\":\"vector[]\"")))));
	TestTrue(TEXT("add map"), Ok(Call(TEXT("bp_add_variable"), P(Path, TEXT(",\"name\":\"Scores\",\"type\":\"map<string, int>\"")))));
	TestTrue(TEXT("add set"), Ok(Call(TEXT("bp_add_variable"), P(Path, TEXT(",\"name\":\"Labels\",\"type\":\"set<name>\"")))));
	TestEqual(TEXT("bad map"), Code(Call(TEXT("bp_add_variable"), P(Path, TEXT(",\"name\":\"Y\",\"type\":\"map<int>\"")))), FString(TEXT("args.invalid")));
	TestEqual(TEXT("bad type"), Code(Call(TEXT("bp_add_variable"), P(Path, TEXT(",\"name\":\"X\",\"type\":\"banana\"")))), FString(TEXT("args.invalid")));
	TestEqual(TEXT("duplicate"), Code(Call(TEXT("bp_add_variable"), P(Path, TEXT(",\"name\":\"Health\",\"type\":\"int\"")))), FString(TEXT("blueprint.variable_exists")));

	const TSharedPtr<FJsonObject> Get = Call(TEXT("bp_add_node"), P(Path, TEXT(",\"type\":\"VariableGet\",\"memberName\":\"Health\"")));
	TestTrue(TEXT("getter"), Ok(Get));
	TestTrue(TEXT("branch"), Ok(Call(TEXT("bp_add_node"), P(Path, TEXT(",\"type\":\"Branch\"")))));
	TestTrue(TEXT("macro"), Ok(Call(TEXT("bp_add_node"), P(Path, TEXT(",\"type\":\"Macro\",\"memberName\":\"ForEachLoop\"")))));
	TestTrue(TEXT("function"), Ok(Call(TEXT("bp_add_function"), P(Path, TEXT(",\"name\":\"Heal\",\"inputs\":[{\"name\":\"Amount\",\"type\":\"float\"}],\"outputs\":[{\"name\":\"NewHealth\",\"type\":\"int\"}]")))));
	TestTrue(TEXT("component"), Ok(Call(TEXT("bp_add_component"), P(Path, TEXT(",\"class\":\"StaticMeshComponent\",\"name\":\"Mesh\"")))));

	const TSharedPtr<FJsonObject> Info = Data(Call(TEXT("bp_get"), P(Path)));
	const TArray<TSharedPtr<FJsonValue>>& Vars = Info->GetArrayField(TEXT("variables"));
	TestEqual(TEXT("four variables"), Vars.Num(), 4);
	if (Vars.Num() == 4)
	{
		TestEqual(TEXT("map type"), Vars[2]->AsObject()->GetStringField(TEXT("type")), FString(TEXT("map<string,int>")));
		TestEqual(TEXT("set type"), Vars[3]->AsObject()->GetStringField(TEXT("type")), FString(TEXT("set<name>")));
	}
	TestEqual(TEXT("one component"), Info->GetArrayField(TEXT("components")).Num(), 1);
	TestTrue(TEXT("compiles"), Data(Call(TEXT("bp_compile"), P(Path)))->GetBoolField(TEXT("succeeded")));

	TestTrue(TEXT("remove var"), Ok(Call(TEXT("bp_remove_variable"), P(Path, TEXT(",\"name\":\"Points\"")))));
	TestTrue(TEXT("reparent"), Ok(Call(TEXT("bp_reparent"), P(Path, TEXT(",\"parent\":\"Pawn\"")))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUCliBlueprintSaveRefusesErrors, "UnrealCliBridge.Blueprint.SaveRefusesCompileErrors", UCli::Test::TestFlags)
bool FUCliBlueprintSaveRefusesErrors::RunTest(const FString&)
{
	const FString Path = UniquePath(TEXT("BP_Broken"));
	Call(TEXT("bp_create"), P(Path));
	// A component method with no target in an Actor Blueprint cannot compile (self is not a StaticMeshComponent).
	// Unreachable nodes are pruned by the compiler, so wire it to BeginPlay.
	const FString Ev = Data(Call(TEXT("bp_add_node"), P(Path, TEXT(",\"type\":\"Event\",\"memberName\":\"BeginPlay\""))))->GetStringField(TEXT("guid"));
	const FString Fn = Data(Call(TEXT("bp_add_node"), P(Path, TEXT(",\"type\":\"CallFunction\",\"memberName\":\"SetStaticMesh\",\"memberParent\":\"StaticMeshComponent\""))))->GetStringField(TEXT("guid"));
	TestTrue(TEXT("wired"), Ok(Call(TEXT("bp_connect"), P(Path, FString::Printf(TEXT(",\"from\":\"%s.then\",\"to\":\"%s.execute\""), *Ev, *Fn)))));
	const TSharedPtr<FJsonObject> C = Data(Call(TEXT("bp_compile"), P(Path)));
	TestFalse(TEXT("does not compile"), C->GetBoolField(TEXT("succeeded")));
	TestEqual(TEXT("save refused"), Code(Call(TEXT("bp_save"), P(Path))), FString(TEXT("blueprint.compile_errors")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUCliBlueprintSearch, "UnrealCliBridge.Blueprint.SearchNodes", UCli::Test::TestFlags)
bool FUCliBlueprintSearch::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> R = Call(TEXT("bp_search_nodes"), TEXT("{\"query\":\"PrintString\"}"));
	bool bFound = false;
	for (const TSharedPtr<FJsonValue>& V : R->GetArrayField(TEXT("data")))
	{
		bFound |= V->AsObject()->GetStringField(TEXT("name")) == TEXT("PrintString");
	}
	TestTrue(TEXT("PrintString found"), bFound);
	const TSharedPtr<FJsonObject> M = Call(TEXT("bp_search_nodes"), TEXT("{\"query\":\"ForEach\",\"kind\":\"macro\"}"));
	TestTrue(TEXT("macro found"), M->GetArrayField(TEXT("data")).Num() > 0);
	return true;
}

#endif
