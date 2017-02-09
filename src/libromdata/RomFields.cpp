/***************************************************************************
 * ROM Properties Page shell extension. (libromdata)                       *
 * RomFields.cpp: ROM fields class.                                        *
 *                                                                         *
 * Copyright (c) 2016 by David Korth.                                      *
 *                                                                         *
 * This program is free software; you can redistribute it and/or modify it *
 * under the terms of the GNU General Public License as published by the   *
 * Free Software Foundation; either version 2 of the License, or (at your  *
 * option) any later version.                                              *
 *                                                                         *
 * This program is distributed in the hope that it will be useful, but     *
 * WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 * GNU General Public License for more details.                            *
 *                                                                         *
 * You should have received a copy of the GNU General Public License along *
 * with this program; if not, write to the Free Software Foundation, Inc., *
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.           *
 ***************************************************************************/

#include "RomFields.hpp"

#include "common.h"
#include "TextFuncs.hpp"

// C includes.
#include <stdlib.h>

// C includes. (C++ namespace)
#include <cassert>
#include <cstdio>
#include <cstring>

// C++ includes.
#include <array>
#include <sstream>
#include <string>
#include <vector>
using std::array;
using std::ostringstream;
using std::string;
using std::vector;

namespace LibRomData {

class RomFieldsPrivate
{
	public:
		RomFieldsPrivate();
		RomFieldsPrivate(const RomFields::Desc *desc, int count);
	private:
		~RomFieldsPrivate();	// call unref() instead

	private:
		RP_DISABLE_COPY(RomFieldsPrivate)

	public:
		/** Reference count functions. **/

		/**
		 * Create a reference of this object.
		 * @return this
		 */
		RomFieldsPrivate *ref(void);

		/**
		 * Unreference this object.
		 */
		void unref(void);

		/**
		 * Is this object currently shared?
		 * @return True if refCount > 1; false if not.
		 */
		inline bool isShared(void) const;

	private:
		// Current reference count.
		int refCount;

	public:
		// ROM field structs.
		vector<RomFields::Field> fields;

		// Data counter for the old-style addField*() functions.
		int dataCount;

