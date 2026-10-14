#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Server/JsonUtil.h"
#include "Server/ToolRegistry.h"

namespace UCli::Test
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	inline TSharedPtr<FJsonObject> Args(const FString& Json)
	{
		return ParseObject(Json);
	}

	inline TSharedPtr<FJsonObject> Call(const FString& Tool, const FString& Json = TEXT("{}"), bool bDryRun = false)
	{
		return FToolRegistry::Get().CallNow(Tool, Args(Json), bDryRun);
	}

	inline bool Ok(const TSharedPtr<FJsonObject>& Reply) { return Reply->GetBoolField(TEXT("ok")); }

	inline TSharedPtr<FJsonObject> Data(const TSharedPtr<FJsonObject>& Reply)
	{
		const TSharedPtr<FJsonObject>* O = nullptr;
		return Reply->TryGetObjectField(TEXT("data"), O) ? *O : MakeShared<FJsonObject>();
	}

	inline FString Code(const TSharedPtr<FJsonObject>& Reply)
	{
		const TSharedPtr<FJsonObject>* E = nullptr;
		return Reply->TryGetObjectField(TEXT("error"), E) ? (*E)->GetStringField(TEXT("code")) : FString();
	}

	inline FString UniquePath(const FString& Prefix)
	{
		return FString::Printf(TEXT("/Game/UCliTests/%s_%s"), *Prefix, *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
	}
}
