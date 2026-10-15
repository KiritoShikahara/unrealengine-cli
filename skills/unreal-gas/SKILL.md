---
name: unreal-gas
description: Use when building with the Gameplay Ability System: attributes (health, mana), gameplay effects (damage, buffs, damage over time), abilities, gameplay cues and gameplay tags, and checking them in play.
version: 1.0.0
tools: [gas_effect_create, gas_ability_create, gas_cue_create, gas_component_add, gas_runtime, tags_add, tags_list, tags_rename, tags_remove]
---

# unreal-gas

## When to use

Use when building with the Gameplay Ability System: attributes (health, mana), gameplay effects (damage, buffs, damage over time), abilities, gameplay cues and gameplay tags, and checking them in play.

## Steps

1. Enable GAS once: `unrealengine-cli gas enable` (plugin and module dependencies), then `unrealengine-cli code apply` (rebuild and restart).
2. Attributes are C++: `unrealengine-cli gas attribute-set HeroAttributes --attr Health=100 --attr MaxHealth=100 --attr Mana=50`, then `code apply`. Add `--no-replicate` for single-player games.
3. Tags before the assets that use them: `unrealengine-cli tags add State.Burning` (`tags list --filter State`, `tags rename`, `tags remove`).
4. Effects: `unrealengine-cli gas effect /Game/GAS/GE_Damage --modifier HeroAttributes.Health:add:-10` (instant). Over time: `--duration 5 --period 1 --grant-tag State.Burning --stacking target --stack-limit 3`; forever: `--duration infinite`.
5. Abilities: `unrealengine-cli gas ability /Game/GAS/GA_Fireball --effect /Game/GAS/GE_Damage [--cost GE] [--cooldown GE] --tag Ability.Fireball` builds ActivateAbility -> CommitAbility -> ApplyGameplayEffectToOwner -> EndAbility. Edit its graph for other behaviour.
6. Cues: `unrealengine-cli gas cue /Game/GAS/GC_Hit --tag GameplayCue.Hit [--actor]`, then implement OnExecute in its graph.
7. Give an actor Blueprint the component: `unrealengine-cli gas component /Game/BP_Hero --attribute-set HeroAttributes`. It also writes `DT_<Blueprint>_<Set>` with the starting values; edit that table to change them.
8. Check in play: `unrealengine-cli pie start`, then `gas state <actor>`, `gas apply <actor> <effect>`, `gas give <actor> <ability>`, `gas activate <actor> <ability>` (add `--world server` in multiplayer). Each returns the attributes, owned tags, abilities and active effects.

## Verify

`gas apply` shows the changed attribute in `state.attributes` (e.g. Health 100 -> 90); `gas activate` returns `activated: true`; granted tags appear in `state.tags` while the effect lasts.

## On failure

| Code | Meaning |
|---|---|
| `gas.not_enabled` | Run `gas enable`, rebuild and restart |
| `gas.bad_attribute` | Attributes are `Set.Attribute` with a compiled AttributeSet class; run `code apply` after generating one |
| `gas.no_component` | The actor has no AbilitySystemComponent: `gas component <bp> --attribute-set <Set>` |
| `tags.not_found` | Add the tag first: `tags add <tag>` |
| `activated: false` | Give the ability first, or its cost, cooldown or tags block it |
| Attributes empty in `gas state` | The actor was placed before `gas component`; delete and place it again |
