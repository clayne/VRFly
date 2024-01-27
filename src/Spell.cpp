#include "Spell.h"


using namespace SKSE;

void SpellCheckMain() {
    auto& playerSt = PlayerState::GetSingleton();
    auto player = playerSt.player;
    auto spellLift = GetLiftSpell();
    auto spellXY = GetXYSpell();
    auto spellXYZ = GetXYZSpell();
    auto spellEmit = GetEmitSpell();
    auto spellEmitFire = GetEmitFireSpell();
    auto spellEmitForce = GetEmitForceSpell();
    if (!spellLift || !spellXY || !spellXYZ || !spellEmit || !spellEmitFire || !spellEmitForce) {
        log::error("Didn't find one of the spells! Return from FlyMain");
        return;
    }

    std::vector<RE::SpellItem*> spellList;
    spellList.push_back(spellLift);
    spellList.push_back(spellXY);
    spellList.push_back(spellXYZ);
    spellList.push_back(spellEmit);
    spellList.push_back(spellEmitFire);
    spellList.push_back(spellEmitForce);

    for (bool isLeft : {false, true}) {
        RE::MagicItem* equip = isLeft ? playerSt.leftSpell : playerSt.rightSpell;
        if (!equip) continue;
        for (RE::SpellItem* spell : spellList) {
            if (player->IsCasting(spell) && spell == equip) {
                log::trace("Player is casting spell: {:x}. Now checking trigger. IsLeft:{}", spell->GetFormID(), isLeft);

                bool isCastingHandMatch = false;
                RE::TESGlobal* g = isLeft ? GetTriggerL() : GetTriggerR();
                if (!g) {
                    log::error("Didn't find global");
                    continue;
                }
                isCastingHandMatch = g->value > 0.9f;

                log::trace("Is trigger pressed:{}", isCastingHandMatch);

                if (isCastingHandMatch) {

                    // Now check if there exists an active effect matching this spell and its slot
                    auto slot = isLeft ? Slot::kLeft : Slot::kRight;

                    // Check for Velo spells
                    if (spell == spellLift || spell == spellXY || spell == spellXYZ || spell == spellEmit ||
                        spell == spellEmitFire) {
                        auto eff = allEffects.FindVeloEffect(slot, spell, nullptr);

                        if (!eff) {
                            // Case 1: this is the first time player casts this spell. Create a new active effect
                            VeloEffect* effect =
                                new VeloEffect(slot, new TrapezoidVelo(0.0f, 0.0f, 0.0f), spell, nullptr);

                            if (spell == spellLift) {
                                effect->xyzSpell = XYZMoveSpell(GetPlayerHandPos(isLeft, player), true, false);
                            } else if (spell == spellXY) {
                                effect->xyzSpell = XYZMoveSpell(GetPlayerHandPos(isLeft, player), false, true);
                            } else if (spell == spellXYZ) {
                                effect->xyzSpell = XYZMoveSpell(GetPlayerHandPos(isLeft, player), true, true);
                            } else if (spell == spellEmit || spell == spellEmitFire) {
                                effect->emitSpell = EmitSpell(true, isLeft);
                            }
                            allEffects.Push(effect);
                        } else {
                            // Case 2: player is continuing casting the spell. Update the effect
                            float conf_LiftModTime = 0.3f;
                            Velo newStable;
                            if (spell == spellLift || spell == spellXY || spell == spellXYZ) {
                                conf_LiftModTime = eff->xyzSpell.modTime;
                                newStable = eff->xyzSpell.CalculateNewStable(GetPlayerHandPos(isLeft, player));
                            } else if (spell == spellEmit || spell == spellEmitFire) {
                                conf_LiftModTime = eff->emitSpell.modTime;
                                newStable = eff->emitSpell.CalculateNewStable();
                            }
                            log::trace("About to Mod velo to: {}, {}, {}", newStable.x, newStable.y, newStable.z);
                            eff->velo->ModStable(newStable.x, newStable.y, newStable.z, conf_LiftModTime);

                            eff->Update();
                        }
                    }

                    // Check for Force spells
                    if (spell == spellEmitForce) {
                        auto eff = allEffects.FindForceEffect(slot, spell);

                        if (!eff) {
                            // Case 1: this is the first time player casts this spell. Create a new active effect
                            ForceEffect* effect = new ForceEffect(slot, Force(), spell);

                            if (spell == spellEmitForce) {
                                effect->emitForceSpell = EmitForceSpell(true, isLeft);
                            }
                            allEffects.Push(effect);
                        } else {
                            // Case 2: player is continuing casting the spell. Update the effect
                            Force newForce;
                            if (spell == spellEmitForce) {
                                newForce = eff->emitForceSpell.CalculateNewForce();
                                eff->force = newForce;
                            }
                            log::trace("Mod force to: {}, {}, {}", newForce.x, newForce.y, newForce.z);

                            eff->Update();
                        }
                    }
                }
            }

            // TODO: Even if player is not casting, if spell match and player pressing trigger, we should cancel jump
            //if (!player->IsCasting(spell) && spell == equip) {
            //    RE::TESGlobal* g = isLeft ? GetTriggerL() : GetTriggerR();
            //    if (g) {
            //        if (g->value > 0.9f) {
            //            //playerSt.CancelFallFlag();
            //            log::trace("Try to cancel fall"); // No use
            //            player->NotifyAnimationGraph("FootLeft"sv);
            //        }
            //    }
            //}
        }

        
    }

    // Case 3: player stopped casting the spell. Delete the velo and delete the effect
    allEffects.DeleteIdleEffects();
}

