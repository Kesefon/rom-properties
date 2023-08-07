/***************************************************************************
 * ROM Properties Page shell extension. (librpfile)                        *
 * XAttrReader_posix.cpp: Extended Attribute reader (POSIX version)        *
 *                                                                         *
 * Copyright (c) 2016-2023 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#include "stdafx.h"
#include "config.librpfile.h"
#include "XAttrReader.hpp"
#include "XAttrReader_p.hpp"

#include "librpcpu/byteswap_rp.h"

#include <fcntl.h>	// AT_FDCWD
#include <sys/stat.h>	// stat(), statx()
#include <sys/ioctl.h>
#include <unistd.h>

#if defined(HAVE_SYS_XATTR_H)
// NOTE: Mac fsetxattr() is the same as Linux but with an extra options parameter.
#  include <sys/xattr.h>
#elif defined(HAVE_SYS_EXTATTR_H)
#  include <sys/types.h>
#  include <sys/extattr.h>
#endif

// EXT2 flags (also used for EXT3, EXT4, and other Linux file systems)
#include "ext2_flags.h"

#ifdef __linux__
// for FS_IOC_GETFLAGS (equivalent to EXT2_IOC_GETFLAGS)
#  include <linux/fs.h>
// for FAT_IOCTL_GET_ATTRIBUTES
#  include <linux/msdos_fs.h>
#endif /* __linux__ */

// O_LARGEFILE is Linux-specific.
#ifndef O_LARGEFILE
#  define O_LARGEFILE 0
#endif /* !O_LARGEFILE */

// Uninitialized vector class.
// Reference: http://andreoffringa.org/?q=uvector
#include "uvector.h"

// C++ STL classes
using std::string;
using std::unique_ptr;

// XAttrReader isn't used by libromdata directly,
// so use some linker hax to force linkage.
extern "C" {
	extern uint8_t RP_LibRpFile_XAttrReader_impl_ForceLinkage;
	uint8_t RP_LibRpFile_XAttrReader_impl_ForceLinkage;
}

