# SPDX-License-Identifier: Unreal
# Tools > Execute Python Script — center rig, hide pipeline from capture, CaptureFaces + StitchPanorama.

import os
import subprocess
import sys
import time

import unreal


def _call_actor_method(actor, *names):
    """Call first available UFUNCTION on actor (snake_case or PascalCase)."""
    for name in names:
        fn = getattr(actor, name, None)
        if callable(fn):
            fn()
            return True
    unreal.log_error("SetupPanoramicRig: no callable method among %s on %s" % (names, actor.get_class().get_name()))
    return False


def _deselect_all(editor_subsystem):
    if hasattr(editor_subsystem, "select_nothing"):
        editor_subsystem.select_nothing()
    elif hasattr(editor_subsystem, "SelectNothing"):
        editor_subsystem.SelectNothing()
    else:
        unreal.log_warning("SetupPanoramicRig: could not clear selection — gizmos may appear in captures.")


def _get_ue_python_exe():
    engine_dir = unreal.Paths.engine_dir()
    candidates = [
        os.path.join(engine_dir, "Binaries", "ThirdParty", "Python3", "Win64", "python.exe"),
        os.path.join(engine_dir, "Binaries", "ThirdParty", "Python3", "Mac", "bin", "python3"),
        os.path.join(engine_dir, "Binaries", "ThirdParty", "Python3", "Linux", "bin", "python3"),
    ]
    for candidate in candidates:
        if os.path.exists(candidate):
            return candidate
    # sys.executable inside UE Python is UnrealEditor.exe — never use it as a Python interpreter.
    # Fall back to a system python3 only if the bundled one truly cannot be found.
    for fallback in ("python3", "python"):
        import shutil
        found = shutil.which(fallback)
        if found and "UnrealEditor" not in found and "UE_" not in found:
            return found
    return "python3"


