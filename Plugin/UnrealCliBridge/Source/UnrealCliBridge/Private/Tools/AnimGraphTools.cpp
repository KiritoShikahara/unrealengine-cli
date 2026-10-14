#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequence.h"
#include "AnimationStateMachineGraph.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "EdGraph/EdGraph.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Server/JsonUtil.h"
#include "Server/ToolRegistry.h"
#include "Tools/BlueprintHelpers.h"
#include "Tools/ToolRegistration.h"

using namespace UCli;

namespace
{
	UAnimBlueprint* RequireAnimBP(const FToolCallRef& Call)
	{
		UAnimBlueprint* B = Cast<UAnimBlueprint>(BP::Load(Call->GetString(TEXT("path"))));
		if (!B)
		{
			Call->Fail(TEXT("anim.not_found"), FString::Printf(TEXT("no Animation Blueprint at '%s'"), *Call->GetString(TEXT("path"))), TEXT("create one: animbp_create"));
		}
		return B;
	}

	UAnimationStateMachineGraph* FindMachine(UAnimBlueprint* B, const FString& Name)
	{
		TArray<UEdGraph*> Graphs;
		B->GetAllGraphs(Graphs);
		for (UEdGraph* G : Graphs)
		{
			if (UAnimationStateMachineGraph* M = Cast<UAnimationStateMachineGraph>(G))
			{
				if (M->GetName() == Name)
				{
					return M;
				}
			}
		}
		return nullptr;
	}

	UAnimStateNode* FindState(UAnimationStateMachineGraph* M, const FString& Name)
	{
		for (UEdGraphNode* N : M->Nodes)
		{
			UAnimStateNode* S = Cast<UAnimStateNode>(N);
			if (S && S->GetStateName() == Name)
			{
				return S;
			}
		}
		return nullptr;
	}

	UEdGraphPin* FirstPin(UEdGraphNode* N, EEdGraphPinDirection Dir)
	{
		for (UEdGraphPin* P : N->Pins)
		{
			if (P && P->Direction == Dir)
			{
				return P;
			}
		}
		return nullptr;
	}

	bool AddSequencePlayer(UAnimStateNode* State, const FString& SequencePath, FString& Error)
	{
		UAnimSequenceBase* Seq = LoadObject<UAnimSequenceBase>(nullptr, *BP::ObjectPath(SequencePath), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Seq)
		{
			Error = FString::Printf(TEXT("no animation at '%s'"), *SequencePath);
			return false;
		}
		UEdGraph* G = State->BoundGraph;
		FGraphNodeCreator<UAnimGraphNode_SequencePlayer> Creator(*G);
		UAnimGraphNode_SequencePlayer* Player = Creator.CreateNode(false);
		Player->Node.SetSequence(Seq);
		Player->NodePosX = -300;
		Creator.Finalize();
		TArray<UAnimGraphNode_StateResult*> Results;
		G->GetNodesOfClass(Results);
		if (Results.Num() > 0)
		{
			G->GetSchema()->TryCreateConnection(FirstPin(Player, EGPD_Output), FirstPin(Results[0], EGPD_Input));
		}
		return true;
	}
}

