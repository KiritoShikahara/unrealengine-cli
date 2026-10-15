import unreal

from .. import tool, ToolError
from ..util import actors

# Classes a preset owns: replace=True removes these before spawning.
_LIGHTING = ("DirectionalLight", "SkyLight", "SkyAtmosphere", "ExponentialHeightFog", "VolumetricCloud", "PostProcessVolume")


def _eas():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _spawn(cls, label, loc=(0, 0, 0), rot=(0, 0, 0)):
    a = _eas().spawn_actor_from_class(cls, unreal.Vector(*loc), unreal.Rotator(rot[2], rot[0], rot[1]))
    a.set_actor_label(label)
    return a


def _sun(intensity, color, pitch):
    sun = _spawn(unreal.DirectionalLight, "Sun", (0, 0, 500), (pitch, -30, 0))
    c = sun.get_component_by_class(unreal.DirectionalLightComponent)
    c.set_mobility(unreal.ComponentMobility.MOVABLE)
    c.set_editor_property("intensity", intensity)
    c.set_editor_property("light_color", unreal.Color(*color, 255))
    c.set_editor_property("atmosphere_sun_light", True)
    return sun


def _sky(intensity):
    sky = _spawn(unreal.SkyLight, "SkyLight", (0, 0, 600))
    c = sky.get_component_by_class(unreal.SkyLightComponent)
    c.set_mobility(unreal.ComponentMobility.MOVABLE)
    c.set_editor_property("real_time_capture", True)
    c.set_editor_property("intensity", intensity)
    return sky


def _post(exposure_bias, min_ev=None, max_ev=None):
    ppv = _spawn(unreal.PostProcessVolume, "PostProcess", (0, 0, 0))
    ppv.set_editor_property("unbound", True)
    s = ppv.get_editor_property("settings")
    s.set_editor_property("override_auto_exposure_bias", True)
    s.set_editor_property("auto_exposure_bias", exposure_bias)
    if min_ev is not None:
        s.set_editor_property("override_auto_exposure_min_brightness", True)
        s.set_editor_property("auto_exposure_min_brightness", min_ev)
        s.set_editor_property("override_auto_exposure_max_brightness", True)
        s.set_editor_property("auto_exposure_max_brightness", max_ev)
    ppv.set_editor_property("settings", s)
    return ppv


@tool("light_preset", "lighting",
      "Light the open level with a preset: outdoor (sun, sky atmosphere, sky light, height fog, clouds, post process), "
      "night (moonlight, dark sky, fog) or indoor (no sun: sky light, a ceiling point light, post process with "
      "eye adaptation). replace=true (default) first removes the level's existing sun, sky, fog, cloud and post process actors.",
      {"preset": {"type": "string", "enum": ["outdoor", "night", "indoor"]}, "replace": {"type": "boolean"}},
      ["preset"], mutating=True)
def light_preset(preset, replace=True):
    if preset not in ("outdoor", "night", "indoor"):
        raise ToolError("args.invalid", "unknown preset '%s'" % preset, hint="outdoor, night or indoor")
    removed = []
    if replace:
        doomed = [a for a in actors() if a.get_class().get_name() in _LIGHTING]
        removed = [a.get_actor_label() for a in doomed]
        for a in doomed:
            _eas().destroy_actor(a)
    spawned = []
    if preset in ("outdoor", "night"):
        night = preset == "night"
        spawned.append(_sun(0.08 if night else 10.0, (120, 140, 255) if night else (255, 244, 230), -15 if night else -40))
        spawned.append(_spawn(unreal.SkyAtmosphere, "SkyAtmosphere"))
        spawned.append(_sky(0.4 if night else 1.0))
        fog = _spawn(unreal.ExponentialHeightFog, "HeightFog", (0, 0, -200))
        fc = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
        fc.set_editor_property("fog_density", 0.04 if night else 0.02)
        spawned.append(fog)
        if not night:
            spawned.append(_spawn(unreal.VolumetricCloud, "Clouds"))
        spawned.append(_post(1.0 if night else 0.0))
    else:
        spawned.append(_sky(0.5))
        lamp = _spawn(unreal.PointLight, "CeilingLight", (0, 0, 350))
        lc = lamp.get_component_by_class(unreal.PointLightComponent)
        lc.set_editor_property("intensity", 5000.0)
        lc.set_editor_property("attenuation_radius", 2000.0)
        spawned.append(lamp)
        spawned.append(_post(0.5, 0.5, 4.0))
    return {"preset": preset, "removed": removed, "spawned": [a.get_actor_label() for a in spawned]}
