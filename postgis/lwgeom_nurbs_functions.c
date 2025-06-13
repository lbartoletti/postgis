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

#include "postgres.h"
#include "fmgr.h"
#include "funcapi.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "catalog/pg_type.h"

#include "../postgis_config.h"
#include "liblwgeom.h"
#include "lwgeom_pg.h"
#include "lwgeom_nurbs.h"

Datum lwgeom_nurbs_make(PG_FUNCTION_ARGS);
Datum lwgeom_nurbs_evaluate(PG_FUNCTION_ARGS);
Datum lwgeom_nurbs_toline(PG_FUNCTION_ARGS);
Datum lwgeom_nurbs_length(PG_FUNCTION_ARGS);
Datum lwgeom_nurbs_startpoint(PG_FUNCTION_ARGS);
Datum lwgeom_nurbs_endpoint(PG_FUNCTION_ARGS);
Datum lwgeom_nurbs_isclosed(PG_FUNCTION_ARGS);
Datum lwgeom_nurbs_isvalid(PG_FUNCTION_ARGS);
Datum lwgeom_nurbs_degree(PG_FUNCTION_ARGS);
Datum lwgeom_nurbs_npoints(PG_FUNCTION_ARGS);

/**
 * ST_MakeNurbsCurve(degree integer, control_points geometry, weights float8[], knots float8[])
 * Create a NURBS curve from control points, weights, and knots
 */
PG_FUNCTION_INFO_V1(lwgeom_nurbs_make);
Datum lwgeom_nurbs_make(PG_FUNCTION_ARGS)
{
	int32_t degree;
	GSERIALIZED *pcontrol_pts;
	ArrayType *weights_array = NULL;
	ArrayType *knots_array = NULL;
	LWGEOM *control_geom;
	LWMPOINT *control_mpt = NULL;
	POINTARRAY *ctrl_pts;
	double *weights = NULL;
	double *knots = NULL;
	uint32_t nweights = 0, nknots = 0;
	LWNURBSCURVE *nurbs;
	GSERIALIZED *result;
	int32_t srid;

	/* Get degree */
	degree = PG_GETARG_INT32(0);
	if (degree < NURBS_MIN_DEGREE || degree > NURBS_MAX_DEGREE)
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("NURBS degree must be between %d and %d",
			       NURBS_MIN_DEGREE, NURBS_MAX_DEGREE)));
	}

	/* Get control points */
	pcontrol_pts = PG_GETARG_GSERIALIZED_P(1);
	control_geom = lwgeom_from_gserialized(pcontrol_pts);
	srid = control_geom->srid;

	/* Convert to MULTIPOINT if necessary */
	if (control_geom->type == POINTTYPE)
	{
		control_mpt = lwmpoint_construct_empty(srid,
		                                     lwgeom_has_z(control_geom),
		                                     lwgeom_has_m(control_geom));
		lwmpoint_add_lwpoint(control_mpt, (LWPOINT*)control_geom);
		control_geom = (LWGEOM*)control_mpt;
	}
	else if (control_geom->type == MULTIPOINTTYPE)
	{
		control_mpt = (LWMPOINT*)control_geom;
	}
	else
	{
		lwgeom_free(control_geom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Control points must be POINT or MULTIPOINT geometry")));
	}

	/* Extract point array from multipoint */
	ctrl_pts = ptarray_construct_empty(lwgeom_has_z(control_geom),
	                                  lwgeom_has_m(control_geom),
	                                  control_mpt->ngeoms);

	for (uint32_t i = 0; i < control_mpt->ngeoms; i++)
	{
		POINT4D pt;
		if (getPoint4d_p(control_mpt->geoms[i]->point, 0, &pt))
		{
			ptarray_append_point(ctrl_pts, &pt, LW_FALSE);
		}
	}

	if (ctrl_pts->npoints < NURBS_MIN_POINTS)
	{
		ptarray_free(ctrl_pts);
		lwgeom_free(control_geom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("NURBS curve requires at least %d control points", NURBS_MIN_POINTS)));
	}

	/* Get weights array if provided */
	if (!PG_ARGISNULL(2))
	{
		weights_array = PG_GETARG_ARRAYTYPE_P(2);
		if (ARR_ELEMTYPE(weights_array) != FLOAT8OID)
		{
			ptarray_free(ctrl_pts);
			lwgeom_free(control_geom);
			ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("Weights array must contain float8 values")));
		}

		nweights = ArrayGetNItems(ARR_NDIM(weights_array), ARR_DIMS(weights_array));
		if (nweights != ctrl_pts->npoints)
		{
			ptarray_free(ctrl_pts);
			lwgeom_free(control_geom);
			ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("Number of weights must equal number of control points")));
		}

		weights = (double*) ARR_DATA_PTR(weights_array);
	}

	/* Get knots array if provided */
	if (!PG_ARGISNULL(3))
	{
		knots_array = PG_GETARG_ARRAYTYPE_P(3);
		if (ARR_ELEMTYPE(knots_array) != FLOAT8OID)
		{
			ptarray_free(ctrl_pts);
			lwgeom_free(control_geom);
			ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("Knots array must contain float8 values")));
		}

		nknots = ArrayGetNItems(ARR_NDIM(knots_array), ARR_DIMS(knots_array));
		if (nknots < ctrl_pts->npoints + degree + 1)
		{
			ptarray_free(ctrl_pts);
			lwgeom_free(control_geom);
			ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("Insufficient knots for NURBS curve")));
		}

		knots = (double*) ARR_DATA_PTR(knots_array);
	}
	else
	{
		/* Generate clamped knot vector */
		knots = lwnurbs_clamped_knots(degree, ctrl_pts->npoints);
		nknots = ctrl_pts->npoints + degree + 1;
	}

	/* Create NURBS curve */
	nurbs = lwnurbs_construct(srid, NULL, degree, ctrl_pts, weights, nweights, knots, nknots);

	/* Free generated knots if we created them */
	if (PG_ARGISNULL(3) && knots)
		lwfree(knots);

	lwgeom_free(control_geom);

	if (!nurbs)
	{
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
			errmsg("Failed to construct NURBS curve")));
	}

	result = geometry_serialize((LWGEOM*)nurbs);
	lwnurbs_free(nurbs);

	PG_RETURN_POINTER(result);
}

