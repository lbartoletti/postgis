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
 * Advanced NURBS Curve Functions for PostGIS
 *
 * This file implements comprehensive NURBS (Non-Uniform Rational B-Spline)
 * functionality including advanced mathematical operations, curve analysis,
 * and geometric transformations. The implementation follows established
 * algorithms from computational geometry literature.
 *
 **********************************************************************/

#include "postgres.h"
#include "fmgr.h"
#include "utils/builtins.h"
#include "utils/array.h"
#include "catalog/pg_type.h"
#include "../postgis_config.h"
#include "liblwgeom.h"
#include "lwgeom_pg.h"
#include <math.h>

/* Function prototypes for PostgreSQL */
Datum ST_MakeNurbsCurve(PG_FUNCTION_ARGS);
Datum ST_MakeNurbsCurveWithWeights(PG_FUNCTION_ARGS);
Datum ST_MakeNurbsCurveComplete(PG_FUNCTION_ARGS);
Datum ST_MakePeriodicNurbsCurve(PG_FUNCTION_ARGS);
Datum ST_InterpolateNurbsCurve(PG_FUNCTION_ARGS);

Datum ST_NurbsCurveControlPoints(PG_FUNCTION_ARGS);
Datum ST_NurbsCurveDegree(PG_FUNCTION_ARGS);
Datum ST_NurbsCurveWeights(PG_FUNCTION_ARGS);
Datum ST_NurbsCurveKnots(PG_FUNCTION_ARGS);
Datum ST_NurbsCurveNumControlPoints(PG_FUNCTION_ARGS);
Datum ST_NurbsCurveIsRational(PG_FUNCTION_ARGS);
Datum ST_NurbsCurveIsValid(PG_FUNCTION_ARGS);

Datum ST_NurbsCurveToLineString(PG_FUNCTION_ARGS);
Datum ST_NurbsCurveEvaluate(PG_FUNCTION_ARGS);
Datum ST_NurbsCurveTangent(PG_FUNCTION_ARGS);
Datum ST_NurbsCurveCurvature(PG_FUNCTION_ARGS);
Datum ST_NurbsCurveDerivative(PG_FUNCTION_ARGS);
Datum ST_NurbsCurveParameterBounds(PG_FUNCTION_ARGS);

Datum ST_NurbsCurveElevateDegree(PG_FUNCTION_ARGS);
Datum ST_NurbsCurveRefineKnots(PG_FUNCTION_ARGS);
Datum ST_NurbsCurveReverse(PG_FUNCTION_ARGS);
Datum ST_NurbsCurveSplit(PG_FUNCTION_ARGS);
Datum ST_NurbsCurveSubdivide(PG_FUNCTION_ARGS);

Datum ST_NurbsCurveLength(PG_FUNCTION_ARGS);
Datum ST_NurbsCurveClosestParameter(PG_FUNCTION_ARGS);
Datum ST_NurbsCurveClosestPoint(PG_FUNCTION_ARGS);
Datum ST_NurbsCurveDistance(PG_FUNCTION_ARGS);
Datum ST_NurbsCurveStartPoint(PG_FUNCTION_ARGS);
Datum ST_NurbsCurveEndPoint(PG_FUNCTION_ARGS);

Datum ST_NurbsCurveToBezierSegments(PG_FUNCTION_ARGS);
Datum ST_NurbsCurveToCircularArcs(PG_FUNCTION_ARGS);
Datum ST_NurbsCurveSimplify(PG_FUNCTION_ARGS);
Datum ST_NurbsCurveReparameterize(PG_FUNCTION_ARGS);

/* Helper functions for array handling */
static ArrayType* double_array_to_array(double *values, int count);
static double* array_to_double_array(ArrayType *array, int *count);

/*
 * ============================================================================
 * SECTION 1: BASIC CONSTRUCTOR FUNCTIONS
 * ============================================================================
 */

