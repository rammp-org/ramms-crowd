"""Builds crowd motion matching content from the Game Animation Sample (GASP) and
assigns it to the crowd animation set (experimental; see doc/CUSTOMIZATION.md,
"Motion matching").

GASP's relaxed walk is captured on the UEFN mannequin. This script:
  1. copies the GASP pieces it needs into this project at their original paths
     (mannequin mesh and rigs, walk and idle clips, MetaHuman retarget rigs, the
     motion matching databases) - skip this if they are already here;
  2. retargets the clips those databases use onto the City Sample crowd body
     into CLIP_PATH, with a copy of RTG_UEFN_to_Metahuman_nrw that copies the
     leg joint rotations (FK) instead of re-solving the legs with IK. The IK
     version kept the feet exactly on their source paths but re-bent the knees:
     straighter at heel strike, more crouched in stance and stiffer in the swing.
     FK keeps the captured knee and ankle motion; the feet then drift a centimetre
     or so, which the runtime foot locking absorbs;
  3. copies GASP's relaxed walk databases onto the crowd skeleton, keeping their
     tuning (URammsCrowdMotionMatchingLibrary.adapt_pose_search_databases);
  4. fills the anim set's MotionMatching sets (Stand / TurnInPlace / Start / Move / Stop).

Run from the editor Python console:
    py "<plugin>/Content/Python/ramms_crowd_build_motion_matching.py"

Create the GASP project from Fab (Unreal Engine 5.8) first and set GASP_CONTENT.
Re-running replaces the generated assets. Set SAVE_ANIM_SET = False to try the
result without saving the anim set (the change then lasts until the editor closes).
"""

import os
import shutil

import unreal

GASP_CONTENT = r"C:/Users/waemf/data/GameAnimationSample/Content"
TARGET_MESH = "/Game/Fab/CitySampleCrowd/Character/Male/NormalWeight/Meshes/m_tal_nrw_base"
# FK-retargeted clips and their databases. (/Game/CrowdMM/Anims and /Databases held
# an earlier IK-retargeted build and are no longer used.)
CLIP_PATH = "/Game/CrowdMM/AnimsFK"
DATABASE_PATH = "/Game/CrowdMM/DatabasesFK"
ANIM_SET_PATH = "/Game/NPCs/DA_CrowdAnimSet"
SAVE_ANIM_SET = True
TAG = "[RammsCrowd motion matching] "

COPY = [
    "Characters/UEFN_Mannequin/Meshes",
    "Characters/UEFN_Mannequin/Rigs",
    "Characters/UEFN_Mannequin/Materials",
    "Characters/UEFN_Mannequin/Textures",
    "Characters/UEFN_Mannequin/Animations/Walk",
    "Characters/UEFN_Mannequin/Animations/Idle",
    "Characters/UEFN_Mannequin/Animations/MotionMatchingData",
    "MetaHumans/Common",
]
SOURCE_DATABASES = "/Game/Characters/UEFN_Mannequin/Animations/MotionMatchingData/Databases/Relaxed/"
SETS = {
    "stand": ["PSD_Relaxed_Stand_Idles"],
    "turn_in_place": ["PSD_Relaxed_Stand_TurnInPlace"],
    "start": ["PSD_Relaxed_Stand_Walk_F_Starts"],
    "move": ["PSD_Relaxed_Stand_Walk_Loops", "PSD_Relaxed_Stand_Walk_F_Turns", "PSD_Relaxed_Stand_Walk_F_Pivots"],
    "stop": ["PSD_Relaxed_Stand_Walk_Stops"],
}
RETARGETER = "/Game/MetaHumans/Common/Common/Rigs/RTG_UEFN_to_Metahuman_nrw"
FK_RETARGETER = "/Game/CrowdMM/Rigs/RTG_UEFN_to_MH_FK"
# Retarget ops that re-solve the limbs with IK; the FK copy turns them off.
IK_OPS = ("IKRetargetOffsetGoalsController", "IKRetargetSpeedPlantingController",
          "IKRetargetRunIKRigController", "IKRetargetAlignPoleVectorController")
