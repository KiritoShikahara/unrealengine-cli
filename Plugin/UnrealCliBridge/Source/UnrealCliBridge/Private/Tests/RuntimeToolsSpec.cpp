#include "Tests/BridgeTestUtil.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace UCli;
using namespace UCli::Test;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUCliRuntimeStatus, "UnrealCliBridge.Runtime.StatusAndList", UCli::Test::TestFlags)
bool FUCliRuntimeStatus::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> S = Call(TEXT("pie_status"));
	TestTrue(TEXT("status ok"), Ok(S));
	TestFalse(TEXT("not playing"), Data(S)->GetBoolField(TEXT("playing")));
	const TSharedPtr<FJsonObject> L = Data(Call(TEXT("automation_list"), TEXT("{\"filter\":\"UnrealCliBridge.System\"}")));
	TestTrue(TEXT("lists own tests"), L->GetNumberField(TEXT("count")) >= 3);
	TestEqual(TEXT("no tests"), Code(Call(TEXT("automation_run"), TEXT("{\"filter\":\"No.Such.Test.Anywhere\"}"))), FString(TEXT("automation.no_tests")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUCliJobs, "UnrealCliBridge.Runtime.Jobs", UCli::Test::TestFlags)
bool FUCliJobs::RunTest(const FString&)
{
	TestEqual(TEXT("unknown job"), Code(Call(TEXT("jobs_get"), TEXT("{\"jobId\":\"job-none\"}"))), FString(TEXT("jobs.not_found")));
	TestEqual(TEXT("cannot cancel"), Code(Call(TEXT("jobs_cancel"), TEXT("{\"jobId\":\"job-none\"}"))), FString(TEXT("jobs.not_cancellable")));
	TestTrue(TEXT("list"), Ok(Call(TEXT("jobs_list"))));
	return true;
}

#endif