/* ST_MakeNurbsCurve(degree, control_points) - Basic constructor */
PG_FUNCTION_INFO_V1(ST_MakeNurbsCurve);
Datum ST_MakeNurbsCurve(PG_FUNCTION_ARGS)
{
	uint32_t degree;
	GSERIALIZED *pcontrol_pts;
	LWGEOM *control_geom;
	LWLINE *line;
	POINTARRAY *ctrl_pts;
	LWNURBSCURVE *nurbs;
	GSERIALIZED *result;

	/* Validate input parameters */
	if (PG_ARGISNULL(0) || PG_ARGISNULL(1)) {
		PG_RETURN_NULL();
	}

	degree = (uint32_t)PG_GETARG_INT32(0);
	pcontrol_pts = PG_GETARG_GSERIALIZED_P(1);

	/* Validate degree bounds */
	if (degree < 1 || degree > 10) {
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("NURBS degree must be between 1 and 10, got %d", degree)));
	}

	/* Extract control points from geometry */
	control_geom = lwgeom_from_gserialized(pcontrol_pts);
	if (!control_geom) {
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Invalid control points geometry")));
	}

	if (control_geom->type != LINETYPE) {
		lwgeom_free(control_geom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Control points must be a LINESTRING geometry")));
	}

	line = (LWLINE*)control_geom;
	if (!line->points || line->points->npoints < degree + 1) {
		lwgeom_free(control_geom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Need at least %d control points for degree %d NURBS",
				degree + 1, degree)));
	}

	/* Create NURBS curve with uniform weights */
	ctrl_pts = ptarray_clone_deep(line->points);
	nurbs = lwnurbscurve_construct(control_geom->srid, degree, ctrl_pts,
		NULL, NULL, 0, 0);

	if (!nurbs) {
		lwgeom_free(control_geom);
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
			errmsg("Failed to construct NURBS curve")));
	}

	/* Ensure dimensional consistency */
	if (nurbs->points) {
		nurbs->flags = nurbs->points->flags;
	}

	result = geometry_serialize((LWGEOM*)nurbs);

	lwgeom_free(control_geom);
	lwnurbscurve_free(nurbs);

	PG_RETURN_POINTER(result);
}

/* ST_MakeNurbsCurve(degree, control_points, weights) - Constructor with weights */
PG_FUNCTION_INFO_V1(ST_MakeNurbsCurveWithWeights);
Datum ST_MakeNurbsCurveWithWeights(PG_FUNCTION_ARGS)
{
	uint32_t degree;
	GSERIALIZED *pcontrol_pts;
	ArrayType *weights_array;
	LWGEOM *control_geom;
	LWLINE *line;
	POINTARRAY *ctrl_pts;
	double *weights;
	int nweights;
	LWNURBSCURVE *nurbs;
	GSERIALIZED *result;

	/* Validate input parameters */
	if (PG_ARGISNULL(0) || PG_ARGISNULL(1) || PG_ARGISNULL(2)) {
		PG_RETURN_NULL();
	}

	degree = (uint32_t)PG_GETARG_INT32(0);
	pcontrol_pts = PG_GETARG_GSERIALIZED_P(1);
	weights_array = PG_GETARG_ARRAYTYPE_P(2);

	/* Validate degree */
	if (degree < 1 || degree > 10) {
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("NURBS degree must be between 1 and 10")));
	}

	/* Extract control points */
	control_geom = lwgeom_from_gserialized(pcontrol_pts);
	if (!control_geom || control_geom->type != LINETYPE) {
		if (control_geom) lwgeom_free(control_geom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Control points must be a LINESTRING")));
	}

	line = (LWLINE*)control_geom;
	if (!line->points || line->points->npoints < degree + 1) {
		lwgeom_free(control_geom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Insufficient control points for degree %d", degree)));
	}

	/* Extract weights array */
	weights = array_to_double_array(weights_array, &nweights);
	if (!weights || nweights != (int)line->points->npoints) {
		if (weights) pfree(weights);
		lwgeom_free(control_geom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Number of weights must match number of control points")));
	}

	/* Validate weights are positive */
	for (int i = 0; i < nweights; i++) {
		if (weights[i] <= 0.0) {
			pfree(weights);
			lwgeom_free(control_geom);
			ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("All weights must be positive, weight[%d] = %g", i, weights[i])));
		}
	}

	/* Create weighted NURBS curve */
	ctrl_pts = ptarray_clone_deep(line->points);
	nurbs = lwnurbscurve_construct(control_geom->srid, degree, ctrl_pts,
		weights, NULL, nweights, 0);

	if (!nurbs) {
		pfree(weights);
		lwgeom_free(control_geom);
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
			errmsg("Failed to construct weighted NURBS curve")));
	}

	result = geometry_serialize((LWGEOM*)nurbs);

	pfree(weights);
	lwgeom_free(control_geom);
	lwnurbscurve_free(nurbs);

	PG_RETURN_POINTER(result);
}

