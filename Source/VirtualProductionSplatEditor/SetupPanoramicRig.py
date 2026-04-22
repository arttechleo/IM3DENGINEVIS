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

    mesh_actors = [a for a in all_actors if a.get_class().get_name() == "StaticMeshActor"]

    if mesh_actors:
        min_x = min(a.get_actor_location().x for a in mesh_actors)
        max_x = max(a.get_actor_location().x for a in mesh_actors)
        min_y = min(a.get_actor_location().y for a in mesh_actors)
        max_y = max(a.get_actor_location().y for a in mesh_actors)
        center_x = (min_x + max_x) / 2.0
        center_y = (min_y + max_y) / 2.0
    else:
        center_x = 0.0
        center_y = 0.0

    eye_level_z = 180.0
    new_location = unreal.Vector(center_x, center_y, eye_level_z)
    panoramic_rig.set_actor_location(new_location, False, True)
    unreal.log("SetupPanoramicRig: moved rig to %s" % new_location)

    hide_class_names = [
        "PanoramicCapture360",
        "GreyboxSceneBuilder",
        "PanoramicExportRunner",
        "WorldLabsRunner",
        "GaussianSplatImportRunner",
        "VPStageSetup",
        "VPPipelineOrchestrator",
        "CineCameraActor",
    ]

    actors_to_hide = []
    for actor in all_actors:
        class_name = actor.get_class().get_name()
        if any(h == class_name or (h in class_name) for h in hide_class_names):
            actors_to_hide.append(actor)

    # Also hide actors whose labels suggest they cause sky artifacts
    additional_hide_labels = ["Ramp", "HorizonPlane", "Horizon"]
    for actor in all_actors:
        label = actor.get_actor_label()
        if any(h.lower() in label.lower() for h in additional_hide_labels):
            if actor not in actors_to_hide:
                actors_to_hide.append(actor)
                unreal.log(f"SetupPanoramicRig: hiding artifact actor: {label}")

    for actor in actors_to_hide:
        if hasattr(actor, "set_actor_hidden_in_game"):
            actor.set_actor_hidden_in_game(True)
        elif hasattr(actor, "SetActorHiddenInGame"):
            actor.SetActorHiddenInGame(True)

    unreal.log("SetupPanoramicRig: hid %d pipeline actors from game/capture" % len(actors_to_hide))

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
