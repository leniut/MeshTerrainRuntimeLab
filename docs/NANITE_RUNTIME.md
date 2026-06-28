# Runtime Nanite - Spike Findings

## Question
Can we build Nanite render data for a terrain mesh **at runtime** (dynamically, at
gameplay time) so the runtime terrain matches the editor render variant
(`HighEndPlatform_RenderData`), which carries Nanite?

## Verdict
- **Editor / PIE builds: YES.** Setting `NaniteSettings.bEnabled = true` and building with
  `bFastBuild = false` runs `UStaticMesh`'s full build path, which compiles real Nanite data.
  This is what the editor bake pipeline and Epic's own tools do; it is also what
  Voxel Plugin demonstrated ("baking nanite meshes at runtime").
- **Packaged / Shipping builds: NO (not via standard API).** Runtime Nanite is blocked by the
  engine itself.
- **Only escape hatch for packaged builds:** manually link the Developer `NaniteBuilder` module
  into the game target and call `Nanite::IBuilderModule::Get().Build()` directly, then attach the
  resulting `Nanite::FResources` to the static mesh render data and `InitResources()`. This is the
  Voxel Plugin "transcode" route - unofficial, heavy (pulls Embree/metis), and slow. Out of scope
  for this spike.

## Evidence (UE 5.8 engine source)
- `UStaticMesh::BuildFromMeshDescriptions` (`Engine/Source/Runtime/Engine/Private/StaticMesh.cpp`):
  ```cpp
  #if !WITH_EDITOR
      // In non-Editor builds, we can only perform fast mesh builds
      check(Params.bFastBuild);
  #endif
  ```
  The fast-build branch only does `BuildFromMeshDescription` + `InitResources()` - no Nanite.
  The full (Nanite-capable) branch is entirely under `#if WITH_EDITOR`.
- `#include "NaniteBuilder.h"` in `StaticMesh.cpp` is inside `#if WITH_EDITOR`.
- The static-mesh to Nanite build (`Nanite::IBuilderModule::Get().Build(FResources&, FInputMeshData&, ...)`)
  lives in the **Developer** modules `MeshBuilder` / `NaniteBuilder`
  (`Engine/Source/Developer/...`), which are not part of a normal runtime/shipping target.
- `NaniteBuilder.Build.cs` comment: *"NaniteBuilder module is an editor module"*; deps include
  `Embree`, `metis`, `MikkTSpace`, `QuadricMeshReduction`.
- Runtime-side storage IS available: `Nanite::FResources` + `FStaticMeshRenderData::NaniteResourcesPtr`
  + `InitResources()` are in the runtime Engine module (`Rendering/NaniteResources.h`). Only the
  *builder* is editor/Developer.

## What this spike implemented
`AMeshTerrainRuntimeLabActor`:
- New `bBuildNaniteData` flag (default true). In **Editor/PIE** the runtime build enables Nanite
  settings and uses `bFastBuild = false`, so `RebuildTerrain()` produces a Nanite-enabled static
  mesh on the `ACompiledSection` - real parity with the editor render variant, in-editor.
- In **non-editor** builds the flag is ignored and the fast path is used (no Nanite) - consistent
  with the engine constraint above.
- Diagnostic `bRuntimeNaniteDataValid` records `UStaticMesh::HasValidNaniteData()` after each build.
- Caveat: the editor full build is significantly slower than the fast build.

## Implementation note
The plugin contains an experimental `bUseManualNaniteBuilder` code path that calls
`Nanite::IBuilderModule` directly. In the current public plugin it is compiled only for
Editor targets (`MTR_WITH_RUNTIME_NANITE_BUILDER=1` only when `Target.Type == TargetType.Editor`).
It is not enabled for Game/packaged targets because installed-engine monolithic builds do not
provide a safe, portable `NaniteBuilder` dependency surface. A packaged Nanite attempt should be a
separate source-engine experiment.

