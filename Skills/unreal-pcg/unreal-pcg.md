---
name: unreal-pcg
description: Use when creating, editing, or running Procedural Content Generation (PCG) graphs via Monolith MCP. Covers graph CRUD, node and edge editing, property reflection, graph parameters, scatter/spline templates, and runtime generation on UPCGComponent. Triggers on PCG, procedural, scatter, foliage scatter, mesh spawner, surface sampler, spline sampler, UPCGGraph, UPCGComponent.
---

# Unreal PCG Workflows

You have access to **Monolith** with 22 PCG actions via `pcg_query()`.

This module is gated by `UMonolithSettings::bEnablePCG` (default `true`) and requires the engine-bundled `PCG` plugin. Sub-namespace: `pcg`.

## Discovery

```
monolith_discover({ namespace: "pcg" })
```

## Asset Path Conventions

All asset paths follow UE content browser format (no `.uasset` extension):

| Location | Path Format | Example |
|---|---|---|
| Project Content/ | `/Game/Path/To/Asset` | `/Game/PCG/PCG_Foliage` |
| Engine plugin | `/PluginName/Path/To/Asset` | `/PCG/Examples/PCG_Demo` |

`create_graph` and the template builders take `asset_path` (package directory) + `asset_name` separately. Other actions take a single `asset_path` pointing at the existing graph.

## Key Parameter Names

- `asset_path` — PCG graph asset path
- `node_id` — node name returned by `add_node` / `get_graph` (UPCGNode::GetName, e.g. `PCGNode_0`). NOT the settings class name.
- `settings_class` — short name accepted by `add_node`. Resolver strips `U`/`PCG` prefix and `Settings` suffix and matches: e.g. `SurfaceSampler` → `UPCGSurfaceSamplerSettings`.
- `from_pin` / `to_pin` — pin **label** (FName as string), NOT pin GUID. Defaults: most nodes use `In` for input and `Out` for output. Exceptions: `SurfaceSampler` input is `Surface`, `CopyPoints` source-input is `Source`. Always call `get_pin_info` first if unsure.
- `property_name` — UE PascalCase UPROPERTY name (e.g. `PointsPerSquaredMeter`, `StaticMesh`). Discover via `get_node_settings`.
- `value` — string form. Internally fed to `FProperty::ImportText_Direct`, so use UE textual format (`"1.5"`, `"true"`, `"(X=10,Y=0,Z=0)"`, `"/Game/Meshes/SM_Rock.SM_Rock"`).
- `actor` — runtime/world actions take **actor name or label** (not full path), e.g. `PCG_Volume_1`.

## Action Reference (22 total)

### Graph CRUD (2)
| Action | Key Params | Purpose |
|---|---|---|
| `create_graph` | `asset_path`, `asset_name` | Create a new empty UPCGGraph asset |
| `get_graph` | `asset_path` | Full structure dump: all nodes (with positions + pins) and edges |

### Node Management (2)
| Action | Key Params | Purpose |
|---|---|---|
| `add_node` | `asset_path`, `settings_class`, `position_x?`, `position_y?` | Add node by short class name (e.g. `SurfaceSampler`). Returns `node_id` |
| `remove_node` | `asset_path`, `node_id` | Remove a node |

### Edge Management (2)
| Action | Key Params | Purpose |
|---|---|---|
| `connect_nodes` | `asset_path`, `from_node`, `from_pin`, `to_node`, `to_pin` | Add edge between pin labels |
| `disconnect_nodes` | `asset_path`, `from_node`, `from_pin`, `to_node`, `to_pin` | Remove edge |

### Property Reflection (2)
| Action | Key Params | Purpose |
|---|---|---|
| `get_node_settings` | `asset_path`, `node_id` | List all editable UPROPERTY fields on the node's settings (names, types, current values) |
| `set_node_property` | `asset_path`, `node_id`, `property_name`, `value` | Set a property by name; uses UE `ImportText` for string→typed conversion |

### Graph Parameters (3)
| Action | Key Params | Purpose |
|---|---|---|
| `get_graph_parameters` | `asset_path` | List user-defined graph params (name, type, current value) |
| `set_graph_parameter` | `asset_path`, `name`, `value`, `type?` | Create or update. `type` accepts `Bool`, `Byte`, `Int32`, `Int64`, `Float`, `Double` (default), `Name`, `String`, `Text` |
| `remove_graph_parameter` | `asset_path`, `name` | Remove user param |