		/**
		 * Deletes allocated strings in this->data.
		 */
		void delete_data(void);
};

/** RomFieldsPrivate **/

RomFieldsPrivate::RomFieldsPrivate()
	: refCount(1)
	, dataCount(0)
{ }

// DEPRECATED: Conversion of old-style desc to new fields.
RomFieldsPrivate::RomFieldsPrivate(const RomFields::Desc *desc, int count)
	: refCount(1)
	, dataCount(0)
{
	// Initialize fields.
	fields.resize(count);
	for (int i = 0; i < count; i++, desc++) {
		RomFields::Field &field = fields.at(i);
		field.name = desc->name;
		field.type = desc->type;
		field.isValid = false;

		switch (field.type) {
			case RomFields::RFT_STRING: {
				if (!desc->str_desc) {
					field.desc.flags = 0;
				} else {
					field.desc.flags = desc->str_desc->formatting;
				}
				break;
			}

			case RomFields::RFT_BITFIELD: {
				assert(desc->bitfield != nullptr);
				if (!desc->bitfield)
					break;
				field.desc.bitfield.elements = desc->bitfield->elements;
				field.desc.bitfield.elemsPerRow = desc->bitfield->elemsPerRow;

				// Copy the flag names.
				vector<rp_string> *names = new vector<rp_string>();
				const int count = desc->bitfield->elements;
				names->reserve(count);
				for (int i = 0; i < count; i++) {
					const rp_char *const name_src = desc->bitfield->names[i];
					names->push_back(name_src ? rp_string(name_src) : rp_string());
				}
				field.desc.bitfield.names = names;
				break;
			}

			case RomFields::RFT_LISTDATA: {
				assert(desc->list_data != nullptr);
				if (!desc->list_data)
					break;

				// Copy the list field names.
				vector<rp_string> *names = new vector<rp_string>();
				const int count = desc->list_data->count;
				names->reserve(count);
				for (int i = 0; i < count; i++) {
					const rp_char *const name_src = desc->list_data->names[i];
					names->push_back(name_src ? rp_string(name_src) : rp_string());
				}
				field.desc.list_data.names = names;
				break;
			}

			case RomFields::RFT_DATETIME: {
				if (!desc->date_time) {
					field.desc.flags = 0;
				} else {
					field.desc.flags = desc->date_time->flags;
				}
				break;
			}

			case RomFields::RFT_AGE_RATINGS: {
				// No formatting for age ratings.
				break;
			}

			default:
				assert(!"Unsupported RomFields::RomFieldsType.");
				break;
		}
	}
}

RomFieldsPrivate::~RomFieldsPrivate()
{
	delete_data();
}

/**
 * Create a reference of this object.
 * @return this
 */
RomFieldsPrivate *RomFieldsPrivate::ref(void)
{
	refCount++;
	return this;
}

/**
 * Unreference this object.
 */
void RomFieldsPrivate::unref(void)
{
	assert(refCount > 0);
	if (--refCount == 0) {
		// All references deleted.
		delete this;
	}
}

/**
 * Is this object currently shared?
 * @return True if refCount > 1; false if not.
 */
inline bool RomFieldsPrivate::isShared(void) const
{
	assert(refCount > 0);
	return (refCount > 1);
}

/**
 * Deletes allocated strings in this->data.
 */
void RomFieldsPrivate::delete_data(void)
{
	// Delete all of the allocated strings in this->fields.
	for (int i = (int)(fields.size() - 1); i >= 0; i--) {
		RomFields::Field &field = this->fields.at(i);
		if (!field.isValid) {
			// No data here.
			continue;
		}

		switch (field.type) {
			case RomFields::RFT_INVALID:
			case RomFields::RFT_DATETIME:
				// No data here.
				break;

			case RomFields::RFT_STRING:
				delete const_cast<rp_string*>(field.data.str);
				break;
			case RomFields::RFT_BITFIELD:
				delete const_cast<vector<rp_string>*>(field.desc.bitfield.names);
				break;
			case RomFields::RFT_LISTDATA:
				delete const_cast<vector<rp_string>*>(field.desc.list_data.names);
				delete const_cast<vector<vector<rp_string> >*>(field.data.list_data);
				break;
			case RomFields::RFT_AGE_RATINGS:
				delete const_cast<array<uint16_t, 16>*>(field.data.age_ratings);
				break;
			default:
				// ERROR!
				assert(!"Unsupported RomFields::RomFieldsType.");
				break;
		}
	}

	// Clear the fields vector.
	this->fields.clear();
}

/** RomFields **/

/**
 * Initialize a ROM Fields class.
 * @param fields Array of fields.
 * @param count Number of fields.
 */
RomFields::RomFields(const Desc *fields, int count)
	: d(new RomFieldsPrivate(fields, count))
{ }

RomFields::~RomFields()
{
	d->unref();
}

/**
 * Copy constructor.
 * @param other Other instance.
 */
RomFields::RomFields(const RomFields &other)
	: d(other.d->ref())
{ }

/**
 * Assignment operator.
 * @param other Other instance.
 * @return This instance.
 */
RomFields &RomFields::operator=(const RomFields &other)
{
	RomFieldsPrivate *d_old = this->d;
	this->d = other.d->ref();
	d_old->unref();
	return *this;
}

/**
 * Detach this instance from all other instances.
 * TODO: Move to RomFieldsPrivate?
 */
void RomFields::detach(void)
{
	if (!d->isShared()) {
		// Only one reference.
		// Nothing to detach from.
		return;
	}

	// Need to detach.
	RomFieldsPrivate *d_new = new RomFieldsPrivate();
	RomFieldsPrivate *d_old = d;
	d_new->fields.resize(d_old->fields.size());
	for (int i = (int)(d_old->fields.size() - 1); i >= 0; i--) {
		const Field &field_old = d_old->fields.at(i);
		Field &field_new = d_new->fields.at(i);
		field_new.name = field_old.name;
		field_new.type = field_old.type;
		field_new.isValid = field_old.isValid;
		if (!field_old.isValid) {
			// No data here.
			field_new.desc.flags = 0;
			field_new.data.generic = 0;
			continue;
		}

		switch (field_old.type) {
			case RFT_INVALID:
				// No data here
				field_new.isValid = false;
				field_new.desc.flags = 0;
				field_new.data.generic = 0;
				break;
			case RFT_STRING:
				field_new.desc.flags = field_old.desc.flags;
				if (field_old.data.str) {
					field_new.data.str = new rp_string(*field_old.data.str);
				} else {
					field_new.data.str = nullptr;
				}
				break;
			case RFT_BITFIELD:
				field_new.desc.bitfield.elements = field_old.desc.bitfield.elements;
				field_new.desc.bitfield.elemsPerRow = field_old.desc.bitfield.elemsPerRow;
				if (field_old.desc.bitfield.names) {
					field_new.desc.bitfield.names = new vector<rp_string>(*field_old.desc.bitfield.names);
				} else {
					field_new.desc.bitfield.names = nullptr;
				}
				field_new.data.bitfield = field_old.data.bitfield;
				break;
			case RFT_LISTDATA:
				// Copy the ListData.
				if (field_old.desc.list_data.names) {
					field_new.desc.list_data.names = new vector<rp_string>(*field_old.desc.list_data.names);
				} else {
					field_new.desc.list_data.names = nullptr;
				}
				if (field_old.data.list_data) {
					field_new.data.list_data = new vector<vector<rp_string> >(*field_old.data.list_data);
				} else {
					field_new.data.list_data = nullptr;
				}
				break;
			case RFT_DATETIME:
				field_new.desc.flags = field_old.desc.flags;
				field_new.data.date_time = field_old.data.date_time;
				break;
			case RFT_AGE_RATINGS:
				field_new.data.age_ratings = field_old.data.age_ratings;
				break;
			default:
				// ERROR!
				assert(!"Unsupported RomFields::RomFieldsType.");
				break;
		}
	}

	// Detached.
	d = d_new;
	d_old->unref();
}

/**
 * Get the abbreviation of an age rating organization.
 * (TODO: Full name function?)
 * @param country Rating country. (See AgeRatingCountry.)
 * @return Abbreviation (in ASCII), or nullptr if invalid.
 */
const char *RomFields::ageRatingAbbrev(int country)
{
	static const char abbrevs[16][8] = {
		"CERO", "ESRB", "",        "USK",
		"PEGI", "MEKU", "PEGI-PT", "BBFC",
		"AGCB", "GRB",  "CGSRR",   "",
		"", "", "", ""
	};

	assert(country >= 0 && country < ARRAY_SIZE(abbrevs));
	if (country < 0 || country >= ARRAY_SIZE(abbrevs)) {
		// Index is out of range.
		return nullptr;
	}

	const char *ret = abbrevs[country];
	if (ret[0] == 0) {
		// Empty string. Return nullptr instead.
		ret = nullptr;
	}
	return ret;
}

/**
 * Decode an age rating into a human-readable string.
 * This does not include the name of the rating organization.
 *
 * NOTE: The returned string is in UTF-8 in order to
 * be able to use special characters.
 *
 * @param country Rating country. (See AgeRatingsCountry.)
 * @param rating Rating value.
 * @return Human-readable string, or empty string if the rating isn't active.
 */
string RomFields::ageRatingDecode(int country, uint16_t rating)
{
	if (!(rating & AGEBF_ACTIVE)) {
		// Rating isn't active.
		return string();
	}

	ostringstream oss;

	// Check for special statuses.
	if (rating & RomFields::AGEBF_PROHIBITED) {
		// Prohibited.
		// TODO: Better description?
		oss << "No";
	} else if (rating & RomFields::AGEBF_PENDING) {
		// Rating is pending.
		oss << "RP";
	} else if (rating & RomFields::AGEBF_NO_RESTRICTION) {
		// No age restriction.
		oss << "All";
	} else {
		// Use the age rating.
		// TODO: Verify these.
		switch (country) {
			case AGE_JAPAN:
				switch (rating & RomFields::AGEBF_MIN_AGE_MASK) {
					case 0:
						oss << "A";
						break;
					case 12:
						oss << "B";
						break;
					case 15:
						oss << "C";
						break;
					case 17:
						oss << "D";
						break;
					case 18:
						oss << "Z";
						break;
					default:
						// Unknown rating. Show the numeric value.
						oss << (rating & AGEBF_MIN_AGE_MASK);
						break;
				}
				break;

			case AGE_USA:
				switch (rating & RomFields::AGEBF_MIN_AGE_MASK) {
					case 3:
						oss << "eC";
						break;
					case 6:
						oss << "E";
						break;
					case 10:
						oss << "E10+";
						break;
					case 13:
						oss << "T";
						break;
					case 17:
						oss << "M";
						break;
					case 18:
						oss << "AO";
						break;
					default:
						// Unknown rating. Show the numeric value.
						oss << (rating & AGEBF_MIN_AGE_MASK);
						break;
				}
				break;

			case AGE_AUSTRALIA:
				switch (rating & RomFields::AGEBF_MIN_AGE_MASK) {
					case 0:
						oss << "G";
						break;
					case 7:
						oss << "PG";
						break;
					case 14:
						oss << "M";
						break;
					case 15:
						oss << "MA15+";
						break;
					case 18:
						oss << "R18+";
						break;
					default:
						// Unknown rating. Show the numeric value.
						oss << (rating & AGEBF_MIN_AGE_MASK);
						break;
				}
				break;

			default:
				// No special handling for this country.
				// Show the numeric value.
				oss << (rating & AGEBF_MIN_AGE_MASK);
				break;
		}
	}

	if (rating & RomFields::AGEBF_ONLINE_PLAY) {
		// Rating may change during online play.
		// TODO: Add a description of this somewhere.
		// NOTE: Unicode U+00B0, encoded as UTF-8.
		oss << "\xC2\xB0";
	}

	return oss.str();
}

/** Field accessors. **/

/**
 * Get the number of fields.
 * @return Number of fields.
 */
int RomFields::count(void) const
{
	return (int)d->fields.size();
}

/**
 * Get a ROM field.
 * @param idx Field index.
 * @return ROM field, or nullptr if the index is invalid.
 */
const RomFields::Field *RomFields::field(int idx) const
{
	if (idx < 0 || idx >= (int)d->fields.size())
		return nullptr;
	return &d->fields[idx];
}

/**
 * Is data loaded?
 * TODO: Rename to empty() after porting to the new addField() functions.
 * @return True if m_data has at least one row; false if m_data is nullptr or empty.
 */
bool RomFields::isDataLoaded(void) const
{
	return (d->dataCount > 0);
}

/** Convenience functions for RomData subclasses. **/

/**
 * Add invalid field data.
 * This effectively hides the field.
 * @return Field index, or -1 on error.
 */
int RomFields::addData_invalid(void)
{
	assert(d->dataCount < d->fields.size());
	if (d->dataCount >= d->fields.size())
		return -1;

	Field &field = d->fields.at(d->dataCount);
	field.isValid = false;
	field.data.generic = 0;
	return d->dataCount++;
}

/**
 * Add string field data.
 * @param str String.
 * @return Field index, or -1 on error.
 */
int RomFields::addData_string(const rp_char *str)
{
	assert(d->dataCount < d->fields.size());
	if (d->dataCount >= d->fields.size())
		return -1;

	// RFT_STRING
	Field &field = d->fields.at(d->dataCount);
	assert(field.type == RFT_STRING);
	// TODO: Null string == empty string?
	if (field.type != RFT_STRING || !str) {
		field.isValid = false;
		field.data.generic = 0;
	} else {
		field.data.str = new rp_string(str);
		field.isValid = true;
	}
	return d->dataCount++;
}

/**
 * Add string field data.
 * @param str String.
 * @return Field index, or -1 on error.
 */
int RomFields::addData_string(const rp_string &str)
{
	assert(d->dataCount < d->fields.size());
	if (d->dataCount >= d->fields.size())
		return -1;

	// RFT_STRING
	Field &field = d->fields.at(d->dataCount);
	assert(field.type == RFT_STRING);
	if (field.type != RFT_STRING) {
		field.isValid = false;
		field.data.generic = 0;
	} else {
		field.data.str = new rp_string(str);
		field.isValid = true;
	}
	return d->dataCount++;
}

/**
 * Add a string field using a numeric value.
 * @param val Numeric value.
 * @param base Base. If not decimal, a prefix will be added.
 * @param digits Number of leading digits. (0 for none)
 * @return Field index, or -1 on error.
 */
int RomFields::addData_string_numeric(uint32_t val, Base base, int digits)
{
	char buf[32];
	int len;
	switch (base) {
		case FB_DEC:
		default:
			len = snprintf(buf, sizeof(buf), "%0*u", digits, val);
			break;
		case FB_HEX:
			len = snprintf(buf, sizeof(buf), "0x%0*X", digits, val);
			break;
		case FB_OCT:
			len = snprintf(buf, sizeof(buf), "0%0*o", digits, val);
			break;
	}

	if (len > (int)sizeof(buf))
		len = sizeof(buf);

	rp_string str = (len > 0 ? latin1_to_rp_string(buf, len) : _RP(""));
	return addData_string(str);
}

/**
 * Add a string field formatted like a hex dump
 * @param buf Input bytes.
 * @param size Byte count.
 * @return Field index, or -1 on error.
 */
int RomFields::addData_string_hexdump(const uint8_t *buf, size_t size)
{
	if (size == 0) {
		return addData_string(_RP(""));
	}

	// Reserve 3 characters per byte.
	// (Two hex digits, plus one space.)
	rp_string rps;
	rps.resize(size * 3);
	size_t rps_pos = 0;

	// Temporary snprintf buffer.
	char hexbuf[8];
	for (; size > 0; size--, buf++, rps_pos += 3) {
		snprintf(hexbuf, sizeof(hexbuf), "%02X ", *buf);
		rps[rps_pos+0] = hexbuf[0];
		rps[rps_pos+1] = hexbuf[1];
		rps[rps_pos+2] = hexbuf[2];
	}

	// Remove the trailing space.
	if (rps.size() > 0) {
		rps.resize(rps.size() - 1);
	}

	return addData_string(rps);
}

/**
 * Add a string field formatted for an address range.
 * @param start Start address.
 * @param end End address.
 * @param suffix Suffix string.
 * @param digits Number of leading digits. (default is 8 for 32-bit)
 * @return Field index, or -1 on error.
 */
int RomFields::addData_string_address_range(uint32_t start, uint32_t end, const rp_char *suffix, int digits)
{
	// Maximum number of digits is 16. (64-bit)
	assert(digits <= 16);
	if (digits > 16) {
		digits = 16;
	}

	// ROM range.
	// TODO: Range helper? (Can't be used for SRAM, though...)
	char buf[64];
	int len = snprintf(buf, sizeof(buf), "0x%0*X - 0x%0*X",
			digits, start, digits, end);
	if (len > (int)sizeof(buf))
		len = sizeof(buf);

	rp_string str;
	if (len > 0) {
		str = latin1_to_rp_string(buf, len);
	}

	if (suffix && suffix[0] != 0) {
		// Append a space and the specified suffix.
		str += _RP_CHR(' ');
		str += suffix;
	}

	return addData_string(str);
}

/**
 * Add a bitfield.
 * @param bitfield Bitfield.
 * @return Field index, or -1 on error.
 */
int RomFields::addData_bitfield(uint32_t bitfield)
{
	assert(d->dataCount < d->fields.size());
	if (d->dataCount >= d->fields.size())
		return -1;

	// RFT_BITFIELD
	Field &field = d->fields.at(d->dataCount);
	assert(field.type == RFT_BITFIELD);
	if (field.type != RFT_BITFIELD) {
		field.isValid = false;
		field.data.generic = 0;
	} else {
		field.data.bitfield = bitfield;
		field.isValid = true;
	}
	return d->dataCount++;
}

/**
 * Add ListData.
 * @param list_data ListData. (must be allocated with new)
 * @return Field index, or -1 on error.
 */
int RomFields::addData_listData(ListData *list_data)
{
	assert(d->dataCount < d->fields.size());
	if (d->dataCount >= d->fields.size())
		return -1;

	// RFT_LISTDATA
	Field &field = d->fields.at(d->dataCount);
	assert(field.type == RFT_LISTDATA);
	if (field.type != RFT_LISTDATA || !list_data) {
		field.isValid = false;
		field.data.generic = 0;
	} else {
		field.data.list_data = new vector<vector<rp_string> >(list_data->data);
		field.isValid = true;
	}
	return d->dataCount++;
}

/**
 * Add DateTime.
 * @param date_time Date/Time.
 * @return Field index, or -1 on error.
 */
int RomFields::addData_dateTime(int64_t date_time)
{
	assert(d->dataCount < d->fields.size());
	if (d->dataCount >= d->fields.size())
		return -1;

	// RFT_DATETIME
	Field &field = d->fields.at(d->dataCount);
	assert(field.type == RFT_BITFIELD);
	if (field.type != RFT_BITFIELD) {
		field.isValid = false;
		field.data.generic = 0;
	} else {
		field.data.date_time = date_time;
		field.isValid = true;
	}
	return d->dataCount++;
}

/**
 * Add age ratings.
 * @param age_ratings Age ratings array. (uint16_t[16])
 * @return Field index, or -1 on error.
 */
int RomFields::addData_ageRatings(uint16_t age_ratings[AGE_MAX])
{
	assert(d->dataCount < d->fields.size());
	if (d->dataCount >= d->fields.size())
		return -1;

	// RFT_AGE_RATINGS
	Field &field = d->fields.at(d->dataCount);
	assert(field.type == RFT_AGE_RATINGS);
	if (field.type != RFT_AGE_RATINGS) {
		field.isValid = false;
		field.data.generic = 0;
	} else {
		array<uint16_t, 16> *tmp = new array<uint16_t, 16>();
		memcpy(tmp->data(), age_ratings, sizeof(uint16_t)*AGE_MAX);
		field.data.age_ratings = tmp;
		field.isValid = true;
	}
	return d->dataCount++;
}

}
