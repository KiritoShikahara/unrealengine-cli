#include "Tests/BridgeTestUtil.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace UCli;
using namespace UCli::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUCliSystemPing, "UnrealCliBridge.System.Ping", UCli::Test::TestFlags)
bool FUCliSystemPing::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> R = Call(TEXT("system_ping"));
	TestTrue(TEXT("ok"), Ok(R));
	TestTrue(TEXT("pong"), Data(R)->GetBoolField(TEXT("pong")));
	TestFalse(TEXT("engine version"), Data(R)->GetStringField(TEXT("engineVersion")).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUCliEditorLogs, "UnrealCliBridge.System.Logs", UCli::Test::TestFlags)
bool FUCliEditorLogs::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> First = Call(TEXT("editor_logs"), TEXT("{\"limit\":1}"));
	const int64 Next = static_cast<int64>(Data(First)->GetNumberField(TEXT("nextSince")));
	UE_LOG(LogTemp, Warning, TEXT("ucli-log-marker"));
	const TSharedPtr<FJsonObject> R = Call(TEXT("editor_logs"), FString::Printf(TEXT("{\"since\":%lld,\"severity\":\"warning\"}"), Next));
	bool bFound = false;
	for (const TSharedPtr<FJsonValue>& L : Data(R)->GetArrayField(TEXT("lines")))
	{
		bFound |= L->AsObject()->GetStringField(TEXT("text")).Contains(TEXT("ucli-log-marker"));
		TestNotEqual(TEXT("severity filter"), L->AsObject()->GetStringField(TEXT("severity")), FString(TEXT("log")));
	}
	TestTrue(TEXT("marker line returned"), bFound);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUCliRegistryValidation, "UnrealCliBridge.Registry.Validation", UCli::Test::TestFlags)
bool FUCliRegistryValidation::RunTest(const FString&)
{
	TestEqual(TEXT("unknown tool"), Code(Call(TEXT("does_not_exist"))), FString(TEXT("tool.unknown")));
	TestEqual(TEXT("missing arg"), Code(Call(TEXT("jobs_get"))), FString(TEXT("args.invalid")));
	TestEqual(TEXT("wrong type"), Code(Call(TEXT("jobs_get"), TEXT("{\"jobId\":3}"))), FString(TEXT("args.invalid")));
	TestEqual(TEXT("bad enum"), Code(Call(TEXT("editor_logs"), TEXT("{\"severity\":\"loud\"}"))), FString(TEXT("args.invalid")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUCliDryRun, "UnrealCliBridge.Registry.DryRun", UCli::Test::TestFlags)
bool FUCliDryRun::RunTest(const FString&)
{
	const FString Path = UniquePath(TEXT("BP_Dry"));
	const TSharedPtr<FJsonObject> R = Call(TEXT("bp_create"), FString::Printf(TEXT("{\"path\":\"%s\"}"), *Path), true);
	TestTrue(TEXT("ok"), Ok(R));
	TestTrue(TEXT("dryRun flag"), Data(R)->GetBoolField(TEXT("dryRun")));
	TestEqual(TEXT("nothing created"), Code(Call(TEXT("bp_get"), FString::Printf(TEXT("{\"path\":\"%s\"}"), *Path))), FString(TEXT("blueprint.not_found")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUCliToolsList, "UnrealCliBridge.System.ToolsList", UCli::Test::TestFlags)
bool FUCliToolsList::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> R = Call(TEXT("tools_list"));
	const TArray<TSharedPtr<FJsonValue>>& Items = R->GetArrayField(TEXT("data"));
	TestTrue(TEXT("has tools"), Items.Num() > 10);
	for (const TSharedPtr<FJsonValue>& V : Items)
	{
		const TSharedPtr<FJsonObject> T = V->AsObject();
		TestTrue(*FString::Printf(TEXT("%s has schema"), *T->GetStringField(TEXT("name"))), T->HasTypedField<EJson::Object>(TEXT("inputSchema")));
		TestFalse(*FString::Printf(TEXT("%s has summary"), *T->GetStringField(TEXT("name"))), T->GetStringField(TEXT("summary")).IsEmpty());
	}
	return true;
}

#endif
