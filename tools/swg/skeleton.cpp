#include "swg/skeleton.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

#include "swg/iff.h"

namespace swm::swg {
namespace {

bool iequals(std::string_view a, std::string_view b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

Quat read_quat(IffReader& reader) {
    Quat q;
    q.w = reader.f32();
    q.x = reader.f32();
    q.y = reader.f32();
    q.z = reader.f32();
    return q;
}

Skeleton parse_sktm(const IffNode& sktm) {
    const IffNode& version = sktm.expect("0002");
    IffReader info(version.expect("INFO").data);
    const uint32_t count = info.u32();

    const std::vector<std::string> names = iff_strings(version.expect("NAME").data);
    if (names.size() != count) {
        throw std::runtime_error("SKTM: joint name count mismatch");
    }
    IffReader parents(version.expect("PRNT").data);
    IffReader pre(version.expect("RPRE").data);
    IffReader post(version.expect("RPST").data);
    IffReader translations(version.expect("BPTR").data);
    IffReader bind(version.expect("BPRO").data);

    Skeleton skeleton;
    skeleton.joints.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        Joint& joint = skeleton.joints[i];
        joint.name = names[i];
        joint.parent = parents.i32();
        if (joint.parent >= int(i)) {
            throw std::runtime_error("SKTM: joint '" + joint.name + "' precedes its parent");
        }
        const Quat pre_rotation = read_quat(pre);
        const Quat post_rotation = read_quat(post);
        joint.translation = {translations.f32(), translations.f32(), translations.f32()};
        joint.rotation = normalized(post_rotation * read_quat(bind) * pre_rotation);
    }
    return skeleton;
}

} // namespace

int Skeleton::find(std::string_view name) const {
    for (size_t i = 0; i < joints.size(); ++i) {
        if (iequals(joints[i].name, name)) {
            return int(i);
        }
    }
    return -1;
}

void Skeleton::attach(const Skeleton& other, std::string_view attach_to) {
    int attach_index = -1;
    if (!attach_to.empty()) {
        attach_index = find(attach_to);
        if (attach_index < 0) {
            throw std::runtime_error("skeleton: no joint '" + std::string(attach_to) + "' to attach to");
        }
    }
    const int offset = int(joints.size());
    for (Joint joint : other.joints) {
        joint.parent = joint.parent < 0 ? attach_index : joint.parent + offset;
        joints.push_back(std::move(joint));
    }
}

std::vector<Mat4> Skeleton::global_transforms() const {
    std::vector<Mat4> globals(joints.size());
    for (size_t i = 0; i < joints.size(); ++i) {
        const Mat4 local = Mat4::from_rotation_translation(joints[i].rotation, joints[i].translation);
        globals[i] = joints[i].parent < 0 ? local : globals[size_t(joints[i].parent)] * local;
    }
    return globals;
}

Skeleton parse_skeleton(std::span<const uint8_t> bytes) {
    const IffNode root = parse_iff_root(bytes);
    if (root.tag == "SKTM") {
        return parse_sktm(root);
    }
    if (root.tag == "SLOD") {
        return parse_sktm(root.expect("0000").expect("SKTM"));
    }
    throw std::runtime_error("not a skeleton (" + root.tag + ")");
}

} // namespace swm::swg
