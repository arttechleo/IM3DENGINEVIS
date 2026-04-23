#!/usr/bin/env python3
"""
Pre-flight checks for VirtualProductionSplat.
Exit 0 = all clear. Exit 1 = one or more FAIL.
"""

import os
import sys
import json
import re
import subprocess
import configparser
import pathlib

PROJECT_ROOT = pathlib.Path(__file__).resolve().parent.parent
UE_ROOT = pathlib.Path("S:/EpicGamesLauncher/UE_5.7")
UE_PYTHON = UE_ROOT / "Engine/Binaries/ThirdParty/Python3/Win64/python.exe"

results = []  # (status, name, detail)

PASS = "PASS"
FAIL = "FAIL"
WARN = "WARN"


def record(status, name, detail=""):
    results.append((status, name, detail))


# ---------------------------------------------------------------------------
# CONFIG CHECKS
# ---------------------------------------------------------------------------

def check_api_key_worldlabs():
    ini = PROJECT_ROOT / "Config/DefaultGame.ini"
    cfg = configparser.ConfigParser(strict=False)
    cfg.read(str(ini))
    key = cfg.get("WorldLabsAPI", "APIKey", fallback="").strip()
    if not key or "YOUR_KEY" in key.upper():
        record(FAIL, "APIKey_WorldLabs", "empty or placeholder in Config/DefaultGame.ini")
    else:
        record(PASS, "APIKey_WorldLabs")


def check_api_key_anthropic():
    ini = PROJECT_ROOT / "Config/DefaultGame.ini"
    cfg = configparser.ConfigParser(strict=False)
    cfg.read(str(ini))
    key = cfg.get("AnthropicAPI", "APIKey", fallback="").strip()
    if not key or "YOUR_KEY" in key.upper():
        record(FAIL, "APIKey_Anthropic", "empty or placeholder in Config/DefaultGame.ini")
    else:
        record(PASS, "APIKey_Anthropic")


def check_defaultgame_not_tracked():
    try:
        out = subprocess.check_output(
            ["git", "ls-files", "Config/DefaultGame.ini"],
            cwd=str(PROJECT_ROOT), text=True, stderr=subprocess.DEVNULL
        ).strip()
        if out:
            record(FAIL, "DefaultGame_NotTracked",
                   "Config/DefaultGame.ini is tracked by git — API keys will be exposed")
        else:
            record(PASS, "DefaultGame_NotTracked")
    except Exception as e:
        record(WARN, "DefaultGame_NotTracked", f"git check failed: {e}")


# ---------------------------------------------------------------------------
# PLUGIN CHECKS
# ---------------------------------------------------------------------------

def check_mlslabs_uplugin():
    uplugin = PROJECT_ROOT / "Plugins/MLSLabsRenderer/MLSLabsRenderer.uplugin"
    if not uplugin.exists():
        record(FAIL, "MLSLabsRenderer_Uplugin", f"not found: {uplugin}")
        return
    try:
        with open(uplugin) as f:
            data = json.load(f)
    except Exception as e:
        record(FAIL, "MLSLabsRenderer_Uplugin", f"JSON parse error: {e}")
        return
    # Vendor ships PostConfigInit — cannot change without modifying plugin files.
    # Accepted phases: PostEngineInit (ideal) or PostConfigInit (vendor default).
    accepted = {"PostEngineInit", "PostConfigInit", "Default"}
    for mod in data.get("Modules", []):
        phase = mod.get("LoadingPhase", "")
        if phase not in accepted:
            record(FAIL, "MLSLabsRenderer_Uplugin",
                   f"module '{mod.get('Name')}' LoadingPhase={phase!r} — unexpected value")
            return
    phases = [(m.get("Name"), m.get("LoadingPhase")) for m in data.get("Modules", [])]
    record(PASS, "MLSLabsRenderer_Uplugin",
           " | ".join(f"{n}={p}" for n, p in phases))


def check_mlslabs_torch_dll():
    dll = PROJECT_ROOT / "Plugins/MLSLabsRenderer/Source/ThirdParty/libTorch/lib/torch_cuda.dll"
    if not dll.exists():
        record(FAIL, "MLSLabsRenderer_TorchDLL",
               f"torch_cuda.dll missing — git filter-repo may have wiped ThirdParty DLLs")
    else:
        record(PASS, "MLSLabsRenderer_TorchDLL")


