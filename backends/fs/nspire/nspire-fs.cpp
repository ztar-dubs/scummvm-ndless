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

#include "backends/fs/nspire/nspire-fs.h"
#include "backends/fs/posix/posix-iostream.h"

#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

Common::String NspireFilesystemNode::stripTnsSuffix(const Common::String &name) {
	if (name.hasSuffix(".tns")) {
		return Common::String(name.c_str(), name.size() - 4);
	}
	return name;
}

Common::String NspireFilesystemNode::appendTnsSuffix(const Common::String &path) {
	if (path.hasSuffix(".tns")) {
		return path;
	}
	return path + ".tns";
}

NspireFilesystemNode::NspireFilesystemNode(const Common::String &p)
	: POSIXFilesystemNode() {

	assert(p.size() > 0);
	_path = Common::normalizePath(p, '/');
	_displayName = Common::lastPathComponent(_path, '/');

	// Try the path as-is first
	struct stat st;
	if (stat(_path.c_str(), &st) == 0) {
		_isValid = true;
		_isDirectory = S_ISDIR(st.st_mode);
		// For files (not directories), strip .tns from display name
		if (!_isDirectory) {
			_displayName = stripTnsSuffix(_displayName);
		}
		return;
	}

	// If not found and path doesn't end with .tns, try with .tns appended
	if (!p.hasSuffix(".tns")) {
		Common::String tnsPath = _path + ".tns";
		if (stat(tnsPath.c_str(), &st) == 0) {
			_path = tnsPath;
			_isValid = true;
			_isDirectory = S_ISDIR(st.st_mode);
			if (!_isDirectory) {
				_displayName = stripTnsSuffix(_displayName);
			}
			return;
		}
	}

	// Path doesn't exist (yet) - it may be created later
	_isValid = false;
	_isDirectory = false;
}

bool NspireFilesystemNode::exists() const {
	if (access(_path.c_str(), F_OK) == 0)
		return true;
	// Try with .tns
	if (!_path.hasSuffix(".tns")) {
		Common::String tnsPath = _path + ".tns";
		return access(tnsPath.c_str(), F_OK) == 0;
	}
	return false;
}

AbstractFSNode *NspireFilesystemNode::getChild(const Common::String &n) const {
	assert(!_path.empty());
	assert(_isDirectory);
	assert(!n.contains('/'));

	Common::String newPath(_path);
	if (_path.lastChar() != '/')
		newPath += '/';
	newPath += n;

	return makeNode(newPath);
}

bool NspireFilesystemNode::getChildren(AbstractFSList &myList, ListMode mode, bool hidden) const {
	assert(_isDirectory);

	DIR *dirp = opendir(_path.c_str());
	if (dirp == NULL)
		return false;

	struct dirent *dp;
	while ((dp = readdir(dirp)) != NULL) {
		// Skip hidden files
		if (dp->d_name[0] == '.' && !hidden)
			continue;
		// Skip . and ..
		if ((dp->d_name[0] == '.' && dp->d_name[1] == 0) ||
		    (dp->d_name[0] == '.' && dp->d_name[1] == '.' && dp->d_name[2] == 0))
			continue;

		Common::String entryPath(_path);
		if (_path.lastChar() != '/')
			entryPath += '/';
		entryPath += dp->d_name;

		// Use stat to determine if it's a file or directory
		struct stat st;
		if (stat(entryPath.c_str(), &st) != 0)
			continue;

		bool isDir = S_ISDIR(st.st_mode);

		// Honor the chosen mode
		if ((mode == Common::FSNode::kListFilesOnly && isDir) ||
		    (mode == Common::FSNode::kListDirectoriesOnly && !isDir))
			continue;

		NspireFilesystemNode *entry = new NspireFilesystemNode();
		entry->_path = entryPath;
		entry->_isValid = true;
		entry->_isDirectory = isDir;

		// Strip .tns from display name for files
		Common::String dname(dp->d_name);
		if (!isDir) {
			entry->_displayName = stripTnsSuffix(dname);
		} else {
			entry->_displayName = dname;
		}

		myList.push_back(entry);
	}

	closedir(dirp);
	return true;
}

Common::SeekableReadStream *NspireFilesystemNode::createReadStream() {
	// Try path as-is first
	StdioStream *stream = PosixIoStream::makeFromPath(_path, StdioStream::WriteMode_Read);
	if (stream)
		return stream;

	// Try with .tns appended
	if (!_path.hasSuffix(".tns")) {
		Common::String tnsPath = _path + ".tns";
		stream = PosixIoStream::makeFromPath(tnsPath, StdioStream::WriteMode_Read);
		if (stream) {
			_path = tnsPath;
			return stream;
		}
	}

	return nullptr;
}

Common::SeekableWriteStream *NspireFilesystemNode::createWriteStream(bool atomic) {
	// Always append .tns for new files (unless already present or is a directory)
	Common::String writePath = _path;
	if (!_isDirectory && !writePath.hasSuffix(".tns")) {
		writePath = writePath + ".tns";
	}

	StdioStream *stream = PosixIoStream::makeFromPath(writePath,
		atomic ? StdioStream::WriteMode_WriteAtomic : StdioStream::WriteMode_Write);

	if (stream) {
		_path = writePath;
	}

	return stream;
}

bool NspireFilesystemNode::createDirectory() {
	if (mkdir(_path.c_str(), 0755) == 0) {
		_isValid = true;
		_isDirectory = true;
		return true;
	}
	return false;
}
