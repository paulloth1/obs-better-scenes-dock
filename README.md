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
- Bottom **toolbar**: add (scene / folder / divider), remove, set color, open
  scene filters, and reorder up/down — plus a full right-click context menu
- Structure persists per scene collection

> Status: early version (v0.2). Drag & drop, scene duplication and in-place
> renaming are coming next.

## How it works

The dock renders a plugin-owned tree where scene entries reference real OBS
scenes by name. Folders and dividers are pure plugin metadata stored per scene
collection in the plugin's config directory. Switching scenes uses the official
OBS frontend API, and the native `scenesDock` is hidden on load so this dock
takes its place (you can bring the native one back from the Docks menu).

## Installation

Grab the build for your platform from the
[Releases](https://github.com/paulloth1/obs-better-scenes-dock/releases) page
(macOS, Windows and Linux are built by CI). On macOS, copy
`better-scenes-dock.plugin` to:

```
~/Library/Application Support/obs-studio/plugins/
```

Requires OBS Studio 32+.

## Building

Official [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate)
layout for CI/releases; for fast local macOS iteration see [dev/README.md](dev/README.md).

## License

GPL-2.0-or-later — see [LICENSE](LICENSE).
