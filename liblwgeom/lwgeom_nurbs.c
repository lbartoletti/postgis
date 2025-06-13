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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "liblwgeom_internal.h"
#include "lwgeom_log.h"

/**
 * Construct a new NURBS curve
 */
LWNURBSCURVE *
lwnurbscurve_construct(int32_t srid, uint32_t degree, POINTARRAY *points,
                      double *weights, double *knots, uint32_t nweights, uint32_t nknots)
{
	LWNURBSCURVE *result;

	if (degree < 1 || degree > 10)
		return NULL;
	if (!points || points->npoints < 2)
		return NULL;

	result = lwalloc(sizeof(LWNURBSCURVE));
	result->type = NURBSCURVETYPE;
	result->flags = points ? points->flags : lwflags(0, 0, 0);
	result->srid = srid;
	result->bbox = NULL;
	result->degree = degree;
	result->points = points;
	result->nweights = nweights;
	result->nknots = nknots;

	/* Copy weights if provided */
	if (weights && nweights > 0) {
		result->weights = lwalloc(sizeof(double) * nweights);
		memcpy(result->weights, weights, sizeof(double) * nweights);
	} else {
		result->weights = NULL;
	}

	/* Copy knots if provided */
	if (knots && nknots > 0) {
		result->knots = lwalloc(sizeof(double) * nknots);
		memcpy(result->knots, knots, sizeof(double) * nknots);
	} else {
		result->knots = NULL;
	}

	return result;
}

/**
 * Construct an empty NURBS curve
 */
LWNURBSCURVE *
lwnurbscurve_construct_empty(int32_t srid, char hasz, char hasm)
{
	LWNURBSCURVE *result = lwalloc(sizeof(LWNURBSCURVE));
	result->type = NURBSCURVETYPE;
	result->flags = lwflags(hasz, hasm, 0);
	result->srid = srid;
	result->bbox = NULL;
	result->degree = 1;
	result->points = ptarray_construct_empty(hasz, hasm, 1);
	result->weights = NULL;
	result->nweights = 0;
	result->knots = NULL;
	result->nknots = 0;
	return result;
}

/**
 * Free NURBS curve memory
 */
void
lwnurbscurve_free(LWNURBSCURVE *curve)
{
	if (!curve) return;

	if (curve->bbox)
		lwfree(curve->bbox);
	if (curve->points)
		ptarray_free(curve->points);
	if (curve->weights)
		lwfree(curve->weights);
	if (curve->knots)
		lwfree(curve->knots);
	lwfree(curve);
}

/**
 * Deep clone NURBS curve
 */
LWNURBSCURVE *
lwnurbscurve_clone_deep(const LWNURBSCURVE *curve)
{
	if (!curve) return NULL;

	POINTARRAY *points = curve->points ? ptarray_clone_deep(curve->points) : NULL;

	return lwnurbscurve_construct(curve->srid, curve->degree, points,
	                             curve->weights, curve->knots,
	                             curve->nweights, curve->nknots);
}

/**
 * Convert to LWGEOM
 */
LWGEOM *
lwnurbscurve_as_lwgeom(const LWNURBSCURVE *obj)
{
	return (LWGEOM *)obj;
}

/**
 * Convert from LWGEOM
 */
LWNURBSCURVE *
lwgeom_as_lwnurbscurve(const LWGEOM *lwgeom)
{
	if (!lwgeom || lwgeom->type != NURBSCURVETYPE)
		return NULL;
	return (LWNURBSCURVE *)lwgeom;
}

/**
 * Convert NURBS curve to linestring
 */
LWLINE *
lwnurbscurve_to_linestring(const LWNURBSCURVE *curve, uint32_t segments)
{
	if (!curve || !curve->points)
		return NULL;

	if (curve->points->npoints == 0) {
		return lwline_construct_empty(curve->srid,
		                            FLAGS_GET_Z(curve->flags),
		                            FLAGS_GET_M(curve->flags));
	}

	if (segments < 2) segments = 32;

	/* Pour cette version minimale, on retourne juste les points de contrôle */
	POINTARRAY *pa = ptarray_clone_deep(curve->points);
	return lwline_construct(curve->srid, NULL, pa);
}
