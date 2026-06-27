# MeshTerrainRuntimeLab Project Scope

## Single Goal

The goal of this project is to create terrain at runtime using Epic's UE 5.8
experimental Mesh Terrain technology.

Runtime terrain must match the technical terrain created by Epic's Mesh Terrain
Mode in the editor. It must not merely look similar.

## Meaning Of "The Same Terrain"

A runtime-created terrain is considered in scope only if it uses the same core
Epic terrain stack that Mesh Terrain Mode uses, including the relevant
MeshTerrain/MeshPartition actors, sections, components, descriptors, data model,
and rendering/collision behavior.

The validation target is:

1. Create a reference terrain in the editor with Epic Mesh Terrain Mode.
2. Inspect its actors, components, properties, mesh partition setup, Nanite
   behavior, materials, collision, and generated section structure.
3. Create terrain at runtime.
4. Confirm that the runtime-created terrain has the same core technical shape as
   the reference terrain, not just similar visuals.

## In Scope

- Runtime creation of real MeshTerrain-compatible terrain.
- Runtime creation of MeshPartition/CompiledSection terrain data when that is
  the actual runtime layer used by Mesh Terrain Mode.
- Reusing or porting the minimal editor builder logic required to create the
  same terrain data at runtime.
- A first milestone may be flat terrain, but it must still be the correct
  terrain type and data path.
- Runtime APIs such as rebuild, material assignment, height data assignment, and
  later chunk updates are valid only when they operate on the correct terrain
  stack.

## Out Of Scope As Product Direction

- A standalone custom terrain system.
- Loose StaticMeshComponent placement as the final terrain path.
- ProceduralMeshComponent or DynamicMesh terrain as a replacement for Mesh
  Terrain.
- Runtime assembly of baked Nanite static mesh chunks as an answer to runtime
  terrain generation.
- Editor-baked Nanite chunk streaming as a substitute for runtime MeshTerrain
  creation.
- Any approach that cannot be compared against an editor-created Mesh Terrain
  reference and shown to use the same core stack.

## Accepted Experiments

Previous experiments may stay in the repository when useful for learning, but
they are not the project goal unless they satisfy runtime MeshTerrain parity.

Known experiments:

- Runtime transient StaticMesh payload added through MeshPartition.
- Editor-baked Nanite static mesh chunk.

These are research artifacts. They do not satisfy the main goal by themselves.

## Course Correction

Scope drift started after the runtime mesh prototype proved that transient mesh
payloads could be added through MeshPartition, but did not provide Nanite parity.

The drift became explicit when performance concerns about large non-Nanite
runtime terrain led to a baked Nanite chunk pipeline. That pipeline addressed a
real performance concern, because UE's Nanite build path is editor/developer
tooling and runtime StaticMesh fast builds do not create full Nanite data.

However, optimizing for Nanite-backed baked chunks changed the problem from
"create MeshTerrain at runtime" to "stream prebuilt assets at runtime". That is
not the project goal.

The project direction is corrected back to runtime MeshTerrain parity:

- first match the editor-created Mesh Terrain technical stack;
- only then optimize chunking, streaming, Nanite behavior, and deformation;
- do not replace the target with baked chunk streaming or a custom mesh terrain.