// CalculateDragSimple assumes that Drag is always in the opposite direction of velocity,
Force CalculateDragSimple(RE::NiPoint3& currentVelo) {
    float conf_dragcoefficient = 0.03f;
    auto f1 = currentVelo * currentVelo.Length() * conf_dragcoefficient * -1.0f;
    log::trace("f1: {}, {}, {}", f1.x, f1.y, f1.z);
    return Force(f1.x, f1.y, f1.z);
}

// CalculateDragComplex calculates drag by the velocity and player's wings direction
Force CalculateDragComplex(RE::NiPoint3& currentVelo) {
    auto& playerSt = PlayerState::GetSingleton();
    float conf_maxDrag = 5.0f;

    // Drag's first part is still opposite to velocity, but smaller
    float conf_dragcoefficient1 = 0.01; 
    auto f1 = currentVelo * currentVelo.Length() * conf_dragcoefficient1 * -1.0f;
    log::trace("f1: {}, {}, {}", f1.x, f1.y, f1.z);


    // Drag's second part is the Dot product of (-velocity) and wings direction, in the direction of wings direction
    float conf_dragcoefficient2 = 0.015; 
    RE::NiPoint3 f2;
    if (playerSt.dirWings.Length() != 0.0f) {
        f2 = playerSt.dirWings / playerSt.dirWings.Length() * currentVelo.Dot(playerSt.dirWings) *
             currentVelo.Length() *
                  conf_dragcoefficient2 * -1.0f;
    }
    log::trace("f2: {}, {}, {}", f2.x, f2.y, f2.z);

    auto f = f1 + f2;
    if (f.Length() > conf_maxDrag) f = f / f.Length() * conf_maxDrag;

    return Force(f);
}

// CalculateLift calculates lift by the velocity and player's wings direction
Force CalculateLift(RE::NiPoint3& currentVelo) {
    auto& playerSt = PlayerState::GetSingleton();

    float conf_maxLift = 11.0f;
    float conf_maxLiftZ = 11.0f;
    float conf_veloMaintainEffect = 0.2f; // when 1.0, completely reduce part of force that slows player down

    // Lift is in the direction of wings direction. Lift = coefficient * v^2 
    float conf_liftcoefficient = 0.07f;
    RE::NiPoint3 f;
    if (playerSt.dirWings.Length() != 0.0f) {
        auto speed = currentVelo.Length();
        f = playerSt.dirWings / playerSt.dirWings.Length() * speed * speed * conf_liftcoefficient;
    }

    

    if (f.Length() > conf_maxLift) f = f / f.Length() * conf_maxLift;
    if (f.z > conf_maxLiftZ) f.z = conf_maxLiftZ;
    if (f.x * currentVelo.x < 0.0f) {
        f.x *= (1 - conf_veloMaintainEffect);
    }
    if (f.y * currentVelo.y < 0.0f) {
        f.y *= (1 - conf_veloMaintainEffect);
    }

    return Force(f);
}

