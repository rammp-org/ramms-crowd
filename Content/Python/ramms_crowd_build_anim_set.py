"""Builds the crowd animation set (URammsCrowdAnimSet) from the City Sample Crowds
pack and points the crowd character blueprint's RammsCrowdAnim component at it.

With the anim set assigned, URammsCrowdAnimComponent installs
URammsCrowdAnimInstance on each pedestrian: body-type walk and idle variants,
speed-matched directional walks, carried props (phone, cup, bag, briefcase,
purse) with their hold poses, head look-at and bump / startle reactions. See
doc/CUSTOMIZATION.md, "Crowd animation".

Run from the editor Python console (idempotent; re-run after updating the pack):
    py "<plugin>/Content/Python/ramms_crowd_build_anim_set.py"

Walk speeds and directions are measured from each clip's root track, so playback
can match the feet to the agent's speed. Set ASSIGN_TO = None to only build the
asset.
"""

import math

import unreal

PACK_ROOT = "/Game/Fab/CitySampleCrowd"
LOCO_ROOT = PACK_ROOT + "/Character/Anims/Loco"
ACCESSORY_ROOT = PACK_ROOT + "/Character/Accessories"
ANIM_SET_PATH = "/Game/NPCs/DA_CrowdAnimSet"
ASSIGN_TO = "/Game/NPCs/BP_CrowdPerson_CS"
TAG = "[RammsCrowd anim set] "

# City Sample body keys (gender_build) and their animation prefixes.
BODIES = [
    ("f_nrw", "FTN"), ("f_ovw", "FTO"), ("f_unw", "FTU"),
    ("m_nrw", "MTN"), ("m_ovw", "MTO"), ("m_unw", "MTU"),
]
# Male underweight backpacks are spelled "uvw" in the pack.
BACKPACK_BUILD = {"m_unw": "m_uvw"}

WALK_SUFFIXES = ["_N_Walk_F", "_N_Walk_F_VarA", "_N_Walk_F_VarB", "_N_Walk_F_VarE", "_N_Walk_F_VarF"]
IDLE_SUFFIXES = ["_N_Idle", "_N_Idle_Base", "_N_Idle_VarA", "_N_Idle_VarB", "_N_Idle_VarC", "_N_Idle_VarD",
                 "_N_Idle_B", "_N_Idle_C", "_N_Idle_D", "_N_Idle_E"]
DIRECTIONALS = ["MTN_N_Walk_45FL", "MTN_N_Walk_45FR", "MTN_N_Walk_LF", "MTN_N_Walk_RF",
                "MTN_N_Walk_45BL", "MTN_N_Walk_45BR", "MTN_N_Walk_B"]
REACTIONS = ["FTN_BlockReact_A", "FTN_BlockReact_B", "FTN_BlockReact_C", "FTN_BlockReact_D",
             "FTN_BlockReact_E", "FTN_BlockReact_F", "FTN_N_BlockReact"]
# Slower than this is an in-place or broken clip, not a walk.
MIN_WALK_SPEED = 60.0

Arms = unreal.RammsCrowdCarryArms
# (name, weight, anim suffix(es), arms, layer head, props, socket); "{body}" in a
# prop name is the backpack body key. Each anim is looked up for the body's own
# prefix first, then the normal-weight one.
CARRIES = {
    "m": [
        ("phone_look", 1.0, ["_Phone_LookAt_A"], Arms.LEFT, True, ["phoneA"], "weapon_l"),
        ("phone_talk", 0.7, ["_Phone_Talk_A"], Arms.LEFT, False, ["phoneA"], "weapon_l"),
        ("phone_hold", 0.5, ["_Phone_Hold_A"], Arms.LEFT, True, ["phoneA"], "weapon_l"),
        ("cup", 0.8, ["_Cup_Hold_A"], Arms.RIGHT, False, ["cupA"], "weapon_r"),
        ("cup_two_hands", 0.3, ["_Cup_Hold_B"], Arms.BOTH, False, ["cupB"], "weapon_r"),
        ("backpack_left", 0.5, ["_Backpack_Hold_A"], Arms.LEFT, False, ["{body}_backpackA", "{body}_backpackB"], "Accessory_Backpack"),
        ("backpack_right", 0.5, ["_Backpack_Hold_B"], Arms.RIGHT, False, ["{body}_backpackA", "{body}_backpackB"], "Accessory_Backpack"),
        ("briefcase", 0.6, ["_Briefcase_Hold"], Arms.LEFT, False, ["briefcaseA", "briefcaseB", "briefcaseC"], "weapon_l"),
    ],
    "f": [
        ("phone", 1.2, ["_CellPhoneIdle_1"], Arms.LEFT, True, ["phoneA"], "weapon_l"),
        ("cup", 0.8, ["_CupIdle_1"], Arms.LEFT, False, ["cupA", "cupB"], "weapon_l"),
        ("backpack", 0.4, ["_BackPack_1"], Arms.LEFT, False, ["{body}_backpackA", "{body}_backpackB"], "Accessory_Backpack"),
        ("backpack_alt", 0.4, ["_BackPack_2"], Arms.LEFT, False, ["{body}_backpackA", "{body}_backpackB"], "Accessory_Backpack"),
        ("purse", 0.8, ["_Purse_Hold"], Arms.RIGHT, False, ["purseA", "purseB", "purseC"], "weapon_r"),
    ],
}


def _log(msg, warn=False):
    (unreal.log_warning if warn else unreal.log)(TAG + msg)


def _index(root, cls):
    """Asset name -> object path for every asset of class cls under root."""
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    found = {}
    for data in registry.get_assets_by_path(root, recursive=True):
        if str(data.asset_class_path.asset_name) == cls:
            found[str(data.asset_name)] = str(data.package_name)
    return found


