#include "MonolithPCGModule.h"
#include "MonolithPCGActions.h"
#include "MonolithToolRegistry.h"
#include "MonolithJsonUtils.h"
#include "MonolithSettings.h"

#define LOCTEXT_NAMESPACE "FMonolithPCGModule"

void FMonolithPCGModule::StartupModule()
{
	if (!GetDefault<UMonolithSettings>()->bEnablePCG) return;
	FMonolithPCGActions::RegisterActions(FMonolithToolRegistry::Get());
	UE_LOG(LogMonolith, Log, TEXT("Monolith — PCG module loaded (22 actions)"));
}

void FMonolithPCGModule::ShutdownModule()
{
	FMonolithToolRegistry::Get().UnregisterNamespace(TEXT("pcg"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMonolithPCGModule, MonolithPCG)
