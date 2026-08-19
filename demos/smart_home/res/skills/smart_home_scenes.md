---
name: smart_home_scenes
description: Scene mapping for the virtual smart home.
priority: 90
flags: enabled,llm_visible,summary_only
---

# Smart Home Scene Policy

This Skill is the source of truth for the scene catalog. The JSON block below
is loaded and validated by the device at startup; do not describe a scene that
is absent from that catalog.

Use `run_scene` when the user asks for a named scene or an obvious home scene.

Supported scenes:

- `sleep`: living light off, bedroom light 10%, bedroom AC cool 26C auto fan.
- `movie`: living light 20%, bedroom light off, bedroom AC cool 24C low fan.
- `away`: all lights off and bedroom AC off.
- `home`: living light 65%, bedroom light 35%, bedroom AC auto 25C auto fan.

If the user asks for a custom scene that is not listed, use individual device tools instead of inventing a scene name.

<!-- scene_catalog:start -->
{
  "version": 1,
  "scenes": [
    {
      "id": "sleep",
      "actions": [
        {"type": "light", "room": "living_room", "on": false, "brightness": 0},
        {"type": "light", "room": "bedroom", "on": true, "brightness": 10},
        {"type": "ac", "room": "bedroom", "on": true, "mode": "cool", "fanSpeed": "auto", "temperature": 26}
      ]
    },
    {
      "id": "movie",
      "actions": [
        {"type": "light", "room": "living_room", "on": true, "brightness": 20},
        {"type": "light", "room": "bedroom", "on": false, "brightness": 0},
        {"type": "ac", "room": "bedroom", "on": true, "mode": "cool", "fanSpeed": "low", "temperature": 24}
      ]
    },
    {
      "id": "away",
      "actions": [
        {"type": "light", "room": "living_room", "on": false, "brightness": 0},
        {"type": "light", "room": "bedroom", "on": false, "brightness": 0},
        {"type": "ac", "room": "bedroom", "on": false, "mode": "auto", "fanSpeed": "auto", "temperature": 26}
      ]
    },
    {
      "id": "home",
      "actions": [
        {"type": "light", "room": "living_room", "on": true, "brightness": 65},
        {"type": "light", "room": "bedroom", "on": true, "brightness": 35},
        {"type": "ac", "room": "bedroom", "on": true, "mode": "auto", "fanSpeed": "auto", "temperature": 25}
      ]
    }
  ]
}
<!-- scene_catalog:end -->
