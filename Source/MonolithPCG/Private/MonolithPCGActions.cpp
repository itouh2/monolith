#include "MonolithPCGActions.h"
#include "MonolithAssetUtils.h"
#include "MonolithParamSchema.h"

#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGSettings.h"
#include "PCGComponent.h"
#include "PCGPin.h"
#include "PCGModule.h"
#include "PCGEdge.h"
#include "PCGCommon.h"

#include "StructUtils/PropertyBag.h"
#include "PCGManagedResource.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EngineUtils.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectIterator.h"
#include "Editor.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

DEFINE_LOG_CATEGORY_STATIC(LogMonolithPCG, Log, All);

// ============================================================================
// Local Helpers
// ============================================================================

static FMonolithActionResult SuccessStr(const FString& Msg)
{
	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetStringField(TEXT("result"), Msg);
	return FMonolithActionResult::Success(R);
}

static FMonolithActionResult SuccessObj(const TSharedPtr<FJsonObject>& Obj)
{
	return FMonolithActionResult::Success(Obj);
}

static FString GetAssetPath(const TSharedPtr<FJsonObject>& Params)
{
	return Params->GetStringField(TEXT("asset_path"));
}

// ============================================================================
// Internal Helpers
// ============================================================================

UPCGGraph* FMonolithPCGActions::LoadGraph(const FString& AssetPath)
{
	return FMonolithAssetUtils::LoadAssetByPath<UPCGGraph>(AssetPath);
}

UPCGNode* FMonolithPCGActions::FindNodeByName(UPCGGraph* Graph, const FString& NodeName)
{
	if (!Graph) return nullptr;

	UPCGNode* FoundNode = nullptr;
	Graph->ForEachNode([&](UPCGNode* Node) -> bool
	{
		if (Node && Node->GetName() == NodeName)
		{
			FoundNode = Node;
			return false; // stop iteration
		}
		return true; // continue
	});
	return FoundNode;
}

UClass* FMonolithPCGActions::ResolveSettingsClass(const FString& ClassName)
{
	// Try exact name first
	UClass* FoundClass = FindFirstObjectSafe<UClass>(*ClassName);
	if (FoundClass && FoundClass->IsChildOf(UPCGSettings::StaticClass()))
	{
		return FoundClass;
	}

	// Try with UPCG prefix and Settings suffix: "SurfaceSampler" -> "UPCGSurfaceSamplerSettings"
	FString NormalizedName = ClassName;

	// Strip leading 'U' if present
	if (NormalizedName.StartsWith(TEXT("U")))
	{
		NormalizedName.RightChopInline(1);
	}

	// Strip "PCG" prefix if present
	if (NormalizedName.StartsWith(TEXT("PCG")))
	{
		NormalizedName.RightChopInline(3);
	}

	// Strip "Settings" suffix if present
	if (NormalizedName.EndsWith(TEXT("Settings")))
	{
		NormalizedName.LeftChopInline(8);
	}

	// Now try: UPCG{NormalizedName}Settings
	FString FullName = FString::Printf(TEXT("PCG%sSettings"), *NormalizedName);
	FoundClass = FindFirstObjectSafe<UClass>(*FullName);
	if (FoundClass && FoundClass->IsChildOf(UPCGSettings::StaticClass()))
	{
		return FoundClass;
	}

	// Try with 'U' prefix for FindFirstObjectSafe
	FString UPrefixed = FString::Printf(TEXT("UPCG%sSettings"), *NormalizedName);
	FoundClass = FindFirstObjectSafe<UClass>(*UPrefixed);
	if (FoundClass && FoundClass->IsChildOf(UPCGSettings::StaticClass()))
	{
		return FoundClass;
	}

	// Brute force: iterate all UPCGSettings subclasses
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->IsChildOf(UPCGSettings::StaticClass()) && !It->HasAnyClassFlags(CLASS_Abstract))
		{
			FString Name = It->GetName();
			// Check if the class name contains the search term
			if (Name.Contains(NormalizedName, ESearchCase::IgnoreCase))
			{
				return *It;
			}
		}
	}

	return nullptr;
}

TSharedPtr<FJsonObject> FMonolithPCGActions::SerializeNode(UPCGNode* Node)
{
	TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
	if (!Node) return NodeObj;

	NodeObj->SetStringField(TEXT("node_id"), Node->GetName());

	const UPCGSettings* Settings = Node->GetSettings();
	if (Settings)
	{
		NodeObj->SetStringField(TEXT("settings_class"), Settings->GetClass()->GetName());
		NodeObj->SetStringField(TEXT("description"), Settings->GetClass()->GetDescription());
	}
	else
	{
		NodeObj->SetStringField(TEXT("settings_class"), TEXT("None"));
	}

#if WITH_EDITOR
	// Node position in editor graph
	int32 PosX = 0, PosY = 0;
	Node->GetNodePosition(PosX, PosY);
	NodeObj->SetNumberField(TEXT("position_x"), PosX);
	NodeObj->SetNumberField(TEXT("position_y"), PosY);
#endif

	// Serialize input and output pins
	NodeObj->SetField(TEXT("input_pins"), MakeShared<FJsonValueArray>(SerializePins(Node, true)));
	NodeObj->SetField(TEXT("output_pins"), MakeShared<FJsonValueArray>(SerializePins(Node, false)));

	return NodeObj;
}

TArray<TSharedPtr<FJsonValue>> FMonolithPCGActions::SerializePins(UPCGNode* Node, bool bInputs)
{
	TArray<TSharedPtr<FJsonValue>> PinArray;
	if (!Node) return PinArray;

	const TArray<TObjectPtr<UPCGPin>>& Pins = bInputs ? Node->GetInputPins() : Node->GetOutputPins();
	for (const UPCGPin* Pin : Pins)
	{
		if (!Pin) continue;

		TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
		PinObj->SetStringField(TEXT("label"), Pin->Properties.Label.ToString());
		PinObj->SetBoolField(TEXT("is_advanced"), Pin->Properties.IsAdvancedPin());
		PinObj->SetBoolField(TEXT("allow_multiple_connections"), Pin->AllowsMultipleConnections());
		PinObj->SetBoolField(TEXT("allow_multiple_data"), Pin->Properties.bAllowMultipleData);
		PinObj->SetBoolField(TEXT("is_connected"), Pin->IsConnected());
		PinObj->SetNumberField(TEXT("num_edges"), Pin->EdgeCount());

#if WITH_EDITORONLY_DATA
		FString Tooltip = Pin->GetTooltip().ToString();
		if (!Tooltip.IsEmpty())
		{
			PinObj->SetStringField(TEXT("tooltip"), Tooltip);
		}
#endif

		PinArray.Add(MakeShared<FJsonValueObject>(PinObj));
	}

	return PinArray;
}

// ============================================================================
// Registration
// ============================================================================

