"""Assigns the foot-placement post-process AnimBP to every City Sample crowd
body mesh, so crowd feet conform to minor obstacles / ramps (see doc: foot IK).

The crowd is animated WITHOUT a main AnimBP (URammsCrowdAnimComponent plays a
blend space directly on the mesh). A post-process AnimBP on the skeletal MESH
ASSET evaluates on top of that single-node playback, which is where the
UE Foot Placement node (AnimationWarping plugin) lives. Modular character
parts follow the leader body component's pose, so assigning to every SK_Base
mesh is safe: only components that actually evaluate animation run the graph.

Prerequisites:
  1. AnimationWarping plugin available (RammsCrowd.uplugin declares it as a
     plugin dependency, so host projects need no extra enablement).
  2. The post-process AnimBP exists (default: /Game/NPCs/ABP_Crowd_FootPlacement_PP,
     Input Pose -> Foot Placement -> Output Pose, skeleton = SK_Base).

Run from the editor Python console:
    py "<plugin>/Content/Python/ramms_crowd_assign_foot_ik.py"

Idempotent; re-run after installing/migrating the pack. Pass a different ABP
path by editing ABP_PATH below. Set REVERT = True to clear the assignment.
"""

import unreal

ABP_PATH = "/Game/NPCs/ABP_Crowd_FootPlacement_PP"
PACK_ROOT = "/Game/Fab/CitySampleCrowd"
SKELETON_NAME = "SK_Base"
# Evaluate the post-process graph only on close LODs (0..LOD_THRESHOLD);
# distant agents are proxy/ISM representations anyway.
LOD_THRESHOLD = 1
REVERT = False
TAG = "[RammsCrowd foot-ik] "


SKELETON_PATH = PACK_ROOT + "/Character/Shared/Rig/" + SKELETON_NAME


def _resolve_skeleton():
    asset = unreal.load_asset(SKELETON_PATH)
    if isinstance(asset, unreal.SkeletalMesh):
        return asset.get_editor_property("skeleton")
    return asset


def main():
    abp_class = None
    if not REVERT:
        skeleton = _resolve_skeleton()
        if skeleton is None:
            unreal.log_error(TAG + "Skeleton not found at %s - is the City Sample pack installed?" % SKELETON_PATH)
            return
        # Always (re)scaffold: the library rebuilds an existing asset's graph in
        # place, so this picks up scaffolder changes (e.g. node class swaps).
        package_path, asset_name = ABP_PATH.rsplit("/", 1)
        abp = unreal.RammsCrowdAnimAssetLibrary.create_foot_placement_post_process_anim_blueprint(
            package_path, asset_name, skeleton)
        if abp is None:
            unreal.log_error(TAG + "Scaffolder failed - see log above.")
            return
        unreal.EditorAssetLibrary.save_loaded_asset(abp, only_if_is_dirty=True)
        abp_class = abp.generated_class()
        if abp_class is None:
            unreal.log_error(TAG + "AnimBP at %s has no generated class - compile it in the editor." % ABP_PATH)
            return

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    ar_filter = unreal.ARFilter(
        package_paths=[PACK_ROOT],
        recursive_paths=True,
        class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "SkeletalMesh")],
    )
    assets = registry.get_assets(ar_filter)

    touched = 0
    skipped = 0
    for asset_data in assets:
        mesh = asset_data.get_asset()
        if mesh is None:
            continue
        skeleton = mesh.get_editor_property("skeleton")
        if skeleton is None or skeleton.get_name() != SKELETON_NAME:
            skipped += 1
            continue

        current = mesh.get_editor_property("post_process_anim_blueprint")
        desired = None if REVERT else abp_class
        if current == desired:
            continue
        mesh.set_editor_property("post_process_anim_blueprint", desired)
        try:
            # Not python-exposed in 5.7 (private UPROPERTY); best-effort. Distant
            # agents are ISM proxies with no anim evaluation, so the cost of
            # all-LOD evaluation on near actors is acceptable without it.
            mesh.set_editor_property("post_process_anim_bp_lod_threshold", LOD_THRESHOLD if not REVERT else -1)
        except Exception:
            pass
        unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=True)
        touched += 1

    unreal.log(TAG + "%s %d mesh(es) (skipped %d non-%s); LOD threshold=%d"
        % ("cleared" if REVERT else "assigned", touched, skipped, SKELETON_NAME, LOD_THRESHOLD))


main()
