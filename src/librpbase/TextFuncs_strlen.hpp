/***************************************************************************
 * ROM Properties Page shell extension. (librpbase)                        *
 * TextFuncs_strlen.hpp: UTF-8 strlen() functions.                         *
 *                                                                         *
 * Copyright (c) 2022 by David Korth.                                      *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#ifndef __ROMPROPERTIES_LIBRPBASE_TEXTFUNCS_STRLEN_HPP__
#define __ROMPROPERTIES_LIBRPBASE_TEXTFUNCS_STRLEN_HPP__

// C includes (C++ namespace)
#include <cstddef>

// C++ includes
#include <string>

namespace LibRpBase {

/**
 * Determine the display length of a UTF-8 string.
 * This is used for monospaced console/text output only.
 * NOTE: Assuming the string is valid UTF-8.
 * @param str UTF-8 string
 * @param max_len Maximum length to check
 * @return Display length
 */
size_t utf8_disp_strlen(const char *str, size_t max_len = std::string::npos);

/**
 * Determine the display length of a UTF-8 string.
 * This is used for monospaced console/text output only.
 * NOTE: Assuming the string is valid UTF-8.
 * @param str UTF-8 string
 * @param max_len Maximum length to check
 * @return Display length
 */
static inline size_t utf8_disp_strlen(const std::string &str, size_t max_len = std::string::npos)
{
	return utf8_disp_strlen(str.c_str(), max_len);
}

}

#endif /* __ROMPROPERTIES_LIBRPBASE_TEXTFUNCS_STRLEN_HPP__ */