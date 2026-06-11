# Better Scenes Dock for OBS Studio

A drop-in replacement for the OBS scene list with the structure it has always
been missing: **nested folders**, **collapsible dividers** and **colors** for
scenes, folders and dividers.

```
▼ 📁 GAME              (green)
     Gameplay
     Gameplay + Cam
  ▶ 📁 Replays  (3)
  ── BREAK ───────────
     BRB
     Pause
```

## Features

- Replaces the native Scenes dock with a tree view
- Click a scene to switch to it (sets the preview scene in Studio Mode)
- Nested, collapsible **folders**
- Labeled **dividers** (lines with optional text)
- **Colors** for scenes, folders and dividers
- Current program scene (red) / preview scene (green) indicators
- Structure persists per scene collection

> Status: early version (v0.2). Adding/removing/renaming scenes and drag & drop
> are coming next; for now use the right-click menu to create folders/dividers,
> set colors and move items.

## How it works

The dock renders a plugin-owned tree where scene entries reference real OBS
scenes by name. Folders and dividers are pure plugin metadata stored per scene
collection in the plugin's config directory. Switching scenes uses the official
OBS frontend API, and the native `scenesDock` is hidden on load so this dock
takes its place (you can bring the native one back from the Docks menu).

## Installation

Currently macOS only (Windows/Linux planned). Copy `better-scenes-dock.plugin`
to:

```
~/Library/Application Support/obs-studio/plugins/
```

Requires OBS Studio 32+.

## Building

Official [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate)
layout for CI/releases; for fast local macOS iteration see [dev/README.md](dev/README.md).

## License

GPL-2.0-or-later — see [LICENSE](LICENSE).
