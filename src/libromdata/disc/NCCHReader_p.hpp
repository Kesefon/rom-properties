/***************************************************************************
 * ROM Properties Page shell extension. (libromdata)                       *
 * NCCHReader_p.hpp: Nintendo 3DS NCCH reader.                             *
 * Private class declaration.                                              *
 *                                                                         *
 * Copyright (c) 2016-2020 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#ifndef __ROMPROPERTIES_LIBROMDATA_DISC_NCCHREADER_P_HPP__
#define __ROMPROPERTIES_LIBROMDATA_DISC_NCCHREADER_P_HPP__

#include "librpbase/config.librpbase.h"
#include "NCCHReader.hpp"

// librpbase, librpcpu
#include "librpcpu/byteswap.h"
#include "librpbase/crypto/KeyManager.hpp"
#ifdef ENABLE_DECRYPTION
#include "../crypto/CtrKeyScrambler.hpp"
#include "../crypto/N3DSVerifyKeys.hpp"
#endif /* ENABLE_DECRYPTION */

// C includes.
#include <stdint.h>

// C++ includes.
#include <vector>

#ifdef ENABLE_DECRYPTION
namespace LibRpBase {
	class IAesCipher;
}
#endif /* ENABLE_DECRYPTION */

namespace LibRomData {

class NCCHReaderPrivate
{
	public:
		NCCHReaderPrivate(NCCHReader *q, uint8_t media_unit_shift,
			off64_t ncch_offset, uint32_t ncch_length);
		~NCCHReaderPrivate();

	private:
		RP_DISABLE_COPY(NCCHReaderPrivate)
	protected:
		NCCHReader *const q_ptr;

	public:
		// NCCH offsets.
		const off64_t ncch_offset;	// NCCH start offset, in bytes.
		const uint32_t ncch_length;	// NCCH length, in bytes.
		const uint8_t media_unit_shift;

		// Current read position within the NCCH.
		// pos = 0 indicates the beginning of the NCCH header.
		// NOTE: This cannot be more than 4 GB,
		// so we're using uint32_t.
		uint32_t pos;

		// Loaded headers.
		enum HeadersPresent {
			HEADER_NONE	= 0,
			HEADER_NCCH	= (1U << 0),
			HEADER_EXHEADER	= (1U << 1),
			HEADER_EXEFS	= (1U << 2),
		};
		uint32_t headers_loaded;	// HeadersPresent

		// NCCH header.
		N3DS_NCCH_Header_t ncch_header;
		// NCCH ExHeader.
		N3DS_NCCH_ExHeader_t ncch_exheader;
		// ExeFS header.
		N3DS_ExeFS_Header_t exefs_header;

		// Encryption key verification result.
		LibRpBase::KeyManager::VerifyResult verifyResult;

		// Non-NCCH content type.
		// We won't extract any information from them,
		// other than the type and the fact that they're
		// not encrypted.
		enum NonNCCHContentType {
			NONCCH_UNKNOWN	= 0,
			NONCCH_NDHT,
			NONCCH_NARC,
		};
		NonNCCHContentType nonNcchContentType;

		/**
		 * Read data from the underlying ROM image.
		 * CIA decryption is automatically handled if set up properly.
		 *
		 * NOTE: Offset and size must both be multiples of 16.
		 *
		 * @param offset	[in] Starting address, relative to the beginning of the NCCH.
		 * @param ptr		[out] Output buffer.
		 * @param size		[in] Amount of data to read.
		 * @return Number of bytes read, or 0 on error.
		 */
		size_t readFromROM(uint32_t offset, void *ptr, size_t size);

		/**
		 * Load the NCCH Extended Header.
		 * @return 0 on success; non-zero on error.
		 */
		int loadExHeader(void);

#ifdef ENABLE_DECRYPTION
		// Title ID. Used for AES-CTR initialization.
		// (Big-endian format)
		uint64_t tid_be;

		// Encryption keys.
		u128_t ncch_keys[2];

		// NCCH cipher.
		LibRpBase::IAesCipher *cipher;

		// Encrypted section addresses.
		struct EncSection {
			uint32_t address;	// Relative to ncch_offset.
			uint32_t ctr_base;	// Base address for the AES-CTR counter.
			uint32_t length;
			uint8_t keyIdx;		// ncch_keys[] index
			uint8_t section;	// N3DS_NCCH_Sections

			EncSection(uint32_t address, uint32_t ctr_base,
				uint32_t length, uint8_t keyIdx, uint8_t section)
				: address(address)
				, ctr_base(ctr_base)
				, length(length)
				, keyIdx(keyIdx)
				, section(section)
				{ }

			/**
			 * Comparison operator for std::sort().
			 */
			inline bool operator<(const EncSection& other) const
			{
				return (other.address < this->address);
			}
		};
		std::vector<EncSection> encSections;

		/**
		 * Find the encrypted section containing a given address.
		 * @param address Address.
		 * @return Index in encSections, or -1 if not encrypted.
		 */
		int findEncSection(uint32_t address) const;

		// TMD content index.
		uint16_t tmd_content_index;

		// Are we using debug keys?
		bool isDebug;

		/**
		 * Close the file and/or IDiscReader.
		 */
		inline void closeFileOrDiscReader(void)
		{
			RP_Q(NCCHReader);
			if (q->m_hasDiscReader) {
				// Delete the IDiscReader, since it's
				// most likely a temporary CIAReader.
				// TODO: Use reference counting?
				delete q->m_discReader;
				q->m_discReader = nullptr;
			} else if (q->m_file) {
				q->m_file->unref();
				q->m_file = nullptr;
			}
		}
#endif /* ENABLE_DECRYPTION */
};

}

#endif /* __ROMPROPERTIES_LIBROMDATA_DISC_NCCHREADER_P_HPP__ */
