"""Optional deep-clean after the City Sample Crowds migration.

The automatic startup migration (init_unreal.py) leaves redirectors — and
possibly duplicate assets pinned by the pack's demo level, whose references the
bulk rename cannot rewrite without loading it — at the old /Game/CitySampleCrowd
path. Everything works through them and the folder is git-ignored, so running
this is cosmetic. It loads the demo level, consolidates every old-path asset
into its migrated twin, saves, and deletes the old folder.

Run from the editor Python console:
    py "<plugin>/Content/Python/ramms_crowd_cleanup_citysample.py"
"""

import unreal

OLD = "/Game/CitySampleCrowd"
FAB = "/Game/Fab/CitySampleCrowd"
DEMO_LEVEL = FAB + "/Maps/CitySampleCrowd_LVL"
TAG = "[RammsCrowd cleanup] "


def log(msg, warn=False):
    (unreal.log_warning if warn else unreal.log)(TAG + str(msg))


ar = unreal.AssetRegistryHelpers.get_asset_registry()
old_assets = [
    str(a.package_name)
    for a in ar.get_assets_by_path(OLD, recursive=True)
    if str(a.asset_class_path.asset_name) != "ObjectRedirector"
]

if old_assets:
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    previous_level = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name().split(".")[0]
    if unreal.EditorAssetLibrary.does_asset_exist(DEMO_LEVEL):
        log(f"loading demo level to fix its references ({len(old_assets)} old-path assets)...")
        les.load_level(DEMO_LEVEL)
    consolidated = 0
    for i, old_pkg in enumerate(old_assets):
        fab_pkg = old_pkg.replace(OLD, FAB, 1)
        old_a = unreal.load_asset(old_pkg)
        fab_a = unreal.load_asset(fab_pkg)
        if old_a and fab_a and unreal.EditorAssetLibrary.consolidate_assets(fab_a, [old_a]):
            consolidated += 1
        if (i + 1) % 50 == 0:
            log(f"consolidated {i + 1}/{len(old_assets)}...")
            unreal.SystemLibrary.collect_garbage()
    log(f"consolidated {consolidated}/{len(old_assets)}")
    les.save_current_level()
    les.load_level(previous_level)

# Rewrite every remaining referencer (hard AND soft) through the redirectors,
# then delete the redirectors — this is what prevents the old path from being
# "resurrected" by later loads through stale soft references.
try:
    n = unreal.RammsCrowdContentLibrary.fix_up_redirectors_in_folder(OLD)
    log(f"redirectors fixed up: {n}")
except Exception as e:
    log(f"redirector fixup unavailable: {e}", warn=True)

unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)
unreal.SystemLibrary.collect_garbage()
if unreal.EditorAssetLibrary.does_directory_exist(OLD):
    ok = unreal.EditorAssetLibrary.delete_directory(OLD)
    log(f"old directory removed: {ok}")
    if not ok:
        log("directory not fully removable from the editor; any remaining "
            "files under Content/CitySampleCrowd are unreferenced and can be "
            "deleted from the filesystem with the editor closed.", warn=True)
else:
    log("old directory already gone — nothing to do.")
