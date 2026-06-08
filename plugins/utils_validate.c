/*****************************************************************************
 *
 * Library of useful input validation functions for plugins
 *
 * License: GPL
 * Copyright (c) 2000 Karl DeBisschop (karl@debisschop.net)
 * Copyright (c) 2002-2024 Monitoring Plugins Development Team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 *****************************************************************************/

#include "common.h"
#include "./utils_validate.h"
#include "./utils.h"
#include <unistd.h>

/* check whether a file exists */
void validate_file_exists(char *path) {
	if (access(path, R_OK) == 0) {
		return;
	}
	usage2(_("file does not exist or is not readable"), path);
}