void FMonolithPCGActions::RegisterActions(FMonolithToolRegistry& Registry)
{
	// Graph CRUD (2)
	Registry.RegisterAction(TEXT("pcg"), TEXT("create_graph"), TEXT("Create a new empty PCG graph asset"),
		FMonolithActionHandler::CreateStatic(&HandleCreateGraph),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Package path (e.g. /Game/PCG)"))
			.Required(TEXT("asset_name"), TEXT("string"), TEXT("Name for the new graph asset"))
			.Build());

	Registry.RegisterAction(TEXT("pcg"), TEXT("get_graph"), TEXT("Get full graph structure: all nodes, edges, and pins"),
		FMonolithActionHandler::CreateStatic(&HandleGetGraph),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("PCG graph asset path"))
			.Build());

	// Node Management (2)
	Registry.RegisterAction(TEXT("pcg"), TEXT("add_node"), TEXT("Add a node to a PCG graph by settings class name"),
		FMonolithActionHandler::CreateStatic(&HandleAddNode),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("PCG graph asset path"))
			.Required(TEXT("settings_class"), TEXT("string"), TEXT("Settings class name (e.g. SurfaceSampler, StaticMeshSpawner, DensityFilter)"))
			.Optional(TEXT("position_x"), TEXT("number"), TEXT("X position in graph editor"), TEXT("0"))
			.Optional(TEXT("position_y"), TEXT("number"), TEXT("Y position in graph editor"), TEXT("0"))
			.Build());

	Registry.RegisterAction(TEXT("pcg"), TEXT("remove_node"), TEXT("Remove a node from a PCG graph"),
		FMonolithActionHandler::CreateStatic(&HandleRemoveNode),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("PCG graph asset path"))
			.Required(TEXT("node_id"), TEXT("string"), TEXT("Node name/ID to remove"))
			.Build());

	// Edge Management (2)
	Registry.RegisterAction(TEXT("pcg"), TEXT("connect_nodes"), TEXT("Connect an output pin to an input pin between two nodes"),
		FMonolithActionHandler::CreateStatic(&HandleConnectNodes),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("PCG graph asset path"))
			.Required(TEXT("from_node"), TEXT("string"), TEXT("Source node name/ID"))
			.Required(TEXT("from_pin"), TEXT("string"), TEXT("Source output pin label (e.g. Out)"))
			.Required(TEXT("to_node"), TEXT("string"), TEXT("Target node name/ID"))
			.Required(TEXT("to_pin"), TEXT("string"), TEXT("Target input pin label (e.g. In)"))
			.Build());

	Registry.RegisterAction(TEXT("pcg"), TEXT("disconnect_nodes"), TEXT("Remove an edge between two node pins"),
		FMonolithActionHandler::CreateStatic(&HandleDisconnectNodes),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("PCG graph asset path"))
			.Required(TEXT("from_node"), TEXT("string"), TEXT("Source node name/ID"))
			.Required(TEXT("from_pin"), TEXT("string"), TEXT("Source output pin label"))
			.Required(TEXT("to_node"), TEXT("string"), TEXT("Target node name/ID"))
			.Required(TEXT("to_pin"), TEXT("string"), TEXT("Target input pin label"))
			.Build());

	// Property Reflection (2)
	Registry.RegisterAction(TEXT("pcg"), TEXT("get_node_settings"), TEXT("Get all editable properties on a node's settings"),
		FMonolithActionHandler::CreateStatic(&HandleGetNodeSettings),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("PCG graph asset path"))
			.Required(TEXT("node_id"), TEXT("string"), TEXT("Node name/ID"))
			.Build());

	Registry.RegisterAction(TEXT("pcg"), TEXT("set_node_property"), TEXT("Set a property value on a node's settings by name"),
		FMonolithActionHandler::CreateStatic(&HandleSetNodeProperty),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("PCG graph asset path"))
			.Required(TEXT("node_id"), TEXT("string"), TEXT("Node name/ID"))
			.Required(TEXT("property_name"), TEXT("string"), TEXT("UPROPERTY name to set"))
			.Required(TEXT("value"), TEXT("string"), TEXT("Value as string (uses UE ImportText for type conversion)"))
			.Build());

	// --- Tier 2: Graph Parameters (3) ---
	Registry.RegisterAction(TEXT("pcg"), TEXT("get_graph_parameters"), TEXT("List all user-defined graph parameters with types and values"),
		FMonolithActionHandler::CreateStatic(&HandleGetGraphParameters),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("PCG graph asset path"))
			.Build());

	Registry.RegisterAction(TEXT("pcg"), TEXT("set_graph_parameter"), TEXT("Create or set a graph parameter value. Creates the parameter if it does not exist."),
		FMonolithActionHandler::CreateStatic(&HandleSetGraphParameter),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("PCG graph asset path"))
			.Required(TEXT("name"), TEXT("string"), TEXT("Parameter name"))
			.Required(TEXT("value"), TEXT("string"), TEXT("Parameter value as string"))
			.Optional(TEXT("type"), TEXT("string"), TEXT("Type for new params: Bool, Byte, Int32, Int64, Float, Double, Name, String, Text"), TEXT("Double"))
			.Build());

	Registry.RegisterAction(TEXT("pcg"), TEXT("remove_graph_parameter"), TEXT("Remove a user-defined graph parameter by name"),
		FMonolithActionHandler::CreateStatic(&HandleRemoveGraphParameter),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("PCG graph asset path"))
			.Required(TEXT("name"), TEXT("string"), TEXT("Parameter name to remove"))
			.Build());

	// --- Tier 2: Layout & Enumeration (3) ---
	Registry.RegisterAction(TEXT("pcg"), TEXT("set_node_position"), TEXT("Set a node's position in the graph editor"),
		FMonolithActionHandler::CreateStatic(&HandleSetNodePosition),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("PCG graph asset path"))
			.Required(TEXT("node_id"), TEXT("string"), TEXT("Node name/ID"))
			.Required(TEXT("position_x"), TEXT("number"), TEXT("X position"))
			.Required(TEXT("position_y"), TEXT("number"), TEXT("Y position"))
			.Build());

	Registry.RegisterAction(TEXT("pcg"), TEXT("list_settings_classes"), TEXT("Enumerate all available PCG node types (UPCGSettings subclasses)"),
		FMonolithActionHandler::CreateStatic(&HandleListSettingsClasses),
		FParamSchemaBuilder()
			.Optional(TEXT("filter"), TEXT("string"), TEXT("Filter class names containing this substring"))
			.Build());

	Registry.RegisterAction(TEXT("pcg"), TEXT("get_pin_info"), TEXT("Get detailed pin information for a specific node including connections"),
		FMonolithActionHandler::CreateStatic(&HandleGetPinInfo),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("PCG graph asset path"))
			.Required(TEXT("node_id"), TEXT("string"), TEXT("Node name/ID"))
			.Build());

	// --- Tier 3: Execution & World Integration (5) ---
	Registry.RegisterAction(TEXT("pcg"), TEXT("list_pcg_components"), TEXT("Find all UPCGComponent instances in the current level with their owner actor and assigned graph"),
		FMonolithActionHandler::CreateStatic(&HandleListPCGComponents),
		FParamSchemaBuilder()
			.Optional(TEXT("filter_graph"), TEXT("string"), TEXT("Only list components assigned to graphs whose path contains this substring"))
			.Build());

	Registry.RegisterAction(TEXT("pcg"), TEXT("assign_graph"), TEXT("Set a PCG graph on a UPCGComponent by actor name or label"),
		FMonolithActionHandler::CreateStatic(&HandleAssignGraph),
		FParamSchemaBuilder()
			.Required(TEXT("actor"), TEXT("string"), TEXT("Actor name or label owning the PCG component"))
			.Required(TEXT("graph_path"), TEXT("string"), TEXT("PCG graph asset path to assign"))
			.Optional(TEXT("component_index"), TEXT("number"), TEXT("Component index if actor has multiple PCG components"), TEXT("0"))
			.Build());

	Registry.RegisterAction(TEXT("pcg"), TEXT("generate"), TEXT("Trigger PCG generation on a component"),
		FMonolithActionHandler::CreateStatic(&HandleGenerate),
		FParamSchemaBuilder()
			.Required(TEXT("actor"), TEXT("string"), TEXT("Actor name or label owning the PCG component"))
			.Optional(TEXT("component_index"), TEXT("number"), TEXT("Component index if actor has multiple"), TEXT("0"))
			.Optional(TEXT("force"), TEXT("boolean"), TEXT("Force regeneration even if not dirty"), TEXT("true"))
			.Build());

	Registry.RegisterAction(TEXT("pcg"), TEXT("cleanup"), TEXT("Clear PCG generation results on a component"),
		FMonolithActionHandler::CreateStatic(&HandleCleanup),
		FParamSchemaBuilder()
			.Required(TEXT("actor"), TEXT("string"), TEXT("Actor name or label owning the PCG component"))
			.Optional(TEXT("component_index"), TEXT("number"), TEXT("Component index if actor has multiple"), TEXT("0"))
			.Optional(TEXT("remove_components"), TEXT("boolean"), TEXT("Also remove generated components"), TEXT("true"))
			.Build());

	Registry.RegisterAction(TEXT("pcg"), TEXT("get_generation_results"), TEXT("Inspect managed resources (spawned actors, ISMs, components) from a PCG component"),
		FMonolithActionHandler::CreateStatic(&HandleGetGenerationResults),
		FParamSchemaBuilder()
			.Required(TEXT("actor"), TEXT("string"), TEXT("Actor name or label owning the PCG component"))
			.Optional(TEXT("component_index"), TEXT("number"), TEXT("Component index if actor has multiple"), TEXT("0"))
			.Build());

	// --- Tier 4: Templates (3) ---
	Registry.RegisterAction(TEXT("pcg"), TEXT("create_scatter_graph"), TEXT("High-level template: creates a surface sampler → density filter → static mesh spawner graph, fully wired"),
		FMonolithActionHandler::CreateStatic(&HandleCreateScatterGraph),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Package path (e.g. /Game/PCG)"))
			.Required(TEXT("asset_name"), TEXT("string"), TEXT("Name for the new graph asset"))
			.Optional(TEXT("mesh_path"), TEXT("string"), TEXT("Static mesh asset path for the spawner (can be set later via set_node_property)"))
			.Optional(TEXT("points_per_sqm"), TEXT("number"), TEXT("Surface sampler points per square meter"), TEXT("1.0"))
			.Build());

	Registry.RegisterAction(TEXT("pcg"), TEXT("create_spline_graph"), TEXT("High-level template: creates a spline sampler → copy points → static mesh spawner graph, fully wired"),
		FMonolithActionHandler::CreateStatic(&HandleCreateSplineGraph),
		FParamSchemaBuilder()
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Package path (e.g. /Game/PCG)"))
			.Required(TEXT("asset_name"), TEXT("string"), TEXT("Name for the new graph asset"))
			.Optional(TEXT("mesh_path"), TEXT("string"), TEXT("Static mesh asset path for the spawner"))
			.Build());

	Registry.RegisterAction(TEXT("pcg"), TEXT("clone_graph"), TEXT("Deep copy an existing PCG graph asset to a new path"),
		FMonolithActionHandler::CreateStatic(&HandleCloneGraph),
		FParamSchemaBuilder()
			.Required(TEXT("source_path"), TEXT("string"), TEXT("Source PCG graph asset path"))
			.Required(TEXT("dest_path"), TEXT("string"), TEXT("Destination package path"))
			.Required(TEXT("dest_name"), TEXT("string"), TEXT("Name for the cloned graph"))
			.Build());
}

