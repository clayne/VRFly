#include "Settings.h"
#include "OnMeleeHit.h"
#include "Utils.h"

using namespace SKSE;
using namespace SKSE::log;


bool play_impact_1(RE::Actor* actor, const RE::BSFixedString& nodeName) {
    auto root = netimmerse_cast<RE::BSFadeNode*>(actor->Get3D());
    if (!root) return false;
    auto bone = netimmerse_cast<RE::NiNode*>(root->GetObjectByName(nodeName));
    if (!bone) return false;

    float reach = 20.0f;
    auto weaponDirection =
        RE::NiPoint3{bone->world.rotate.entry[0][1], bone->world.rotate.entry[1][1], bone->world.rotate.entry[2][1]};
    RE::NiPoint3 to = bone->world.translate + weaponDirection * reach;
    RE::NiPoint3 P_V = {0.0f, 0.0f, 0.0f};

    return play_impact_2(actor, RE::TESForm::LookupByID<RE::BGSImpactData>(0x0004BB52), &P_V, &to, bone);
}

// From: https://github.com/fenix31415/UselessFenixUtils
bool play_impact_2(RE::TESObjectREFR* a, RE::BGSImpactData* impact, RE::NiPoint3* P_V, RE::NiPoint3* P_from,
                               RE::NiNode* bone) {
    return play_impact_3(a->GetParentCell(), 1.0f, impact->GetModel(), P_V, P_from, 1.0f, 7, bone);
}

bool play_impact_3(RE::TESObjectCELL* cell, float a_lifetime, const char* model, RE::NiPoint3* a_rotation,
                               RE::NiPoint3* a_position, float a_scale, uint32_t a_flags, RE::NiNode* a_target) {
    return RE::BSTempEffectParticle::Spawn(cell, a_lifetime, model, *a_rotation, *a_position, a_scale, a_flags,
                                           a_target);
}

bool HasShield(RE::Actor* actor) {
    if (auto equipL = actor->GetEquippedObject(true); equipL) {
        if (equipL->IsArmor()) {
            return true;
        }
    }
    return false;
}

std::string formatNiPoint3(RE::NiPoint3& pos) {
    std::ostringstream stream;
    stream << "(" << pos.x << ", " << pos.y << ", " << pos.z << ")";
    return stream.str();
}

bool IsNiPointZero(const RE::NiPoint3& pos) {
    if (pos.x == 0.0f && pos.y == 0.0f && pos.z == 0.0f) {
        return true;
    } else {
        return false;
    }
}

bool AnyPointZero(const RE::NiPoint3& A, const RE::NiPoint3& B, const RE::NiPoint3& C, const RE::NiPoint3& D) { 
    if (IsNiPointZero(A) || IsNiPointZero(B) || IsNiPointZero(C) || IsNiPointZero(D)) {
        return true;
    }
    return false;
}

RE::NiMatrix3 ConvertToPlayerSpace(const RE::NiMatrix3& R_weapon_world_space,
                                   const RE::NiMatrix3& R_player_world_space) {
    // Transpose of player's rotation matrix in world space
    RE::NiMatrix3 R_player_world_space_transposed;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            R_player_world_space_transposed.entry[i][j] = R_player_world_space.entry[j][i];
        }
    }

    // Multiplying with weapon's rotation matrix in world space
    RE::NiMatrix3 R_weapon_player_space;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            R_weapon_player_space.entry[i][j] = 0.0f;
            for (int k = 0; k < 3; ++k) {
                R_weapon_player_space.entry[i][j] +=
                    R_player_world_space_transposed.entry[i][k] * R_weapon_world_space.entry[k][j];
            }
        }
    }

    return R_weapon_player_space;
}

RE::NiMatrix3 ConvertToWorldSpace(const RE::NiMatrix3& R_weapon_player_space,
                                  const RE::NiMatrix3& R_player_world_space) {
    RE::NiMatrix3 R_weapon_world_space;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            R_weapon_world_space.entry[i][j] = 0.0f;
            for (int k = 0; k < 3; ++k) {
                R_weapon_world_space.entry[i][j] +=
                    R_player_world_space.entry[i][k] * R_weapon_player_space.entry[k][j];
            }
        }
    }
    return R_weapon_world_space;
}


