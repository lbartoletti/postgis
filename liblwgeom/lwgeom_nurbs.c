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
#include "stringbuffer.h"
#include "lwgeom_log.h"
#include "lwgeom_nurbs.h"

/**
 * Construct a LWNURBSCURVE following PostGIS patterns
 */
LWNURBSCURVE *
lwnurbs_construct(int32_t srid, GBOX *bbox, uint32_t degree,
                 POINTARRAY *ctrl_pts, double *weights,
                 uint32_t nweights, double *knots, uint32_t nknots)
{
	LWNURBSCURVE *result;

	/* Basic validation */
	if (degree < NURBS_MIN_DEGREE || degree > NURBS_MAX_DEGREE)
	{
		lwerror("lwnurbs_construct: invalid degree %d", degree);
		return NULL;
	}

	if (ctrl_pts && ctrl_pts->npoints < NURBS_MIN_POINTS)
	{
		lwerror("lwnurbs_construct: minimum %d control points required", NURBS_MIN_POINTS);
		return NULL;
	}

	result = (LWNURBSCURVE*) lwalloc(sizeof(LWNURBSCURVE));
	result->type = NURBSCURVETYPE;
	result->flags = ctrl_pts ? ctrl_pts->flags : lwflags(0, 0, 0);
	FLAGS_SET_BBOX(result->flags, bbox ? 1 : 0);
	result->srid = srid;
	result->bbox = bbox;
	result->degree = degree;
	result->ctrl_pts = ctrl_pts;
	result->nweights = nweights;
	result->nknots = nknots;

	/* Copy weights if provided */
	if (weights && nweights > 0)
	{
		result->weights = (double*) lwalloc(sizeof(double) * nweights);
		memcpy(result->weights, weights, sizeof(double) * nweights);
	}
	else
	{
		result->weights = NULL;
	}

	/* Copy knots if provided */
	if (knots && nknots > 0)
	{
		result->knots = (double*) lwalloc(sizeof(double) * nknots);
		memcpy(result->knots, knots, sizeof(double) * nknots);
	}
	else
	{
		result->knots = NULL;
	}

	return result;
}

/**
 * Construct an empty NURBS curve
 */
