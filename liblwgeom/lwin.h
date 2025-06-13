/**********************************************************************
 *
 * PostGIS - Spatial Types for PostgreSQL
 * http://postgis.net
 *
 * PostGIS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * PostGIS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PostGIS.  If not, see <http://www.gnu.org/licenses/>.
 *
 **********************************************************************
 *
 * Copyright (C) 2025 PostGIS contributors
 *
 **********************************************************************/

#ifndef _LWIN_H
#define _LWIN_H 1

/** Max depth in a geometry. Matches the default YYINITDEPTH for WKT */
#define LW_PARSER_MAX_DEPTH 200

/**
* Used for passing the parse state between the parsing functions.
*/
typedef struct
{
	const uint8_t *wkb; /* Points to start of WKB */
	int32_t srid;    /* Current SRID we are handling */
	size_t wkb_size; /* Expected size of WKB */
	int8_t swap_bytes;  /* Do an endian flip? */
	int8_t check;       /* Simple validity checks on geometries */
	int8_t lwtype;      /* Current type we are handling */
	int8_t has_z;       /* Z? */
	int8_t has_m;       /* M? */
	int8_t has_srid;    /* SRID? */
	int8_t error;       /* An error was found (not enough bytes to read) */
	uint8_t depth;      /* Current recursion level (to prevent stack overflows). Maxes at LW_PARSER_MAX_DEPTH */
	const uint8_t *pos; /* Current parse position */
} wkb_parse_state;

#endif
