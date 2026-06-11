# Scene Dividers for OBS Studio

Labeled, colored dividers **directly in the native OBS scene list** — like the
structure you wish your scene list had when it grows past a dozen scenes.

```
  Intro
  Starting Soon
  ─────── GAME ───────        ← divider (not clickable)
  Gameplay
  Gameplay + Cam
  ─────── BREAK ──────
  Break
  BRB
```

## Features

- Dividers appear as a line with a centered label in the regular scene list —
  no replacement dock, your workflow stays the same
- Optional accent color per divider
- Dividers are **not selectable**: clicking one can never switch your program scene
- Managed via *Tools → Scene Dividers…*: add, rename, color, remove, move up/down
- Persists per scene collection, survives restarts and collection switches

## How it works

A divider is a real (empty) scene marked via libobs private settings, which OBS
persists inside the scene collection. The plugin restyles those rows in the
scene list (custom item delegate) and removes their selectable flag. Because
every row stays a real scene, OBS keeps handling ordering, saving and loading
natively — the plugin never injects foreign rows into OBS internals.

### Known limitations

- Divider scenes are real scenes, so they are visible to obs-websocket clients
  (Stream Deck, Companion, …) and in scene dropdowns of other plugins.
- Assigning a scene-switch hotkey to a divider scene will switch to it (empty
  output). Don't do that.
- Grid mode of the scene list renders dividers as plain tiles for now.

## Installation

Currently macOS only (Windows/Linux builds are planned). Download the latest
release and copy `scene-dividers.plugin` to:

```
~/Library/Application Support/obs-studio/plugins/
```

Requires OBS Studio 32+.

## Building

The repository uses the official [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate)
layout (`CMakePresets.json`, CI builds). For fast local iteration on macOS
without a full Xcode install, see [dev/README.md](dev/README.md).

## License

GPL-2.0-or-later — see [LICENSE](LICENSE).