/**
 * ST_NurbsEvaluate(nurbscurve geometry, t float8)
 * Evaluate NURBS curve at parameter t
 */
PG_FUNCTION_INFO_V1(lwgeom_nurbs_evaluate);
Datum lwgeom_nurbs_evaluate(PG_FUNCTION_ARGS)
{
	GSERIALIZED *pnurbs;
	double t;
	LWGEOM *lwgeom;
	LWNURBSCURVE *nurbs;
	POINT4D pt;
	LWPOINT *lwpt;
	GSERIALIZED *result;

	pnurbs = PG_GETARG_GSERIALIZED_P(0);
	t = PG_GETARG_FLOAT8(1);

	lwgeom = lwgeom_from_gserialized(pnurbs);
	if (lwgeom->type != NURBSCURVETYPE)
	{
		lwgeom_free(lwgeom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Geometry must be a NURBS curve")));
	}

	nurbs = (LWNURBSCURVE*)lwgeom;

	if (lwnurbs_interpolate_point(nurbs, t, &pt) != LW_SUCCESS)
	{
		lwgeom_free(lwgeom);
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
			errmsg("Failed to evaluate NURBS curve at parameter %g", t)));
	}

	lwpt = lwpoint_make(nurbs->srid,
	                   FLAGS_GET_Z(nurbs->flags),
	                   FLAGS_GET_M(nurbs->flags),
	                   &pt);

	lwgeom_free(lwgeom);
	result = geometry_serialize((LWGEOM*)lwpt);
	lwpoint_free(lwpt);

	PG_RETURN_POINTER(result);
}

/**
 * ST_NurbsToLine(nurbscurve geometry, segments integer)
 * Convert NURBS curve to linestring
 */