RE::NiMatrix3 adjustNodeRotation(RE::NiNode* baseNode, RE::NiMatrix3& rotation, RE::NiPoint3 adjust, bool useAdjust2) {
    RE::NiMatrix3 newRotation = rotation;
    if (baseNode) {
        auto rotation_base = baseNode->world.rotate;
        newRotation = ConvertToPlayerSpace(rotation, rotation_base);

        if (useAdjust2) {
            RE::NiMatrix3 adjust2;
            // Set up a 90-degree rotation matrix around Z-axis
            adjust2.entry[0][0] = 0.0f;
            adjust2.entry[0][1] = 0.0f;
            adjust2.entry[0][2] = 1.0f;
            adjust2.entry[1][0] = 0.0f;
            adjust2.entry[1][1] = 1.0f;
            adjust2.entry[1][2] = 0.0f;
            adjust2.entry[2][0] = -1.0f;
            adjust2.entry[2][1] = 0.0f;
            adjust2.entry[2][2] = 0.0f;

            newRotation = newRotation * adjust2;
        } else {
            newRotation = newRotation * RE::NiMatrix3(adjust.x, adjust.y, adjust.z);
        }


        newRotation = ConvertToWorldSpace(newRotation, rotation_base);
    } else {
        log::warn("Base node is null");
    }
    return newRotation;
}

uint32_t GetBaseFormID(uint32_t formId) { return formId & 0x00FFFFFF; }

uint32_t GetFullFormID(const uint8_t modIndex, uint32_t formLower) { return (modIndex << 24) | formLower; }

uint32_t GetFullFormID_ESL(const uint8_t modIndex, const uint16_t esl_index, uint32_t formLower) {
    return (modIndex << 24) | (esl_index << 12) | formLower;
}

// The time slow spell is given by SpeelWheel VR by Shizof. Thanks for the support!
// May return nullptr
RE::SpellItem* GetTimeSlowSpell_SpeelWheel() { 
    auto handler = RE::TESDataHandler::GetSingleton();
    if (!handler) {
        log::error("GetTimeSlowSpell: failed to get TESDataHandler");
        return nullptr;
    }
    auto spellWheelIndex = handler->GetLoadedModIndex("SpellWheelVR.esp");
    if (!spellWheelIndex.has_value()) {
        log::trace("GetTimeSlowSpell: failed to get spellWheel");
        return nullptr;
    }

    //auto spellWheelMod = handler->LookupLoadedModByName("SpellWheelVR.esp");
    //if (spellWheelMod) {
    //    log::trace("GetTimeSlowSpell: get spellWheel mod");
    //} else {
    //    log::trace("GetTimeSlowSpell: didn't get spellWheel mod");
    //}

    auto vrikMod = handler->LookupLoadedModByName("vrik.esp");
    if (vrikMod) {
        log::trace("GetTimeSlowSpell: get vrik mod");
    } else {
        log::trace("GetTimeSlowSpell: didn't get vrik mod");
    }

    // Thanks to Shizof, there is a spell with formID 20A00 that is specifically added in Spell Wheel VR for our convenience 
    // So we should try this one first
    RE::FormID partFormID1 = 0x020A00;
    RE::FormID partFormID2 = 0x000EA5; // this is the spell that Spell wheel itself uses to slow time. We should avoid using it now
    RE::FormID fullFormID1 = GetFullFormID(spellWheelIndex.value(), partFormID1);
    RE::SpellItem* timeSlowSpell = RE::TESForm::LookupByID<RE::SpellItem>(fullFormID1); 
    if (!timeSlowSpell) {
        RE::FormID fullFormID2 = GetFullFormID(spellWheelIndex.value(), partFormID2);
        timeSlowSpell = RE::TESForm::LookupByID<RE::SpellItem>(fullFormID2);
        if (!timeSlowSpell) {
            log::error("GetTimeSlowSpell: failed to get timeslow spell");
            return nullptr;
        }
    }
    return timeSlowSpell;
}