// ============================================================================
// Graph CRUD
// ============================================================================

FMonolithActionResult FMonolithPCGActions::HandleCreateGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString PackagePath = Params->GetStringField(TEXT("asset_path"));
	FString AssetName = Params->GetStringField(TEXT("asset_name"));

	if (PackagePath.IsEmpty() || AssetName.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("asset_path and asset_name are required"));
	}

	FString FullPath = PackagePath / AssetName;

	// Check if asset already exists
	if (FMonolithAssetUtils::AssetExists(FullPath))
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Asset already exists at %s"), *FullPath));
	}

	UPackage* Pkg = CreatePackage(*FullPath);
	if (!Pkg)
	{
		return FMonolithActionResult::Error(TEXT("Failed to create package"));
	}

	UPCGGraph* Graph = NewObject<UPCGGraph>(Pkg, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
	if (!Graph)
	{
		return FMonolithActionResult::Error(TEXT("Failed to create PCG graph"));
	}

	FAssetRegistryModule::AssetCreated(Graph);
	Pkg->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), Graph->GetPathName());
	Result->SetBoolField(TEXT("success"), true);
	return SuccessObj(Result);
}

FMonolithActionResult FMonolithPCGActions::HandleGetGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = GetAssetPath(Params);
	if (AssetPath.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("asset_path is required"));
	}

	UPCGGraph* Graph = LoadGraph(AssetPath);
	if (!Graph)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load PCG graph at %s"), *AssetPath));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), Graph->GetPathName());

	// Collect all nodes
	TArray<TSharedPtr<FJsonValue>> NodeArray;
	TArray<TSharedPtr<FJsonValue>> EdgeArray;

	Graph->ForEachNode([&](UPCGNode* Node) -> bool
	{
		if (!Node) return true;

		NodeArray.Add(MakeShared<FJsonValueObject>(SerializeNode(Node)));

		// Collect edges from this node's output pins
		for (const UPCGPin* OutPin : Node->GetOutputPins())
		{
			if (!OutPin) continue;

			// Walk the Edges array on the pin to find connected pins
			for (const UPCGEdge* Edge : OutPin->Edges)
			{
				if (!Edge || !Edge->IsValid()) continue;

				const UPCGPin* OtherPin = Edge->GetOtherPin(OutPin);
				if (!OtherPin || !OtherPin->Node) continue;

				TSharedPtr<FJsonObject> EdgeObj = MakeShared<FJsonObject>();
				EdgeObj->SetStringField(TEXT("from_node"), Node->GetName());
				EdgeObj->SetStringField(TEXT("from_pin"), OutPin->Properties.Label.ToString());
				EdgeObj->SetStringField(TEXT("to_node"), OtherPin->Node->GetName());
				EdgeObj->SetStringField(TEXT("to_pin"), OtherPin->Properties.Label.ToString());
				EdgeArray.Add(MakeShared<FJsonValueObject>(EdgeObj));
			}
		}

		return true; // continue iteration
	});

	Result->SetArrayField(TEXT("nodes"), NodeArray);
	Result->SetArrayField(TEXT("edges"), EdgeArray);

	// Input/Output node info
	if (UPCGNode* InputNode = Graph->GetInputNode())
	{
		Result->SetStringField(TEXT("input_node"), InputNode->GetName());
	}
	if (UPCGNode* OutputNode = Graph->GetOutputNode())
	{
		Result->SetStringField(TEXT("output_node"), OutputNode->GetName());
	}

	return SuccessObj(Result);
}

// ============================================================================
// Node Management
// ============================================================================

FMonolithActionResult FMonolithPCGActions::HandleAddNode(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = GetAssetPath(Params);
	FString SettingsClassName = Params->GetStringField(TEXT("settings_class"));

	if (AssetPath.IsEmpty() || SettingsClassName.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("asset_path and settings_class are required"));
	}

	UPCGGraph* Graph = LoadGraph(AssetPath);
	if (!Graph)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load PCG graph at %s"), *AssetPath));
	}

	UClass* SettingsClass = ResolveSettingsClass(SettingsClassName);
	if (!SettingsClass)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Could not resolve PCG settings class '%s'. Try full name like 'PCGSurfaceSamplerSettings'."), *SettingsClassName));
	}

	UPCGSettings* NewSettings = nullptr;
	UPCGNode* NewNode = Graph->AddNodeOfType(SettingsClass, NewSettings);

	if (!NewNode)
	{
		return FMonolithActionResult::Error(TEXT("Failed to add node to graph"));
	}

#if WITH_EDITOR
	// Set position if provided
	int32 PosX = 0, PosY = 0;
	if (Params->HasField(TEXT("position_x")))
	{
		PosX = static_cast<int32>(Params->GetNumberField(TEXT("position_x")));
	}
	if (Params->HasField(TEXT("position_y")))
	{
		PosY = static_cast<int32>(Params->GetNumberField(TEXT("position_y")));
	}
	NewNode->SetNodePosition(PosX, PosY);
#endif

	Graph->GetOuter()->MarkPackageDirty();

	// Return node info
	TSharedPtr<FJsonObject> Result = SerializeNode(NewNode);
	Result->SetBoolField(TEXT("success"), true);
	return SuccessObj(Result);
}

FMonolithActionResult FMonolithPCGActions::HandleRemoveNode(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = GetAssetPath(Params);
	FString NodeId = Params->GetStringField(TEXT("node_id"));

	if (AssetPath.IsEmpty() || NodeId.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("asset_path and node_id are required"));
	}

	UPCGGraph* Graph = LoadGraph(AssetPath);
	if (!Graph)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load PCG graph at %s"), *AssetPath));
	}

	UPCGNode* Node = FindNodeByName(Graph, NodeId);
	if (!Node)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Node '%s' not found in graph"), *NodeId));
	}

	// Don't allow removing input/output nodes
	if (Node == Graph->GetInputNode() || Node == Graph->GetOutputNode())
	{
		return FMonolithActionResult::Error(TEXT("Cannot remove built-in input/output nodes"));
	}

	Graph->RemoveNode(Node);
	Graph->GetOuter()->MarkPackageDirty();

	return SuccessStr(FString::Printf(TEXT("Node '%s' removed"), *NodeId));
}

// ============================================================================
// Edge Management
// ============================================================================

