#include "bf2/odf.h"

#include <array>
#include <cctype>
#include <cstring>
#include <map>

namespace swm::bf2 {

namespace {

// Zero-terminated string payload.
std::string chunk_string(std::span<const uint8_t> data) {
    const char* text = reinterpret_cast<const char*>(data.data());
    return std::string(text, strnlen(text, data.size()));
}

// Property names to recognize. Documented ODF keys only.
constexpr std::array known_names = {
    // weapons
    "ClassLabel", "GeometryName", "HighResGeometry", "OrdnanceName", "FirePointName", "ShotDelay", "ReloadTime",
    "RoundsPerClip", "HeatPerShot", "HeatRecoverRate", "HeatThreshold", "MaxRange", "MinRange", "OptimalRange",
    "LockOnRange", "ShotElevate", "ZoomMin", "ZoomMax", "ZoomFirstPerson", "SpreadPerShot", "SpreadRecoverRate",
    "SpreadThreshold", "SpreadLimit", "StandStillSpread", "StandMoveSpread", "CrouchStillSpread",
    "CrouchMoveSpread", "ProneStillSpread", "ProneMoveSpread", "PitchSpread", "YawSpread", "FireSound",
    "FireEmptySound", "ReloadSound", "ChargeSound", "OverheatSound", "OverheatStopSound", "ChangeModeSound",
    "WeaponChangeSound", "MuzzleFlash", "FlashColor", "FlashLength", "FlashLightColor", "FlashLightRadius",
    "FlashLightDuration", "Discharge", "IconTexture", "HUDTag", "AnimationBank", "ScopeTexture", "TriggerSingle",
    "SalvoCount", "SalvoDelay", "ShotsPerSalvo", "ShotPatternCount", "InitialSalvoDelay", "MaxPressedTime",
    "FireAnim", "AITargetPerson", "AITargetVehicle", "AITargetBuilding", "RecoilStrengthLight",
    "RecoilLengthLight", "RecoilDecayLight", "RecoilStrengthHeavy", "RecoilLengthHeavy", "RecoilDecayHeavy",
    "Mode", "ModeTexture", "ReticuleTexture", "OverheatSoundPitch", "ScopeSound", "FireLoopSound",
    "ChargeRateLight", "ChargeRateHeavy", "MaxChargeStrengthLight", "MaxChargeStrengthHeavy", "ChargeDelayLight",
    "ChargeDelayHeavy", "TargetEnemy", "TargetFriendly", "TargetNeutral", "TargetLockMaxDistance",
    "TargetLockMaxDistanceLoss", "AutoAimSize", "LockTime", "LockOffTime", "ZoomTurnDivisorMin",
    "ZoomTurnDivisorMax", "ExtremeRange",
    // ordnance
    "Velocity", "LifeSpan", "MaxDamage", "Gravity", "Rebound", "LaserTexture", "LaserGlowColor", "LaserLength",
    "LaserWidth", "GlowLength", "GlowWidth", "LightColor", "LightRadius", "ImpactEffect", "ImpactEffectWater",
    "ImpactEffectSoft", "ImpactEffectRigid", "ImpactEffectStatic", "ImpactEffectTerrain", "ImpactEffectShield",
    "VehicleScale", "ShieldScale", "PersonScale", "DroidScale", "AnimalScale", "BuildingScale",
    "BuildingDeadScale", "BuildingUnbuiltScale", "ExplosionName", "ExplosionImpact", "ExplosionExpire",
    "CollisionOtherSound", "CollisionWaterSound", "MaxRange", "ImpactSound",
    // explosions
    "Damage", "DamageRadiusInner", "DamageRadiusOuter", "Push", "PushRadiusInner", "PushRadiusOuter",
    "Shake", "ShakeLength", "ShakeRadiusInner", "ShakeRadiusOuter", "Effect", "Sound",
    // soldiers
    "MaxHealth", "MaxSpeed", "MaxStrafeSpeed", "MaxTurnSpeed", "JumpHeight", "JumpForwardSpeedFactor",
    "JumpStrafeSpeedFactor", "RollSpeedFactor", "Acceleration", "SprintAccelerateTime", "AddHealth",
    "WeaponName", "WeaponAmmo", "WeaponChannel", "MaxEnergy", "EnergyRestore", "EnergyRestoreIdle",
    "EnergyDrainSprint", "EnergyMinSprint", "EnergyCostJump", "EnergyCostRoll", "AimFactorPostureStand",
    "AimFactorPostureCrouch", "AimFactorPostureProne", "AimFactorStrafe", "AimFactorMove", "SoldierAnimation",
    "SkeletonName", "AnimatorName", "CollisionScale", "ControlSpeed", "AimerPitchLimits", "AimerYawLimits",
    "UnitType", "GeometryLowRes", "HealthType", "DropItemClass", "DropItemProbability", "NextDropItem",
    "HurtSound", "DeathSound", "ChunkSound", "FoleyFXGroup", "VOSound", "PointsToUnlock", "IconTexture",
};

const std::map<uint32_t, std::string_view>& name_table() {
    static const auto table = [] {
        std::map<uint32_t, std::string_view> names;
        for (const char* name : known_names) {
            names.emplace(odf_hash(name), name);
        }
        return names;
    }();
    return table;
}

} // namespace

uint32_t odf_hash(std::string_view name) {
    uint32_t hash = 2166136261u;
    for (const char c : name) {
        hash ^= uint32_t(uint8_t(std::tolower(static_cast<unsigned char>(c))));
        hash *= 16777619u;
    }
    return hash;
}

std::string_view odf_property_name(uint32_t hash) {
    const auto& table = name_table();
    auto it = table.find(hash);
    return it == table.end() ? std::string_view() : it->second;
}

std::optional<std::string> OdfClass::get(std::string_view name) const {
    const uint32_t hash = odf_hash(name);
    std::optional<std::string> value;
    for (const auto& [key, text] : properties) {
        if (key == hash) {
            value = text;
        }
    }
    return value;
}

void OdfLibrary::add(const UcfbNode& root) {
    for (OdfClass& odf : read_odf_classes(root)) {
        if (!find(odf.type)) {
            classes_.push_back(std::move(odf));
        }
    }
}

const OdfClass* OdfLibrary::find(std::string_view type) const {
    for (const OdfClass& odf : classes_) {
        if (odf.type == type) {
            return &odf;
        }
    }
    return nullptr;
}

OdfClass OdfLibrary::resolve(std::string_view type) const {
    std::vector<const OdfClass*> chain;
    for (const OdfClass* odf = find(type); odf && chain.size() < 32; odf = find(odf->base)) {
        chain.push_back(odf);
    }
    OdfClass resolved;
    if (chain.empty()) {
        resolved.type = std::string(type);
        return resolved;
    }
    resolved.kind = chain.front()->kind;
    resolved.type = chain.front()->type;
    resolved.base = chain.back()->base;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        resolved.properties.insert(resolved.properties.end(), (*it)->properties.begin(), (*it)->properties.end());
    }
    return resolved;
}

std::vector<OdfClass> read_odf_classes(const UcfbNode& root) {
    std::vector<OdfClass> classes;
    visit_ucfb(root, [&](const UcfbNode& node, int) {
        if (node.tag != "entc" && node.tag != "ordc" && node.tag != "wpnc" && node.tag != "expc") {
            return;
        }
        OdfClass odf;
        odf.kind = node.tag;
        for (const UcfbNode& child : node.children) {
            if (child.tag == "TYPE") {
                odf.type = chunk_string(child.data);
            } else if (child.tag == "BASE") {
                odf.base = chunk_string(child.data);
            } else if (child.tag == "PROP" && child.data.size() >= 4) {
                uint32_t hash = 0;
                std::memcpy(&hash, child.data.data(), 4);
                odf.properties.emplace_back(hash, chunk_string(child.data.subspan(4)));
            }
        }
        classes.push_back(std::move(odf));
    });
    return classes;
}

} // namespace swm::bf2
