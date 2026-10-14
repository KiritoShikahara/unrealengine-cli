#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "FabPageBridge.generated.h"

/** Bound into the Fab browser page as window.ue.unrealcliagent; scripts report their results through it. */
UCLASS()
class UFabPageBridge : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void Report(const FString& Id, const FString& Json);

	TMap<FString, FString> Results;
};
