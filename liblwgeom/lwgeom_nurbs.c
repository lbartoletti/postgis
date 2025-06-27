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

/* Construct a weighted NURBS point */
NURBSPOINT *
nurbspoint_construct(double x, double y, double z, double m, double weight, lwflags_t flags)
{
    NURBSPOINT *point = lwalloc(sizeof(NURBSPOINT));

    point->x = x;
    point->y = y;
    point->z = FLAGS_GET_Z(flags) ? z : 0.0;
    point->m = FLAGS_GET_M(flags) ? m : 0.0;
    point->weight = weight > 0.0 ? weight : 1.0;

    return point;
}

/* Free a NURBS point */
void
nurbspoint_free(NURBSPOINT *point)
{
    if (point) lwfree(point);
}

/* Construct a new NURBS curve */
LWNURBSCURVE *
lwnurbscurve_construct(int32_t srid, uint32_t degree, NURBSPOINT *points, uint32_t npoints,
                      double *knots, uint32_t nknots, double start_m, double end_m)
{
    LWNURBSCURVE *result;

    if (degree < 1 || degree > 10) return NULL;
    if (npoints > 0 && npoints < degree + 1) return NULL;

    result = lwalloc(sizeof(LWNURBSCURVE));
    result->type = NURBSCURVETYPE;
    result->srid = srid;
    result->bbox = NULL;
    result->degree = degree;
    result->npoints = npoints;
    result->nknots = nknots;
    result->start_measure = start_m;
    result->end_measure = end_m;
    result->flags = 0;

    /* Set flags based on first point if available */
    if (points && npoints > 0) {
        if (points[0].z != 0.0) FLAGS_SET_Z(result->flags, 1);
        if (points[0].m != 0.0) FLAGS_SET_M(result->flags, 1);
    }

    /* Copy points */
    if (points && npoints > 0) {
        result->points = lwalloc(sizeof(NURBSPOINT) * npoints);
        memcpy(result->points, points, sizeof(NURBSPOINT) * npoints);
    } else {
        result->points = NULL;
    }

    /* Copy knots */
    if (knots && nknots > 0) {
        result->knots = lwalloc(sizeof(double) * nknots);
        memcpy(result->knots, knots, sizeof(double) * nknots);
    } else {
        result->knots = NULL;
    }

    return result;
}

/* Construct an empty NURBS curve */
LWNURBSCURVE *
lwnurbscurve_construct_empty(int32_t srid, char hasz, char hasm)
{
    LWNURBSCURVE *result = lwalloc(sizeof(LWNURBSCURVE));
    result->type = NURBSCURVETYPE;
    result->flags = lwflags(hasz, hasm, 0);
    result->srid = srid;
    result->bbox = NULL;
    result->degree = 1;
    result->npoints = 0;
    result->nknots = 0;
    result->points = NULL;
    result->knots = NULL;
    result->start_measure = 0.0;
    result->end_measure = 0.0;
    return result;
}

/* Free NURBS curve memory */
void
lwnurbscurve_free(LWNURBSCURVE *curve)
{
    if (!curve) return;

    if (curve->bbox) lwfree(curve->bbox);
    if (curve->points) lwfree(curve->points);
    if (curve->knots) lwfree(curve->knots);
    lwfree(curve);
}

/* Deep clone NURBS curve */
LWNURBSCURVE *
lwnurbscurve_clone_deep(const LWNURBSCURVE *curve)
{
    if (!curve) return NULL;

    return lwnurbscurve_construct(curve->srid, curve->degree, curve->points, curve->npoints,
                                 curve->knots, curve->nknots, curve->start_measure, curve->end_measure);
}

/* Convert to LWGEOM */
LWGEOM *
lwnurbscurve_as_lwgeom(const LWNURBSCURVE *obj)
{
    return (LWGEOM *)obj;
}

/* Convert from LWGEOM */
LWNURBSCURVE *
lwgeom_as_lwnurbscurve(const LWGEOM *lwgeom)
{
    if (!lwgeom || lwgeom->type != NURBSCURVETYPE) return NULL;
    return (LWNURBSCURVE *)lwgeom;
}

/* Validate NURBS curve parameters */
int
lwnurbscurve_validate(const LWNURBSCURVE *curve)
{
    uint32_t i;

    if (!curve) return LW_FALSE;
    if (curve->degree < 1 || curve->degree > 10) return LW_FALSE;
    if (curve->npoints > 0 && curve->npoints < curve->degree + 1) return LW_FALSE;

    /* Validate weights are positive */
    for (i = 0; i < curve->npoints; i++) {
        if (curve->points[i].weight <= 0.0) return LW_FALSE;
    }

    /* Validate knot vector is non-decreasing */
    for (i = 1; i < curve->nknots; i++) {
        if (curve->knots[i] < curve->knots[i-1]) return LW_FALSE;
    }

    /* Validate knot count relationship: nknots = npoints + degree + 1 */
    if (curve->nknots > 0 && curve->nknots != curve->npoints + curve->degree + 1) {
        return LW_FALSE;
    }

    return LW_TRUE;
}

/* Convert NURBS curve to linestring approximation */
LWLINE *
lwnurbscurve_stroke(const LWNURBSCURVE *curve, uint32_t segments)
{
    POINTARRAY *pa;
    POINT4D pt;
    uint32_t i;
    double t, step;

    if (!curve || curve->npoints == 0) {
        return lwline_construct_empty(curve->srid, FLAGS_GET_Z(curve->flags), FLAGS_GET_M(curve->flags));
    }

    if (segments < 2) segments = 32;

    pa = ptarray_construct(FLAGS_GET_Z(curve->flags), FLAGS_GET_M(curve->flags), segments);
    step = 1.0 / (segments - 1);

    /* Simple linear interpolation for now - can be enhanced with proper NURBS evaluation */
    for (i = 0; i < segments; i++) {
        t = i * step;

        /* Linear interpolation between first and last control point */
        if (curve->npoints >= 2) {
            double inv_t = 1.0 - t;
            pt.x = inv_t * curve->points[0].x + t * curve->points[curve->npoints-1].x;
            pt.y = inv_t * curve->points[0].y + t * curve->points[curve->npoints-1].y;

            if (FLAGS_GET_Z(curve->flags)) {
                pt.z = inv_t * curve->points[0].z + t * curve->points[curve->npoints-1].z;
            }
            if (FLAGS_GET_M(curve->flags)) {
                pt.m = inv_t * curve->start_measure + t * curve->end_measure;
            }
        } else {
            pt.x = curve->points[0].x;
            pt.y = curve->points[0].y;
            pt.z = curve->points[0].z;
            pt.m = curve->start_measure;
        }

        ptarray_set_point4d(pa, i, &pt);
    }

    return lwline_construct(curve->srid, NULL, pa);
}