SOURCE_MESH = "/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin"


def copy_gasp_content():
    project_content = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())
    for sub in COPY:
        source = os.path.join(GASP_CONTENT, sub)
        target = os.path.join(project_content, sub)
        if os.path.exists(target):
            continue
        if not os.path.isdir(source):
            raise RuntimeError(TAG + "missing " + source + " (create the GASP project from Fab, or fix GASP_CONTENT)")
        shutil.copytree(source, target)
        unreal.log(TAG + "copied " + sub)
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous(["/Game/Characters", "/Game/MetaHumans"], True)


def make_fk_retargeter():
    """A copy of RETARGETER with the IK ops turned off (legs retargeted by FK)."""
    if not unreal.EditorAssetLibrary.does_asset_exist(FK_RETARGETER):
        unreal.EditorAssetLibrary.duplicate_asset(RETARGETER, FK_RETARGETER)
    retargeter = unreal.load_asset(FK_RETARGETER)
    controller = unreal.IKRetargeterController.get_controller(retargeter)
    for index in range(controller.get_num_retarget_ops()):
        op = controller.get_op_controller(index)
        if op is not None and type(op).__name__ in IK_OPS:
            controller.set_retarget_op_enabled(index, False)
    unreal.EditorAssetLibrary.save_loaded_asset(retargeter, False)
    return retargeter


def source_clips():
    """The animation sequences the SETS databases use."""
    paths = set()
    for group in SETS.values():
        for name in group:
            database = unreal.load_asset(SOURCE_DATABASES + name)
            if database is None:
                continue
            for index in range(database.get_num_animation_assets()):
                asset = database.get_animation_asset(index)
                if isinstance(asset, unreal.AnimSequence):
                    paths.add(asset.get_path_name().split(".")[0])
    return [unreal.EditorAssetLibrary.find_asset_data(path) for path in sorted(paths)]


def retarget_clips():
    clips = source_clips()
    made = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
        clips, unreal.load_asset(SOURCE_MESH), unreal.load_asset(TARGET_MESH), make_fk_retargeter(),
        "", "", "", "_MH", CLIP_PATH, False, False, True)
    unreal.EditorAssetLibrary.save_directory(CLIP_PATH, only_if_is_dirty=True, recursive=True)
    unreal.log(TAG + "retargeted %d clips" % len(made))


def build_databases():
    skeleton = unreal.load_asset(TARGET_MESH).get_editor_property("skeleton")
    names = sorted({name for group in SETS.values() for name in group})
    sources = [unreal.load_asset(SOURCE_DATABASES + name) for name in names]
    made = unreal.RammsCrowdMotionMatchingLibrary.adapt_pose_search_databases(
        [s for s in sources if s], skeleton, CLIP_PATH, "_MH", DATABASE_PATH, "MH")
    return {db.get_name(): db for db in made}


def assign(databases):
    anim_set = unreal.load_asset(ANIM_SET_PATH)
    sets = unreal.RammsCrowdMotionDatabases()
    for field, group in SETS.items():
        found = []
        for name in group:
            source = unreal.load_asset(SOURCE_DATABASES + name)
            db = databases.get((source.get_name() if source else name) + "_MH")
            if db:
                found.append(db)
            else:
                unreal.log_warning(TAG + "no database for " + name)
        sets.set_editor_property(field, found)
    anim_set.set_editor_property("motion_matching", sets)
    anim_set.set_editor_property("use_motion_matching", True)
    if SAVE_ANIM_SET:
        unreal.EditorAssetLibrary.save_loaded_asset(anim_set, False)
    unreal.log(TAG + "assigned to " + ANIM_SET_PATH)


def main():
    copy_gasp_content()
    retarget_clips()
    assign(build_databases())


if __name__ == "__main__":
    main()