RE::SpellItem* GetTimeSlowSpell_Mine() {

    RE::FormID partFormID = 0x000D63; // D63 is spell, D62 is effect
    RE::SpellItem* timeSlowSpell;
    //RE::FormID fullFormID = GetFullFormID(weaponCollisionIndex.value(), partFormID);
    for (uint16_t i = 0; i <= 0xFFF; i++) {
        RE::FormID fullFormID = GetFullFormID_ESL(0xFE, i, partFormID);
        timeSlowSpell = RE::TESForm::LookupByID<RE::SpellItem>(fullFormID);
        if (timeSlowSpell) break;
    } 
    
    
    if (!timeSlowSpell) {
        log::error("GetTimeSlowSpell: failed to get timeslow spell");
        return nullptr;
    }
    return timeSlowSpell;
}

float generateRandomFloat(float min, float max) {
    // Create a random device and use it to seed the Mersenne Twister engine
    std::random_device rd;
    std::mt19937 gen(rd());

    // Define the distribution, range is min to max
    std::uniform_real_distribution<float> dis(min, max);

    // Generate and return the random number
    return dis(gen);
}

twoNodes HandleClawRaces(RE::Actor* actor, RE::NiPoint3& posWeaponBottomL, RE::NiPoint3& posWeaponBottomR,
                       RE::NiPoint3& posWeaponTopL, RE::NiPoint3& posWeaponTopR) {

    if (actor && actor->GetRace() &&
        (actor->GetRace()->formID == werewolfRace || GetBaseFormID(actor->GetRace()->formID) == raceWereBear  ||
         GetBaseFormID(actor->GetRace()->formID) == vampLord)) {
    } else {
        return twoNodes(nullptr, nullptr);
    }

    bool isRace = false;

    const auto actorRoot = netimmerse_cast<RE::BSFadeNode*>(actor->Get3D());
    if (!actorRoot) {
        log::warn("Fail to get actorRoot:{}", actor->GetBaseObject()->GetName());
        return twoNodes(nullptr, nullptr);
    }

    const auto weaponNodeNameL = "NPC L Hand [RHnd]"sv;  // yes, werewolves have [RHnd] even for left hand
    const auto weaponNodeNameL_Alt = "NPC L Hand [LHnd]"sv;
    const auto weaponNodeNameR = "NPC R Hand [RHnd]"sv;
    auto weaponNodeL = netimmerse_cast<RE::NiNode*>(actorRoot->GetObjectByName(weaponNodeNameL));
    auto weaponNodeR = netimmerse_cast<RE::NiNode*>(actorRoot->GetObjectByName(weaponNodeNameR));
    if (!weaponNodeL) weaponNodeL = netimmerse_cast<RE::NiNode*>(actorRoot->GetObjectByName(weaponNodeNameL_Alt));

    // for bears
    const auto weaponNodeNameL_Alt2 = "NPC LThumb02"sv;
    const auto weaponNodeNameR_Alt2 = "NPC RThumb02"sv;
    if (!weaponNodeL) weaponNodeL = netimmerse_cast<RE::NiNode*>(actorRoot->GetObjectByName(weaponNodeNameL_Alt2));
    if (!weaponNodeR) weaponNodeR = netimmerse_cast<RE::NiNode*>(actorRoot->GetObjectByName(weaponNodeNameR_Alt2));

    
    const auto nodeBaseStr = "NPC Pelvis [Pelv]"sv;  // base of at least werewolves
    const auto baseNode = netimmerse_cast<RE::NiNode*>(actorRoot->GetObjectByName(nodeBaseStr));

    if (weaponNodeL && weaponNodeR) { 
        isRace = true;

        float reachR(5.0f), handleR(34.0f); // our rotation matrix kinda inverts the rotation
        posWeaponBottomR = weaponNodeR->world.translate;

        auto rotationR = weaponNodeR->world.rotate;

        // Adjust right claw rotation. Original rotation is like the claw is holding a dagger, but we want it sticking out
        rotationR = adjustNodeRotation(baseNode, rotationR, RE::NiPoint3(1.5f, 0.0f, 0.0f), false);

        auto weaponDirectionR = RE::NiPoint3{rotationR.entry[0][1], rotationR.entry[1][1], rotationR.entry[2][1]};

        posWeaponTopR = posWeaponBottomR + weaponDirectionR * reachR;
        posWeaponBottomR = posWeaponBottomR - weaponDirectionR * handleR;

        float reachL(5.0f), handleL(34.0f);
        posWeaponBottomL = weaponNodeL->world.translate;

        auto rotationL = weaponNodeL->world.rotate;
        rotationL = adjustNodeRotation(baseNode, rotationL, RE::NiPoint3(1.5f, 0.0f, 0.0f), false);

        auto weaponDirectionL = RE::NiPoint3{rotationL.entry[0][1], rotationL.entry[1][1], rotationL.entry[2][1]};

        posWeaponTopL = posWeaponBottomL + weaponDirectionL * reachL;
        posWeaponBottomL = posWeaponBottomL - weaponDirectionL * handleL;


        log::trace("Is werewolf or werebear or trolls. actor:{}", actor->GetBaseObject()->GetName());
    }

    if (isRace) {
        return twoNodes(weaponNodeL, weaponNodeR);
    } else {
        return twoNodes(nullptr, nullptr);
    }
}