FMonolithActionResult FMonolithPCGActions::HandleConnectNodes(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = GetAssetPath(Params);
	FString FromNodeName = Params->GetStringField(TEXT("from_node"));
	FString FromPinLabel = Params->GetStringField(TEXT("from_pin"));
	FString ToNodeName = Params->GetStringField(TEXT("to_node"));
	FString ToPinLabel = Params->GetStringField(TEXT("to_pin"));

	if (AssetPath.IsEmpty() || FromNodeName.IsEmpty() || FromPinLabel.IsEmpty() ||
		ToNodeName.IsEmpty() || ToPinLabel.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("asset_path, from_node, from_pin, to_node, and to_pin are all required"));
	}

	UPCGGraph* Graph = LoadGraph(AssetPath);
	if (!Graph)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load PCG graph at %s"), *AssetPath));
	}

	UPCGNode* FromNode = FindNodeByName(Graph, FromNodeName);
	UPCGNode* ToNode = FindNodeByName(Graph, ToNodeName);

	if (!FromNode)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Source node '%s' not found"), *FromNodeName));
	}
	if (!ToNode)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Target node '%s' not found"), *ToNodeName));
	}

	UPCGNode* ResultNode = Graph->AddEdge(FromNode, FName(*FromPinLabel), ToNode, FName(*ToPinLabel));
	if (!ResultNode)
	{
		return FMonolithActionResult::Error(FString::Printf(
			TEXT("Failed to connect %s.%s -> %s.%s. Check pin labels are correct."),
			*FromNodeName, *FromPinLabel, *ToNodeName, *ToPinLabel));
	}

	Graph->GetOuter()->MarkPackageDirty();
	return SuccessStr(FString::Printf(TEXT("Connected %s.%s -> %s.%s"), *FromNodeName, *FromPinLabel, *ToNodeName, *ToPinLabel));
}

FMonolithActionResult FMonolithPCGActions::HandleDisconnectNodes(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = GetAssetPath(Params);
	FString FromNodeName = Params->GetStringField(TEXT("from_node"));
	FString FromPinLabel = Params->GetStringField(TEXT("from_pin"));
	FString ToNodeName = Params->GetStringField(TEXT("to_node"));
	FString ToPinLabel = Params->GetStringField(TEXT("to_pin"));

	if (AssetPath.IsEmpty() || FromNodeName.IsEmpty() || FromPinLabel.IsEmpty() ||
		ToNodeName.IsEmpty() || ToPinLabel.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("asset_path, from_node, from_pin, to_node, and to_pin are all required"));
	}

	UPCGGraph* Graph = LoadGraph(AssetPath);
	if (!Graph)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load PCG graph at %s"), *AssetPath));
	}

	UPCGNode* FromNode = FindNodeByName(Graph, FromNodeName);
	UPCGNode* ToNode = FindNodeByName(Graph, ToNodeName);

	if (!FromNode)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Source node '%s' not found"), *FromNodeName));
	}
	if (!ToNode)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Target node '%s' not found"), *ToNodeName));
	}

	// Use the graph-level RemoveEdge API
	bool bRemoved = Graph->RemoveEdge(FromNode, FName(*FromPinLabel), ToNode, FName(*ToPinLabel));
	if (!bRemoved)
	{
		return FMonolithActionResult::Error(FString::Printf(
			TEXT("No edge found between %s.%s and %s.%s"),
			*FromNodeName, *FromPinLabel, *ToNodeName, *ToPinLabel));
	}

	Graph->GetOuter()->MarkPackageDirty();
	return SuccessStr(FString::Printf(TEXT("Disconnected %s.%s -> %s.%s"), *FromNodeName, *FromPinLabel, *ToNodeName, *ToPinLabel));
}

// ============================================================================
// Property Reflection
// ============================================================================

FMonolithActionResult FMonolithPCGActions::HandleGetNodeSettings(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = GetAssetPath(Params);
	FString NodeId = Params->GetStringField(TEXT("node_id"));

	if (AssetPath.IsEmpty() || NodeId.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("asset_path and node_id are required"));
	}

	UPCGGraph* Graph = LoadGraph(AssetPath);
	if (!Graph)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load PCG graph at %s"), *AssetPath));
	}

	UPCGNode* Node = FindNodeByName(Graph, NodeId);
	if (!Node)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Node '%s' not found in graph"), *NodeId));
	}

	UPCGSettings* Settings = Node->GetSettings();
	if (!Settings)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Node '%s' has no settings"), *NodeId));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("node_id"), NodeId);
	Result->SetStringField(TEXT("settings_class"), Settings->GetClass()->GetName());

	TArray<TSharedPtr<FJsonValue>> PropertiesArray;

	for (TFieldIterator<FProperty> It(Settings->GetClass()); It; ++It)
	{
		FProperty* Prop = *It;

		// Only show editable properties
		if (!Prop->HasAnyPropertyFlags(CPF_Edit)) continue;

		// Skip properties from UObject base
		if (Prop->GetOwnerClass() == UObject::StaticClass()) continue;

		TSharedPtr<FJsonObject> PropObj = MakeShared<FJsonObject>();
		PropObj->SetStringField(TEXT("name"), Prop->GetName());
		PropObj->SetStringField(TEXT("type"), Prop->GetCPPType());

		// Get current value as string
		FString ValueStr;
		const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Settings);
		Prop->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, Settings, PPF_None);
		PropObj->SetStringField(TEXT("value"), ValueStr);

		// Category
		FString Category = Prop->GetMetaData(TEXT("Category"));
		if (!Category.IsEmpty())
		{
			PropObj->SetStringField(TEXT("category"), Category);
		}

		// Tooltip/description
#if WITH_EDITOR
		FString Tooltip = Prop->GetToolTipText().ToString();
		if (!Tooltip.IsEmpty())
		{
			PropObj->SetStringField(TEXT("tooltip"), Tooltip);
		}
#endif

		// Flags
		PropObj->SetBoolField(TEXT("is_overridable"), Prop->HasMetaData(TEXT("PCG_Overridable")));
		PropObj->SetBoolField(TEXT("is_instance_editable"), Prop->HasAnyPropertyFlags(CPF_Edit));

		PropertiesArray.Add(MakeShared<FJsonValueObject>(PropObj));
	}

	Result->SetArrayField(TEXT("properties"), PropertiesArray);
	return SuccessObj(Result);
}

FMonolithActionResult FMonolithPCGActions::HandleSetNodeProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = GetAssetPath(Params);
	FString NodeId = Params->GetStringField(TEXT("node_id"));
	FString PropertyName = Params->GetStringField(TEXT("property_name"));
	FString Value = Params->GetStringField(TEXT("value"));

	if (AssetPath.IsEmpty() || NodeId.IsEmpty() || PropertyName.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("asset_path, node_id, and property_name are required"));
	}

	UPCGGraph* Graph = LoadGraph(AssetPath);
	if (!Graph)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load PCG graph at %s"), *AssetPath));
	}

	UPCGNode* Node = FindNodeByName(Graph, NodeId);
	if (!Node)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Node '%s' not found in graph"), *NodeId));
	}

	UPCGSettings* Settings = Node->GetSettings();
	if (!Settings)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Node '%s' has no settings"), *NodeId));
	}

	// Find the property by name
	FProperty* TargetProp = nullptr;
	for (TFieldIterator<FProperty> It(Settings->GetClass()); It; ++It)
	{
		if (It->GetName() == PropertyName)
		{
			TargetProp = *It;
			break;
		}
	}

	if (!TargetProp)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Property '%s' not found on %s"), *PropertyName, *Settings->GetClass()->GetName()));
	}

	// Import the value
	void* ValuePtr = TargetProp->ContainerPtrToValuePtr<void>(Settings);
	const TCHAR* ImportResult = TargetProp->ImportText_Direct(*Value, ValuePtr, Settings, PPF_None);

	if (!ImportResult)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to set '%s' to '%s'. Check value format for type %s."), *PropertyName, *Value, *TargetProp->GetCPPType()));
	}

	Graph->GetOuter()->MarkPackageDirty();

	// Read back the value to confirm
	FString NewValueStr;
	TargetProp->ExportTextItem_Direct(NewValueStr, ValuePtr, nullptr, Settings, PPF_None);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("property_name"), PropertyName);
	Result->SetStringField(TEXT("new_value"), NewValueStr);
	return SuccessObj(Result);
}

// ============================================================================
// Tier 2 — Graph Parameters
// ============================================================================