PG_FUNCTION_INFO_V1(lwgeom_nurbs_toline);
Datum lwgeom_nurbs_toline(PG_FUNCTION_ARGS)
{
	GSERIALIZED *pnurbs;
	int32_t segments;
	LWGEOM *lwgeom;
	LWNURBSCURVE *nurbs;
	LWLINE *line;
	GSERIALIZED *result;

	pnurbs = PG_GETARG_GSERIALIZED_P(0);
	segments = PG_GETARG_INT32(1);

	if (segments < 2)
		segments = NURBS_DEFAULT_SAMPLES;

	lwgeom = lwgeom_from_gserialized(pnurbs);
	if (lwgeom->type != NURBSCURVETYPE)
	{
		lwgeom_free(lwgeom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Geometry must be a NURBS curve")));
	}

	nurbs = (LWNURBSCURVE*)lwgeom;
	line = lwnurbs_to_linestring(nurbs, segments);

	lwgeom_free(lwgeom);

	if (!line)
	{
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
			errmsg("Failed to convert NURBS curve to linestring")));
	}

	result = geometry_serialize((LWGEOM*)line);
	lwline_free(line);

	PG_RETURN_POINTER(result);
}

/**
 * ST_NurbsLength(nurbscurve geometry)
 * Calculate NURBS curve length
 */
