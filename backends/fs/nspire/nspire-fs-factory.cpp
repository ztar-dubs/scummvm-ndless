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

#include "backends/fs/nspire/nspire-fs-factory.h"
#include "backends/fs/nspire/nspire-fs.h"

#include <unistd.h>
#include <limits.h>

#ifndef MAXPATHLEN
#define MAXPATHLEN 256
#endif

AbstractFSNode *NspireFilesystemFactory::makeRootFileNode() const {
	return new NspireFilesystemNode("/");
}

AbstractFSNode *NspireFilesystemFactory::makeCurrentDirectoryFileNode() const {
	char buf[MAXPATHLEN];
	return getcwd(buf, MAXPATHLEN) ? new NspireFilesystemNode(buf) : NULL;
}

AbstractFSNode *NspireFilesystemFactory::makeFileNodePath(const Common::String &path) const {
	assert(!path.empty());
	return new NspireFilesystemNode(path);
}