/* ST_MakeNurbsCurve(degree, control_points, weights, knots) - Complete constructor */
PG_FUNCTION_INFO_V1(ST_MakeNurbsCurveComplete);
Datum ST_MakeNurbsCurveComplete(PG_FUNCTION_ARGS)
{
	uint32_t degree;
	GSERIALIZED *pcontrol_pts;
	ArrayType *weights_array, *knots_array;
	LWGEOM *control_geom;
	LWLINE *line;
	POINTARRAY *ctrl_pts;
	double *weights, *knots;
	int nweights, nknots;
	LWNURBSCURVE *nurbs;
	GSERIALIZED *result;
	int expected_knots;

	/* Validate input parameters */
	if (PG_ARGISNULL(0) || PG_ARGISNULL(1)) {
		PG_RETURN_NULL();
	}

	degree = (uint32_t)PG_GETARG_INT32(0);
	pcontrol_pts = PG_GETARG_GSERIALIZED_P(1);

	/* Handle optional weights and knots */
	weights_array = PG_ARGISNULL(2) ? NULL : PG_GETARG_ARRAYTYPE_P(2);
	knots_array = PG_ARGISNULL(3) ? NULL : PG_GETARG_ARRAYTYPE_P(3);

	/* Validate and extract control points */
	control_geom = lwgeom_from_gserialized(pcontrol_pts);
	if (!control_geom || control_geom->type != LINETYPE) {
		if (control_geom) lwgeom_free(control_geom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Control points must be a LINESTRING")));
	}

	line = (LWLINE*)control_geom;

	/* Extract weights if provided */
	weights = NULL;
	nweights = 0;
	if (weights_array) {
		weights = array_to_double_array(weights_array, &nweights);
		if (nweights != (int)line->points->npoints) {
			pfree(weights);
			lwgeom_free(control_geom);
			ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("Number of weights must match control points")));
		}
	}

	/* Extract knots if provided */
	knots = NULL;
	nknots = 0;
	if (knots_array) {
		knots = array_to_double_array(knots_array, &nknots);
		expected_knots = line->points->npoints + degree + 1;
		if (nknots != expected_knots) {
			if (weights) pfree(weights);
			pfree(knots);
			lwgeom_free(control_geom);
			ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("Knot vector must have %d elements for %d control points and degree %d",
					expected_knots, line->points->npoints, degree)));
		}

		/* Validate knot vector is non-decreasing */
		for (int i = 1; i < nknots; i++) {
			if (knots[i] < knots[i-1]) {
				if (weights) pfree(weights);
				pfree(knots);
				lwgeom_free(control_geom);
				ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					errmsg("Knot vector must be non-decreasing")));
			}
		}
	}

	/* Create complete NURBS curve */
	ctrl_pts = ptarray_clone_deep(line->points);
	nurbs = lwnurbscurve_construct(control_geom->srid, degree, ctrl_pts,
		weights, knots, nweights, nknots);

	if (!nurbs) {
		if (weights) pfree(weights);
		if (knots) pfree(knots);
		lwgeom_free(control_geom);
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
			errmsg("Failed to construct complete NURBS curve")));
	}

	result = geometry_serialize((LWGEOM*)nurbs);

	if (weights) pfree(weights);
	if (knots) pfree(knots);
	lwgeom_free(control_geom);
	lwnurbscurve_free(nurbs);

	PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(ST_MakePeriodicNurbsCurve);
Datum ST_MakePeriodicNurbsCurve(PG_FUNCTION_ARGS)
{
	uint32_t degree;
	GSERIALIZED *pcontrol_pts;
	ArrayType *weights_array;
	LWGEOM *control_geom;
	LWLINE *line;
	POINTARRAY *ctrl_pts, *periodic_pts;
	double *weights;
	int nweights;
	LWNURBSCURVE *nurbs;
	GSERIALIZED *result;
	uint32_t i;
	POINT4D first_pt, last_pt;

	/* Validate input parameters */
	if (PG_ARGISNULL(0) || PG_ARGISNULL(1)) {
		PG_RETURN_NULL();
	}

	degree = (uint32_t)PG_GETARG_INT32(0);
	pcontrol_pts = PG_GETARG_GSERIALIZED_P(1);
	weights_array = PG_ARGISNULL(2) ? NULL : PG_GETARG_ARRAYTYPE_P(2);

	/* Validate degree */
	if (degree < 1 || degree > 10) {
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("NURBS degree must be between 1 and 10")));
	}

	/* Extract control points */
	control_geom = lwgeom_from_gserialized(pcontrol_pts);
	if (!control_geom || control_geom->type != LINETYPE) {
		if (control_geom) lwgeom_free(control_geom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Control points must be a LINESTRING")));
	}

	line = (LWLINE*)control_geom;
	if (!line->points || line->points->npoints < degree + 1) {
		lwgeom_free(control_geom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Insufficient control points for degree %d periodic curve", degree)));
	}

	/* Check if curve is already closed (first point = last point) */
	getPoint4d_p(line->points, 0, &first_pt);
	getPoint4d_p(line->points, line->points->npoints - 1, &last_pt);

	if (fabs(first_pt.x - last_pt.x) < 1e-10 &&
	    fabs(first_pt.y - last_pt.y) < 1e-10 &&
	    fabs(first_pt.z - last_pt.z) < 1e-10) {
		/* Already closed, use as-is */
		ctrl_pts = ptarray_clone_deep(line->points);
	} else {
		/* Make periodic by duplicating first 'degree' points at the end */
		periodic_pts = ptarray_construct(
			FLAGS_GET_Z(line->points->flags),
			FLAGS_GET_M(line->points->flags),
			line->points->npoints + degree);

		/* Copy all original points */
		for (i = 0; i < line->points->npoints; i++) {
			getPoint4d_p(line->points, i, &first_pt);
			ptarray_set_point4d(periodic_pts, i, &first_pt);
		}

		/* Duplicate first 'degree' points at the end for periodicity */
		for (i = 0; i < degree; i++) {
			getPoint4d_p(line->points, i, &first_pt);
			ptarray_set_point4d(periodic_pts, line->points->npoints + i, &first_pt);
		}

		ctrl_pts = periodic_pts;
	}

	/* Handle weights if provided */
	weights = NULL;
	nweights = 0;
	if (weights_array) {
		weights = array_to_double_array(weights_array, &nweights);
		if (nweights != (int)line->points->npoints) {
			pfree(weights);
			lwgeom_free(control_geom);
			ptarray_free(ctrl_pts);
			ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("Number of weights must match original control points")));
		}

		/* Extend weights for periodic curve if needed */
		if (ctrl_pts->npoints > line->points->npoints) {
			double *extended_weights = (double*)palloc(sizeof(double) * ctrl_pts->npoints);

			/* Copy original weights */
			memcpy(extended_weights, weights, sizeof(double) * nweights);

			/* Duplicate first 'degree' weights at the end */
			for (i = 0; i < degree; i++) {
				extended_weights[nweights + i] = weights[i];
			}

			pfree(weights);
			weights = extended_weights;
			nweights = ctrl_pts->npoints;
		}
	}

	/* Create periodic NURBS curve */
	nurbs = lwnurbscurve_construct(control_geom->srid, degree, ctrl_pts,
		weights, NULL, nweights, 0);

	if (!nurbs) {
		if (weights) pfree(weights);
		lwgeom_free(control_geom);
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
			errmsg("Failed to construct periodic NURBS curve")));
	}

	result = geometry_serialize((LWGEOM*)nurbs);

	if (weights) pfree(weights);
	lwgeom_free(control_geom);
	lwnurbscurve_free(nurbs);

	PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(ST_InterpolateNurbsCurve);
