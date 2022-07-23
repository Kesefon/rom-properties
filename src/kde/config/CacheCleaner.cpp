/***************************************************************************
 * ROM Properties Page shell extension. (KDE)                              *
 * CacheCleaner.cpp: Cache cleaner object for CacheTab.                    *
 *                                                                         *
 * Copyright (c) 2016-2022 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#include "stdafx.h"
#include "config.librpfile.h"
#include "CacheCleaner.hpp"

// libunixcommon
#include "libunixcommon/userdirs.hpp"

// librpfile
#include "librpfile/FileSystem.hpp"
using namespace LibRpFile;

// C includes
#include <dirent.h>
#include <fcntl.h>	// AT_FDCWD
#include <sys/stat.h>	// stat(), statx()

// d_type compatibility values
#include "d_type.h"

// C++ STL classes
using std::list;
using std::pair;
using std::string;

/**
 * Get a file's type using stat().
 * TODO: Move to libunixcommon?
 * @param path File path
 * @param deref If true, dereference symbolic links (lstat)
 * @return File type
 */
static uint8_t get_dtype_via_stat(const char *path, bool deref = false)
{
	// TODO: statx() if it's available.
	struct stat sb;
	int ret = (unlikely(deref) ? stat(path, &sb) : lstat(path, &sb));
	if (ret != 0) {
		// stat() failed.
		return DT_UNKNOWN;
	}

	uint8_t d_type;
	switch (sb.st_mode & S_IFMT) {
		default:
			// Not supported.
			d_type = DT_UNKNOWN;
			break;
		case S_IFDIR:
			d_type = DT_DIR;
			break;
		case S_IFREG:
			d_type = DT_REG;
			break;
		case S_IFLNK:
			d_type = DT_LNK;
			break;
	}
	return d_type;
}

/**
 * Recursively scan a directory for files.
 * @param path	[in] Path to scan.
 * @param rlist	[in/out] Return list for filenames and file types. (d_type)
 * @return 0 on success; non-zero on error.
 */
static int recursiveScan(const char *path, list<pair<tstring, uint8_t> > &rlist)
{
	DIR *pdir = opendir(path);
	if (!pdir) {
		// Error opening the directory.
		return -errno;
	}

	// Search for files and directories.
	struct dirent *dirent;
	while ((dirent = readdir(pdir)) != nullptr) {
		// Skip "." and "..".
		if (dirent->d_name[0] == '.' &&
		    (dirent->d_name[1] == '\0' ||
		     (dirent->d_name[1] == '.' && dirent->d_name[2] == '\0')))
		{
			continue;
		}

		string fullpath(path);
		fullpath += '/';
		fullpath += dirent->d_name;

		// Check if this is a regular file.
		uint8_t d_type = dirent->d_type;
		switch (d_type) {
			default:
				// Not supported.
				// TODO: Better error message.
				closedir(pdir);
				return -EIO;

			case DT_REG:
			case DT_DIR:
				// Supported.
				break;

			case DT_LNK:
				// Symbolic link. Dereference it and check again.
				d_type = get_dtype_via_stat(fullpath.c_str(), true);
				switch (d_type) {
					case DT_REG:
					case DT_DIR:
						// Supported.
						break;

					case DT_UNKNOWN:
						// This is probably a dangling symlink.
						// Delete it anyway.
						break;

					default:
						// Not supported.
						// TODO: Better error message.
						closedir(pdir);
						return -EIO;
				}
				break;

			case DT_UNKNOWN:
				// Unknown. Use stat().
				d_type = get_dtype_via_stat(fullpath.c_str(), false);
				switch (d_type) {
					default:
						// Not supported.
						// TODO: Better error message.
						closedir(pdir);
						return -EIO;

					case DT_REG:
					case DT_DIR:
						// Supported.
						break;

					case DT_LNK:
						// Symbolic link. Dereference it and check again.
						d_type = get_dtype_via_stat(fullpath.c_str(), true);
						switch (d_type) {
							case DT_REG:
							case DT_DIR:
								// Supported.
								break;

							case DT_UNKNOWN:
								// This is probably a dangling symlink.
								// Delete it anyway.
								break;

							default:
								// Not supported.
								// TODO: Better error message.
								closedir(pdir);
								return -EIO;
						}
						break;
				}
				break;
		}

		// Check the filename to see if we should delete it.
		if (d_type == DT_REG || d_type == DT_UNKNOWN) {
			// Thumbs.db files can be deleted.
			if (!strcasecmp(dirent->d_name, _T("Thumbs.db")))
				goto isok;

			// Check the extension.
			size_t len = strlen(dirent->d_name);
			if (len <= 4) {
				// Filename is too short. This is bad.
				closedir(pdir);
				return -EIO;
			}

			const char *pExt = &dirent->d_name[len-4];
			if (strcasecmp(pExt, _T(".png")) != 0 &&
			    strcasecmp(pExt, _T(".jpg")) != 0)
			{
				// Extension is not valid.
				closedir(pdir);
				return -EIO;
			}

			// All checks pass.
		}
	isok:

		// If this is a directory, recursively scan it, then add it.
		if (d_type == DT_DIR) {
			// Recursively scan it.
			recursiveScan(fullpath.c_str(), rlist);
		}

		// Add the filename and file type.
		rlist.emplace_back(std::move(fullpath), d_type);
	};
	closedir(pdir);

	return 0;
}

