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
 * B-spline basis function (Cox-de Boor algorithm)
 */
static double
bspline_basis(int i, int p, double t, const double *knots, int nknots)
{
	if (p == 0) {
		return (t >= knots[i] && t < knots[i + 1]) ? 1.0 : 0.0;
	}

	double left = 0.0, right = 0.0;

	if (knots[i + p] != knots[i]) {
		left = (t - knots[i]) / (knots[i + p] - knots[i]) *
		       bspline_basis(i, p - 1, t, knots, nknots);
	}

	if (knots[i + p + 1] != knots[i + 1]) {
		right = (knots[i + p + 1] - t) / (knots[i + p + 1] - knots[i + 1]) *
		        bspline_basis(i + 1, p - 1, t, knots, nknots);
	}

	return left + right;
}

/**
 * Evaluate NURBS curve at parameter t
 */
int
lwnurbscurve_interpolate_point(const LWNURBSCURVE *curve, double t, POINT4D *pt)
{
	if (!curve || !curve->points || !pt)
		return LW_FAILURE;

	if (curve->points->npoints == 0)
		return LW_FAILURE;

	/* Clamp parameter */
	if (t < 0.0) t = 0.0;
	if (t > 1.0) t = 1.0;

	uint32_t n = curve->points->npoints;
	uint32_t p = curve->degree;
	int hasz = FLAGS_GET_Z(curve->flags);
	int hasm = FLAGS_GET_M(curve->flags);

	pt->x = pt->y = pt->z = pt->m = 0.0;
	double weight_sum = 0.0;
	POINT4D ctrl_pt;

	for (uint32_t i = 0; i < n; i++) {
		getPoint4d_p(curve->points, i, &ctrl_pt);

		double basis = 1.0;
		if (curve->knots && curve->nknots > 0) {
			basis = bspline_basis(i, p, t, curve->knots, curve->nknots);
		} else {
			/* Bezier case - use Bernstein polynomials */
			double binom = 1.0;
			for (uint32_t j = 0; j < i; j++) {
				binom *= (double)(n - 1 - j) / (double)(j + 1);
			}
			basis = binom * pow(t, (double)i) * pow(1.0 - t, (double)(n - 1 - i));
		}

		double weight = (curve->weights && i < curve->nweights) ? curve->weights[i] : 1.0;
		double w_basis = weight * basis;

		pt->x += w_basis * ctrl_pt.x;
		pt->y += w_basis * ctrl_pt.y;
		if (hasz) pt->z += w_basis * ctrl_pt.z;
		if (hasm) pt->m += w_basis * ctrl_pt.m;

		weight_sum += w_basis;
	}

	if (weight_sum > 0.0) {
		pt->x /= weight_sum;
		pt->y /= weight_sum;
		if (hasz) pt->z /= weight_sum;
		if (hasm) pt->m /= weight_sum;
	}

	return LW_SUCCESS;
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

	int hasz = FLAGS_GET_Z(curve->flags);
	int hasm = FLAGS_GET_M(curve->flags);
	POINTARRAY *pa = ptarray_construct_empty(hasz, hasm, segments + 1);

	for (uint32_t i = 0; i <= segments; i++) {
		double t = (double)i / (double)segments;
		POINT4D pt;

		if (lwnurbscurve_interpolate_point(curve, t, &pt) == LW_SUCCESS) {
			ptarray_append_point(pa, &pt, LW_FALSE);
		}
	}

	return lwline_construct(curve->srid, NULL, pa);
}