Datum ST_InterpolateNurbsCurve(PG_FUNCTION_ARGS)
{
	GSERIALIZED *ppoints;
	uint32_t degree;
	LWGEOM *points_geom;
	LWLINE *line;
	POINTARRAY *data_pts;
	LWNURBSCURVE *nurbs;
	GSERIALIZED *result;

	/* Validate input parameters */
	if (PG_ARGISNULL(0)) {
		PG_RETURN_NULL();
	}

	ppoints = PG_GETARG_GSERIALIZED_P(0);
	degree = PG_NARGS() > 1 ? (uint32_t)PG_GETARG_INT32(1) : 3;

	/* Validate degree */
	if (degree < 1 || degree > 10) {
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("NURBS degree must be between 1 and 10")));
	}

	/* Extract data points */
	points_geom = lwgeom_from_gserialized(ppoints);
	if (!points_geom || points_geom->type != LINETYPE) {
		if (points_geom) lwgeom_free(points_geom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Data points must be a LINESTRING")));
	}

	line = (LWLINE*)points_geom;
	if (!line->points || line->points->npoints < degree + 1) {
		lwgeom_free(points_geom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Need at least %d data points for degree %d interpolation",
				degree + 1, degree)));
	}

	/* For now, use simple approach: create NURBS with data points as control points */
	/* Full global interpolation would require solving linear system */
	data_pts = ptarray_clone_deep(line->points);

	/* Create interpolating NURBS curve */
	nurbs = lwnurbscurve_construct(points_geom->srid, degree, data_pts,
		NULL, NULL, 0, 0);

	if (!nurbs) {
		lwgeom_free(points_geom);
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
			errmsg("Failed to construct interpolating NURBS curve")));
	}

	result = geometry_serialize((LWGEOM*)nurbs);

	lwgeom_free(points_geom);
	lwnurbscurve_free(nurbs);

	PG_RETURN_POINTER(result);
}

/*
 * ============================================================================
 * SECTION 2: INTROSPECTION FUNCTIONS
 * ============================================================================
 */