def check_mlslabs_binaries():
    dll = PROJECT_ROOT / "Plugins/MLSLabsRenderer/Binaries/Win64/UnrealEditor-MLSLabsRenderer.dll"
    if not dll.exists():
        record(WARN, "MLSLabsRenderer_Binaries", "plugin DLL missing — needs rebuild")
    else:
        record(PASS, "MLSLabsRenderer_Binaries")


def check_unrealclaude_uplugin():
    uplugin = PROJECT_ROOT / "Plugins/UnrealClaude/UnrealClaude.uplugin"
    if not uplugin.exists():
        record(WARN, "UnrealClaude_Uplugin", f"not found: {uplugin}")
        return
    with open(uplugin) as f:
        data = json.load(f)
    for mod in data.get("Modules", []):
        phase = mod.get("LoadingPhase", "")
        if phase == "PostConfigInit":
            record(WARN, "UnrealClaude_Uplugin",
                   f"module '{mod.get('Name')}' LoadingPhase=PostConfigInit (may cause ordering issues)")
            return
    record(PASS, "UnrealClaude_Uplugin")


# ---------------------------------------------------------------------------
# SOURCE CHECKS
# ---------------------------------------------------------------------------

API_KEY_PATTERN = re.compile(r'sk-ant-[a-zA-Z0-9]|wlt_[a-zA-Z0-9]')


def check_no_hardcoded_api_keys():
    scan_dirs = [
        PROJECT_ROOT / "Source",
        PROJECT_ROOT / "Plugins/VirtualProductionSplat",
    ]
    hits = []
    for d in scan_dirs:
        if not d.exists():
            continue
        for f in d.rglob("*"):
            if f.suffix not in (".cpp", ".h", ".py", ".cs"):
                continue
            try:
                text = f.read_text(encoding="utf-8", errors="ignore")
                if API_KEY_PATTERN.search(text):
                    hits.append(str(f.relative_to(PROJECT_ROOT)))
            except Exception:
                pass
    if hits:
        record(FAIL, "NoHardcodedAPIKeys", "found in: " + ", ".join(hits))
    else:
        record(PASS, "NoHardcodedAPIKeys")


def check_uproject_plugins():
    uproject = PROJECT_ROOT / "VirtualProductionSplat.uproject"
    if not uproject.exists():
        record(FAIL, "UProjectPlugins", "VirtualProductionSplat.uproject not found")
        return
    with open(uproject) as f:
        data = json.load(f)
    required = {"MLSLabsRenderer", "UnrealClaude"}
    found = {}
    for p in data.get("Plugins", []):
        name = p.get("Name", "")
        if name in required:
            found[name] = p.get("Enabled", True)
    missing = required - set(found.keys())
    disabled = [n for n, en in found.items() if not en]
    if missing:
        record(FAIL, "UProjectPlugins", f"missing plugins: {missing}")
    elif disabled:
        record(FAIL, "UProjectPlugins", f"disabled plugins: {disabled}")
    else:
        record(PASS, "UProjectPlugins")


def check_panorama_script():
    script = PROJECT_ROOT / "Source/VirtualProductionSplatEditor/StitchEquirectangular.py"
    if not script.exists():
        record(FAIL, "PanoramaScript", f"StitchEquirectangular.py not found at {script}")
    else:
        record(PASS, "PanoramaScript")


def check_convert_script():
    # Search common locations
    candidates = [
        PROJECT_ROOT / "Source/VirtualProductionSplatEditor/ConvertSpzToPly.py",
        PROJECT_ROOT / "Source/VirtualProductionSplat/ConvertSpzToPly.py",
        PROJECT_ROOT / "Tools/ConvertSpzToPly.py",
    ]
    for c in candidates:
        if c.exists():
            record(PASS, "ConvertScript", str(c.relative_to(PROJECT_ROOT)))
            return
    record(FAIL, "ConvertScript",
           "ConvertSpzToPly.py not found — checked Source/VirtualProductionSplatEditor/, Source/VirtualProductionSplat/, Tools/")


def check_ue_python():
    if UE_PYTHON.exists():
        record(PASS, "UEPythonExe")
    else:
        record(FAIL, "UEPythonExe", f"not found: {UE_PYTHON}")


