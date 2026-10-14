---
name: unreal-sequencer-audio
description: Use when building Level Sequences (tracks, keys) or Sound Cues.
version: 1.0.0
tools: [sequence_create, sequence_track_add, sequence_key_add, soundcue_create]
---

# unreal-sequencer-audio

## When to use

Use when building Level Sequences (tracks, keys) or Sound Cues.

## Steps

1. `sequence_create` path, endFrame, fps.
2. `sequence_track_add` track (Transform, Visibility, Audio, Event, Fade) and actor label.
3. `sequence_key_add` actor/channel (e.g. Location.X)/frame/value.
4. `soundcue_create` path with an optional wave.

## Verify

`sequence_track_add` lists the channels; keys are added without errors.

## On failure

`sequencer.channel_not_found` lists the channels.