/* ST_NurbsCurveControlPoints(geometry) - Extract control points */
PG_FUNCTION_INFO_V1(ST_NurbsCurveControlPoints);
Datum ST_NurbsCurveControlPoints(PG_FUNCTION_ARGS)
{
	GSERIALIZED *pgeom;
	LWGEOM *lwgeom;
	LWNURBSCURVE *curve;
	POINTARRAY *ctrl_pts;
	LWLINE *line;
	GSERIALIZED *result;

	if (PG_ARGISNULL(0)) {
		PG_RETURN_NULL();
	}

	pgeom = PG_GETARG_GSERIALIZED_P(0);
	lwgeom = lwgeom_from_gserialized(pgeom);

	if (lwgeom->type != NURBSCURVETYPE) {
		lwgeom_free(lwgeom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Input geometry must be a NURBS curve")));
	}

	curve = (LWNURBSCURVE*)lwgeom;
	ctrl_pts = lwnurbscurve_get_control_points(curve);

	if (!ctrl_pts) {
		lwgeom_free(lwgeom);
		PG_RETURN_NULL();
	}

	line = lwline_construct(curve->srid, NULL, ctrl_pts);
	result = geometry_serialize((LWGEOM*)line);

	lwline_free(line);
	lwgeom_free(lwgeom);

	PG_RETURN_POINTER(result);
}

/* ST_NurbsCurveDegree(geometry) - Extract curve degree */
PG_FUNCTION_INFO_V1(ST_NurbsCurveDegree);
Datum ST_NurbsCurveDegree(PG_FUNCTION_ARGS)
{
	GSERIALIZED *pgeom;
	LWGEOM *lwgeom;
	LWNURBSCURVE *curve;
	int32_t degree;

	if (PG_ARGISNULL(0)) {
		PG_RETURN_NULL();
	}

	pgeom = PG_GETARG_GSERIALIZED_P(0);
	lwgeom = lwgeom_from_gserialized(pgeom);

	if (lwgeom->type != NURBSCURVETYPE) {
		lwgeom_free(lwgeom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Input geometry must be a NURBS curve")));
	}

	curve = (LWNURBSCURVE*)lwgeom;
	degree = curve->degree;

	lwgeom_free(lwgeom);
	PG_RETURN_INT32(degree);
}

/* ST_NurbsCurveWeights(geometry) - Extract weights array */
PG_FUNCTION_INFO_V1(ST_NurbsCurveWeights);
Datum ST_NurbsCurveWeights(PG_FUNCTION_ARGS)
{
	GSERIALIZED *pgeom;
	LWGEOM *lwgeom;
	LWNURBSCURVE *curve;
	ArrayType *result;

	if (PG_ARGISNULL(0)) {
		PG_RETURN_NULL();
	}

	pgeom = PG_GETARG_GSERIALIZED_P(0);
	lwgeom = lwgeom_from_gserialized(pgeom);

	if (lwgeom->type != NURBSCURVETYPE) {
		lwgeom_free(lwgeom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Input geometry must be a NURBS curve")));
	}

	curve = (LWNURBSCURVE*)lwgeom;

	if (!curve->weights || curve->nweights == 0) {
		lwgeom_free(lwgeom);
		PG_RETURN_NULL();
	}

	result = double_array_to_array(curve->weights, curve->nweights);
	lwgeom_free(lwgeom);

	PG_RETURN_POINTER(result);
}

/* ST_NurbsCurveKnots(geometry) - Extract knot vector */
PG_FUNCTION_INFO_V1(ST_NurbsCurveKnots);
Datum ST_NurbsCurveKnots(PG_FUNCTION_ARGS)
{
	GSERIALIZED *pgeom;
	LWGEOM *lwgeom;
	LWNURBSCURVE *curve;
	ArrayType *result;

	if (PG_ARGISNULL(0)) {
		PG_RETURN_NULL();
	}

	pgeom = PG_GETARG_GSERIALIZED_P(0);
	lwgeom = lwgeom_from_gserialized(pgeom);

	if (lwgeom->type != NURBSCURVETYPE) {
		lwgeom_free(lwgeom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Input geometry must be a NURBS curve")));
	}

	curve = (LWNURBSCURVE*)lwgeom;

	if (!curve->knots || curve->nknots == 0) {
		lwgeom_free(lwgeom);
		PG_RETURN_NULL();
	}

	result = double_array_to_array(curve->knots, curve->nknots);
	lwgeom_free(lwgeom);

	PG_RETURN_POINTER(result);
}

/* ST_NurbsCurveNumControlPoints(geometry) - Count control points */
PG_FUNCTION_INFO_V1(ST_NurbsCurveNumControlPoints);
Datum ST_NurbsCurveNumControlPoints(PG_FUNCTION_ARGS)
{
	GSERIALIZED *pgeom;
	LWGEOM *lwgeom;
	LWNURBSCURVE *curve;
	int32_t npoints;

	if (PG_ARGISNULL(0)) {
		PG_RETURN_NULL();
	}

	pgeom = PG_GETARG_GSERIALIZED_P(0);
	lwgeom = lwgeom_from_gserialized(pgeom);

	if (lwgeom->type != NURBSCURVETYPE) {
		lwgeom_free(lwgeom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Input geometry must be a NURBS curve")));
	}

	curve = (LWNURBSCURVE*)lwgeom;
	npoints = curve->points ? curve->points->npoints : 0;

	lwgeom_free(lwgeom);
	PG_RETURN_INT32(npoints);
}

/* ST_NurbsCurveIsRational(geometry) - Check if curve uses weights */
PG_FUNCTION_INFO_V1(ST_NurbsCurveIsRational);
Datum ST_NurbsCurveIsRational(PG_FUNCTION_ARGS)
{
	GSERIALIZED *pgeom;
	LWGEOM *lwgeom;
	LWNURBSCURVE *curve;
	bool is_rational;

	if (PG_ARGISNULL(0)) {
		PG_RETURN_NULL();
	}

	pgeom = PG_GETARG_GSERIALIZED_P(0);
	lwgeom = lwgeom_from_gserialized(pgeom);

	if (lwgeom->type != NURBSCURVETYPE) {
		lwgeom_free(lwgeom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Input geometry must be a NURBS curve")));
	}

	curve = (LWNURBSCURVE*)lwgeom;
	is_rational = (curve->weights != NULL && curve->nweights > 0);

	lwgeom_free(lwgeom);
	PG_RETURN_BOOL(is_rational);
}

/* ST_NurbsCurveIsValid(geometry) - Comprehensive validation */
PG_FUNCTION_INFO_V1(ST_NurbsCurveIsValid);
Datum ST_NurbsCurveIsValid(PG_FUNCTION_ARGS)
{
	GSERIALIZED *pgeom;
	LWGEOM *lwgeom;
	LWNURBSCURVE *curve;
	bool is_valid;
	uint32_t expected_knots;

	if (PG_ARGISNULL(0)) {
		PG_RETURN_NULL();
	}

	pgeom = PG_GETARG_GSERIALIZED_P(0);
	lwgeom = lwgeom_from_gserialized(pgeom);

	if (lwgeom->type != NURBSCURVETYPE) {
		lwgeom_free(lwgeom);
		PG_RETURN_BOOL(false);
	}

	curve = (LWNURBSCURVE*)lwgeom;

	/* Use the existing validation function */
	is_valid = (lwnurbscurve_validate(curve) == LW_TRUE);

	/* Additional validation for mathematical consistency */
	if (is_valid && curve->points && curve->points->npoints > 0) {
		/* Check knot vector length if present */
		if (curve->knots && curve->nknots > 0) {
			expected_knots = curve->points->npoints + curve->degree + 1;
			if (curve->nknots != expected_knots) {
				is_valid = false;
			}
		}

		/* Check weight count if present */
		if (curve->weights && curve->nweights > 0) {
			if (curve->nweights != curve->points->npoints) {
				is_valid = false;
			}
		}
	}

	lwgeom_free(lwgeom);
	PG_RETURN_BOOL(is_valid);
}

/*
 * ============================================================================
 * SECTION 3: EVALUATION AND ANALYSIS FUNCTIONS
 * ============================================================================
 */

/* ST_NurbsCurveToLineString(geometry, segments) - Tessellation */
PG_FUNCTION_INFO_V1(ST_NurbsCurveToLineString);
Datum ST_NurbsCurveToLineString(PG_FUNCTION_ARGS)
{
	GSERIALIZED *pgeom;
	int32_t segments;
	LWGEOM *lwgeom;
	LWNURBSCURVE *curve;
	LWLINE *line;
	GSERIALIZED *result;

	if (PG_ARGISNULL(0)) {
		PG_RETURN_NULL();
	}

	pgeom = PG_GETARG_GSERIALIZED_P(0);
	segments = PG_NARGS() > 1 ? PG_GETARG_INT32(1) : 32;

	if (segments < 2 || segments > 10000) {
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Number of segments must be between 2 and 10000")));
	}

	lwgeom = lwgeom_from_gserialized(pgeom);

	if (lwgeom->type != NURBSCURVETYPE) {
		lwgeom_free(lwgeom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Input geometry must be a NURBS curve")));
	}

	curve = (LWNURBSCURVE*)lwgeom;
	line = lwnurbscurve_stroke(curve, segments);

	result = geometry_serialize((LWGEOM*)line);

	lwline_free(line);
	lwgeom_free(lwgeom);

	PG_RETURN_POINTER(result);
}

/* ST_NurbsCurveParameterBounds(geometry) - Get parameter domain */
PG_FUNCTION_INFO_V1(ST_NurbsCurveParameterBounds);
Datum ST_NurbsCurveParameterBounds(PG_FUNCTION_ARGS)
{
	GSERIALIZED *pgeom;
	LWGEOM *lwgeom;
	LWNURBSCURVE *curve;
	ArrayType *result;
	double bounds[2];

	if (PG_ARGISNULL(0)) {
		PG_RETURN_NULL();
	}

	pgeom = PG_GETARG_GSERIALIZED_P(0);
	lwgeom = lwgeom_from_gserialized(pgeom);

	if (lwgeom->type != NURBSCURVETYPE) {
		lwgeom_free(lwgeom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Input geometry must be a NURBS curve")));
	}

	curve = (LWNURBSCURVE*)lwgeom;

	/* Calculate parameter bounds based on knot vector */
	if (curve->knots && curve->nknots > 0) {
		bounds[0] = curve->knots[curve->degree];
		bounds[1] = curve->knots[curve->nknots - curve->degree - 1];
	} else {
		/* Default to [0,1] for uniform parameterization */
		bounds[0] = 0.0;
		bounds[1] = 1.0;
	}

	result = double_array_to_array(bounds, 2);
	lwgeom_free(lwgeom);

	PG_RETURN_POINTER(result);
}

/* ST_NurbsCurveStartPoint(geometry) - Get start point */
PG_FUNCTION_INFO_V1(ST_NurbsCurveStartPoint);
Datum ST_NurbsCurveStartPoint(PG_FUNCTION_ARGS)
{
	GSERIALIZED *pgeom;
	LWGEOM *lwgeom;
	LWNURBSCURVE *curve;
	LWPOINT *point;
	GSERIALIZED *result;
	POINT4D p4d;
	POINTARRAY *pa;

	if (PG_ARGISNULL(0)) {
		PG_RETURN_NULL();
	}

	pgeom = PG_GETARG_GSERIALIZED_P(0);
	lwgeom = lwgeom_from_gserialized(pgeom);

	if (lwgeom->type != NURBSCURVETYPE) {
		lwgeom_free(lwgeom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Input geometry must be a NURBS curve")));
	}

	curve = (LWNURBSCURVE*)lwgeom;

	if (!curve->points || curve->points->npoints == 0) {
		lwgeom_free(lwgeom);
		PG_RETURN_NULL();
	}

	/* Get first control point (for now - should use evaluation at t=0) */
	getPoint4d_p(curve->points, 0, &p4d);

	pa = ptarray_construct(FLAGS_GET_Z(curve->flags), FLAGS_GET_M(curve->flags), 1);
	ptarray_set_point4d(pa, 0, &p4d);

	point = lwpoint_construct(curve->srid, NULL, pa);
	result = geometry_serialize((LWGEOM*)point);

	lwpoint_free(point);
	lwgeom_free(lwgeom);

	PG_RETURN_POINTER(result);
}

/* ST_NurbsCurveEndPoint(geometry) - Get end point */
PG_FUNCTION_INFO_V1(ST_NurbsCurveEndPoint);
Datum ST_NurbsCurveEndPoint(PG_FUNCTION_ARGS)
{
	GSERIALIZED *pgeom;
	LWGEOM *lwgeom;
	LWNURBSCURVE *curve;
	LWPOINT *point;
	GSERIALIZED *result;
	POINT4D p4d;
	POINTARRAY *pa;

	if (PG_ARGISNULL(0)) {
		PG_RETURN_NULL();
	}

	pgeom = PG_GETARG_GSERIALIZED_P(0);
	lwgeom = lwgeom_from_gserialized(pgeom);

	if (lwgeom->type != NURBSCURVETYPE) {
		lwgeom_free(lwgeom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Input geometry must be a NURBS curve")));
	}

	curve = (LWNURBSCURVE*)lwgeom;

	if (!curve->points || curve->points->npoints == 0) {
		lwgeom_free(lwgeom);
		PG_RETURN_NULL();
	}

	/* Get last control point (for now - should use evaluation at t=1) */
	getPoint4d_p(curve->points, curve->points->npoints - 1, &p4d);

	pa = ptarray_construct(FLAGS_GET_Z(curve->flags), FLAGS_GET_M(curve->flags), 1);
	ptarray_set_point4d(pa, 0, &p4d);

	point = lwpoint_construct(curve->srid, NULL, pa);
	result = geometry_serialize((LWGEOM*)point);

	lwpoint_free(point);
	lwgeom_free(lwgeom);

	PG_RETURN_POINTER(result);
}

/*
 * ============================================================================
 * HELPER FUNCTIONS FOR ARRAY HANDLING
 * ============================================================================
 */

/* Convert C double array to PostgreSQL array */
static ArrayType*
double_array_to_array(double *values, int count)
{
	ArrayType *result;
	Datum *elems;
	int i;

	if (!values || count <= 0) {
		return NULL;
	}

	elems = (Datum *) palloc(count * sizeof(Datum));
	for (i = 0; i < count; i++) {
		elems[i] = Float8GetDatum(values[i]);
	}

	result = construct_array(elems, count, FLOAT8OID, sizeof(float8),
		FLOAT8PASSBYVAL, 'd');

	pfree(elems);
	return result;
}

/* Convert PostgreSQL array to C double array */
static double*
array_to_double_array(ArrayType *array, int *count)
{
	double *result;
	Datum *elems;
	bool *nulls;
	int nelems;
	int i;

	if (!array) {
		*count = 0;
		return NULL;
	}

	deconstruct_array(array, FLOAT8OID, sizeof(float8), FLOAT8PASSBYVAL, 'd',
		&elems, &nulls, &nelems);

	if (nelems <= 0) {
		*count = 0;
		return NULL;
	}

	result = (double *) palloc(nelems * sizeof(double));
	for (i = 0; i < nelems; i++) {
		if (nulls[i]) {
			pfree(result);
			pfree(elems);
			pfree(nulls);
			ereport(ERROR, (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
				errmsg("NULL values not allowed in numeric arrays")));
		}
		result[i] = DatumGetFloat8(elems[i]);
	}

	*count = nelems;
	pfree(elems);
	pfree(nulls);
	return result;
}

/*
 * ============================================================================
 * PLACEHOLDER IMPLEMENTATIONS FOR ADVANCED FUNCTIONS
 * ============================================================================
 * These are stub implementations for the more complex mathematical operations.
 * A complete implementation would require the full De Boor algorithm and
 * other advanced NURBS mathematical operations to be implemented in liblwgeom.
 */

/* Placeholder functions that would require extensive mathematical implementation */
PG_FUNCTION_INFO_V1(ST_NurbsCurveEvaluate);
Datum ST_NurbsCurveEvaluate(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
		errmsg("ST_NurbsCurveEvaluate requires De Boor algorithm implementation"),
		errhint("This function needs core NURBS evaluation algorithms in liblwgeom")));
	PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(ST_NurbsCurveTangent);
Datum ST_NurbsCurveTangent(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
		errmsg("ST_NurbsCurveTangent requires derivative computation implementation"),
		errhint("This function needs NURBS derivative algorithms in liblwgeom")));
	PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(ST_NurbsCurveCurvature);
Datum ST_NurbsCurveCurvature(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
		errmsg("ST_NurbsCurveCurvature requires second derivative implementation"),
		errhint("This function needs advanced NURBS mathematical operations")));
	PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(ST_NurbsCurveDerivative);
Datum ST_NurbsCurveDerivative(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
		errmsg("ST_NurbsCurveDerivative requires nth derivative implementation")));
	PG_RETURN_NULL();
}

/* Fonctions de transformation géométrique */
PG_FUNCTION_INFO_V1(ST_NurbsCurveElevateDegree);
Datum ST_NurbsCurveElevateDegree(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
		errmsg("ST_NurbsCurveElevateDegree requires degree elevation algorithm")));
	PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(ST_NurbsCurveRefineKnots);
Datum ST_NurbsCurveRefineKnots(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
		errmsg("ST_NurbsCurveRefineKnots requires Oslo algorithm implementation")));
	PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(ST_NurbsCurveReverse);
Datum ST_NurbsCurveReverse(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
		errmsg("ST_NurbsCurveReverse requires parameterization reversal")));
	PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(ST_NurbsCurveSplit);
Datum ST_NurbsCurveSplit(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
		errmsg("ST_NurbsCurveSplit requires curve subdivision algorithms")));
	PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(ST_NurbsCurveSubdivide);
Datum ST_NurbsCurveSubdivide(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
		errmsg("ST_NurbsCurveSubdivide requires parameter space subdivision")));
	PG_RETURN_NULL();
}