void vibrateController(int hapticFrame, bool isLeft) {
    auto papyrusVM = RE::BSScript::Internal::VirtualMachine::GetSingleton();

    if (papyrusVM) {
        hapticFrame = hapticFrame < iHapticStrMin ? iHapticStrMin : hapticFrame;
        hapticFrame = hapticFrame > iHapticStrMax ? iHapticStrMax : hapticFrame;

        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;

        log::trace("Calling papyrus");
        if (papyrusVM->TypeIsValid("VRIK"sv)) {
            log::trace("VRIK is installed");
            RE::BSScript::IFunctionArguments* hapticArgs;
            if (isLeft) {
                hapticArgs = RE::MakeFunctionArguments(true, (int)hapticFrame, (int)iHapticLengthMicroSec);
            } else {
                hapticArgs = RE::MakeFunctionArguments(false, (int)hapticFrame, (int)iHapticLengthMicroSec);
            }
            // Function VrikHapticPulse(Bool onLeftHand, Int frames, Int microsec) native global
            papyrusVM->DispatchStaticCall("VRIK"sv, "VrikHapticPulse"sv, hapticArgs, callback);
        } else {
            log::trace("VRIK not installed");
            // Now we call vanilla's script Game.ShakeController(float afLeftStrength, float afRightStrength, float
            // afDuration) afLeftStrength: The strength of the left motor. Clamped from 0 to 1. afRightStrength: The
            // strength of the right motor.Clamped from 0 to 1. afDuration : How long to shake the controller - in
            // seconds.
            if (papyrusVM->TypeIsValid("Game"sv)) {
                RE::BSScript::IFunctionArguments* hapticArgs;
                if (isLeft) {
                    hapticArgs = RE::MakeFunctionArguments(((float)hapticFrame) / ((float)iHapticStrMax), 0.0f,
                                                           (float)iHapticLengthMicroSec / 1000000);
                } else {
                    hapticArgs = RE::MakeFunctionArguments(0.0f, ((float)hapticFrame) / ((float)iHapticStrMax),
                                                           (float)iHapticLengthMicroSec / 1000000);
                }
                papyrusVM->DispatchStaticCall("Game"sv, "ShakeController"sv, hapticArgs, callback);
            } else {
                log::trace("Failed to find vanilla Game script");
            }
        }

        log::trace("Finished calling papyrus");
    }
}
