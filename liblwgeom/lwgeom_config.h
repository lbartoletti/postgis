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
 * Copyright 2025 Loïc Bartoletti <loic.bartoletti@oslandia.com>
 *
 **********************************************************************/


#ifndef LWGEOM_CONFIG_H
#define LWGEOM_CONFIG_H

#if defined(__GNUC__) || defined(__clang__)
  #define PRINTF_FORMAT(fmt_index, args_index) __attribute__ ((format (printf, fmt_index, args_index)))
#else
  #define PRINTF_FORMAT(fmt_index, args_index)
#endif

/* strcasecmp is defined in strings.h not string.h */
#if !defined(_MSC_VER) && (defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__)))
  #include <strings.h>
#endif
/* compatibility for MSVC */
#ifdef _MSC_VER
  #define strcasecmp _stricmp
  #define strncasecmp _strnicmp
#endif

/* Mathematical constants from FreeBSD's msun implementation */
#ifndef M_E
#define M_E         2.7182818284590452354      /* e */
#endif

#ifndef M_LOG2E
#define M_LOG2E     1.4426950408889634074      /* log 2e */
#endif

#ifndef M_LOG10E
#define M_LOG10E    0.43429448190325182765     /* log 10e */
#endif

#ifndef M_LN2
#define M_LN2       0.69314718055994530942     /* log e2 */
#endif

#ifndef M_LN10
#define M_LN10      2.30258509299404568402     /* log e10 */
#endif

#ifndef M_PI
#define M_PI        3.14159265358979323846     /* pi */
#endif

#ifndef M_PI_2
#define M_PI_2      1.57079632679489661923     /* pi/2 */
#endif

#ifndef M_PI_4
#define M_PI_4      0.78539816339744830962     /* pi/4 */
#endif

#ifndef M_1_PI
#define M_1_PI      0.31830988618379067154     /* 1/pi */
#endif

#ifndef M_2_PI
#define M_2_PI      0.63661977236758134308     /* 2/pi */
#endif

#ifndef M_2_SQRTPI
#define M_2_SQRTPI  1.12837916709551257390     /* 2/sqrt(pi) */
#endif

#ifndef M_SQRT2
#define M_SQRT2     1.41421356237309504880     /* sqrt(2) */
#endif

#ifndef M_SQRT1_2
#define M_SQRT1_2   0.70710678118654752440     /* 1/sqrt(2) */
#endif

#endif /* LWGEOM_CONFIG_H */
