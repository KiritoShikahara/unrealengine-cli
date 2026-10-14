import unreal

from .. import ToolError, tool
from ..util import create_asset, find_actor, load_asset_or_fail

TRACKS = {
    "Transform": "MovieScene3DTransformTrack",
    "Visibility": "MovieSceneVisibilityTrack",
    "Audio": "MovieSceneAudioTrack",
    "Event": "MovieSceneEventTrack",
    "Fade": "MovieSceneFadeTrack",
}


@tool("soundcue_create", "audio", "Create a Sound Cue (optionally wrapping a Sound Wave).",
      {"path": {"type": "string"}, "wave": {"type": "string"}}, ["path"], mutating=True, undoable=False)
def soundcue_create(path, wave=None):
    f = unreal.SoundCueFactoryNew()
    if wave:
        f.set_editor_property("initial_sound_wave", load_asset_or_fail(wave, unreal.SoundWave))
    cue = create_asset(path, unreal.SoundCue, f)
    return {"path": cue.get_path_name()}


@tool("sequence_create", "sequencer", "Create a Level Sequence with a frame range.",
      {"path": {"type": "string"}, "endFrame": {"type": "integer"}, "fps": {"type": "integer"}}, ["path"],
      mutating=True, undoable=False)
def sequence_create(path, endFrame=150, fps=30):
    seq = create_asset(path, unreal.LevelSequence, unreal.LevelSequenceFactoryNew())
    seq.set_display_rate(unreal.FrameRate(fps, 1))
    seq.set_playback_start(0)
    seq.set_playback_end(endFrame)
    return {"path": seq.get_path_name(), "endFrame": endFrame, "fps": fps}


def _binding(seq, label):
    for b in seq.get_bindings():
        if str(b.get_display_name()) == label:
            return b
    return None


@tool("sequence_track_add", "sequencer", "Bind a level actor (by label) and add a track: Transform | Visibility | "
      "Audio | Event | Fade (Fade/Audio/Event need no actor).",
      {"path": {"type": "string"}, "track": {"type": "string", "enum": list(TRACKS)}, "actor": {"type": "string"}},
      ["path", "track"], mutating=True)
def sequence_track_add(path, track, actor=None):
    seq = load_asset_or_fail(path, unreal.LevelSequence)
    cls = getattr(unreal, TRACKS[track])
    if actor:
        b = _binding(seq, actor) or seq.add_possessable(find_actor(actor))
        t = b.add_track(cls)
    else:
        t = seq.add_track(cls)
    section = t.add_section()
    section.set_range(seq.get_playback_start(), seq.get_playback_end())
    return {"path": path, "track": track, "actor": actor, "channels": [str(c.channel_name) for c in section.get_all_channels()]}


@tool("sequence_key_add", "sequencer", "Add a key on a bound actor's track channel (e.g. channel 'Location.X').",
      {"path": {"type": "string"}, "actor": {"type": "string"}, "channel": {"type": "string"},
       "frame": {"type": "integer"}, "value": {"type": "number"}}, ["path", "actor", "channel", "frame", "value"],
      mutating=True)
def sequence_key_add(path, actor, channel, frame, value):
    seq = load_asset_or_fail(path, unreal.LevelSequence)
    b = _binding(seq, actor)
    if b is None:
        raise ToolError("sequencer.binding_not_found", "'%s' is not bound" % actor, hint="add a track first: sequence_track_add")
    names = []
    for t in b.get_tracks():
        for s in t.get_sections():
            for c in s.get_all_channels():
                names.append(str(c.channel_name))
                if str(c.channel_name) == channel:
                    c.add_key(unreal.FrameNumber(frame), value)
                    return {"path": path, "actor": actor, "channel": channel, "frame": frame, "value": value}
    raise ToolError("sequencer.channel_not_found", "no channel '%s'" % channel, hint="channels: " + ", ".join(names))
