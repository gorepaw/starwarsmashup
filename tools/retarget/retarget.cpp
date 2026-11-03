#include "retarget/retarget.h"

#include <optional>
#include <stdexcept>

#include "bf2/hash.h"

namespace swm::retarget {
namespace {

struct Pose {
    std::vector<Quat> rotation;  // world space
    std::vector<Vec3> position;  // world space
};

Pose swg_rest_pose(const swg::Skeleton& skeleton) {
    Pose pose;
    for (const swg::Joint& joint : skeleton.joints) {
        if (joint.parent < 0) {
            pose.rotation.push_back(joint.rotation);
            pose.position.push_back(joint.translation);
        } else {
            const size_t p = size_t(joint.parent);
            pose.rotation.push_back(pose.rotation[p] * joint.rotation);
            pose.position.push_back(pose.position[p] + rotate(pose.rotation[p], joint.translation));
        }
    }
    return pose;
}

// Battlefront's animation data is mirrored in X relative to Galaxies: in the
// human rest pose the right hand is at -X (Galaxies: +X) while both face +Z.
// Mirroring X maps one onto the other (the same operation as to_gltf()).
Vec3 bf2_to_swg(Vec3 v) {
    return to_gltf(v);
}
Quat bf2_to_swg(Quat q) {
    return to_gltf(q);
}

// Battlefront pose at `frame` (rest pose if `animation` is null), in Galaxies
// space. The root joint always uses its rest transform, which removes root motion.
Pose bf2_pose(const bf2::AnimationSkeleton& skeleton, const bf2::Animation* animation, uint32_t frame) {
    Pose pose;
    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        const bf2::AnimationJoint& joint = skeleton.joints[i];
        Quat local_rotation = joint.rotation;
        Vec3 local_translation = joint.translation;
        const bf2::AnimationTrack* track = animation ? animation->track(joint.hash) : nullptr;
        if (track && joint.parent_hash != 0) {
            local_rotation = track->rotations[frame];
            local_translation = track->translations[frame];
        }
        local_rotation = bf2_to_swg(local_rotation);
        local_translation = bf2_to_swg(local_translation);
        const int parent = joint.parent_hash == 0 ? -1 : skeleton.find(joint.parent_hash);
        if (parent < 0) {
            pose.rotation.push_back(local_rotation);
            pose.position.push_back(local_translation);
        } else {
            pose.rotation.push_back(pose.rotation[size_t(parent)] * local_rotation);
            pose.position.push_back(pose.position[size_t(parent)] + rotate(pose.rotation[size_t(parent)], local_translation));
        }
    }
    return pose;
}

int require_swg(const swg::Skeleton& skeleton, const std::string& name) {
    const int index = skeleton.find(name);
    if (index < 0) {
        throw std::runtime_error("retarget: Galaxies skeleton has no joint '" + name + "'");
    }
    return index;
}

int require_bf2(const bf2::AnimationSkeleton& skeleton, const std::string& name) {
    const int index = skeleton.find(bf2::name_hash(name));
    if (index < 0) {
        throw std::runtime_error("retarget: Battlefront skeleton has no bone '" + name + "'");
    }
    return index;
}

struct ResolvedMapping {
    int bf2_bone = -1;
    Quat alignment;  // rotates the Galaxies rest bone direction onto the Battlefront one
};

} // namespace

std::vector<BoneMapping> humanoid_mapping() {
    return {
        {"root", "bone_root", "spine3", "bone_ribcage"},
        {"spine1", "bone_a_spine", "spine2", "bone_b_spine"},
        {"spine2", "bone_b_spine", "spine3", "bone_ribcage"},
        {"spine3", "bone_ribcage", "neck", "bone_neck"},
        {"neck", "bone_neck", "Head", "bone_head"},
        {"Head", "bone_head", "", ""},
        {"lClav", "bone_l_clavicle", "lArm", "bone_l_upperarm"},
        {"lArm", "bone_l_upperarm", "lForeArm", "bone_l_forearm"},
        {"lForeArm", "bone_l_forearm", "lWrist", "bone_l_hand"},
        {"lWrist", "bone_l_hand", "", ""},
        {"rClav", "bone_r_clavicle", "rArm", "bone_r_upperarm"},
        {"rArm", "bone_r_upperarm", "rForeArm", "bone_r_forearm"},
        {"rForeArm", "bone_r_forearm", "rWrist", "bone_r_hand"},
        {"rWrist", "bone_r_hand", "", ""},
        {"lThigh", "bone_l_thigh", "lShin", "bone_l_calf"},
        {"lShin", "bone_l_calf", "lAnkle", "bone_l_foot"},
        {"lAnkle", "bone_l_foot", "lToe", "bone_l_toe"},
        {"lToe", "bone_l_toe", "", ""},
        {"rThigh", "bone_r_thigh", "rShin", "bone_r_calf"},
        {"rShin", "bone_r_calf", "rAnkle", "bone_r_foot"},
        {"rAnkle", "bone_r_foot", "rToe", "bone_r_toe"},
        {"rToe", "bone_r_toe", "", ""},
    };
}