### Layout & Enumeration (3)
| Action | Key Params | Purpose |
|---|---|---|
| `set_node_position` | `asset_path`, `node_id`, `position_x`, `position_y` | Reposition node in graph editor |
| `list_settings_classes` | `filter?` | Enumerate all UPCGSettings subclasses (filter is substring-match) |
| `get_pin_info` | `asset_path`, `node_id` | Detailed pins (label, allow_multiple, edge count, tooltip, current connections) |

### Execution & World Integration (5)
| Action | Key Params | Purpose |
|---|---|---|
| `list_pcg_components` | `filter_graph?` | All UPCGComponent in the active level + their owner actor + assigned graph |
| `assign_graph` | `actor`, `graph_path`, `component_index?` | Assign graph to a UPCGComponent by actor name/label |
| `generate` | `actor`, `component_index?`, `force?` (default true) | Trigger generation on a component |
| `cleanup` | `actor`, `component_index?`, `remove_components?` (default true) | Clear generated results |
| `get_generation_results` | `actor`, `component_index?` | Inspect managed actors / ISMs / components, plus `is_generating` flag |

### Templates (3)
| Action | Key Params | Purpose |
|---|---|---|
| `create_scatter_graph` | `asset_path`, `asset_name`, `mesh_path?`, `points_per_sqm?` (default 1.0) | Builds and wires `Input → SurfaceSampler → DensityFilter → StaticMeshSpawner → Output`. `points_per_sqm` is set via the `PointsPerSquaredMeter` property on the sampler |
| `create_spline_graph` | `asset_path`, `asset_name`, `mesh_path?` | Builds `Input → SplineSampler → CopyPoints (Source pin) → StaticMeshSpawner → Output` |
| `clone_graph` | `source_path`, `dest_path`, `dest_name` | `DuplicateObject` deep copy of an existing graph |

## Common Workflows

### Inspect an existing graph
```
pcg_query({ action: "get_graph", params: { asset_path: "/Game/PCG/PCG_Foliage" } })
// → returns nodes[] (each with node_id, settings_class, position, input_pins, output_pins) and edges[]
```

### Build a graph from scratch (manual)
```
pcg_query({ action: "create_graph", params: { asset_path: "/Game/PCG", asset_name: "PCG_Rocks" } })

// Add nodes — capture returned node_id values
pcg_query({ action: "add_node", params: { asset_path: "/Game/PCG/PCG_Rocks", settings_class: "SurfaceSampler", position_x: 0, position_y: 0 } })
pcg_query({ action: "add_node", params: { asset_path: "/Game/PCG/PCG_Rocks", settings_class: "StaticMeshSpawner", position_x: 400, position_y: 0 } })

// Discover the actual pin labels before wiring
pcg_query({ action: "get_pin_info", params: { asset_path: "/Game/PCG/PCG_Rocks", node_id: "<sampler node_id>" } })

// Wire pins
pcg_query({ action: "connect_nodes", params: {
  asset_path: "/Game/PCG/PCG_Rocks",
  from_node: "<sampler node_id>", from_pin: "Out",
  to_node:   "<spawner node_id>", to_pin:   "In"
}})
```

### Build a graph from a template
```
pcg_query({ action: "create_scatter_graph", params: {
  asset_path: "/Game/PCG", asset_name: "PCG_Grass",
  mesh_path: "/Game/Meshes/SM_GrassClump.SM_GrassClump",
  points_per_sqm: 4.0
}})
```

### Set a node property (e.g. mesh on a spawner)
```
pcg_query({ action: "get_node_settings", params: { asset_path: "/Game/PCG/PCG_Rocks", node_id: "<spawner node_id>" } })
// → discover the actual property name (e.g. "StaticMesh" or "Mesh")
pcg_query({ action: "set_node_property", params: {
  asset_path: "/Game/PCG/PCG_Rocks", node_id: "<spawner node_id>",
  property_name: "StaticMesh", value: "/Game/Meshes/SM_Rock.SM_Rock"
}})
```

### Expose a graph parameter
```
pcg_query({ action: "set_graph_parameter", params: {
  asset_path: "/Game/PCG/PCG_Rocks", name: "Density", value: "2.5", type: "Double"
}})
```