namespace LibRpFile {

/** XAttrReaderPrivate **/

XAttrReaderPrivate::XAttrReaderPrivate(const char *filename)
	: fd(-1)
	, lastError(0)
	, hasLinuxAttributes(false)
	, hasDosAttributes(false)
	, hasGenericXAttrs(false)
	, linuxAttributes(0)
	, dosAttributes(0)
{
	// Make sure this is a regular file or a directory.
	mode_t mode;

#ifdef HAVE_STATX
	struct statx sbx;
	int ret = statx(AT_FDCWD, filename, 0, STATX_TYPE, &sbx);
	if (ret != 0 || !(sbx.stx_mask & STATX_TYPE)) {
		// An error occurred.
		lastError = -errno;
		if (lastError == 0) {
			lastError = -ENOTSUP;
		}
		return;
	}
	mode = sbx.stx_mode;
#else /* !HAVE_STATX */
	struct stat sb;
	errno = 0;
	if (!stat(filename, &sb)) {
		// stat() failed.
		lastError = -errno;
		if (lastError == 0) {
			lastError = -ENOTSUP;
		}
		return;
	}
	mode = sb.st_mode;
#endif /* HAVE_STATX */

	if (!S_ISREG(mode) && !S_ISDIR(mode)) {
		// This is neither a regular file nor a directory.
		lastError = -ENOTSUP;
		return;
	}

	// Open the file to get attributes.
	// TODO: Move this to librpbase or libromdata,
	// and add configure checks for FAT_IOCTL_GET_ATTRIBUTES.
#define OPEN_FLAGS (O_RDONLY|O_NONBLOCK|O_LARGEFILE)
	errno = 0;
	fd = open(filename, OPEN_FLAGS);
	if (fd < 0) {
		// Error opening the file.
		lastError = -errno;
		if (lastError == 0) {
			lastError = -EIO;
		}
		return;
	}

	// Initialize attributes.
	lastError = init();
	close(fd);
}

/**
 * Initialize attributes.
 * Internal fd (filename on Windows) must be set.
 * @return 0 on success; negative POSIX error code on error.
 */
int XAttrReaderPrivate::init(void)
{
	// Verify the file type again using fstat().
	mode_t mode;

#ifdef HAVE_STATX
	struct statx sbx;
	int ret = statx(fd, "", AT_EMPTY_PATH, STATX_TYPE, &sbx);
	if (ret != 0 || !(sbx.stx_mask & STATX_TYPE)) {
		// An error occurred.
		int err = -errno;
		if (err == 0) {
			err = -EIO;
		}
		return err;
	}
	mode = sbx.stx_mode;
#else /* !HAVE_STATX */
	struct stat sb;
	errno = 0;
	if (!fstat(fd, &sb)) {
		// fstat() failed.
		int err = -errno;
		if (err == 0) {
			err = -EIO;
		}
		return err;
	}
	mode = sb.st_mode;
#endif /* HAVE_STATX */

	if (!S_ISREG(mode) && !S_ISDIR(mode)) {
		// This is neither a regular file nor a directory.
		return -ENOTSUP;
	}

	// Load the attributes.
	loadLinuxAttrs();
	loadDosAttrs();
	loadGenericXattrs();
	return 0;
}

/**
 * Load Linux attributes, if available.
 * Internal fd (filename on Windows) must be set.
 * @return 0 on success; negative POSIX error code on error.
 */
int XAttrReaderPrivate::loadLinuxAttrs(void)
{
	// Attempt to get EXT2 flags.

#ifdef __linux__
	// NOTE: The ioctl is defined as using long, but the actual
	// kernel code uses int.
	int ret;
	if (!ioctl(fd, FS_IOC_GETFLAGS, &linuxAttributes)) {
		// ioctl() succeeded. We have EXT2 flags.
		hasLinuxAttributes = true;
		ret = 0;
	} else {
		// No EXT2 flags on this file.
		// Assume this file system doesn't support them.
		ret = errno;
		if (ret == 0) {
			ret = -EIO;
		}

		linuxAttributes = 0;
		hasLinuxAttributes = false;
	}
	return ret;
#else /* !__linux__ */
	// Not supported.
	return -ENOTSUP;
#endif /* __linux__ */
}

/**
 * Load MS-DOS attributes, if available.
 * Internal fd (filename on Windows) must be set.
 * @return 0 on success; negative POSIX error code on error.
 */
int XAttrReaderPrivate::loadDosAttrs(void)
{
	// Attempt to get MS-DOS attributes.

#ifdef __linux__
	// ioctl (Linux vfat only)
	if (!ioctl(fd, FAT_IOCTL_GET_ATTRIBUTES, &dosAttributes)) {
		// ioctl() succeeded. We have MS-DOS attributes.
		hasDosAttributes = true;
		return 0;
	}

	// Try system xattrs:
	// ntfs3 has: system.dos_attrib, system.ntfs_attrib
	// ntfs-3g has: system.ntfs_attrib, system.ntfs_attrib_be
	// Attribute is stored as a 32-bit DWORD.
	union {
		uint8_t u8[16];
		uint32_t u32;
	} buf;

	struct DosAttrName {
		const char name[23];
		bool be32;
	};
	static const DosAttrName dosAttrNames[] = {
		{"system.ntfs_attrib_be", true},
		{"system.ntfs_attrib", false},
		{"system.dos_attrib", false},
	};
	for (const auto &p : dosAttrNames) {
		ssize_t sz = fgetxattr(fd, p.name, buf.u8, sizeof(buf.u8));
		if (sz == 4) {
			dosAttributes = (p.be32) ? be32_to_cpu(buf.u32) : le32_to_cpu(buf.u32);
			hasDosAttributes = true;
			return 0;
		}
	}

	// No valid attributes found.
	return -ENOENT;
#else /* !__linux__ */
	// Not supported.
	return -ENOTSUP;
#endif /* __linux__ */
}

/**
 * Load generic xattrs, if available.
 * (POSIX xattr on Linux; ADS on Windows)
 * Internal fd (filename on Windows) must be set.
 * @return 0 on success; negative POSIX error code on error.
 */
int XAttrReaderPrivate::loadGenericXattrs(void)
{
	genericXAttrs.clear();

#ifdef HAVE_SYS_EXTATTR_H
	// TODO: XAttrReader_freebsd.cpp ran this twice: once for user and once for system.
	// TODO: Read system namespace attributes.
	const int attrnamespace = EXTATTR_NAMESPACE_USER;

	// Namespace prefix
	const char *s_namespace;
	switch (attrnamespace) {
		case EXTATTR_NAMESPACE_SYSTEM:
			s_namespace = "system: ";
			break;
		case EXTATTR_NAMESPACE_USER:
			s_namespace = "user: ";
			break;
		default:
			assert(!"Invalid attribute namespace.");
			s_namespace = "invalid: ";
			break;
	}
#endif /* HAVE_SYS_EXTATTR_H */

	// Partially based on KIO's FileProtocol::copyXattrs().
	// Reference: https://invent.kde.org/frameworks/kio/-/blob/584a81fd453858db432a573c011a1433bc6947e1/src/kioworkers/file/file_unix.cpp#L521
	ssize_t listlen = 0;
	ao::uvector<char> keylist;
	keylist.reserve(256);
	while (true) {
		keylist.resize(listlen);
#if defined(HAVE_SYS_XATTR_H) && !defined(__stub_getxattr) && !defined(__APPLE__)
		listlen = flistxattr(fd, keylist.data(), listlen);
#elif defined(__APPLE__)
		listlen = flistxattr(fd, keylist.data(), listlen, 0);
#elif defined(HAVE_SYS_EXTATTR_H)
		listlen = extattr_list_fd(fd, attrnamespace, listlen == 0 ? nullptr : keylist.data(), listlen);
#endif

		if (listlen > 0 && keylist.size() == 0) {
			continue;
		} else if (listlen > 0 && keylist.size() > 0) {
			break;
		} else if (listlen == -1 && errno == ERANGE) {
			// Not sure when this case would be hit...
			listlen = 0;
			continue;
		} else if (listlen == 0) {
			// No extended attributes.
			hasGenericXAttrs = true;
			return 0;
		} else if (listlen == -1 && errno == ENOTSUP) {
			// Filesystem does not support extended attributes.
			return -ENOTSUP;
		}
	}

	if (listlen == 0 || keylist.empty()) {
		// Shouldn't be empty...
		return -EIO;
	}
	keylist.resize(listlen);

#ifdef HAVE_SYS_XATTR_H
	// Linux, macOS: List should end with a NULL terminator.
	if (keylist[keylist.size()-1] != '\0') {
		// Not NULL-terminated...
		return -EIO;
	}
#endif /* HAVE_SYS_XATTR_H */

	// Value buffer
	ao::uvector<char> value;
	value.reserve(256);

	// Linux, macOS: List contains NULL-terminated strings.
	// FreeBSD: List contains counted (but not NULL-terminated) strings.
	// Process strings until we reach list_buf + list_size.
	const char *const keylist_end = keylist.data() + keylist.size();
	const char *p = keylist.data();
	while (p < keylist_end) {
#if defined(HAVE_SYS_XATTR_H)
		const char *name = p;
		if (name[0] == '\0') {
			// Empty name. Assume we're at the end of the list.
			break;
		}

		// NOTE: If p == keylist_end here, then we're at the last attribute.
		// Only fail if p > keylist_end, because that indicates an overflow.
		p += strlen(name) + 1;
		if (p > keylist_end)
			break;
#elif defined(HAVE_SYS_EXTATTR_H)
		uint8_t len = *p;

		// NOTE: If p + 1 + len == keylist_end here, then we're at the last attribute.
		// Only fail if p + 1 + len > keylist_end, because that indicates an overflow.
			if (p + 1 + len > keylist_end) {
			// Out of bounds.
			break;
		}

		string name(p+1, len);
		p += 1 + len;
#endif

		ssize_t valuelen = 0;
		do {
			// Get the value for this attribute.
			// NOTE: valuelen does *not* include a NULL-terminator.
			value.resize(valuelen);
#if defined(HAVE_SYS_XATTR_H) && !defined(__stub_getxattr) && !defined(__APPLE__)
			valuelen = fgetxattr(fd, name, value.data(), valuelen);
#elif defined(__APPLE__)
			valuelen = fgetxattr(fd, name, nullptr, value.data(), 0);
#elif defined(HAVE_SYS_EXTATTR_H)
			valuelen = extattr_get_fd(fd, attrnamespace, name.c_str(), nullptr, 0);
#endif

			if (valuelen > 0 && value.size() == 0) {
				continue;
			} else if (valuelen > 0 && value.size() > 0) {
				break;
			} else if (valuelen == -1 && errno == ERANGE) {
				valuelen = 0;
				continue;
			} else if (valuelen == 0) {
				// attr value is an empty string
				break;
			}
		} while (true);

		if (valuelen < 0) {
			// Not valid. Go to the next attribute.
			continue;
		}

		// We have the attribute.
		// NOTE: Not checking for duplicates, since there
		// shouldn't be duplicate attribute names.
		string s_value(value.data(), valuelen);
#if HAVE_SYS_EXTATTR_H
		string s_name_prefixed(s_namespace);
		s_name_prefixed.append(name);
		genericXAttrs.emplace(std::move(s_name_prefixed), std::move(s_value));
#else /* !HAVE_SYS_EXTATTR_H */
		genericXAttrs.emplace(name, std::move(s_value));
#endif /* HAVE_SYS_EXTATTR_H */
	}

	// Extended attributes retrieved.
	hasGenericXAttrs = true;
	return 0;
}

}