/*
Copyright (C) 2013 Victor Luchits

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
#ifndef _HASH_H
#define _HASH_H

#include "../gameshared/q_arch.h"

#include <utility>

/**
 * An utility to get a case-insensitive hash code of a string along with its length
 * @param s a string that is supposed to be zero-terminated
 * @return a pair of a hash code and a length of a supplied string
 * @note Use with caution for performance-sensitive code.
 * Case insensitivity is only guaranteed for the ASCII character set.
 */
std::pair<uint32_t, size_t> GetHashAndLength( const char *s );

/**
 * An utility to get a case-insensitive hash code of a string part specified by its length.
 * This is a "dual" (to some degree) version of {@code GetHashAndLength()}
 * @param s an address of a string part
 * @param length a length of a given string part
 * @return a hash code of the given string part.
 * @note Use with caution for a performance-sensitive code.
 * Case insensitivity is only guaranteed for the ASCII character set.
 */
uint32_t GetHashForLength( const char *s, size_t length );

unsigned int COM_SuperFastHash( const unsigned char * data, size_t len, unsigned int hash );
unsigned int COM_SuperFastHash64BitInt( uint64_t data );

#endif
