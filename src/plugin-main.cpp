/*
Scene Dividers — Labeled dividers for the OBS scene list
Copyright (C) 2026 Paul Loth <mail@paulloth.de>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version. See <https://www.gnu.org/licenses/>.
*/

#include <obs-module.h>
#include <plugin-support.h>

#include "sd-dialog.hpp"
#include "sd-scene-list.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "Scene Dividers loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

/* Frontend + Qt main thread are ready only after loading. */
void obs_module_post_load(void)
{
	sd::scene_list_init();
	sd::dialog_register_menu();
}

void obs_module_unload(void)
{
	sd::dialog_close();
	sd::scene_list_shutdown();
}
