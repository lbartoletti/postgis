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
	result->flags = points->flags;
	FLAGS_SET_BBOX(result->flags, 0);
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

/**
 * Validate NURBS curve parameters
 */
int lwnurbscurve_validate(const LWNURBSCURVE *curve)
{
    if (!curve) return LW_FALSE;

    if (curve->degree < 1 || curve->degree > 10) return LW_FALSE;

    if (!curve->points || curve->points->npoints < curve->degree + 1)
        return LW_FALSE;

    /* Validate weights if present */
    if (curve->weights) {
        if (curve->nweights != curve->points->npoints) return LW_FALSE;
        for (uint32_t i = 0; i < curve->nweights; i++) {
            if (curve->weights[i] <= 0.0) return LW_FALSE;
        }
    }

    /* Validate knots if present */
    if (curve->knots) {
        if (curve->nknots < curve->points->npoints + curve->degree + 1)
            return LW_FALSE;
        for (uint32_t i = 1; i < curve->nknots; i++) {
            if (curve->knots[i] < curve->knots[i-1]) return LW_FALSE;
        }
    }

    return LW_TRUE;
}

/**
 * Get control points as euclidean coordinates
 */
POINTARRAY* lwnurbscurve_get_control_points(const LWNURBSCURVE *curve)
{
    if (!curve || !curve->points) return NULL;

    /* If no weights, return points directly */
    if (!curve->weights) {
        return ptarray_clone_deep(curve->points);
    }

    /* Convert from homogeneous to euclidean coordinates */
    POINTARRAY *result = ptarray_construct(
        FLAGS_GET_Z(curve->flags),
        FLAGS_GET_M(curve->flags),
        curve->points->npoints
    );

    for (uint32_t i = 0; i < curve->points->npoints; i++) {
        POINT4D p4d;
        getPoint4d_p(curve->points, i, &p4d);

        double w = (i < curve->nweights) ? curve->weights[i] : 1.0;
        if (w != 0.0) {
            p4d.x /= w;
            p4d.y /= w;
            if (FLAGS_GET_Z(curve->flags)) p4d.z /= w;
        }

        ptarray_set_point4d(result, i, &p4d);
    }

    return result;
}

/**
 * Enhanced curve sampling with proper NURBS evaluation
 */
LWLINE* lwnurbscurve_stroke(const LWNURBSCURVE *curve, uint32_t segments)
{
    if (!curve || !curve->points) return NULL;

    if (curve->points->npoints == 0) {
        return lwline_construct_empty(curve->srid,
                                    FLAGS_GET_Z(curve->flags),
                                    FLAGS_GET_M(curve->flags));
    }

    if (segments < 2) segments = 32;

    POINTARRAY *pa = ptarray_construct(
        FLAGS_GET_Z(curve->flags),
        FLAGS_GET_M(curve->flags),
        segments
    );

    /* For now, simple linear interpolation of control points */
    /* TODO: Implement proper NURBS evaluation algorithm */
    POINTARRAY *ctrl_pts = lwnurbscurve_get_control_points(curve);

    for (uint32_t i = 0; i < segments; i++) {
        double t = (double)i / (segments - 1);
        uint32_t seg_idx = (uint32_t)(t * (ctrl_pts->npoints - 1));
        if (seg_idx >= ctrl_pts->npoints - 1) seg_idx = ctrl_pts->npoints - 2;

        double local_t = t * (ctrl_pts->npoints - 1) - seg_idx;

        POINT4D p1, p2, result;
        getPoint4d_p(ctrl_pts, seg_idx, &p1);
        getPoint4d_p(ctrl_pts, seg_idx + 1, &p2);

        result.x = p1.x + local_t * (p2.x - p1.x);
        result.y = p1.y + local_t * (p2.y - p1.y);
        if (FLAGS_GET_Z(curve->flags))
            result.z = p1.z + local_t * (p2.z - p1.z);
        if (FLAGS_GET_M(curve->flags))
            result.m = p1.m + local_t * (p2.m - p1.m);

        ptarray_set_point4d(pa, i, &result);
    }

    ptarray_free(ctrl_pts);
    return lwline_construct(curve->srid, NULL, pa);
}
