# Better Scenes Dock for OBS Studio

A drop-in replacement for the OBS scene list with the structure it has always
been missing: **nested folders**, **collapsible dividers** and **colors** for
scenes, folders and dividers.

<p align="center">
  <img src="docs/images/better-scenes-dock.png" alt="The Better Scenes dock showing colored scenes, a folder, a divider and the right-click menu" width="440">
</p>

## Features

- Replaces the native Scenes dock with a tree view
- Click a scene to switch to it; in **Studio Mode** a click sets preview and a
  double-click transitions it to program
- Nested, collapsible **folders** with a scene-count badge
- Labeled **dividers** (lines with optional text)
- **Colors** for scenes, folders and dividers, with a preset palette picker
- Current program scene (red) / preview scene (green) indicators
- **Search box** to filter scenes (can be hidden via Options)
- Bottom **toolbar**: add (scene / folder / divider), remove, set color, open
  scene filters, reorder up/down, and an Options menu
- Full right-click **scene menu** matching the native dock: rename, duplicate
  (refs or full copies), scene projector (windowed / per-monitor fullscreen),
  transition override (with duration), save scene screenshot, show in multiview,
  copy/paste filters
- The dock order is **kept in sync with OBS' scene list**, so Multiview and
  other tools follow what you see
- Renaming a scene anywhere (even outside the dock) keeps its folder & color
- Structure persists per scene collection

## How it works

The dock renders a plugin-owned tree where scene entries reference real OBS
scenes by name. Folders and dividers are pure plugin metadata stored per scene
collection in the plugin's config directory. Switching scenes uses the official
OBS frontend API, and the native `scenesDock` is hidden on load so this dock
takes its place (you can bring the native one back from the Docks menu). The
flattened scene order is mirrored back into OBS' own scene list so Multiview and
anything else reading the scene order stays consistent with the dock.

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