/** CacheCleaner **/

CacheCleaner::CacheCleaner(QObject *parent, CacheCleaner::CacheDir cacheDir)
	: super(parent)
	, m_cacheDir(cacheDir)
{
	qRegisterMetaType<CacheDir>();
}

/**
 * Run the task.
 * This should be connected to QThread::started().
 */
void CacheCleaner::run(void)
{
	string cacheDir;
	QString qs_err;
	switch (m_cacheDir) {
		default:
			assert(!"Invalid cache directory specified.");
			qs_err = tr("Invalid cache directory specified.");
			break;

		case CacheCleaner::CD_System:
			// System thumbnails. (~/.cache/thumbnails)
			cacheDir = LibUnixCommon::getCacheDirectory();
			if (cacheDir.empty()) {
				qs_err = tr("Unable to get the XDG cache directory.");
				break;
			}
			// Append "/thumbnails".
			cacheDir += "/thumbnails";
			if (!LibUnixCommon::isWritableDirectory(cacheDir.c_str())) {
				// Thumbnails subdirectory does not exist. (or is not writable)
				// TODO: Check specifically if it's not writable or doesn't exist?
				qs_err = tr("Thumbnails cache directory does not exist.");
				break;
			}
			break;

		case CacheCleaner::CD_RomProperties:
			// rom-properties cache. (~/.cache/rom-properties)
			cacheDir = FileSystem::getCacheDirectory();
			if (cacheDir.empty()) {
				qs_err = tr("Unable to get the rom-properties cache directory.");
				break;
			}

			// Does the cache directory exist?
			// If it doesn't, we'll act like it's empty.
			if (FileSystem::access(cacheDir.c_str(), R_OK) != 0) {
				emit progress(1, 1, false);
				emit cacheIsEmpty(m_cacheDir);
				emit finished();
				return;
			}

			break;
	}

	if (!qs_err.isEmpty()) {
		// An error occurred trying to get the directory.
		emit progress(1, 1, true);
		emit error(qs_err);
		emit finished();
		return;
	}

	// Recursively scan the cache directory.
	// TODO: Do we really want to store everything in a list? (Wastes memory.)
	// Maybe do a simple counting scan first, then delete.
	list<pair<string, uint8_t> > rlist;
	int ret = recursiveScan(cacheDir.c_str(), rlist);
	if (ret != 0) {
		// Non-image file found.
		QString qs_err;
		switch (m_cacheDir) {
			default:
				assert(!"Invalid cache directory specified.");
				qs_err = tr("Invalid cache directory specified.");
				break;
			case CacheCleaner::CD_System:
				qs_err = tr("System thumbnail cache has unexpected files. Not clearing it.");
				break;
			case CacheCleaner::CD_RomProperties:
				qs_err = tr("rom-properties cache has unexpected files. Not clearing it.");
				break;
		}
		emit progress(1, 1, true);
		emit error(qs_err);
		emit finished();
		return;
	} else if (rlist.empty()) {
		// Cache directory is empty.
		emit progress(1, 1, false);
		emit cacheIsEmpty(m_cacheDir);
		emit finished();
		return;
	}

	// Delete all of the files and subdirectories.
	emit progress(0, rlist.size(), false);
	unsigned int count = 0;
	unsigned int dirErrs = 0, fileErrs = 0;
	bool hasErrors = false;
	for (const auto &p : rlist) {
		if (p.second == DT_DIR) {
			// Remove the directory.
			int ret = rmdir(p.first.c_str());
			if (ret != 0) {
				dirErrs++;
				hasErrors = true;
			}
		} else {
			// Delete the file.
			// TODO: Does the parent directory mode need to be changed to writable?
			int ret = unlink(p.first.c_str());
			if (ret != 0) {
				fileErrs++;
				hasErrors = true;
			}
		}

		// TODO: Restrict update frequency to X number of files/directories?
		count++;
		emit progress(count, rlist.size(), hasErrors);
	}

	// Directory processed.
	emit cacheCleared(m_cacheDir, dirErrs, fileErrs);
	emit finished();
}
