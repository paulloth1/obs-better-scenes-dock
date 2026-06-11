/*
Better Scenes Dock — folders, dividers & colors for the OBS scene list
Copyright (C) 2026 Paul Loth <mail@paulloth.de>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version. See <https://www.gnu.org/licenses/>.
*/

#pragma once

namespace bsd {

/* Registers the "Better Scenes" dock and wires up frontend callbacks.
 * Call from obs_module_post_load (frontend + Qt main thread ready). */
void dock_register();
void dock_unregister();

} // namespace bsd
