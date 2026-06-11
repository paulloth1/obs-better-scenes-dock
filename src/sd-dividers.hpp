/*
Scene Dividers — Labeled dividers for the OBS scene list
Copyright (C) 2026 Paul Loth <mail@paulloth.de>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version. See <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <obs.h>

#include <string>

namespace sd {

/* Divider state lives in the scene source's *private* settings, which libobs
 * persists inside the scene collection (obs.c saves "private_settings" per
 * source). That makes dividers survive restarts and collection switches and
 * keeps them robust against renames — no magic name prefixes needed. */
constexpr const char *kDividerKey = "scene_dividers_marker";
constexpr const char *kColorKey = "scene_dividers_color";

bool is_divider(obs_source_t *source);
bool is_divider_by_name(const char *name);

/* Accent color as "#RRGGBB"; empty string = theme default. */
std::string divider_color(obs_source_t *source);
std::string divider_color_by_name(const char *name);
void set_divider_color_by_name(const char *name, const char *hex_or_empty);

/* True if the label consists only of dashes/underscores/box-drawing chars,
 * i.e. the divider should be painted as a plain line without text. */
bool label_is_plain_line(const char *label);

/* Creates the marker scene (empty label -> plain line). Returns the unique
 * scene name actually used, or an empty string on failure. The scene appears
 * in the scene list right below the currently selected scene. */
std::string create_divider(const char *label);

/* Both refuse to touch scenes that are not dividers. */
bool remove_divider_by_name(const char *name);
bool rename_divider(const char *old_name, const char *new_name);

} // namespace sd