FMonolithActionResult FMonolithPCGActions::HandleGetGraphParameters(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = GetAssetPath(Params);
	if (AssetPath.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("asset_path is required"));
	}

	UPCGGraph* Graph = LoadGraph(AssetPath);
	if (!Graph)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load PCG graph at %s"), *AssetPath));
	}

	const FInstancedPropertyBag* UserParams = Graph->GetUserParametersStruct();
	TArray<TSharedPtr<FJsonValue>> ParamsArray;

	if (UserParams && UserParams->GetPropertyBagStruct())
	{
		TConstArrayView<FPropertyBagPropertyDesc> Descs = UserParams->GetPropertyBagStruct()->GetPropertyDescs();
		for (const FPropertyBagPropertyDesc& Desc : Descs)
		{
			TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
			ParamObj->SetStringField(TEXT("name"), Desc.Name.ToString());
			ParamObj->SetStringField(TEXT("type"), UEnum::GetValueAsString(Desc.ValueType));

			// Get current value as string
			TValueOrError<FString, EPropertyBagResult> ValueResult = UserParams->GetValueSerializedString(Desc.Name);
			if (ValueResult.HasValue())
			{
				ParamObj->SetStringField(TEXT("value"), ValueResult.GetValue());
			}
			else
			{
				ParamObj->SetStringField(TEXT("value"), TEXT("(unreadable)"));
			}

			ParamsArray.Add(MakeShared<FJsonValueObject>(ParamObj));
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetNumberField(TEXT("count"), ParamsArray.Num());
	Result->SetArrayField(TEXT("parameters"), ParamsArray);
	return SuccessObj(Result);
}

FMonolithActionResult FMonolithPCGActions::HandleSetGraphParameter(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = GetAssetPath(Params);
	FString ParamName = Params->GetStringField(TEXT("name"));
	FString Value = Params->GetStringField(TEXT("value"));
	FString TypeStr = Params->GetStringField(TEXT("type"));

	if (AssetPath.IsEmpty() || ParamName.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("asset_path and name are required"));
	}

	UPCGGraph* Graph = LoadGraph(AssetPath);
	if (!Graph)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load PCG graph at %s"), *AssetPath));
	}

	FName PropName(*ParamName);

	// Check if parameter already exists
	const FInstancedPropertyBag* UserParams = Graph->GetUserParametersStruct();
	bool bExists = false;
	if (UserParams && UserParams->GetPropertyBagStruct())
	{
		for (const FPropertyBagPropertyDesc& Desc : UserParams->GetPropertyBagStruct()->GetPropertyDescs())
		{
			if (Desc.Name == PropName)
			{
				bExists = true;
				break;
			}
		}
	}

	// If parameter doesn't exist, create it
	if (!bExists)
	{
		// Resolve type string to EPropertyBagPropertyType
		EPropertyBagPropertyType BagType = EPropertyBagPropertyType::Double; // default

		if (!TypeStr.IsEmpty())
		{
			if (TypeStr.Equals(TEXT("Bool"), ESearchCase::IgnoreCase)) BagType = EPropertyBagPropertyType::Bool;
			else if (TypeStr.Equals(TEXT("Byte"), ESearchCase::IgnoreCase)) BagType = EPropertyBagPropertyType::Byte;
			else if (TypeStr.Equals(TEXT("Int32"), ESearchCase::IgnoreCase) || TypeStr.Equals(TEXT("Int"), ESearchCase::IgnoreCase)) BagType = EPropertyBagPropertyType::Int32;
			else if (TypeStr.Equals(TEXT("Int64"), ESearchCase::IgnoreCase)) BagType = EPropertyBagPropertyType::Int64;
			else if (TypeStr.Equals(TEXT("Float"), ESearchCase::IgnoreCase)) BagType = EPropertyBagPropertyType::Float;
			else if (TypeStr.Equals(TEXT("Double"), ESearchCase::IgnoreCase)) BagType = EPropertyBagPropertyType::Double;
			else if (TypeStr.Equals(TEXT("Name"), ESearchCase::IgnoreCase)) BagType = EPropertyBagPropertyType::Name;
			else if (TypeStr.Equals(TEXT("String"), ESearchCase::IgnoreCase)) BagType = EPropertyBagPropertyType::String;
			else if (TypeStr.Equals(TEXT("Text"), ESearchCase::IgnoreCase)) BagType = EPropertyBagPropertyType::Text;
		}

		TArray<FPropertyBagPropertyDesc> NewDescs;
		NewDescs.Add(FPropertyBagPropertyDesc(PropName, BagType));
		EPropertyBagAlterationResult AddResult = Graph->AddUserParameters(NewDescs);
		if (AddResult != EPropertyBagAlterationResult::Success)
		{
			return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to create parameter '%s'"), *ParamName));
		}
	}

	// Set the value via UpdateUserParametersStruct for proper change propagation
	bool bSetOk = false;
	Graph->UpdateUserParametersStruct([&](FInstancedPropertyBag& Bag)
	{
		EPropertyBagResult SetResult = Bag.SetValueSerializedString(PropName, Value);
		bSetOk = (SetResult == EPropertyBagResult::Success);
	});

	if (!bSetOk)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to set value '%s' on parameter '%s'. Check type compatibility."), *Value, *ParamName));
	}

	Graph->GetOuter()->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("name"), ParamName);
	Result->SetBoolField(TEXT("created"), !bExists);
	return SuccessObj(Result);
}

FMonolithActionResult FMonolithPCGActions::HandleRemoveGraphParameter(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = GetAssetPath(Params);
	FString ParamName = Params->GetStringField(TEXT("name"));

	if (AssetPath.IsEmpty() || ParamName.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("asset_path and name are required"));
	}

	UPCGGraph* Graph = LoadGraph(AssetPath);
	if (!Graph)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load PCG graph at %s"), *AssetPath));
	}

	FName PropName(*ParamName);

	// Remove via UpdateUserParametersStruct for proper change propagation
	bool bRemoved = false;
	Graph->UpdateUserParametersStruct([&](FInstancedPropertyBag& Bag)
	{
		EPropertyBagAlterationResult RemoveResult = Bag.RemovePropertyByName(PropName);
		bRemoved = (RemoveResult == EPropertyBagAlterationResult::Success);
	});

	if (!bRemoved)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Parameter '%s' not found or could not be removed"), *ParamName));
	}

	Graph->GetOuter()->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("removed"), ParamName);
	return SuccessObj(Result);
}

// ============================================================================
// Tier 2 — Layout & Enumeration
// ============================================================================

FMonolithActionResult FMonolithPCGActions::HandleSetNodePosition(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = GetAssetPath(Params);
	FString NodeId = Params->GetStringField(TEXT("node_id"));

	if (AssetPath.IsEmpty() || NodeId.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("asset_path and node_id are required"));
	}

	UPCGGraph* Graph = LoadGraph(AssetPath);
	if (!Graph)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load PCG graph at %s"), *AssetPath));
	}

	UPCGNode* Node = FindNodeByName(Graph, NodeId);
	if (!Node)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Node '%s' not found in graph"), *NodeId));
	}

	int32 PosX = static_cast<int32>(Params->GetNumberField(TEXT("position_x")));
	int32 PosY = static_cast<int32>(Params->GetNumberField(TEXT("position_y")));

	Node->SetNodePosition(PosX, PosY);
	Graph->GetOuter()->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("node_id"), NodeId);
	Result->SetNumberField(TEXT("position_x"), PosX);
	Result->SetNumberField(TEXT("position_y"), PosY);
	return SuccessObj(Result);
}

FMonolithActionResult FMonolithPCGActions::HandleListSettingsClasses(const TSharedPtr<FJsonObject>& Params)
{
	FString Filter = Params->GetStringField(TEXT("filter"));

	TArray<TSharedPtr<FJsonValue>> ClassesArray;

	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (!It->IsChildOf(UPCGSettings::StaticClass()) || It->HasAnyClassFlags(CLASS_Abstract))
		{
			continue;
		}

		FString ClassName = It->GetName();

		// Apply filter if provided
		if (!Filter.IsEmpty() && !ClassName.Contains(Filter, ESearchCase::IgnoreCase))
		{
			continue;
		}

		TSharedPtr<FJsonObject> ClassObj = MakeShared<FJsonObject>();
		ClassObj->SetStringField(TEXT("class_name"), ClassName);

		// Generate a short name by stripping PCG prefix and Settings suffix
		FString ShortName = ClassName;
		if (ShortName.StartsWith(TEXT("PCG")))
		{
			ShortName.RightChopInline(3);
		}
		if (ShortName.EndsWith(TEXT("Settings")))
		{
			ShortName.LeftChopInline(8);
		}
		ClassObj->SetStringField(TEXT("short_name"), ShortName);

#if WITH_EDITOR
		FString Description = It->GetDescription();
		if (!Description.IsEmpty())
		{
			ClassObj->SetStringField(TEXT("description"), Description);
		}
#endif

		ClassesArray.Add(MakeShared<FJsonValueObject>(ClassObj));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("count"), ClassesArray.Num());
	Result->SetArrayField(TEXT("classes"), ClassesArray);
	return SuccessObj(Result);
}