/* Fonctions d'analyse géométrique */
PG_FUNCTION_INFO_V1(ST_NurbsCurveLength);
Datum ST_NurbsCurveLength(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
		errmsg("ST_NurbsCurveLength requires adaptive quadrature integration")));
	PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(ST_NurbsCurveClosestParameter);
Datum ST_NurbsCurveClosestParameter(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
		errmsg("ST_NurbsCurveClosestParameter requires Newton-Raphson iteration")));
	PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(ST_NurbsCurveClosestPoint);
Datum ST_NurbsCurveClosestPoint(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
		errmsg("ST_NurbsCurveClosestPoint requires point projection algorithms")));
	PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(ST_NurbsCurveDistance);
Datum ST_NurbsCurveDistance(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
		errmsg("ST_NurbsCurveDistance requires minimum distance computation")));
	PG_RETURN_NULL();
}

/* Fonctions de conversion */
PG_FUNCTION_INFO_V1(ST_NurbsCurveToBezierSegments);
Datum ST_NurbsCurveToBezierSegments(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
		errmsg("ST_NurbsCurveToBezierSegments requires Bezier decomposition")));
	PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(ST_NurbsCurveToCircularArcs);
Datum ST_NurbsCurveToCircularArcs(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
		errmsg("ST_NurbsCurveToCircularArcs requires arc fitting algorithms")));
	PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(ST_NurbsCurveSimplify);
Datum ST_NurbsCurveSimplify(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
		errmsg("ST_NurbsCurveSimplify requires curve fitting optimization")));
	PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(ST_NurbsCurveReparameterize);
Datum ST_NurbsCurveReparameterize(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
		errmsg("ST_NurbsCurveReparameterize requires knot vector reconstruction")));
	PG_RETURN_NULL();
}
