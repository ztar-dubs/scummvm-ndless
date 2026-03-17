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

#ifndef NSPIRE_FILESYSTEM_H
#define NSPIRE_FILESYSTEM_H

#include "backends/fs/posix/posix-fs.h"

/**
 * TI-Nspire filesystem node.
 *
 * On the Nspire, all files must have a .tns extension to be visible
 * in the calculator's file explorer and to survive transfers via TI-Connect.
 *
 * This wrapper transparently handles the .tns suffix:
 * - When listing directory contents, .tns is stripped from display names
 *   so ScummVM can find files by their expected names
 * - When opening/creating files, .tns is appended to the real path
 * - Directories are NOT suffixed (they use .tns in their actual name already)
 */
class NspireFilesystemNode : public POSIXFilesystemNode {
protected:
	AbstractFSNode *makeNode(const Common::String &path) const override {
		return new NspireFilesystemNode(path);
	}

	NspireFilesystemNode() : POSIXFilesystemNode() {}

public:
	NspireFilesystemNode(const Common::String &path);

	bool exists() const override;
	AbstractFSNode *getChild(const Common::String &n) const override;
	bool getChildren(AbstractFSList &list, ListMode mode, bool hidden) const override;

	Common::SeekableReadStream *createReadStream() override;
	Common::SeekableWriteStream *createWriteStream(bool atomic) override;
	bool createDirectory() override;

	/**
	 * Strip .tns suffix from a filename for display purposes.
	 */
	static Common::String stripTnsSuffix(const Common::String &name);

	/**
	 * Append .tns suffix to a path if it doesn't already have one
	 * and the path is not a directory.
	 */
	static Common::String appendTnsSuffix(const Common::String &path);
};

#endif // NSPIRE_FILESYSTEM_H