FMonolithActionResult FMonolithPCGActions::HandleGetPinInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = GetAssetPath(Params);
	FString NodeId = Params->GetStringField(TEXT("node_id"));

	if (AssetPath.IsEmpty() || NodeId.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("asset_path and node_id are required"));
	}

	UPCGGraph* Graph = LoadGraph(AssetPath);
	if (!Graph)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load PCG graph at %s"), *AssetPath));
	}

	UPCGNode* Node = FindNodeByName(Graph, NodeId);
	if (!Node)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Node '%s' not found in graph"), *NodeId));
	}

	auto SerializeDetailedPins = [](const TArray<TObjectPtr<UPCGPin>>& Pins) -> TArray<TSharedPtr<FJsonValue>>
	{
		TArray<TSharedPtr<FJsonValue>> PinArray;
		for (const UPCGPin* Pin : Pins)
		{
			if (!Pin) continue;

			TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
			PinObj->SetStringField(TEXT("label"), Pin->Properties.Label.ToString());
			PinObj->SetBoolField(TEXT("is_advanced"), Pin->Properties.IsAdvancedPin());
			PinObj->SetBoolField(TEXT("allow_multiple_connections"), Pin->AllowsMultipleConnections());
			PinObj->SetBoolField(TEXT("allow_multiple_data"), Pin->Properties.bAllowMultipleData);
			PinObj->SetBoolField(TEXT("is_connected"), Pin->IsConnected());
			PinObj->SetNumberField(TEXT("num_edges"), Pin->EdgeCount());

			// List connected nodes
			TArray<TSharedPtr<FJsonValue>> ConnectedArray;
			for (const UPCGEdge* Edge : Pin->Edges)
			{
				if (!Edge) continue;
				const UPCGPin* OtherPin = Edge->GetOtherPin(Pin);
				if (!OtherPin || !OtherPin->Node) continue;

				TSharedPtr<FJsonObject> ConnObj = MakeShared<FJsonObject>();
				ConnObj->SetStringField(TEXT("node_id"), OtherPin->Node->GetName());
				ConnObj->SetStringField(TEXT("pin_label"), OtherPin->Properties.Label.ToString());
				ConnectedArray.Add(MakeShared<FJsonValueObject>(ConnObj));
			}
			PinObj->SetArrayField(TEXT("connections"), ConnectedArray);

#if WITH_EDITORONLY_DATA
			FString Tooltip = Pin->GetTooltip().ToString();
			if (!Tooltip.IsEmpty())
			{
				PinObj->SetStringField(TEXT("tooltip"), Tooltip);
			}
#endif

			PinArray.Add(MakeShared<FJsonValueObject>(PinObj));
		}
		return PinArray;
	};

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("node_id"), NodeId);
	Result->SetField(TEXT("input_pins"), MakeShared<FJsonValueArray>(SerializeDetailedPins(Node->GetInputPins())));
	Result->SetField(TEXT("output_pins"), MakeShared<FJsonValueArray>(SerializeDetailedPins(Node->GetOutputPins())));
	return SuccessObj(Result);
}

// ============================================================================
// Tier 3 — Execution & World Integration
// ============================================================================

/** Helper: find PCG components on an actor by name/label in the current editor world */
static TArray<UPCGComponent*> FindPCGComponentsOnActor(const FString& ActorName, AActor** OutActor = nullptr)
{
	TArray<UPCGComponent*> Result;
	if (!GEditor || !GEditor->GetEditorWorldContext().World()) return Result;

	UWorld* World = GEditor->GetEditorWorldContext().World();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		// Match by name or label
		bool bMatch = Actor->GetName() == ActorName
			|| Actor->GetActorNameOrLabel() == ActorName
			|| Actor->GetActorLabel() == ActorName;

		if (!bMatch) continue;

		TArray<UPCGComponent*> Components;
		Actor->GetComponents<UPCGComponent>(Components);
		if (Components.Num() > 0)
		{
			if (OutActor) *OutActor = Actor;
			return Components;
		}
	}
	return Result;
}

FMonolithActionResult FMonolithPCGActions::HandleListPCGComponents(const TSharedPtr<FJsonObject>& Params)
{
	FString FilterGraph = Params->GetStringField(TEXT("filter_graph"));

	if (!GEditor || !GEditor->GetEditorWorldContext().World())
	{
		return FMonolithActionResult::Error(TEXT("No editor world available"));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	TArray<TSharedPtr<FJsonValue>> ComponentsArray;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		TArray<UPCGComponent*> PCGComps;
		Actor->GetComponents<UPCGComponent>(PCGComps);

		for (int32 Idx = 0; Idx < PCGComps.Num(); ++Idx)
		{
			UPCGComponent* Comp = PCGComps[Idx];
			if (!Comp) continue;

			UPCGGraph* Graph = Comp->GetGraph();
			FString GraphPath = Graph ? Graph->GetPathName() : TEXT("None");

			// Apply graph filter
			if (!FilterGraph.IsEmpty() && !GraphPath.Contains(FilterGraph, ESearchCase::IgnoreCase))
			{
				continue;
			}

			TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
			CompObj->SetStringField(TEXT("actor_name"), Actor->GetName());
			CompObj->SetStringField(TEXT("actor_label"), Actor->GetActorNameOrLabel());
			CompObj->SetNumberField(TEXT("component_index"), Idx);
			CompObj->SetStringField(TEXT("component_name"), Comp->GetName());
			CompObj->SetStringField(TEXT("graph_path"), GraphPath);
			CompObj->SetBoolField(TEXT("is_generating"), Comp->IsGenerating());

			// Transform
			FVector Loc = Actor->GetActorLocation();
			CompObj->SetStringField(TEXT("location"), FString::Printf(TEXT("%.0f, %.0f, %.0f"), Loc.X, Loc.Y, Loc.Z));

			ComponentsArray.Add(MakeShared<FJsonValueObject>(CompObj));
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("count"), ComponentsArray.Num());
	Result->SetArrayField(TEXT("components"), ComponentsArray);
	return SuccessObj(Result);
}

FMonolithActionResult FMonolithPCGActions::HandleAssignGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorName = Params->GetStringField(TEXT("actor"));
	FString GraphPath = Params->GetStringField(TEXT("graph_path"));
	int32 CompIndex = static_cast<int32>(Params->GetNumberField(TEXT("component_index")));

	if (ActorName.IsEmpty() || GraphPath.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("actor and graph_path are required"));
	}

	AActor* FoundActor = nullptr;
	TArray<UPCGComponent*> Components = FindPCGComponentsOnActor(ActorName, &FoundActor);

	if (Components.Num() == 0)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("No PCG components found on actor '%s'"), *ActorName));
	}

	if (CompIndex < 0 || CompIndex >= Components.Num())
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("component_index %d out of range (actor has %d PCG components)"), CompIndex, Components.Num()));
	}

	UPCGGraph* Graph = FMonolithAssetUtils::LoadAssetByPath<UPCGGraph>(GraphPath);
	if (!Graph)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load PCG graph at %s"), *GraphPath));
	}

	UPCGComponent* Comp = Components[CompIndex];
	Comp->SetGraph(Graph);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actor"), FoundActor->GetName());
	Result->SetStringField(TEXT("graph_path"), Graph->GetPathName());
	return SuccessObj(Result);
}