// CalculateVerticalHelper returns a force that is vertical to velocity (so it energy is conserved)
// and the force is in the same plane formed by velocity and the middle of player's hands
// This force helps velocity to fastly turn to where player's hands point, while conserve the energy
Force CalculateVerticalHelper(RE::NiPoint3& currentVelo) {
    float conf_helpCoefficient = 0.3f;
    float conf_maxHelp = 7.0f;

    auto& playerSt = PlayerState::GetSingleton();
    float conf_shoulderHeight = 100.0f;

    RE::NiPoint3 force = RE::NiPoint3(0.0f, 0.0f, 0.0f);

    auto eff = allEffects.GetWingEff();
    if (!eff) {
        log::error("CalculateVerticalHelper cannot get ForceEffect that contains wingSpell");
        return force;
    }


    auto leftPos = eff->wingSpell.handPosL;
    auto rightPos = eff->wingSpell.handPosR;
    RE::NiPoint3 middlePos = RE::NiPoint3(0.0f, 0.0f, conf_shoulderHeight);
    RE::NiPoint3 vectorA = leftPos - middlePos;
    RE::NiPoint3 vectorB = rightPos - middlePos;
    RE::NiPoint3 handAverage = (vectorA + vectorB) / 2;
    if (handAverage.Length() == 0) {
        log::warn("handAverage is 0");
        return force;
    }
    handAverage /= handAverage.Length();

    
    if (currentVelo.Length() == 0) {
        log::trace("currentVelo is 0");
        return force;
    }

   // Step 1: Compute a vector perpendicular to the plane formed by velocity and desiredDirection
    RE::NiPoint3 perpToPlane = currentVelo.Cross(handAverage);

    // Step 2: Compute the cross product of this new vector with the velocity vector
    force = perpToPlane.Cross(currentVelo);

    // Normalize the force vector
    float vel = currentVelo.Length();
    float dotVeloHand = currentVelo.Dot(handAverage) / vel;
    if (dotVeloHand < 0.0f) return force;

    if (force.Length() == 0) {
        log::warn("force is 0");
        return force;
    }
    force = force / force.Length() * (vel * vel) * conf_helpCoefficient * (1 - abs(currentVelo.Dot(handAverage)) / vel);
    if (force.Length() > conf_maxHelp && force.Length() != 0) force = force / force.Length() * conf_maxHelp;


    return force;
}

