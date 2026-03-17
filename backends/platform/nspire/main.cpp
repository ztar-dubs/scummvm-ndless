/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
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
 */

#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include "common/scummsys.h"
#include "backends/platform/nspire/osystem-nspire.h"
#include "base/main.h"

#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
	// Change to the program's directory so relative paths work
	// On Nspire, argv[0] contains the full path like /documents/scummvm/scummvm.tns
	if (argc > 0 && argv[0]) {
		char progdir[256];
		strncpy(progdir, argv[0], sizeof(progdir) - 1);
		progdir[sizeof(progdir) - 1] = '\0';
		char *last_slash = strrchr(progdir, '/');
		if (last_slash) {
			*last_slash = '\0';
			chdir(progdir);
		}
	}

	g_system = new OSystem_NSPIRE();
	assert(g_system);

	int res = scummvm_main(argc, argv);
	g_system->destroy();
	return res;
}