PG_FUNCTION_INFO_V1(lwgeom_nurbs_length);
Datum lwgeom_nurbs_length(PG_FUNCTION_ARGS)
{
	GSERIALIZED *pnurbs;
	LWGEOM *lwgeom;
	LWNURBSCURVE *nurbs;
	double length;

	pnurbs = PG_GETARG_GSERIALIZED_P(0);
	lwgeom = lwgeom_from_gserialized(pnurbs);

	if (lwgeom->type != NURBSCURVETYPE)
	{
		lwgeom_free(lwgeom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Geometry must be a NURBS curve")));
	}

	nurbs = (LWNURBSCURVE*)lwgeom;
	length = lwnurbs_length(nurbs);
	lwgeom_free(lwgeom);

	PG_RETURN_FLOAT8(length);
}

/**
 * ST_NurbsStartPoint(nurbscurve geometry)
 * Get NURBS curve start point
 */
PG_FUNCTION_INFO_V1(lwgeom_nurbs_startpoint);
Datum lwgeom_nurbs_startpoint(PG_FUNCTION_ARGS)
{
	GSERIALIZED *pnurbs;
	LWGEOM *lwgeom;
	LWNURBSCURVE *nurbs;
	POINT4D pt;
	LWPOINT *lwpt;
	GSERIALIZED *result;

	pnurbs = PG_GETARG_GSERIALIZED_P(0);
	lwgeom = lwgeom_from_gserialized(pnurbs);

	if (lwgeom->type != NURBSCURVETYPE)
	{
		lwgeom_free(lwgeom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Geometry must be a NURBS curve")));
	}

	nurbs = (LWNURBSCURVE*)lwgeom;

	if (lwnurbs_startpoint(nurbs, &pt) != LW_SUCCESS)
	{
		lwgeom_free(lwgeom);
		PG_RETURN_NULL();
	}

	lwpt = lwpoint_make(nurbs->srid,
	                   FLAGS_GET_Z(nurbs->flags),
	                   FLAGS_GET_M(nurbs->flags),
	                   &pt);

	lwgeom_free(lwgeom);
	result = geometry_serialize((LWGEOM*)lwpt);
	lwpoint_free(lwpt);

	PG_RETURN_POINTER(result);
}

/**
 * ST_NurbsEndPoint(nurbscurve geometry)
 * Get NURBS curve end point
 */
PG_FUNCTION_INFO_V1(lwgeom_nurbs_endpoint);
Datum lwgeom_nurbs_endpoint(PG_FUNCTION_ARGS)
{
	GSERIALIZED *pnurbs;
	LWGEOM *lwgeom;
	LWNURBSCURVE *nurbs;
	POINT4D pt;
	LWPOINT *lwpt;
	GSERIALIZED *result;

	pnurbs = PG_GETARG_GSERIALIZED_P(0);
	lwgeom = lwgeom_from_gserialized(pnurbs);

	if (lwgeom->type != NURBSCURVETYPE)
	{
		lwgeom_free(lwgeom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Geometry must be a NURBS curve")));
	}

	nurbs = (LWNURBSCURVE*)lwgeom;

	if (lwnurbs_endpoint(nurbs, &pt) != LW_SUCCESS)
	{
		lwgeom_free(lwgeom);
		PG_RETURN_NULL();
	}

	lwpt = lwpoint_make(nurbs->srid,
	                   FLAGS_GET_Z(nurbs->flags),
	                   FLAGS_GET_M(nurbs->flags),
	                   &pt);

	lwgeom_free(lwgeom);
	result = geometry_serialize((LWGEOM*)lwpt);
	lwpoint_free(lwpt);

	PG_RETURN_POINTER(result);
}

/**
 * ST_NurbsIsClosed(nurbscurve geometry)
 * Check if NURBS curve is closed
 */
PG_FUNCTION_INFO_V1(lwgeom_nurbs_isclosed);
Datum lwgeom_nurbs_isclosed(PG_FUNCTION_ARGS)
{
	GSERIALIZED *pnurbs;
	LWGEOM *lwgeom;
	LWNURBSCURVE *nurbs;
	int is_closed;

	pnurbs = PG_GETARG_GSERIALIZED_P(0);
	lwgeom = lwgeom_from_gserialized(pnurbs);

	if (lwgeom->type != NURBSCURVETYPE)
	{
		lwgeom_free(lwgeom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Geometry must be a NURBS curve")));
	}

	nurbs = (LWNURBSCURVE*)lwgeom;
	is_closed = lwnurbs_is_closed(nurbs);
	lwgeom_free(lwgeom);

	PG_RETURN_BOOL(is_closed);
}

/**
 * ST_NurbsIsValid(nurbscurve geometry)
 * Check if NURBS curve is valid
 */
PG_FUNCTION_INFO_V1(lwgeom_nurbs_isvalid);
Datum lwgeom_nurbs_isvalid(PG_FUNCTION_ARGS)
{
	GSERIALIZED *pnurbs;
	LWGEOM *lwgeom;
	LWNURBSCURVE *nurbs;
	int is_valid;

	pnurbs = PG_GETARG_GSERIALIZED_P(0);
	lwgeom = lwgeom_from_gserialized(pnurbs);

	if (lwgeom->type != NURBSCURVETYPE)
	{
		lwgeom_free(lwgeom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Geometry must be a NURBS curve")));
	}

	nurbs = (LWNURBSCURVE*)lwgeom;
	is_valid = lwnurbs_is_valid(nurbs);
	lwgeom_free(lwgeom);

	PG_RETURN_BOOL(is_valid);
}

/**
 * ST_NurbsDegree(nurbscurve geometry)
 * Get NURBS curve degree
 */
PG_FUNCTION_INFO_V1(lwgeom_nurbs_degree);
Datum lwgeom_nurbs_degree(PG_FUNCTION_ARGS)
{
	GSERIALIZED *pnurbs;
	LWGEOM *lwgeom;
	LWNURBSCURVE *nurbs;
	uint32_t degree;

	pnurbs = PG_GETARG_GSERIALIZED_P(0);
	lwgeom = lwgeom_from_gserialized(pnurbs);

	if (lwgeom->type != NURBSCURVETYPE)
	{
		lwgeom_free(lwgeom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Geometry must be a NURBS curve")));
	}

	nurbs = (LWNURBSCURVE*)lwgeom;
	degree = lwnurbs_get_degree(nurbs);
	lwgeom_free(lwgeom);

	PG_RETURN_INT32(degree);
}

/**
 * ST_NurbsNPoints(nurbscurve geometry)
 * Get number of control points in NURBS curve
 */
PG_FUNCTION_INFO_V1(lwgeom_nurbs_npoints);
Datum lwgeom_nurbs_npoints(PG_FUNCTION_ARGS)
{
	GSERIALIZED *pnurbs;
	LWGEOM *lwgeom;
	LWNURBSCURVE *nurbs;
	uint32_t npoints;

	pnurbs = PG_GETARG_GSERIALIZED_P(0);
	lwgeom = lwgeom_from_gserialized(pnurbs);

	if (lwgeom->type != NURBSCURVETYPE)
	{
		lwgeom_free(lwgeom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Geometry must be a NURBS curve")));
	}

	nurbs = (LWNURBSCURVE*)lwgeom;
	npoints = lwnurbs_get_npoints(nurbs);
	lwgeom_free(lwgeom);

	PG_RETURN_INT32(npoints);
}
