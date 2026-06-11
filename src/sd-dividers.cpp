/*
Scene Dividers — Labeled dividers for the OBS scene list
Copyright (C) 2026 Paul Loth <mail@paulloth.de>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version. See <https://www.gnu.org/licenses/>.
*/

#include "sd-dividers.hpp"

#include <plugin-support.h>

namespace sd {

bool is_divider(obs_source_t *source)
{
	if (!source)
		return false;
	obs_data_t *priv = obs_source_get_private_settings(source);
	const bool marker = obs_data_get_bool(priv, kDividerKey);
	obs_data_release(priv);
	return marker;
}

bool is_divider_by_name(const char *name)
{
	obs_source_t *source = obs_get_source_by_name(name);
	const bool marker = is_divider(source);
	obs_source_release(source);
	return marker;
}

std::string divider_color(obs_source_t *source)
{
	if (!source)
		return "";
	obs_data_t *priv = obs_source_get_private_settings(source);
	std::string color = obs_data_get_string(priv, kColorKey);
	obs_data_release(priv);
	return color;
}

std::string divider_color_by_name(const char *name)
{
	obs_source_t *source = obs_get_source_by_name(name);
	std::string color = divider_color(source);
	obs_source_release(source);
	return color;
}

void set_divider_color_by_name(const char *name, const char *hex_or_empty)
{
	obs_source_t *source = obs_get_source_by_name(name);
	if (source && is_divider(source)) {
		obs_data_t *priv = obs_source_get_private_settings(source);
		obs_data_set_string(priv, kColorKey, hex_or_empty ? hex_or_empty : "");
		obs_data_release(priv);
	}
	obs_source_release(source);
}

bool label_is_plain_line(const char *label)
{
	if (!label || !*label)
		return true;
	for (const char *p = label; *p;) {
		const unsigned char c = (unsigned char)*p;
		if (c == '-' || c == '_' || c == ' ' || c == '~') {
			p++;
		} else if (c == 0xE2 && (unsigned char)p[1] == 0x94 && (unsigned char)p[2] == 0x80) {
			p += 3; /* U+2500 BOX DRAWINGS LIGHT HORIZONTAL */
		} else if (c == 0xE2 && (unsigned char)p[1] == 0x80 && (unsigned char)p[2] == 0x94) {
			p += 3; /* U+2014 EM DASH */
		} else {
			return false;
		}
	}
	return true;
}

std::string create_divider(const char *label)
{
	const std::string base = (label && *label) ? label : "─────";
	std::string name = base;
	int suffix = 2;
	for (;;) {
		obs_source_t *existing = obs_get_source_by_name(name.c_str());
		if (!existing)
			break;
		obs_source_release(existing);
		/* Scene names must be unique. For plain lines just grow the
		 * line; for labels append a counter like OBS does. */
		if (label_is_plain_line(base.c_str()))
			name += "─";
		else
			name = base + " (" + std::to_string(suffix++) + ")";
	}

	obs_scene_t *scene = obs_scene_create(name.c_str());
	if (!scene) {
		obs_log(LOG_WARNING, "could not create divider scene '%s'", name.c_str());
		return "";
	}
	obs_source_t *source = obs_scene_get_source(scene);
	obs_data_t *priv = obs_source_get_private_settings(source);
	obs_data_set_bool(priv, kDividerKey, true);
	obs_data_release(priv);
	/* The frontend's source_create handler already attached the scene to
	 * the scene list (which holds its own reference), so ours can go. */
	obs_scene_release(scene);
	return name;
}

bool remove_divider_by_name(const char *name)
{
	obs_source_t *source = obs_get_source_by_name(name);
	const bool ok = is_divider(source);
	if (ok)
		obs_source_remove(source);
	obs_source_release(source);
	return ok;
}

bool rename_divider(const char *old_name, const char *new_name)
{
	if (!new_name || !*new_name)
		return false;
	obs_source_t *taken = obs_get_source_by_name(new_name);
	if (taken) {
		obs_source_release(taken);
		return false;
	}
	obs_source_t *source = obs_get_source_by_name(old_name);
	const bool ok = is_divider(source);
	if (ok)
		obs_source_set_name(source, new_name);
	obs_source_release(source);
	return ok;
}

} // namespace sd
