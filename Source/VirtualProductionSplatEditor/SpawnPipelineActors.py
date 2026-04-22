# SPDX-License-Identifier: Unreal
# Tools > Execute Python Script — spawns pipeline actors and wires AVPPipelineOrchestrator.

import unreal

actor_classes = [
    "/Script/VirtualProductionSplatEditor.GreyboxSceneBuilder",
    "/Script/VirtualProductionSplat.PanoramicCapture360",
    "/Script/VirtualProductionSplatEditor.PanoramicExportRunner",
    "/Script/VirtualProductionSplatEditor.WorldLabsRunner",
    "/Script/VirtualProductionSplatEditor.GaussianSplatImportRunner",
    "/Script/VirtualProductionSplatEditor.VPStageSetup",
    "/Script/VirtualProductionSplatEditor.VPPipelineOrchestrator",
]

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
if world is None:
    unreal.log_error("SpawnPipelineActors: get_editor_world() returned None")
else:
    unreal.log("SpawnPipelineActors: editor world = %s" % world.get_name())

editor_actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

rotation = unreal.Rotator(0, 0, 0)
spacing = 200.0
x = 0.0

spawned_actors = []

for class_path in actor_classes:
    short_name = class_path.split(".")[-1]
    cls = unreal.load_class(None, class_path)
    if cls is None:
        unreal.log_error("SpawnPipelineActors: load_class failed for %s" % class_path)
        continue

    location = unreal.Vector(x, 0.0, 0.0)
    x += spacing

    actor = editor_actor_subsystem.spawn_actor_from_class(cls, location, rotation)
    if actor is None:
        unreal.log_error("SpawnPipelineActors: spawn failed for %s" % short_name)
        continue

    actor.set_actor_label(short_name)
    spawned_actors.append(actor)

    unreal.log("SpawnPipelineActors: spawned %s at X=%.0f" % (short_name, location.x))

all_actors = editor_actor_subsystem.get_all_level_actors()
by_class_name = {}
for a in all_actors:
    cname = a.get_class().get_name()
    if cname not in by_class_name:
        by_class_name[cname] = a

orchestrator = by_class_name.get("VPPipelineOrchestrator")
if orchestrator is None:
    unreal.log_error("SpawnPipelineActors: no VPPipelineOrchestrator in level after spawn")
else:
    wiring = [
        ("SceneBuilder", "GreyboxSceneBuilder"),
        ("CameraRig", "PanoramicCapture360"),
        ("ExportRunner", "PanoramicExportRunner"),
        ("WorldLabsRunner", "WorldLabsRunner"),
        ("SplatImporter", "GaussianSplatImportRunner"),
        ("StageSetup", "VPStageSetup"),
    ]
    for prop_name, uclass_name in wiring:
        ref = by_class_name.get(uclass_name)
        if ref is None:
            unreal.log_error("SpawnPipelineActors: missing actor class %s for property %s" % (uclass_name, prop_name))
            continue
        try:
            orchestrator.set_editor_property(prop_name, ref)
            unreal.log("SpawnPipelineActors: set %s -> %s" % (prop_name, uclass_name))
        except Exception as e:
            unreal.log_error("SpawnPipelineActors: set_editor_property(%s) failed: %s" % (prop_name, str(e)))

# Position panoramic rig at default eye level (refined by SetupPanoramicRig.py after greybox build)
for actor in spawned_actors:
    if "PanoramicCapture360" in actor.get_class().get_name():
        actor.set_actor_location(unreal.Vector(0, 0, 160), False, True)
        unreal.log("SpawnPipelineActors: positioned PanoramicCapture360 at (0, 0, 160)")
        break

# Reset hidden-in-game so respawn doesn't inherit stale visibility
for actor in spawned_actors:
    if hasattr(actor, "set_actor_hidden_in_game"):
        actor.set_actor_hidden_in_game(False)
    elif hasattr(actor, "SetActorHiddenInGame"):
        actor.SetActorHiddenInGame(False)

unreal.log(
    "Pipeline actors spawned and wired. Select AVPPipelineOrchestrator and hit Run Full Pipeline in the Details panel."
)

print("=" * 60)
print("Next step: Tools > Execute Python Script > SetupPanoramicRig.py")
print("This will center the rig, hide gizmos, capture 6 faces, and stitch the panorama.")
print("=" * 60)
