---
name: unreal-advanced-animation
description: Use when sharing animations between skeletons or rigging characters: IK Rigs, IK Retargeters and batch retargeting, Control Rigs, and Motion Matching (Pose Search) databases.
version: 1.1.0
tools: [anim_ikrig_create, anim_retargeter_create, anim_retarget, controlrig_create, controlrig_get, controlrig_add_bone, controlrig_add_null, controlrig_add_control, posesearch_create]
---

# unreal-advanced-animation

## When to use

Use when sharing animations between skeletons or rigging characters: IK Rigs, IK Retargeters and batch retargeting, Control Rigs, and Motion Matching (Pose Search) databases.

## Steps

1. IK Rig per skeletal mesh: `unrealengine-cli anim ikrig /Game/Anim/IK_Source --mesh /Game/Chars/Source` (auto retarget chains and full-body IK; `--empty` for none). The result lists the chains.
2. Retargeter: `unrealengine-cli anim retargeter /Game/Anim/RTG_SourceToTarget --source /Game/Anim/IK_Source --target /Game/Anim/IK_Target`; check the chain mapping in the result.
3. Retarget animations: `unrealengine-cli anim retarget /Game/Anim/RTG_SourceToTarget /Game/Anim/Walk /Game/Anim/Run --folder /Game/Anim/Target`. New assets get the suffix `_Retargeted`.
4. Control Rig: `unrealengine-cli anim controlrig /Game/Rigs/CR_Hero --mesh /Game/Chars/Hero` imports the bones. Build the hierarchy with `controlrig_add_null` (spaces), `controlrig_add_control` (controlType transform|float|bool|position|rotator|scale, shape e.g. Circle_Thick, color, offset location) and `controlrig_add_bone`; `parent` is an element name (empty = top level). `controlrig_get` (optionally `type: bone|null|control`) lists elements with parents. Wire the Forward Solve graph in the Control Rig editor.
5. Motion Matching (PoseSearch plugin): `unrealengine-cli anim posesearch /Game/Anim/PSD_Locomotion --skeleton /Game/Chars/Hero_Skeleton /Game/Anim/Walk /Game/Anim/Idle` creates the schema and the database.

## Verify

`anim retarget` lists the created assets; `asset deps <asset>` shows they reference the target skeleton.

## On failure

| Code | Meaning |
|---|---|
| `anim.retarget_failed` | The assets are not for the source rig's skeleton, or results with that suffix already exist |
| `anim.ikrig_failed` | The mesh is not usable by an IK Rig |
| `posesearch.plugin_disabled` | Enable PoseSearch in the .uproject, rebuild and restart |