The manual builder sources its geometry from the **same `FMeshData`** used for the `UStaticMesh`
fast build (`BuildAndInjectRuntimeNanite(StaticMesh, MeshData)`), not from a regenerated grid. This
guarantees the Nanite representation matches the fallback geometry exactly, including the optional
parity skirt (`bApplyEditorSkirtForParity`) and any non-flat height. It relies on the `FMeshData`
being densely packed (sequential appends, no deletions), which `CreateFlatRuntimeMeshData`
guarantees.

## Runtime material parity (non-editor builds)
`ACompiledSection::SetMaterialInstance` only accepts a `UMaterialInstanceConstant` (not creatable at
runtime) and `SetChannelTexture` binds the channel texture through an editor-only call. So in
Editor/PIE the section gets a savable `UMaterialInstanceConstant`, but in packaged/Shipping builds
that path is unavailable. To keep a real per-instance material at runtime, the actor now applies a
`UMaterialInstanceDynamic` (with the channel texture bound via the runtime-safe
`SetTextureParameterValue`) directly to the section's generated mesh components
(`ApplyRuntimeMaterialInstance`, compiled under `#if !WITH_EDITOR`). For the default flat case the
channel table is all-inactive (`[255,255,255,255]`), so this is mostly a correctness/parity
guarantee rather than a visible change, but it gives non-editor terrain a proper material handle for
later channel/height work.

## Validation
Codex validated the spike in UE 5.8 Editor/PIE through official Unreal MCP after clean
`MeshTerrainRuntimeEditor Win64 Development` and `MeshTerrainRuntime Win64 Development`
builds and an editor restart:

- PIE actor: `AMeshTerrainRuntimeLabActor`
- `BuildVariantName`: `HighEndPlatform_RenderData`
- `bBuildNaniteData`: `true`
- `bUseManualNaniteBuilder`: `false`
- `bRuntimeNaniteDataValid`: `true`
- Runtime world contains one generated `AMeshPartition` and one generated `ACompiledSection`.
- Editor scene was cleaned before validation: no `Landscape`, `LandscapeProxy`, `MeshPartition`,
  or `CompiledSection` actors remain outside PIE; one lab actor remains to generate terrain at
  `BeginPlay`.

Official MCP parity snapshots match the transient editor-pipeline reference for the flat
16x16, 2000x2000, 10x10 UV tiling case when `bApplyEditorSkirtForParity=true`:

- `UMeshPartitionDefinition`: `/MeshPartition/DataAssets/MPD_Default.MPD_Default`
- build variant: `HighEndPlatform_RenderData`
- channel texture: `/MeshPartition/Textures/Void2DArray.Void2DArray`
- channel table: `[255,255,255,255]`
- channel texcoord desc: `{x:200,y:200}`
- render component collision profile: `NoCollision`
- material slot name: `MegaMeshStaticMeshMaterial`
- component/static mesh material: generated `CompiledSectionMIC`
- section/component bounds: min `(-1010,-1010,-10)`, max `(1010,1010,0)`
- static mesh:
  - `naniteEnabled`: `true`
  - `naniteFallbackPercentTriangles`: `0.2`
  - `supportRayTracing`: `true`
  - `generateMeshDistanceField`: `false`
  - `numLODs`: `1`

For normal visual runtime tests `bApplyEditorSkirtForParity` defaults to `false`. In that mode the
terrain surface and visible border stay flat, with bounds min `(-1000,-1000,0)`, max
`(1000,1000,0)`. This avoids the editor transformer's lowered 10-unit skirt casting visible edge
shadows while keeping the parity path available for explicit comparison.

Known remaining difference: build hashes/guids are runtime-generated, not the deterministic editor
DDC keys. That does not affect live runtime terrain behavior, but it matters if this runtime path
must share editor DDC/cache artifacts later.

## Consequence for project scope
- Runtime MeshTerrain parity **including Nanite** is achievable for editor/PIE workflows now.
- For a **packaged** runtime, full Nanite parity requires porting/linking the Developer
  NaniteBuilder path (Voxel-Plugin style). This is the remaining hard, unofficial step and should
  be a deliberate, separately-scoped decision.