void UCli::RegisterAnimGraphTools(FToolRegistry& R)
{
	R.Add(TEXT("statemachine_add"), TEXT("anim"), TEXT("Add a state machine to an Animation Blueprint's AnimGraph and wire it to the output pose."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"name":{"type":"string"}},"required":["path","name"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UAnimBlueprint* B = RequireAnimBP(Call);
			if (!B) return;
			const FString Name = Call->GetString(TEXT("name"));
			if (FindMachine(B, Name))
			{
				Call->Fail(TEXT("anim.exists"), FString::Printf(TEXT("state machine '%s' exists"), *Name));
				return;
			}
			UEdGraph* AnimGraph = BP::FindGraph(B, TEXT("AnimGraph"));
			if (!AnimGraph)
			{
				Call->Fail(TEXT("anim.no_animgraph"), TEXT("the blueprint has no AnimGraph"));
				return;
			}
			B->Modify();
			AnimGraph->Modify();
			FGraphNodeCreator<UAnimGraphNode_StateMachine> Creator(*AnimGraph);
			UAnimGraphNode_StateMachine* SM = Creator.CreateNode(false);
			SM->NodePosX = -300;
			Creator.Finalize();
			FBlueprintEditorUtils::RenameGraph(SM->EditorStateMachineGraph, Name);
			TArray<UAnimGraphNode_Root*> Roots;
			AnimGraph->GetNodesOfClass(Roots);
			if (Roots.Num() > 0)
			{
				AnimGraph->GetSchema()->TryCreateConnection(FirstPin(SM, EGPD_Output), FirstPin(Roots[0], EGPD_Input));
			}
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(B);
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("machine"), SM->EditorStateMachineGraph->GetName());
			O->SetBoolField(TEXT("connectedToOutput"), Roots.Num() > 0);
			Call->Succeed(O);
		});

	R.Add(TEXT("state_add"), TEXT("anim"), TEXT("Add a state to a state machine (first state becomes the entry); optional sequence plays in it."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"machine":{"type":"string"},"name":{"type":"string"},"sequence":{"type":"string"}},"required":["path","machine","name"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UAnimBlueprint* B = RequireAnimBP(Call);
			if (!B) return;
			UAnimationStateMachineGraph* M = FindMachine(B, Call->GetString(TEXT("machine")));
			if (!M)
			{
				Call->Fail(TEXT("anim.machine_not_found"), FString::Printf(TEXT("no state machine '%s'"), *Call->GetString(TEXT("machine"))), TEXT("add one: statemachine_add"));
				return;
			}
			const FString Name = Call->GetString(TEXT("name"));
			if (FindState(M, Name))
			{
				Call->Fail(TEXT("anim.exists"), FString::Printf(TEXT("state '%s' exists"), *Name));
				return;
			}
			B->Modify();
			M->Modify();
			int32 Count = 0;
			for (UEdGraphNode* N : M->Nodes) Count += Cast<UAnimStateNode>(N) ? 1 : 0;
			FGraphNodeCreator<UAnimStateNode> Creator(*M);
			UAnimStateNode* State = Creator.CreateNode(false);
			State->NodePosX = 300 * (Count + 1);
			Creator.Finalize();
			State->OnRenameNode(Name);
			if (Count == 0 && M->EntryNode)
			{
				M->GetSchema()->TryCreateConnection(FirstPin(M->EntryNode, EGPD_Output), State->GetInputPin());
			}
			FString Error;
			if (Call->Has(TEXT("sequence")) && !AddSequencePlayer(State, Call->GetString(TEXT("sequence")), Error))
			{
				Call->Fail(TEXT("anim.sequence_not_found"), Error);
				return;
			}
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(B);
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("state"), State->GetStateName());
			O->SetBoolField(TEXT("entry"), Count == 0);
			Call->Succeed(O);
		});

	R.Add(TEXT("transition_add"), TEXT("anim"), TEXT("Add a transition between two states (automatic=true uses the time-remaining rule)."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"machine":{"type":"string"},"from":{"type":"string"},"to":{"type":"string"},"automatic":{"type":"boolean"}},"required":["path","machine","from","to"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UAnimBlueprint* B = RequireAnimBP(Call);
			if (!B) return;
			UAnimationStateMachineGraph* M = FindMachine(B, Call->GetString(TEXT("machine")));
			UAnimStateNode* From = M ? FindState(M, Call->GetString(TEXT("from"))) : nullptr;
			UAnimStateNode* To = M ? FindState(M, Call->GetString(TEXT("to"))) : nullptr;
			if (!From || !To)
			{
				Call->Fail(TEXT("anim.state_not_found"), TEXT("machine, from or to state not found"));
				return;
			}
			B->Modify();
			M->Modify();
			FGraphNodeCreator<UAnimStateTransitionNode> Creator(*M);
			UAnimStateTransitionNode* T = Creator.CreateNode(false);
			Creator.Finalize();
			T->CreateConnections(From, To);
			T->bAutomaticRuleBasedOnSequencePlayerInState = Call->GetBool(TEXT("automatic"));
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(B);
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("transition"), FString::Printf(TEXT("%s->%s"), *From->GetStateName(), *To->GetStateName()));
			Call->Succeed(O);
		});

	R.Add(TEXT("sequence_player_add"), TEXT("anim"), TEXT("Play an animation sequence in an existing state (wired to the state result)."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"machine":{"type":"string"},"state":{"type":"string"},"sequence":{"type":"string"}},"required":["path","machine","state","sequence"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UAnimBlueprint* B = RequireAnimBP(Call);
			if (!B) return;
			UAnimationStateMachineGraph* M = FindMachine(B, Call->GetString(TEXT("machine")));
			UAnimStateNode* S = M ? FindState(M, Call->GetString(TEXT("state"))) : nullptr;
			if (!S)
			{
				Call->Fail(TEXT("anim.state_not_found"), TEXT("machine or state not found"));
				return;
			}
			B->Modify();
			S->BoundGraph->Modify();
			FString Error;
			if (!AddSequencePlayer(S, Call->GetString(TEXT("sequence")), Error))
			{
				Call->Fail(TEXT("anim.sequence_not_found"), Error);
				return;
			}
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(B);
			Call->Succeed(MakeShared<FJsonObject>());
		});
}