FMonolithActionResult FMonolithPCGActions::HandleGenerate(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorName = Params->GetStringField(TEXT("actor"));
	int32 CompIndex = static_cast<int32>(Params->GetNumberField(TEXT("component_index")));
	bool bForce = true;
	if (Params->HasField(TEXT("force")))
	{
		bForce = Params->GetBoolField(TEXT("force"));
	}

	if (ActorName.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("actor is required"));
	}

	AActor* FoundActor = nullptr;
	TArray<UPCGComponent*> Components = FindPCGComponentsOnActor(ActorName, &FoundActor);

	if (Components.Num() == 0)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("No PCG components found on actor '%s'"), *ActorName));
	}

	if (CompIndex < 0 || CompIndex >= Components.Num())
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("component_index %d out of range (actor has %d PCG components)"), CompIndex, Components.Num()));
	}

	UPCGComponent* Comp = Components[CompIndex];

	if (!Comp->GetGraph())
	{
		return FMonolithActionResult::Error(TEXT("PCG component has no graph assigned"));
	}

	Comp->GenerateLocal(bForce);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actor"), FoundActor->GetName());
	Result->SetStringField(TEXT("graph"), Comp->GetGraph()->GetPathName());
	Result->SetBoolField(TEXT("forced"), bForce);
	return SuccessObj(Result);
}

FMonolithActionResult FMonolithPCGActions::HandleCleanup(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorName = Params->GetStringField(TEXT("actor"));
	int32 CompIndex = static_cast<int32>(Params->GetNumberField(TEXT("component_index")));
	bool bRemoveComponents = true;
	if (Params->HasField(TEXT("remove_components")))
	{
		bRemoveComponents = Params->GetBoolField(TEXT("remove_components"));
	}

	if (ActorName.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("actor is required"));
	}

	AActor* FoundActor = nullptr;
	TArray<UPCGComponent*> Components = FindPCGComponentsOnActor(ActorName, &FoundActor);

	if (Components.Num() == 0)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("No PCG components found on actor '%s'"), *ActorName));
	}

	if (CompIndex < 0 || CompIndex >= Components.Num())
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("component_index %d out of range (actor has %d PCG components)"), CompIndex, Components.Num()));
	}

	UPCGComponent* Comp = Components[CompIndex];
	Comp->CleanupLocal(bRemoveComponents);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actor"), FoundActor->GetName());
	Result->SetBoolField(TEXT("removed_components"), bRemoveComponents);
	return SuccessObj(Result);
}

FMonolithActionResult FMonolithPCGActions::HandleGetGenerationResults(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorName = Params->GetStringField(TEXT("actor"));
	int32 CompIndex = static_cast<int32>(Params->GetNumberField(TEXT("component_index")));

	if (ActorName.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("actor is required"));
	}

	AActor* FoundActor = nullptr;
	TArray<UPCGComponent*> Components = FindPCGComponentsOnActor(ActorName, &FoundActor);

	if (Components.Num() == 0)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("No PCG components found on actor '%s'"), *ActorName));
	}

	if (CompIndex < 0 || CompIndex >= Components.Num())
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("component_index %d out of range (actor has %d PCG components)"), CompIndex, Components.Num()));
	}

	UPCGComponent* Comp = Components[CompIndex];

	int32 TotalResources = 0;
	int32 ActorCount = 0;
	int32 ComponentCount = 0;
	int32 ISMCount = 0;
	TArray<TSharedPtr<FJsonValue>> ResourcesArray;

	Comp->ForEachManagedResource([&](UPCGManagedResource* Resource)
	{
		if (!Resource) return;
		TotalResources++;

		TSharedPtr<FJsonObject> ResObj = MakeShared<FJsonObject>();
		ResObj->SetStringField(TEXT("type"), Resource->GetClass()->GetName());

		if (UPCGManagedActors* ManagedActors = Cast<UPCGManagedActors>(Resource))
		{
			const TArray<TSoftObjectPtr<AActor>>& Actors = ManagedActors->GetConstGeneratedActors();
			int32 Count = Actors.Num();
			ResObj->SetNumberField(TEXT("count"), Count);
			ActorCount += Count;

			// List first few actors
			TArray<TSharedPtr<FJsonValue>> ActorNames;
			int32 Listed = 0;
			for (const TSoftObjectPtr<AActor>& ActorPtr : Actors)
			{
				if (Listed >= 10) break;
				if (AActor* GenActor = ActorPtr.Get())
				{
					ActorNames.Add(MakeShared<FJsonValueString>(GenActor->GetName()));
					Listed++;
				}
			}
			ResObj->SetArrayField(TEXT("actors"), ActorNames);
		}
		else if (Cast<UPCGManagedISMComponent>(Resource))
		{
			ISMCount++;
			ResObj->SetStringField(TEXT("subtype"), TEXT("ISM"));
		}
		else if (Cast<UPCGManagedComponent>(Resource))
		{
			ComponentCount++;
			ResObj->SetStringField(TEXT("subtype"), TEXT("Component"));
		}

		ResourcesArray.Add(MakeShared<FJsonValueObject>(ResObj));
	});

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("actor"), FoundActor->GetName());
	Result->SetBoolField(TEXT("is_generating"), Comp->IsGenerating());
	Result->SetNumberField(TEXT("total_resources"), TotalResources);
	Result->SetNumberField(TEXT("managed_actor_count"), ActorCount);
	Result->SetNumberField(TEXT("managed_component_count"), ComponentCount);
	Result->SetNumberField(TEXT("managed_ism_count"), ISMCount);
	Result->SetArrayField(TEXT("resources"), ResourcesArray);
	return SuccessObj(Result);
}

// ============================================================================
// Tier 4 — Templates
// ============================================================================

/** Helper: create a graph asset, returns nullptr and sets OutError on failure */
static UPCGGraph* CreateGraphAsset(const FString& PackagePath, const FString& AssetName, FString& OutError)
{
	FString FullPath = PackagePath / AssetName;

	if (FMonolithAssetUtils::AssetExists(FullPath))
	{
		OutError = FString::Printf(TEXT("Asset already exists at %s"), *FullPath);
		return nullptr;
	}

	UPackage* Pkg = CreatePackage(*FullPath);
	if (!Pkg)
	{
		OutError = TEXT("Failed to create package");
		return nullptr;
	}

	UPCGGraph* Graph = NewObject<UPCGGraph>(Pkg, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
	if (!Graph)
	{
		OutError = TEXT("Failed to create PCG graph");
		return nullptr;
	}

	FAssetRegistryModule::AssetCreated(Graph);
	return Graph;
}

FMonolithActionResult FMonolithPCGActions::HandleCreateScatterGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString PackagePath = Params->GetStringField(TEXT("asset_path"));
	FString AssetName = Params->GetStringField(TEXT("asset_name"));
	FString MeshPath = Params->HasField(TEXT("mesh_path")) ? Params->GetStringField(TEXT("mesh_path")) : FString();
	double PointsPerSqM = Params->HasField(TEXT("points_per_sqm")) ? Params->GetNumberField(TEXT("points_per_sqm")) : 1.0;

	if (PackagePath.IsEmpty() || AssetName.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("asset_path and asset_name are required"));
	}

	FString Error;
	UPCGGraph* Graph = CreateGraphAsset(PackagePath, AssetName, Error);
	if (!Graph) return FMonolithActionResult::Error(Error);

	// Add nodes: SurfaceSampler → DensityFilter → StaticMeshSpawner
	UPCGSettings* SamplerSettings = nullptr;
	UPCGNode* SamplerNode = Graph->AddNodeOfType(ResolveSettingsClass(TEXT("SurfaceSampler")), SamplerSettings);

	UPCGSettings* FilterSettings = nullptr;
	UPCGNode* FilterNode = Graph->AddNodeOfType(ResolveSettingsClass(TEXT("DensityFilter")), FilterSettings);

	UPCGSettings* SpawnerSettings = nullptr;
	UPCGNode* SpawnerNode = Graph->AddNodeOfType(ResolveSettingsClass(TEXT("StaticMeshSpawner")), SpawnerSettings);

	if (!SamplerNode || !FilterNode || !SpawnerNode)
	{
		return FMonolithActionResult::Error(TEXT("Failed to create one or more template nodes. Ensure PCG plugin has SurfaceSampler, DensityFilter, and StaticMeshSpawner."));
	}

	// Layout for readability
	SamplerNode->SetNodePosition(0, 0);
	FilterNode->SetNodePosition(400, 0);
	SpawnerNode->SetNodePosition(800, 0);

	// Wire: Input → Sampler → Filter → Spawner → Output
	// Note: PCG Input node's output pin is labeled "In", Output node's input pin is labeled "Out"
	Graph->AddEdge(Graph->GetInputNode(), PCGPinConstants::DefaultInputLabel, SamplerNode, FName(TEXT("Surface")));
	Graph->AddEdge(SamplerNode, PCGPinConstants::DefaultOutputLabel, FilterNode, PCGPinConstants::DefaultInputLabel);
	Graph->AddEdge(FilterNode, PCGPinConstants::DefaultOutputLabel, SpawnerNode, PCGPinConstants::DefaultInputLabel);
	Graph->AddEdge(SpawnerNode, PCGPinConstants::DefaultOutputLabel, Graph->GetOutputNode(), PCGPinConstants::DefaultOutputLabel);

	// Set points per square meter on sampler if the property exists
	if (SamplerSettings)
	{
		FProperty* PointsProp = SamplerSettings->GetClass()->FindPropertyByName(TEXT("PointsPerSquaredMeter"));
		if (PointsProp)
		{
			FString ValueStr = FString::SanitizeFloat(PointsPerSqM);
			void* ValuePtr = PointsProp->ContainerPtrToValuePtr<void>(SamplerSettings);
			PointsProp->ImportText_Direct(*ValueStr, ValuePtr, SamplerSettings, PPF_None);
		}
	}

	// Set mesh path on spawner if provided
	if (!MeshPath.IsEmpty() && SpawnerSettings)
	{
		FProperty* MeshProp = SpawnerSettings->GetClass()->FindPropertyByName(TEXT("StaticMesh"));
		if (!MeshProp) MeshProp = SpawnerSettings->GetClass()->FindPropertyByName(TEXT("Mesh"));
		if (MeshProp)
		{
			void* ValuePtr = MeshProp->ContainerPtrToValuePtr<void>(SpawnerSettings);
			MeshProp->ImportText_Direct(*MeshPath, ValuePtr, SpawnerSettings, PPF_None);
		}
	}

	Graph->GetOuter()->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), Graph->GetPathName());
	Result->SetStringField(TEXT("sampler_node"), SamplerNode->GetName());
	Result->SetStringField(TEXT("filter_node"), FilterNode->GetName());
	Result->SetStringField(TEXT("spawner_node"), SpawnerNode->GetName());
	Result->SetStringField(TEXT("pipeline"), TEXT("Input → SurfaceSampler → DensityFilter → StaticMeshSpawner → Output"));
	return SuccessObj(Result);
}