// WingsCheckMain updates player's wings status
void WingsCheckMain() {
    auto& playerSt = PlayerState::GetSingleton();
    auto player = playerSt.player;
    auto spellWingsFlag = GetWingsFlagSpell();
    playerSt.isSlappingWings = false;
    if (!spellWingsFlag) {
        log::error("Didn't find spellWingsFlag! Return from WingsCheckMain");
        return;
    }

    // If player doesn't have this spell, meaning they are not flying
    if (!player->HasSpell(spellWingsFlag)) {
        playerSt.hasWings = false;
        log::trace("Player doesn't have the spellWingsFlag");
        return;
    }


    playerSt.hasWings = true;


    // Player can put one hand to forehead to see nearby lifting stuff
    if (iFrameCount - playerSt.lastSpawnSteamFrame > 400 && iFrameCount % 100 != 17) { // don't run at the same frame as normal sparse scan
        if (IsPlayerHandCloseToHead(player)) {
            RE::DebugNotification("Visualizing lifting wind");
            playerSt.lastSpawnSteamFrame = iFrameCount;
            playerSt.SparseScan(6000.0f);
            playerSt.SpawnSteamNearby(6000.0f);
        } 
    }

    
    // Code for living creature lift (Spiritual Lift)
    if (playerSt.isInMidAir) {
        auto uponActor = playerSt.FindActorAtFoot();
        if (uponActor) {
            ExtraLiftManager::GetSingleton().AddSpiritual(uponActor, player);
            if (iFrameCount - playerSt.lastNotification > 240) {
                playerSt.lastNotification = iFrameCount;
                auto name = uponActor->GetDisplayFullName();
                char message[256];
                std::sprintf(message, "Spiritual lift from %s", name);
                RE::DebugNotification(message);
                playerSt.isInSpiritualLift = true;
            }
        } else {
            playerSt.isInSpiritualLift = false;

        }
    }

    auto slot = Slot::kHead;
    auto eff = allEffects.FindForceEffect(slot, spellWingsFlag);

    if (!eff) {
        // It's the first frame for player to cast this shout
        eff = new ForceEffect(slot, Force(), spellWingsFlag);

        eff->wingSpell = WingSpell(true);

        allEffects.Push(eff);
    }

    // No matter what, update the effect to prevent it from being deleted
    eff->force = Force(0.0f, 0.0f, 0.0f);
    eff->Update();

    // If player on ground, wing direction is reset
    if (playerSt.isInMidAir == false) {
        playerSt.dirWings = RE::NiPoint3(0.0f, 0.0f, 1.0f);
        playerSt.everSetWingDirSinceThisFlight = false;
    }
      

    // If player isn't holding both grips, don't do anything
    auto gripL = GetGripL();
    auto gripR = GetGripR();
    if (!gripL || !gripR) {
        log::error("Didn't find gripL or gripR! Return from WingsCheckMain");
        return;
    }
    if (gripL->value < 0.1f || gripR->value < 0.1f || iFrameCount - iLastPressGrip > 3) {
        log::trace("Player not holding both grips");
        return;
    }


    // Update hand pos that's gonna used in calculations related to wings
    // But only when player is not slapping wings and not recently slapped wings
    if (playerSt.isSlappingWings == false &&
        (iFrameCount - playerSt.frameLastSlap > 20 || iFrameCount - playerSt.frameLastSlap < 0)) {
        eff->wingSpell.UpdateHandPos();
    }

    static int skyDivingCountDown = 20;
    auto speedL = playerSt.speedBuf.GetVelocity(5, true);
    auto speedR = playerSt.speedBuf.GetVelocity(5, false);
    log::trace("Player controller speedL: {}, {}, {}", speedL.x, speedL.y, speedL.z);
    log::trace("Player controller speedR: {}, {}, {}", speedR.x, speedR.y, speedR.z);
    float conf_slapThres = 1.0f;

    
    if ((speedL.Length() > conf_slapThres || speedR.Length() > conf_slapThres) && iFrameCount - playerSt.lastSuddenTurnFrame > 8) {
        // Case 1: player is moving controllers fast, and also not turning. Set force based on controller speed
        playerSt.isSkyDiving = false;
        skyDivingCountDown = 20;

        Force newForce;
        newForce = eff->wingSpell.CalculateSlapForce(speedL, speedR);
        eff->force = newForce;
        log::trace("Mod force from Wings to: {}, {}, {}", newForce.x, newForce.y, newForce.z);

        // Only update dirWings if this is the first frame player starts slapping
        if (playerSt.isSlappingWings == false) {
            playerSt.UpdateWingDir(eff->wingSpell.handPosL, eff->wingSpell.handPosR);
        }

        // play sound
        static int playSoundWaitCount;
        if (iFrameCount - playerSt.lastSoundFrame > 90) {
            bool mustPlay = false;
            if (playSoundWaitCount >= 15) {
                mustPlay = true;
                playSoundWaitCount = 0;
            }
            float handSpeed = speedL.Length() > speedR.Length() ? speedL.Length() : speedR.Length();

            auto player = playerSt.player;
            RE::FormID sound;
            float volume = 2.0f;
            if (iFrameCount - playerSt.lastOngroundFrame < 10 && handSpeed > 1.5f) {
                //NPCDragonTakeoffFXSD
                sound = 0x3E5D8;
            } else if (handSpeed > 1.5f) {
                //NPCDragonWingFlapTakeoffSD
                volume = 2.5f;
                sound = 0x3D122;
            } else {
                // NPCDragonWingFlapSD
                volume = 2.5f;
                sound = 0x3CE00;
            }
            if (mustPlay) {
                PlayWingAnimation(playerSt.player, speedL, speedR);

                if (iFrameCount >= playerSt.reenableVibrateFrame) {
                    vibrateController(speedL.Length() * 5, 70000, true);
                    vibrateController(speedR.Length() * 5, 70000, false);
                    playerSt.reenableVibrateFrame = iFrameCount + 70 / 8;  // 70 is ms, 8 is ms/frame
                }


                playerSt.lastSoundFrame = iFrameCount;
                SKSE::GetTaskInterface()->AddTask([player, sound, volume]() { play_sound(player, sound, volume); });
            
            } else if (sound == 0x3E5D8) {
                PlayWingAnimation(playerSt.player, speedL, speedR);

                // Try to play wing animation
                playerSt.player->NotifyAnimationGraph("eFlyingUp"sv);
                if (iFrameCount >= playerSt.reenableVibrateFrame) {
                    vibrateController(speedL.Length() * 7, 100000, true);
                    vibrateController(speedR.Length() * 7, 100000, false);
                    playerSt.reenableVibrateFrame = iFrameCount + 100 / 8;  // number is ms, 8 is ms/frame
                }


                playerSt.lastSoundFrame = iFrameCount;
                SKSE::GetTaskInterface()->AddTask([player, sound, volume]() { play_sound(player, sound, volume); });
            } else {
                // Don't play, let wait to see if in the next 15 frames we can trigger 
            }

            playSoundWaitCount++;
        }


        playerSt.isSlappingWings = true;
        playerSt.frameLastSlap = iFrameCount;
    } else {
        // Case 2: player is moving controllers slow. This changes their wing direction
        playerSt.everSetWingDirSinceThisFlight = true;

        
        if (iFrameCount >= playerSt.reenableVibrateFrame) {
            if (speedL.Length() > 0.2f)
                vibrateController(1, 800, true);
            if (speedR.Length() > 0.2f)
                vibrateController(1, 800, false);
            playerSt.reenableVibrateFrame = iFrameCount + 8;
        }

        auto wingDirZ = playerSt.UpdateWingDir(eff->wingSpell.handPosL, eff->wingSpell.handPosR);
        log::trace("Change wings direction to: {}, {}, {}", playerSt.dirWings.x, playerSt.dirWings.y,
                   playerSt.dirWings.z);

        bool conf_skyDiving = true;
        if (conf_skyDiving && wingDirZ < 0.0f) {
            skyDivingCountDown--;
            if (skyDivingCountDown == 0) playerSt.isSkyDiving = true;
        } else {
            playerSt.isSkyDiving = false;
            skyDivingCountDown = 20;
        }
    }


}


