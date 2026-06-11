/*
Scene Dividers — Labeled dividers for the OBS scene list
Copyright (C) 2026 Paul Loth <mail@paulloth.de>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version. See <https://www.gnu.org/licenses/>.
*/

#pragma once

class QListWidget;

namespace sd {

/* Hooks into OBS' native scene list (QListWidget "scenes" in the main
 * window): installs the divider item delegate and keeps divider rows
 * unselectable. Call init from obs_module_post_load, shutdown from unload. */
void scene_list_init();
void scene_list_shutdown();

/* The native scene list, or nullptr while not hooked. Row order in this
 * widget IS the scene order OBS persists ("scene_order"). */
QListWidget *scene_list_widget();

void restyle_scene_list();
void schedule_restyle();

/* Forces OBS to rebuild its multiview projectors so a freshly changed
 * "show_in_multiview" flag takes effect immediately (OBS reacts to the
 * SceneTree::scenesReordered signal with UpdateMultiviewProjectors). */
void refresh_multiview();

/* Moves a row inside the native list (divider rows only — moving a normal
 * scene this way would fight with OBS' own selection handling). */
bool move_scene_row(int from, int to);

} // namespace sd