def setup_and_capture():
    unreal.log(
        "SetupPanoramicRig: TIP — manually place BP_Sky_Sphere in level for sky color above geometry"
    )

    editor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    all_actors = editor_subsystem.get_all_level_actors()

    panoramic_rig = None

    for actor in all_actors:
        class_name = actor.get_class().get_name()
        if class_name == "PanoramicCapture360":
            panoramic_rig = actor

    if panoramic_rig is None:
        unreal.log_error(
            "SetupPanoramicRig: APanoramicCapture360 not found in level. Run SpawnPipelineActors.py first."
        )
        return

    # ---- Classes that are pipeline tooling, never part of the captured environment ----
    pipeline_class_names = [
        "PanoramicCapture360",
        "GreyboxSceneBuilder",
        "PanoramicExportRunner",
        "WorldLabsRunner",
        "GaussianSplatImportRunner",
        "VPStageSetup",
        "VPPipelineOrchestrator",
        "CineCameraActor",
    ]
    sky_tokens = ["sky", "horizon", "atmosphere", "fog", "sun"]

    def _is_pipeline(actor):
        cn = actor.get_class().get_name()
        return any(h == cn or (h in cn) for h in pipeline_class_names)

    def _is_sky(actor):
        label = actor.get_actor_label().lower()
        if any(t in label for t in sky_tokens):
            return True
        for tag in actor.tags:
            if any(t in str(tag).lower() for t in sky_tokens):
                return True
        return False

    def _has_tag(actor, tagname):
        return any(str(t).lower() == tagname.lower() for t in actor.tags)

    def _is_greybox_geo(actor):
        if _is_pipeline(actor) or _is_sky(actor):
            return False
        if _has_tag(actor, "VPGreybox"):
            return True
        return actor.get_class().get_name() == "StaticMeshActor"

    # ---- Fix 3: place rig at greybox bounding-box centroid (XY), eye level Z = 160cm ----
    geo_actors = [a for a in all_actors if _is_greybox_geo(a)]
    if geo_actors:
        min_x = min_y = float("inf")
        max_x = max_y = float("-inf")
        for a in geo_actors:
            origin, extent = a.get_actor_bounds(False)
            min_x = min(min_x, origin.x - extent.x)
            max_x = max(max_x, origin.x + extent.x)
            min_y = min(min_y, origin.y - extent.y)
            max_y = max(max_y, origin.y + extent.y)
        center_x = (min_x + max_x) / 2.0
        center_y = (min_y + max_y) / 2.0
    else:
        center_x = 0.0
        center_y = 0.0
        unreal.log_warning("SetupPanoramicRig: no greybox geometry found — placing rig at origin.")

    eye_level_z = 160.0
    new_location = unreal.Vector(center_x, center_y, eye_level_z)
    panoramic_rig.set_actor_location(new_location, False, True)
    unreal.log("SetupPanoramicRig: moved rig to bounds centroid %s (from %d geo actors)" % (new_location, len(geo_actors)))

    # ---- Fix 1: hide EVERYTHING except the greybox geometry we explicitly want captured ----
    keep_label_tokens = ["ground", "wall_north", "wall_east", "wall_west", "pillar"]

    def _is_keeper(actor):
        if actor.get_class().get_name() != "StaticMeshActor":
            return False
        if _has_tag(actor, "VPGreybox"):
            return True
        label = actor.get_actor_label().lower()
        return any(tok in label for tok in keep_label_tokens)

    actors_to_hide = [a for a in all_actors if not _is_keeper(a)]

    # ---- Fix 2: hide only sky-atmosphere and fog (they bleed sky/ocean into the capture). ----
    # Keep SkyLight + DirectionalLight ON so the lit greybox material reads correctly.
    sky_class_tokens = ["SkyAtmosphere", "ExponentialHeightFog"]
    sky_hidden = 0
    for actor in all_actors:
        cn = actor.get_class().get_name()
        if any(tok in cn for tok in sky_class_tokens):
            if actor not in actors_to_hide:
                actors_to_hide.append(actor)
            sky_hidden += 1
    unreal.log("SetupPanoramicRig: flagged %d sky-atmosphere/fog actor(s) for hiding" % sky_hidden)

    # ---- Diagnostic: exactly what is visible vs hidden during capture ----
    hide_set = set(actors_to_hide)
    for a in all_actors:
        line = "KEEP: %s (%s)" % (a.get_actor_label(), a.get_class().get_name()) if a not in hide_set \
            else "HIDE: %s (%s)" % (a.get_actor_label(), a.get_class().get_name())
        print(line)
        unreal.log(line)

    for actor in actors_to_hide:
        if hasattr(actor, "set_actor_hidden_in_game"):
            actor.set_actor_hidden_in_game(True)
        elif hasattr(actor, "SetActorHiddenInGame"):
            actor.SetActorHiddenInGame(True)

    kept = [a for a in all_actors if _is_keeper(a)]
    unreal.log("SetupPanoramicRig: hid %d actors, kept %d greybox geo actor(s)" % (len(actors_to_hide), len(kept)))

    # ---- Fix 3: force flat grey LIT material onto every StaticMeshActor before capture ----
    # Lights are on now, so use the default UE grey (BasicShapeMaterial) — no purple, works with lighting.
    neutral_mat = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/BasicShapeMaterial")
    if neutral_mat is None:
        unreal.log_warning("SetupPanoramicRig: /Engine/BasicShapes/BasicShapeMaterial not found.")
    else:
        painted = 0
        for actor in all_actors:
            if actor.get_class().get_name() != "StaticMeshActor":
                continue
            smc = actor.get_component_by_class(unreal.StaticMeshComponent)
            if smc is None:
                continue
            for i in range(smc.get_num_materials()):
                smc.set_material(i, neutral_mat)
            painted += 1
        unreal.log("SetupPanoramicRig: applied BasicShapeMaterial (flat grey) to %d StaticMeshActor(s)" % painted)

    _deselect_all(editor_subsystem)

    unreal.log("SetupPanoramicRig: waiting for editor to settle...")
    time.sleep(0.5)

    unreal.log("SetupPanoramicRig: calling CaptureFaces...")
    _call_actor_method(panoramic_rig, "capture_faces", "CaptureFaces")

    for actor in actors_to_hide:
        if hasattr(actor, "set_actor_hidden_in_game"):
            actor.set_actor_hidden_in_game(False)
        elif hasattr(actor, "SetActorHiddenInGame"):
            actor.SetActorHiddenInGame(False)

    unreal.log("SetupPanoramicRig: unhid pipeline actors")

    script_path = os.path.normpath(
        os.path.join(
            unreal.Paths.project_dir(),
            "Source",
            "VirtualProductionSplatEditor",
            "StitchEquirectangular.py",
        )
    )
    saved_dir = unreal.Paths.project_saved_dir()

    if os.path.exists(script_path):
        ue_python = _get_ue_python_exe()
        unreal.log("SetupPanoramicRig: running stitch as subprocess via %s" % ue_python)
        try:
            result = subprocess.run(
                [ue_python, script_path, saved_dir],
                capture_output=True,
                text=True,
                timeout=120,
            )
            if result.returncode == 0:
                unreal.log("SetupPanoramicRig: stitch complete\n%s" % (result.stdout or ""))
            else:
                unreal.log_error("SetupPanoramicRig: stitch failed\n%s" % (result.stderr or result.stdout or ""))
        except subprocess.TimeoutExpired:
            unreal.log_error("SetupPanoramicRig: stitch subprocess timed out after 120s")
        except Exception as e:
            unreal.log_error("SetupPanoramicRig: stitch subprocess error: %s" % e)
    else:
        unreal.log_error("SetupPanoramicRig: stitch script not found at %s" % script_path)

    unreal.log("SetupPanoramicRig: done. Check Saved/GreyboxExports/panorama_360.png")


setup_and_capture()