// To player, their wings seem to be an art object
// For Bloody Dragon Wings, I want "mAnimation4s"
// Can google "NiControllerManager" to learn more
// From UAW author, we know the following animations can trigger wing animation:
// eFlyingUp: very noticable up and down animation
// JumpFall: extends the wings flatly, but forbids casting spells
// BeginWeaponDraw, BeginCastRight: wings move forward a little, good animation
// MRh_SpellFire_Event, Magic_Equip_Out, AttackStartRightHand: no animation
// We can also use the dozens of animation events called in AAFlyScriptB.psc
void PlayWingAnimation(RE::Actor* player, RE::NiPoint3 speedL, RE::NiPoint3 speedR) {
    auto& playerSt = PlayerState::GetSingleton();
    if (speedL.Length() == 0 || speedR.Length() == 0) return;
    speedL = speedL / speedL.Length();
    speedR = speedR / speedR.Length();
    // If both speed have negative Z that is less than -0.7f, play eFlyingUp
    if (speedL.z < -0.7f && speedR.z < -0.7f) {
        player->NotifyAnimationGraph("eFlyingUp"sv);
        playerSt.lastFlyUpFrame = iFrameCount;
        return;
    }

    // Otherwise, play BeginWeaponDraw
    player->NotifyAnimationGraph("BeginWeaponDraw"sv);
    return;
}