swg::AnimationClip retarget_animation(const swg::Skeleton& target, const bf2::AnimationSkeleton& source_skeleton,
                                      const bf2::Animation& animation, const std::vector<BoneMapping>& mapping,
                                      std::string name) {
    const Pose swg_rest = swg_rest_pose(target);
    const Pose bf2_rest = bf2_pose(source_skeleton, nullptr, 0);

    // Resolve the mapping, computing each joint's rest alignment.
    std::vector<std::optional<ResolvedMapping>> resolved(target.joints.size());
    for (const BoneMapping& m : mapping) {
        const int j = require_swg(target, m.swg_joint);
        ResolvedMapping r;
        r.bf2_bone = require_bf2(source_skeleton, m.bf2_bone);
        if (!m.swg_aim.empty()) {
            const int swg_aim = require_swg(target, m.swg_aim);
            const int bf2_aim = require_bf2(source_skeleton, m.bf2_aim);
            const Vec3 swg_direction = normalized(swg_rest.position[size_t(swg_aim)] - swg_rest.position[size_t(j)]);
            const Vec3 bf2_direction =
                normalized(bf2_rest.position[size_t(bf2_aim)] - bf2_rest.position[size_t(r.bf2_bone)]);
            r.alignment = rotation_between(swg_direction, bf2_direction);
        } else {
            for (int a = target.joints[size_t(j)].parent; a >= 0; a = target.joints[size_t(a)].parent) {
                if (resolved[size_t(a)]) {
                    r.alignment = resolved[size_t(a)]->alignment;
                    break;
                }
            }
        }
        resolved[size_t(j)] = r;
    }

    // The weapon joint copies Battlefront's weapon hardpoint orientation in
    // world space, so guns are held the way Battlefront holds them. Both
    // games' weapon models point +Z with +Y up (Galaxies E-11: muzzle at
    // z = 0.305, scope at y = 0.104), so no extra correction is applied.
    const int swg_weapon = target.find("hold_r");
    const int bf2_weapon = source_skeleton.find(bf2::name_hash("hp_weapons"));

    // Hip height ratio for the vertical root motion.
    const int swg_hip = require_swg(target, "lThigh");
    const int bf2_hip = require_bf2(source_skeleton, "bone_l_thigh");
    const int bf2_root = require_bf2(source_skeleton, "bone_root");
    const float hip_scale = bf2_rest.position[size_t(bf2_hip)].y != 0
                                ? swg_rest.position[size_t(swg_hip)].y / bf2_rest.position[size_t(bf2_hip)].y
                                : 1.0f;

    swg::AnimationClip clip;
    clip.name = std::move(name);
    clip.fps = bf2::animation_fps;
    for (uint32_t frame = 0; frame < animation.frame_count; ++frame) {
        const Pose source = bf2_pose(source_skeleton, &animation, frame);
        std::vector<Quat> world(target.joints.size());
        std::vector<Quat> local(target.joints.size());
        for (size_t j = 0; j < target.joints.size(); ++j) {
            const int parent = target.joints[j].parent;
            const Quat parent_world = parent < 0 ? Quat{} : world[size_t(parent)];
            if (resolved[j]) {
                const size_t b = size_t(resolved[j]->bf2_bone);
                const Quat delta = source.rotation[b] * conjugate(bf2_rest.rotation[b]);
                world[j] = normalized(delta * resolved[j]->alignment * swg_rest.rotation[j]);
            } else if (int(j) == swg_weapon && bf2_weapon >= 0) {
                world[j] = source.rotation[size_t(bf2_weapon)];
            } else {
                world[j] = parent_world * target.joints[j].rotation;
            }
            local[j] = normalized(conjugate(parent_world) * world[j]);
        }
        clip.rotations.push_back(std::move(local));

        const float lift = (source.position[size_t(bf2_root)].y - bf2_rest.position[size_t(bf2_root)].y) * hip_scale;
        clip.root_translations.push_back(target.joints[0].translation + Vec3{0, lift, 0});
    }
    return clip;
}

} // namespace swm::retarget
