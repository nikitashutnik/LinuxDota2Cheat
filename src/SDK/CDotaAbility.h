#pragma once

// C_DOTABaseAbility data that is not networked (so not in the schema), read the way the game's own getters read it.
// The Panorama JS API (Entities.*, Abilities.*, Items.*) can't be used for this: in a real match every one of those
// handlers returns nothing once m_nGameState ranks past HERO_SELECTION, unless m_iGameMode is CUSTOM (15) or EVENT (19),
// which is why the hero demo (a custom game) works and a match doesn't. Verified in the Oct 2026 libclient handlers:
//   Abilities.GetAbilityName -> *(const char**)ability[+0x770]      the ability's KV definition starts with its name
//   Abilities.IsPassive      -> definition[+0x58] & 2               DOTA_ABILITY_BEHAVIOR_PASSIVE
//   Abilities.GetMaxLevel    -> m_nMaxLevelOverride >= 0 ? it : definition[+0x90]
constexpr int kAbilityDefinition = 0x770;
constexpr int kDefinitionBehavior = 0x58;
constexpr int kDefinitionMaxLevel = 0x90;

// The definition is attached lazily: every getter calls this when ability[+0x770] is still null. It looks the definition
// up by the entity's name and stores it at +0x770 (an empty one if the name is unknown). The second argument is unused.
typedef void (* LoadAbilityDefinitionFn)( void* ability, int unused );
inline LoadAbilityDefinitionFn LoadAbilityDefinition;
