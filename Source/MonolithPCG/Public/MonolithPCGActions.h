#pragma once

#include "CoreMinimal.h"
#include "MonolithToolRegistry.h"

class UPCGGraph;
class UPCGNode;
class UPCGSettings;

/**
 * PCG domain action handlers for Monolith.
 * Tier 1: Graph CRUD, node management, edge management, property reflection (8 actions).
 * Tier 2: Graph parameters, node positioning, settings enumeration, pin info (6 actions).
 * Tier 3: Execution & world integration (5 actions).
 * Tier 4: Templates — high-level graph creation (3 actions).
 */
class FMonolithPCGActions
{
public:
	/** Register all PCG actions with the tool registry */
	static void RegisterActions(FMonolithToolRegistry& Registry);

	// --- Graph CRUD (2) ---
	static FMonolithActionResult HandleCreateGraph(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleGetGraph(const TSharedPtr<FJsonObject>& Params);

	// --- Node Management (2) ---
	static FMonolithActionResult HandleAddNode(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleRemoveNode(const TSharedPtr<FJsonObject>& Params);

	// --- Edge Management (2) ---
	static FMonolithActionResult HandleConnectNodes(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleDisconnectNodes(const TSharedPtr<FJsonObject>& Params);

	// --- Property Reflection (2) ---
	static FMonolithActionResult HandleGetNodeSettings(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleSetNodeProperty(const TSharedPtr<FJsonObject>& Params);

	// --- Graph Parameters (3) ---
	static FMonolithActionResult HandleGetGraphParameters(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleSetGraphParameter(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleRemoveGraphParameter(const TSharedPtr<FJsonObject>& Params);

	// --- Layout & Enumeration (3) ---
	static FMonolithActionResult HandleSetNodePosition(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleListSettingsClasses(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleGetPinInfo(const TSharedPtr<FJsonObject>& Params);

	// --- Execution & World Integration (5) ---
	static FMonolithActionResult HandleListPCGComponents(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleAssignGraph(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleGenerate(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleCleanup(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleGetGenerationResults(const TSharedPtr<FJsonObject>& Params);

	// --- Templates (3) ---
	static FMonolithActionResult HandleCreateScatterGraph(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleCreateSplineGraph(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleCloneGraph(const TSharedPtr<FJsonObject>& Params);

private:
	/** Load a PCG graph asset by path */
	static UPCGGraph* LoadGraph(const FString& AssetPath);

	/** Find a node in a graph by name */
	static UPCGNode* FindNodeByName(UPCGGraph* Graph, const FString& NodeName);

	/** Resolve a short settings class name (e.g. "SurfaceSampler") to a UClass */
	static UClass* ResolveSettingsClass(const FString& ClassName);

	/** Serialize a node to JSON (id, type, position, pins) */
	static TSharedPtr<FJsonObject> SerializeNode(UPCGNode* Node);

	/** Serialize pin information for a node's settings */
	static TArray<TSharedPtr<FJsonValue>> SerializePins(UPCGNode* Node, bool bInputs);
};