FMonolithActionResult FMonolithPCGActions::HandleCreateSplineGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString PackagePath = Params->GetStringField(TEXT("asset_path"));
	FString AssetName = Params->GetStringField(TEXT("asset_name"));
	FString MeshPath = Params->HasField(TEXT("mesh_path")) ? Params->GetStringField(TEXT("mesh_path")) : FString();

	if (PackagePath.IsEmpty() || AssetName.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("asset_path and asset_name are required"));
	}

	FString Error;
	UPCGGraph* Graph = CreateGraphAsset(PackagePath, AssetName, Error);
	if (!Graph) return FMonolithActionResult::Error(Error);

	// Add nodes: SplineSampler → CopyPoints → StaticMeshSpawner
	UPCGSettings* SplineSamplerSettings = nullptr;
	UPCGNode* SplineSamplerNode = Graph->AddNodeOfType(ResolveSettingsClass(TEXT("SplineSampler")), SplineSamplerSettings);

	UPCGSettings* CopyPointsSettings = nullptr;
	UPCGNode* CopyPointsNode = Graph->AddNodeOfType(ResolveSettingsClass(TEXT("CopyPoints")), CopyPointsSettings);

	UPCGSettings* SpawnerSettings = nullptr;
	UPCGNode* SpawnerNode = Graph->AddNodeOfType(ResolveSettingsClass(TEXT("StaticMeshSpawner")), SpawnerSettings);

	if (!SplineSamplerNode || !CopyPointsNode || !SpawnerNode)
	{
		return FMonolithActionResult::Error(TEXT("Failed to create one or more template nodes. Ensure PCG plugin has SplineSampler, CopyPoints, and StaticMeshSpawner."));
	}

	// Layout
	SplineSamplerNode->SetNodePosition(0, 0);
	CopyPointsNode->SetNodePosition(400, 0);
	SpawnerNode->SetNodePosition(800, 0);

	// Wire: Input → SplineSampler → CopyPoints → StaticMeshSpawner → Output
	// Note: PCG Input node's output pin is labeled "In", Output node's input pin is labeled "Out"
	Graph->AddEdge(Graph->GetInputNode(), PCGPinConstants::DefaultInputLabel, SplineSamplerNode, PCGPinConstants::DefaultInputLabel);
	Graph->AddEdge(SplineSamplerNode, PCGPinConstants::DefaultOutputLabel, CopyPointsNode, FName(TEXT("Source")));
	Graph->AddEdge(CopyPointsNode, PCGPinConstants::DefaultOutputLabel, SpawnerNode, PCGPinConstants::DefaultInputLabel);
	Graph->AddEdge(SpawnerNode, PCGPinConstants::DefaultOutputLabel, Graph->GetOutputNode(), PCGPinConstants::DefaultOutputLabel);

	// Set mesh on spawner if provided
	if (!MeshPath.IsEmpty() && SpawnerSettings)
	{
		FProperty* MeshProp = SpawnerSettings->GetClass()->FindPropertyByName(TEXT("StaticMesh"));
		if (!MeshProp) MeshProp = SpawnerSettings->GetClass()->FindPropertyByName(TEXT("Mesh"));
		if (MeshProp)
		{
			void* ValuePtr = MeshProp->ContainerPtrToValuePtr<void>(SpawnerSettings);
			MeshProp->ImportText_Direct(*MeshPath, ValuePtr, SpawnerSettings, PPF_None);
		}
	}

	Graph->GetOuter()->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), Graph->GetPathName());
	Result->SetStringField(TEXT("spline_sampler_node"), SplineSamplerNode->GetName());
	Result->SetStringField(TEXT("copy_points_node"), CopyPointsNode->GetName());
	Result->SetStringField(TEXT("spawner_node"), SpawnerNode->GetName());
	Result->SetStringField(TEXT("pipeline"), TEXT("Input → SplineSampler → CopyPoints → StaticMeshSpawner → Output"));
	return SuccessObj(Result);
}

FMonolithActionResult FMonolithPCGActions::HandleCloneGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString SourcePath = Params->GetStringField(TEXT("source_path"));
	FString DestPath = Params->GetStringField(TEXT("dest_path"));
	FString DestName = Params->GetStringField(TEXT("dest_name"));

	if (SourcePath.IsEmpty() || DestPath.IsEmpty() || DestName.IsEmpty())
	{
		return FMonolithActionResult::Error(TEXT("source_path, dest_path, and dest_name are required"));
	}

	UPCGGraph* SourceGraph = FMonolithAssetUtils::LoadAssetByPath<UPCGGraph>(SourcePath);
	if (!SourceGraph)
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Failed to load source graph at %s"), *SourcePath));
	}

	FString FullDestPath = DestPath / DestName;
	if (FMonolithAssetUtils::AssetExists(FullDestPath))
	{
		return FMonolithActionResult::Error(FString::Printf(TEXT("Asset already exists at %s"), *FullDestPath));
	}

	UPackage* DestPkg = CreatePackage(*FullDestPath);
	if (!DestPkg)
	{
		return FMonolithActionResult::Error(TEXT("Failed to create destination package"));
	}

	UPCGGraph* ClonedGraph = DuplicateObject<UPCGGraph>(SourceGraph, DestPkg, FName(*DestName));
	if (!ClonedGraph)
	{
		return FMonolithActionResult::Error(TEXT("DuplicateObject failed"));
	}

	ClonedGraph->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
	FAssetRegistryModule::AssetCreated(ClonedGraph);
	DestPkg->MarkPackageDirty();

	// Count nodes in the clone
	int32 NodeCount = 0;
	ClonedGraph->ForEachNode([&](UPCGNode* Node) -> bool
	{
		NodeCount++;
		return true;
	});

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("source_path"), SourcePath);
	Result->SetStringField(TEXT("cloned_path"), ClonedGraph->GetPathName());
	Result->SetNumberField(TEXT("node_count"), NodeCount);
	return SuccessObj(Result);
}