def check_torch_importable():
    if not UE_PYTHON.exists():
        record(WARN, "TorchImportable", "UE Python not found — skipping import check")
        return
    try:
        out = subprocess.check_output(
            [str(UE_PYTHON), "-c", "import torch; print('ok', torch.__version__)"],
            text=True, timeout=15, stderr=subprocess.STDOUT
        )
        record(PASS, "TorchImportable", out.strip())
    except subprocess.CalledProcessError as e:
        record(FAIL, "TorchImportable", e.output.strip()[:200])
    except Exception as e:
        record(WARN, "TorchImportable", f"check failed: {e}")


def check_numpy_importable():
    if not UE_PYTHON.exists():
        record(WARN, "NumpyImportable", "UE Python not found — skipping import check")
        return
    try:
        subprocess.check_output(
            [str(UE_PYTHON), "-c", "import numpy"],
            text=True, timeout=10, stderr=subprocess.STDOUT
        )
        record(PASS, "NumpyImportable")
    except subprocess.CalledProcessError as e:
        record(FAIL, "NumpyImportable", e.output.strip()[:200])
    except Exception as e:
        record(WARN, "NumpyImportable", f"check failed: {e}")


# ---------------------------------------------------------------------------
# BUILD CHECKS
# ---------------------------------------------------------------------------

def check_ddc_size():
    ddc = PROJECT_ROOT / "Saved/DerivedDataCache"
    if not ddc.exists():
        record(PASS, "DDCSize", "DDC directory not present")
        return
    total = sum(f.stat().st_size for f in ddc.rglob("*") if f.is_file())
    gb = total / 1024 ** 3
    if gb > 10.0:
        record(WARN, "DDCSize", f"{gb:.1f} GB — consider wiping with fix_mlslabs.bat")
    else:
        record(PASS, "DDCSize", f"{gb:.1f} GB")


def check_no_nested_git():
    nested = []
    for plugin_dir in ["Plugins/SplatRenderer", "Plugins/UnrealClaude"]:
        git_dir = PROJECT_ROOT / plugin_dir / ".git"
        if git_dir.exists():
            nested.append(plugin_dir)
    if nested:
        record(WARN, "NoNestedGit", f"nested .git found in: {nested} — may cause gitlink issues")
    else:
        record(PASS, "NoNestedGit")


def check_git_status_clean():
    try:
        out = subprocess.check_output(
            ["git", "status", "--porcelain"],
            cwd=str(PROJECT_ROOT), text=True, stderr=subprocess.DEVNULL
        ).strip()
        if out:
            # Count only tracked modified files (lines not starting with ??)
            tracked = [l for l in out.splitlines() if not l.startswith("??")]
            if tracked:
                record(WARN, "GitStatus_Clean",
                       f"{len(tracked)} tracked file(s) modified (unsaved changes)")
                return
        record(PASS, "GitStatus_Clean")
    except Exception as e:
        record(WARN, "GitStatus_Clean", f"git check failed: {e}")


# ---------------------------------------------------------------------------
# RUN ALL CHECKS
# ---------------------------------------------------------------------------

check_api_key_worldlabs()
check_api_key_anthropic()
check_defaultgame_not_tracked()
check_mlslabs_uplugin()
check_mlslabs_torch_dll()
check_mlslabs_binaries()
check_unrealclaude_uplugin()
check_no_hardcoded_api_keys()
check_uproject_plugins()
check_panorama_script()
check_convert_script()
check_ue_python()
check_torch_importable()
check_numpy_importable()
check_ddc_size()
check_no_nested_git()
check_git_status_clean()

# ---------------------------------------------------------------------------
# PRINT SUMMARY
# ---------------------------------------------------------------------------

icons = {PASS: "[PASS]", FAIL: "[FAIL]", WARN: "[WARN]"}

import io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

print()
print("=" * 64)
print("  VirtualProductionSplat Pre-flight Checks")
print("=" * 64)
for status, name, detail in results:
    icon = icons[status]
    line = f"  {icon}  {name}"
    if detail:
        line += f" -- {detail}"
    print(line)
print("=" * 64)

fail_count = sum(1 for s, _, _ in results if s == FAIL)
warn_count = sum(1 for s, _, _ in results if s == WARN)
print(f"\n  {len(results)} checks: {fail_count} FAIL, {warn_count} WARN\n")

sys.exit(1 if fail_count > 0 else 0)
