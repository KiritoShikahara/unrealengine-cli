#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UFunction;
class FJsonObject;

namespace UCli::BP
{
	FString ObjectPath(const FString& PackagePath);
	UBlueprint* Load(const FString& Path);
	UClass* ResolveClass(const FString& Name);
	UEdGraph* FindGraph(UBlueprint* Blueprint, const FString& Name);
	FString TypeString(const FEdGraphPinType& Type);
	bool ParseType(const FString& Spec, FEdGraphPinType& Out, FString& Error);
	FString NodeId(const UEdGraph* Graph, const UEdGraphNode* Node);
	TSharedPtr<FJsonObject> NodeJson(const UEdGraph* Graph, UEdGraphNode* Node);
	UEdGraphNode* ResolveNode(UEdGraph* Graph, const FString& Ref);
	UEdGraphPin* ResolvePin(UEdGraph* Graph, const FString& Ref, FString& Error, EEdGraphPinDirection Direction = EGPD_MAX);
	UFunction* ResolveFunction(UBlueprint* Blueprint, const FString& Name, const FString& Parent);
	TSharedPtr<FJsonObject> CompileJson(UBlueprint* Blueprint);
}