LWNURBSCURVE *
lwnurbs_construct_empty(int32_t srid, char hasz, char hasm)
{
	LWNURBSCURVE *result = lwalloc(sizeof(LWNURBSCURVE));
	result->type = NURBSCURVETYPE;
	result->flags = lwflags(hasz, hasm, 0);
	result->srid = srid;
	result->bbox = NULL;
	result->degree = 1;
	result->ctrl_pts = ptarray_construct_empty(hasz, hasm, 1);
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
lwnurbs_free(LWNURBSCURVE *nurbs)
{
	if (!nurbs) return;

	if (nurbs->bbox)
		lwfree(nurbs->bbox);
	if (nurbs->ctrl_pts)
		ptarray_free(nurbs->ctrl_pts);
	if (nurbs->weights)
		lwfree(nurbs->weights);
	if (nurbs->knots)
		lwfree(nurbs->knots);
	lwfree(nurbs);
}

/**
 * Release NURBS curve (for lwgeom_release pattern)
 */
void
lwnurbs_release(LWNURBSCURVE *nurbs)
{
	lwgeom_release(lwnurbs_as_lwgeom(nurbs));
}

/**
 * B-spline basis function implementation (De Boor algorithm)
 */
double
bspline_basis(int i, int p, double t, const double *knots, int nknots)
{
	if (p == 0)
	{
		if (i >= 0 && i < nknots - 1 && t >= knots[i] && t < knots[i + 1])
			return 1.0;
		else if (i == nknots - 2 && fabs(t - knots[nknots - 1]) < NURBS_TOLERANCE)
			return 1.0;
		else
			return 0.0;
	}

	double left = 0.0, right = 0.0;

	if (i + p < nknots && fabs(knots[i + p] - knots[i]) > NURBS_TOLERANCE)
	{
		left = (t - knots[i]) / (knots[i + p] - knots[i]) *
		       bspline_basis(i, p - 1, t, knots, nknots);
	}

	if (i + p + 1 < nknots && fabs(knots[i + p + 1] - knots[i + 1]) > NURBS_TOLERANCE)
	{
		right = (knots[i + p + 1] - t) / (knots[i + p + 1] - knots[i + 1]) *
		        bspline_basis(i + 1, p - 1, t, knots, nknots);
	}

	return left + right;
}

/**
 * Evaluate NURBS curve at parameter t
 */
int
lwnurbs_interpolate_point(const LWNURBSCURVE *nurbs, double t, POINT4D *pt)
{
	if (!nurbs || !nurbs->ctrl_pts || !pt)
		return LW_FAILURE;

	if (nurbs->ctrl_pts->npoints == 0)
		return LW_FAILURE;

	/* Clamp parameter to [0,1] */
	if (t < 0.0) t = 0.0;
	if (t > 1.0) t = 1.0;

	uint32_t n = nurbs->ctrl_pts->npoints;
	uint32_t p = nurbs->degree;
	int hasz = FLAGS_GET_Z(nurbs->flags);
	int hasm = FLAGS_GET_M(nurbs->flags);

	/* Initialize point */
	pt->x = pt->y = pt->z = pt->m = 0.0;

	double weight_sum = 0.0;
	POINT4D ctrl_pt;

	for (uint32_t i = 0; i < n; i++)
	{
		getPoint4d_p(nurbs->ctrl_pts, i, &ctrl_pt);

		double basis = bspline_basis(i, p, t, nurbs->knots, nurbs->nknots);
		double weight = (nurbs->weights && i < nurbs->nweights) ? nurbs->weights[i] : 1.0;
		double w_basis = weight * basis;

		pt->x += w_basis * ctrl_pt.x;
		pt->y += w_basis * ctrl_pt.y;
		if (hasz) pt->z += w_basis * ctrl_pt.z;
		if (hasm) pt->m += w_basis * ctrl_pt.m;

		weight_sum += w_basis;
	}

	/* Normalize by weight sum for rational curves */
	if (weight_sum > NURBS_TOLERANCE)
	{
		pt->x /= weight_sum;
		pt->y /= weight_sum;
		if (hasz) pt->z /= weight_sum;
		if (hasm) pt->m /= weight_sum;
	}

	return LW_SUCCESS;
}

/**
 * Convert NURBS curve to linestring with sampling
 */
LWLINE *
lwnurbs_to_linestring(const LWNURBSCURVE *nurbs, uint32_t segments)
{
	if (!nurbs || !nurbs->ctrl_pts)
		return NULL;

	if (lwnurbs_is_empty(nurbs))
		return lwline_construct_empty(nurbs->srid,
		                            FLAGS_GET_Z(nurbs->flags),
		                            FLAGS_GET_M(nurbs->flags));

	if (segments < 2)
		segments = NURBS_DEFAULT_SAMPLES;

	int hasz = FLAGS_GET_Z(nurbs->flags);
	int hasm = FLAGS_GET_M(nurbs->flags);
	POINTARRAY *pa = ptarray_construct_empty(hasz, hasm, segments + 1);

	for (uint32_t i = 0; i <= segments; i++)
	{
		double t = (double)i / (double)segments;
		POINT4D pt;

		if (lwnurbs_interpolate_point(nurbs, t, &pt) == LW_SUCCESS)
		{
			ptarray_append_point(pa, &pt, LW_FALSE);
		}
	}

	return lwline_construct(nurbs->srid, NULL, pa);
}

/**
 * Calculate NURBS curve length (approximate)
 */
double
lwnurbs_length(const LWNURBSCURVE *nurbs)
{
	if (!nurbs || lwnurbs_is_empty(nurbs))
		return 0.0;

	LWLINE *line = lwnurbs_to_linestring(nurbs, 64);
	if (!line)
		return 0.0;

	double length = lwgeom_length((LWGEOM*)line);
	lwline_free(line);
	return length;
}

/**
 * Validate NURBS curve
 */
int
lwnurbs_is_valid(const LWNURBSCURVE *nurbs)
{
	if (!nurbs) return LW_FALSE;

	/* Check degree */
	if (nurbs->degree < NURBS_MIN_DEGREE || nurbs->degree > NURBS_MAX_DEGREE)
		return LW_FALSE;

	/* Check control points */
	if (!nurbs->ctrl_pts)
		return LW_FALSE;

	if (nurbs->ctrl_pts->npoints > 0 && nurbs->ctrl_pts->npoints < NURBS_MIN_POINTS)
		return LW_FALSE;

	/* Check weights if present */
	if (nurbs->weights && nurbs->nweights != nurbs->ctrl_pts->npoints)
		return LW_FALSE;

	/* Check knots if present */
	if (nurbs->knots && nurbs->nknots < nurbs->ctrl_pts->npoints + nurbs->degree + 1)
		return LW_FALSE;

	return LW_TRUE;
}

/**
 * Check if NURBS curve is empty
 */
int
lwnurbs_is_empty(const LWNURBSCURVE *nurbs)
{
	return (!nurbs || !nurbs->ctrl_pts || nurbs->ctrl_pts->npoints == 0);
}

/**
 * Check if NURBS curve is closed
 */
int
lwnurbs_is_closed(const LWNURBSCURVE *nurbs)
{
	if (lwnurbs_is_empty(nurbs))
		return LW_FALSE;

	POINT4D start, end;
	if (lwnurbs_startpoint(nurbs, &start) && lwnurbs_endpoint(nurbs, &end))
	{
		return (fabs(start.x - end.x) < NURBS_TOLERANCE &&
		        fabs(start.y - end.y) < NURBS_TOLERANCE &&
		        fabs(start.z - end.z) < NURBS_TOLERANCE &&
		        fabs(start.m - end.m) < NURBS_TOLERANCE);
	}
	return LW_FALSE;
}

/**
 * Clone NURBS curve (shallow copy)
 */
LWNURBSCURVE *
lwnurbs_clone(const LWNURBSCURVE *nurbs)
{
	if (!nurbs) return NULL;
	return (LWNURBSCURVE*)lwline_clone((LWLINE*)nurbs);
}

/**
 * Deep clone NURBS curve
 */
LWNURBSCURVE *
lwnurbs_clone_deep(const LWNURBSCURVE *nurbs)
{
	if (!nurbs) return NULL;

	POINTARRAY *ctrl_pts = nurbs->ctrl_pts ? ptarray_clone_deep(nurbs->ctrl_pts) : NULL;
	GBOX *bbox = nurbs->bbox ? gbox_clone(nurbs->bbox) : NULL;

	return lwnurbs_construct(nurbs->srid, bbox, nurbs->degree, ctrl_pts,
	                        nurbs->weights, nurbs->nweights,
	                        nurbs->knots, nurbs->nknots);
}

/**
 * Compute bounding box for NURBS curve
 */
void
lwnurbs_compute_bbox_p(LWNURBSCURVE *nurbs)
{
	if (!nurbs || !nurbs->ctrl_pts)
		return;

	if (nurbs->bbox)
	{
		lwfree(nurbs->bbox);
		nurbs->bbox = NULL;
	}

	/* Use control points for approximate bbox */
	if (nurbs->ctrl_pts->npoints > 0)
	{
		GBOX *box = lwalloc(sizeof(GBOX));
		ptarray_calculate_gbox_cartesian(nurbs->ctrl_pts, box);
		nurbs->bbox = box;
		FLAGS_SET_BBOX(nurbs->flags, 1);
	}
}

/**
 * Generate uniform knot vector
 */
double *
lwnurbs_uniform_knots(uint32_t degree, uint32_t nctrl)
{
	uint32_t nknots = nctrl + degree + 1;
	double *knots = lwalloc(sizeof(double) * nknots);

	for (uint32_t i = 0; i < nknots; i++)
	{
		knots[i] = (double)i / (double)(nknots - 1);
	}

	return knots;
}

/**
 * Generate clamped knot vector
 */
double *
lwnurbs_clamped_knots(uint32_t degree, uint32_t nctrl)
{
	uint32_t nknots = nctrl + degree + 1;
	double *knots = lwalloc(sizeof(double) * nknots);

	/* Clamp start */
	for (uint32_t i = 0; i <= degree; i++)
		knots[i] = 0.0;

	/* Internal knots */
	for (uint32_t i = degree + 1; i < nctrl; i++)
		knots[i] = (double)(i - degree) / (double)(nctrl - degree);

	/* Clamp end */
	for (uint32_t i = nctrl; i < nknots; i++)
		knots[i] = 1.0;

	return knots;
}

/**
 * Get NURBS degree
 */
uint32_t
lwnurbs_get_degree(const LWNURBSCURVE *nurbs)
{
	return nurbs ? nurbs->degree : 0;
}

/**
 * Get number of control points
 */
uint32_t
lwnurbs_get_npoints(const LWNURBSCURVE *nurbs)
{
	return (nurbs && nurbs->ctrl_pts) ? nurbs->ctrl_pts->npoints : 0;
}

/**
 * Get start point
 */
int
lwnurbs_startpoint(const LWNURBSCURVE *nurbs, POINT4D *pt)
{
	return lwnurbs_interpolate_point(nurbs, 0.0, pt);
}

/**
 * Get end point
 */
int
lwnurbs_endpoint(const LWNURBSCURVE *nurbs, POINT4D *pt)
{
	return lwnurbs_interpolate_point(nurbs, 1.0, pt);
}

/**
 * Convert NURBS to LWGEOM
 */
LWGEOM *
lwnurbs_as_lwgeom(const LWNURBSCURVE *nurbs)
{
	return (LWGEOM*)nurbs;
}

/**
 * Convert LWGEOM to NURBS (with type check)
 */
LWNURBSCURVE *
lwgeom_as_lwnurbs(const LWGEOM *lwgeom)
{
	if (!lwgeom || lwgeom->type != NURBSCURVETYPE)
		return NULL;
	return (LWNURBSCURVE*)lwgeom;
}

/**
 * WKB serialization size calculation
 */
size_t
lwnurbs_to_wkb_size(const LWNURBSCURVE *nurbs, uint8_t variant)
{
	size_t size = WKB_BYTE_SIZE + WKB_INT_SIZE + WKB_INT_SIZE; /* endian + type + npoints */
	if (nurbs && nurbs->ctrl_pts)
	{
		size += sizeof(double) * FLAGS_NDIMS(nurbs->flags) * nurbs->ctrl_pts->npoints;
		size += sizeof(uint32_t); /* degree */
		size += sizeof(uint32_t) + nurbs->nweights * sizeof(double); /* weights */
		size += sizeof(uint32_t) + nurbs->nknots * sizeof(double); /* knots */
	}

	return size;
}

/**
 * Simple WKT output (basic implementation)
 */
char *
lwnurbs_to_wkt(const LWNURBSCURVE *nurbs, uint8_t variant, int precision, size_t *size_out)
{
	char *result;

	if (lwnurbs_is_empty(nurbs))
	{
		result = lwalloc(32);
		strcpy(result, "NURBSCURVE EMPTY");
		if (size_out) *size_out = strlen(result);
		return result;
	}

	/* For now, convert to linestring and output that */
	LWLINE *line = lwnurbs_to_linestring(nurbs, NURBS_DEFAULT_SAMPLES);
	if (line)
	{
		result = lwgeom_to_wkt((LWGEOM*)line, variant, precision, size_out);
		lwline_free(line);
		return result;
	}

	result = lwalloc(32);
	strcpy(result, "NURBSCURVE EMPTY");
	if (size_out) *size_out = strlen(result);
	return result;
}

LWNURBSCURVE *
lwnurbs_from_wkb_state(wkb_parse_state *s)
{
	// TODO: FIX: Convert to empty atm
	return lwnurbs_construct_empty(s->srid, s->has_z, s->has_m);
}
