"""RammsCrowd content bootstrap — runs automatically at editor startup.

Validates that the (uncommitted, user-installed) City Sample Crowds pack is
present where the crowd assets expect it, and performs the one-time migration
from Fab's default install location. No user action needed beyond installing
the pack from the Epic Launcher. See doc/SETUP.md §0.
"""

import os

import unreal

PACK_FAB = "/Game/Fab/CitySampleCrowd"
PACK_DEFAULT = "/Game/CitySampleCrowd"
KEY_ASSET = "/Character/Shared/Rig/SK_Base"  # cheap presence probe
TAG = "[RammsCrowd setup] "

_state = {"handle": None, "ran": False}


def _content_file(game_path):
    """Map /Game/... asset path to the .uasset file on disk."""
    rel = game_path.replace("/Game/", "", 1) + ".uasset"
    return os.path.join(unreal.SystemLibrary.get_project_content_directory(), rel)


FAB_SEARCH_URL = "https://www.fab.com/search?q=city%20sample%20crowds"


def _log(msg, warn=False):
    (unreal.log_warning if warn else unreal.log)(TAG + msg)


def _toast(msg, warn=False, link_text="", link_url="", duration=12.0):
    """Editor toast (bottom-right) so setup issues are visible without the log."""
    try:
        unreal.RammsCrowdContentLibrary.show_setup_notification(msg, link_text, link_url, duration, warn)
    except Exception:
        pass  # library not built yet / headless — the log line still happened


def _has_pack(root):
    # Probe the FILESYSTEM, not the asset registry: at startup the registry can be
    # stale or mid-scan, and a wrong answer here once triggered a bogus migration.
    return os.path.isfile(_content_file(root + KEY_ASSET))


def _run_setup():
    fab = _has_pack(PACK_FAB)
    default = _has_pack(PACK_DEFAULT)

    if fab and not default:
        _log(f"City Sample Crowds OK at {PACK_FAB}.")
        return

    if fab and default:
        msg = (
            f"City Sample Crowds found at BOTH {PACK_FAB} and {PACK_DEFAULT}. "
            "Leaving both untouched — delete the copy you do not want "
            "(the crowd assets use the Fab path). See RammsCrowd doc/SETUP.md."
        )
        _log(msg, warn=True)
        _toast("RammsCrowd: City Sample Crowds exists at two paths — see Output Log.", warn=True)
        return

    if default and not fab:
        _log(
            f"City Sample Crowds found at its default install path — migrating to "
            f"{PACK_FAB} (one-time, may take a few minutes; the editor will be busy)..."
        )
        _toast("RammsCrowd: migrating City Sample Crowds to Content/Fab — one-time, the editor will be busy for a few minutes...", duration=8.0)
        ok = unreal.EditorAssetLibrary.rename_directory(PACK_DEFAULT, PACK_FAB)
        if ok:
            _log(
                f"Migration complete. Crowd content now at {PACK_FAB}. "
                f"Redirectors/leftovers may remain at {PACK_DEFAULT} (harmless, "
                "git-ignored); run ramms_crowd_cleanup_citysample.py to remove them."
            )
            _toast("RammsCrowd: City Sample Crowds migration complete — crowd characters ready.")
        else:
            _log(
                "Automatic migration failed. In the Content Browser, drag the "
                f"CitySampleCrowd folder into Fab/ manually, or re-run "
                f"init_unreal.py. See RammsCrowd doc/SETUP.md.",
                warn=True,
            )
            _toast("RammsCrowd: automatic content migration FAILED — see Output Log for manual steps.", warn=True, duration=20.0)
        return

    _log(
        "City Sample Crowds is NOT installed — crowd agents will simulate but "
        "render as proxy meshes only (no characters). Install it free from the "
        "Epic Games Launcher: Fab Library -> 'City Sample Crowds' -> Add to "
        "Project, then restart the editor (migration then runs automatically). "
        "See Plugins/RammsCrowd/doc/SETUP.md §0.",
        warn=True,
    )
    _toast(
        "RammsCrowd: City Sample Crowds is not installed — crowd agents will "
        "render as proxy meshes. Get it free on Fab, add to project, restart.",
        warn=True,
        link_text="Open Fab: City Sample Crowds",
        link_url=FAB_SEARCH_URL,
        duration=20.0,
    )


def _tick(_delta):
    # Defer until the asset registry finishes its initial scan, then run once.
    try:
        if unreal.AssetRegistryHelpers.get_asset_registry().is_loading_assets():
            return
    except Exception:
        return
    if _state["handle"] is not None:
        unreal.unregister_slate_post_tick_callback(_state["handle"])
        _state["handle"] = None
    if _state["ran"]:
        return
    _state["ran"] = True
    try:
        _run_setup()
    except Exception as exc:  # never break editor startup
        _log(f"setup check failed: {exc}", warn=True)


if hasattr(unreal, "EditorAssetLibrary"):
    _state["handle"] = unreal.register_slate_post_tick_callback(_tick)
