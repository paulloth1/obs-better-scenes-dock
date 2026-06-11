/*
Scene Dividers — Labeled dividers for the OBS scene list
Copyright (C) 2026 Paul Loth <mail@paulloth.de>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version. See <https://www.gnu.org/licenses/>.
*/

#pragma once

namespace sd {

/* Adds "Scene Dividers…" to the Tools menu (call from obs_module_post_load).
 * The dialog manages dividers: add, rename, color, remove, move up/down. */
void dialog_register_menu();
void dialog_close();

} // namespace sd