### Run on a level
```
pcg_query({ action: "list_pcg_components" })
// → find owning actor name

pcg_query({ action: "assign_graph", params: { actor: "PCG_Volume_1", graph_path: "/Game/PCG/PCG_Rocks" } })
pcg_query({ action: "generate",      params: { actor: "PCG_Volume_1", force: true } })
pcg_query({ action: "get_generation_results", params: { actor: "PCG_Volume_1" } })
// → inspect managed actors / ISMs / components

pcg_query({ action: "cleanup", params: { actor: "PCG_Volume_1" } })
```

### Clone an existing graph as a starting point
```
pcg_query({ action: "clone_graph", params: {
  source_path: "/Game/PCG/PCG_BaseScatter",
  dest_path:   "/Game/PCG/Variants",
  dest_name:   "PCG_DenseScatter"
}})
```

## Rules

- The **node identifier** is the `node_id` returned from `add_node` / `get_graph` (e.g. `PCGNode_0`). It is NOT the settings class name. Save it from the response — the registry has no "find node by class".
- Pin labels are FNames and **vary per node**. Most use `In`/`Out`, but `SurfaceSampler` input is `Surface` and `CopyPoints` source-input is `Source`. When in doubt, call `get_pin_info` (or `get_graph` once and re-use).
- `add_node` accepts short class names because the resolver strips `U`/`PCG`/`Settings`. `SurfaceSampler`, `UPCGSurfaceSamplerSettings`, and `PCGSurfaceSamplerSettings` all resolve to the same class. Use `list_settings_classes` to enumerate.
- `set_node_property` and `set_graph_parameter` take **string `value`** and route through `FProperty::ImportText_Direct`. Use UE textual format:
  - bool → `"true"` / `"false"`
  - vector → `"(X=10,Y=0,Z=0)"`
  - asset reference → full asset path (`"/Game/Meshes/SM_Rock.SM_Rock"`)
  - struct → UE struct text form
- Property names are reflection-based UE PascalCase. Always confirm via `get_node_settings` before setting — names differ between similar nodes (`StaticMesh` vs `Mesh` vs `MeshAsset`).
- Templates auto-wire via the engine's actual default labels (`PCGPinConstants::DefaultInputLabel` = `In`, `DefaultOutputLabel` = `Out`) plus per-node specifics (`SurfaceSampler` `Surface`, `CopyPoints` `Source`). If `add_node` of any template stage fails, the engine PCG plugin probably doesn't have that settings class — the template surfaces it as an error.
- World/runtime actions (`assign_graph` / `generate` / `cleanup` / `get_generation_results` / `list_pcg_components`) operate on the **active editor world's level**, by actor **name or label** (not full path). `component_index` defaults to 0 — only set it when an actor has multiple `UPCGComponent` instances.
- The whole module is gated by `bEnablePCG` in MonolithSettings. If `pcg_query` is missing entirely, check that flag and that the `PCG` engine plugin is enabled.

## Known Limitations

- **No `set_pin_default` / per-pin literal**: PCG drives values via property settings on the upstream node, not pin defaults. Use `set_node_property` on the source node, or expose a graph parameter and bind it.
- **No "find node by settings class" / no pin GUID API**: identify nodes by `node_id` strings only. Cache them when you create them.
- **`save` is implicit**: graph mutations call `MarkPackageDirty()` but do not write to disk. The next editor save (or `editor_query.save_*`) persists the graph.
- **`generate` returns immediately**: it triggers async work. Poll `get_generation_results` (`is_generating` flag) if you need to wait.
- **No subgraph instancing actions**: this module is single-graph. Subgraph nodes work at runtime but the API doesn't expose subgraph-specific helpers — treat them as ordinary nodes whose settings reference another `UPCGGraph` asset.
- **No HISM/ISM-specific tuning**: `set_node_property` covers what `StaticMeshSpawner` exposes, but it's just reflection — there is no helper for HISM properties beyond what UE itself surfaces on the spawner settings.

## Cross-Skill Notes

- Combine with `unreal-mesh` to author the underlying StaticMesh / pick scatter targets, then assign via `set_node_property` (`StaticMesh` UPROPERTY).
- Combine with `unreal-materials` if a scatter target needs a custom material — the material agent creates the material first, then the StaticMesh references it; PCG just spawns the mesh.
- Use `unreal-project-search` (`project_query.search`) to find existing PCG graphs (`type: "PCGGraph"`) before creating duplicates.
- Use `unreal-debugging` (`editor_query.search_logs`) when `generate` succeeds but produces nothing — PCG logs to `LogPCG` with the cause.