def _root_motion(anim):
    """(speed cm/s, direction deg relative to +Y facing, +90 = right) from the root track."""
    length = anim.get_play_length()
    if length <= 0.0:
        return 0.0, 0.0
    start = unreal.AnimationLibrary.get_bone_pose_for_time(anim, "root", 0.0, False).translation
    end = unreal.AnimationLibrary.get_bone_pose_for_time(anim, "root", length, False).translation
    dx, dy = end.x - start.x, end.y - start.y
    speed = math.hypot(dx, dy) / length
    # Meshes face +Y with their left at +X, so right is -X.
    direction = math.degrees(math.atan2(-dx, dy)) if speed > 1.0 else 0.0
    return speed, direction


def _clip(anim, direction=None):
    speed, measured = _root_motion(anim)
    clip = unreal.RammsCrowdLocomotionClip()
    clip.set_editor_property("sequence", anim)
    clip.set_editor_property("speed_cm_s", max(speed, 1.0))
    clip.set_editor_property("direction_degrees", measured if direction is None else direction)
    return clip, speed


def main():
    anims = _index(LOCO_ROOT, "AnimSequence")
    props = _index(ACCESSORY_ROOT, "StaticMesh")
    if not anims:
        unreal.log_error(TAG + "No animations under %s - is the City Sample Crowds pack installed?" % LOCO_ROOT)
        return None

    def load(name):
        path = anims.get(name)
        return unreal.load_asset(path) if path else None

    bodies = []
    for key, prefix in BODIES:
        gender = key[0]
        normal = gender.upper() + "TN"
        body = unreal.RammsCrowdBodyAnims()
        body.set_editor_property("body_key", key)

        walks = []
        for suffix in WALK_SUFFIXES:
            anim = load(prefix + suffix)
            if anim is None:
                continue
            clip, speed = _clip(anim, direction=0.0)
            if speed < MIN_WALK_SPEED:
                _log("skipping %s: root speed %.0f cm/s" % (anim.get_name(), speed), warn=True)
                continue
            walks.append(clip)
        body.set_editor_property("walks", walks)

        quick = load(prefix + "_N_Walk_F_Quickly") or load(normal + "_N_Walk_F_Quickly") or load("MTN_N_WalkQuickly_F" if gender == "m" else "")
        if quick is not None:
            clip, _ = _clip(quick, direction=0.0)
            body.set_editor_property("quick_walk", clip)

        body.set_editor_property("idles", [a for a in (load(prefix + s) for s in IDLE_SUFFIXES) if a is not None])

        carries = []
        backpack_key = BACKPACK_BUILD.get(key, key)
        for name, weight, suffixes, arms, head, prop_names, socket in CARRIES[gender]:
            pose = None
            for suffix in suffixes:
                pose = load(prefix + suffix) or load(normal + suffix)
                if pose is not None:
                    break
            if pose is None:
                continue
            meshes = []
            for prop in prop_names:
                path = props.get(prop.replace("{body}", backpack_key))
                if path:
                    meshes.append(unreal.load_asset(path))
            style = unreal.RammsCrowdCarryStyle()
            style.set_editor_property("name", name)
            style.set_editor_property("weight", weight)
            style.set_editor_property("pose", pose)
            style.set_editor_property("arms", arms)
            style.set_editor_property("layer_head", head)
            style.set_editor_property("props", meshes)
            style.set_editor_property("socket", socket)
            carries.append(style)
        body.set_editor_property("carries", carries)
        bodies.append(body)
        _log("%s: %d walks, quick %s, %d idles, %d carry styles" % (
            key, len(walks), quick.get_name() if quick else "none", len(body.get_editor_property("idles")), len(carries)))

    directionals = []
    for name in DIRECTIONALS:
        anim = load(name)
        if anim is None:
            continue
        clip, speed = _clip(anim)
        if speed < MIN_WALK_SPEED:
            continue
        directionals.append(clip)
        _log("directional %s: %.0f cm/s at %.0f deg" % (name, speed, clip.get_editor_property("direction_degrees")))

    reactions = [a for a in (load(n) for n in REACTIONS) if a is not None]

    anim_set = unreal.load_asset(ANIM_SET_PATH)
    if anim_set is None:
        package_path, asset_name = ANIM_SET_PATH.rsplit("/", 1)
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.RammsCrowdAnimSet)
        anim_set = unreal.AssetToolsHelpers.get_asset_tools().create_asset(asset_name, package_path, unreal.RammsCrowdAnimSet, factory)
    anim_set.set_editor_property("bodies", bodies)
    anim_set.set_editor_property("directionals", directionals)
    anim_set.set_editor_property("reactions", reactions)
    unreal.EditorAssetLibrary.save_loaded_asset(anim_set, only_if_is_dirty=False)
    _log("saved %s: %d bodies, %d directionals, %d reactions" % (ANIM_SET_PATH, len(bodies), len(directionals), len(reactions)))

    if ASSIGN_TO:
        _assign(anim_set)
    return anim_set


def _assign(anim_set):
    blueprint = unreal.load_asset(ASSIGN_TO)
    if blueprint is None:
        _log("%s not found; assign the anim set to its RammsCrowdAnim component by hand" % ASSIGN_TO, warn=True)
        return
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    assigned = False
    for handle in subsystem.k2_gather_subobject_data_for_blueprint(blueprint):
        data = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(handle)
        component = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(data)
        if isinstance(component, unreal.RammsCrowdAnimComponent):
            component.set_editor_property("anim_set", anim_set)
            assigned = True
    if not assigned:
        _log("%s has no RammsCrowdAnim component" % ASSIGN_TO, warn=True)
        return
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False)
    _log("assigned to %s" % ASSIGN_TO)


if __name__ == "__main__":
    main()
