/*
 * PostGIS NURBS Support - PostgreSQL Functions (Updated)
 * File: liblwgeom/lwgeom_nurbs_functions.c
 *
 * PostgreSQL interface functions that return native NURBS/Bezier geometry types.
 * Preserves control points and curve parameters for SQL/MM compliance.
 *
 * Copyright (c) 2024 PostGIS contributors
 * License: GPL v2+
 */

#include "postgres.h"
#include "fmgr.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "lwgeom_pg.h"
#include "lwgeom_nurbs.h"

PG_FUNCTION_INFO_V1(ST_BezierCurve);
PG_FUNCTION_INFO_V1(ST_NurbsCurve);
PG_FUNCTION_INFO_V1(ST_CurvePoint);
PG_FUNCTION_INFO_V1(ST_CurveToLinestring);
PG_FUNCTION_INFO_V1(ST_IsValidCurve);
PG_FUNCTION_INFO_V1(ST_CurveLength);
PG_FUNCTION_INFO_V1(ST_CurveControlPoints);
PG_FUNCTION_INFO_V1(ST_CurveKnots);

/*
 * Create a Bezier curve from control points
 * ST_BezierCurve(geometry[]) -> BEZIERCURVE
 */
Datum ST_BezierCurve(PG_FUNCTION_ARGS)
{
    ArrayType *array;
    Datum *elems;
    bool *nulls;
    int nelems;
    GSERIALIZED *geom;
    LWGEOM *lwgeom;
    LWPOINT *lwpoint;
    POINT4D *points;
    POINTARRAY *pa;
    LWBEZIERCURVE *curve;
    uint32_t srid = 0;
    uint8_t flags = 0;
    int i;

    if (PG_ARGISNULL(0))
        PG_RETURN_NULL();

    array = PG_GETARG_ARRAYTYPE_P(0);

    /* Deconstruct array */
    deconstruct_array(array, get_fn_expr_argtype(fcinfo->flinfo, 0),
                     -1, false, 'd', &elems, &nulls, &nelems);

    if (nelems < 2 || nelems > 10) {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                       errmsg("Bezier curve requires 2-10 control points")));
    }

    /* Extract points */
    points = lwalloc(nelems * sizeof(POINT4D));
    for (i = 0; i < nelems; i++) {
        if (nulls[i]) {
            ereport(ERROR, (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                           errmsg("Control points cannot be null")));
        }

        geom = (GSERIALIZED *)DatumGetPointer(elems[i]);
        lwgeom = lwgeom_from_gserialized(geom);

        if (lwgeom->type != POINTTYPE) {
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                           errmsg("All elements must be points")));
        }

        lwpoint = (LWPOINT *)lwgeom;
        if (i == 0) {
            srid = lwgeom->srid;
            flags = lwgeom->flags;
        }

        getPoint4d_p(lwpoint->point, 0, &points[i]);
        lwgeom_free(lwgeom);
    }

    /* Create point array with weights (M coordinate) */
    pa = lwnurbs_points_with_weights(points, NULL, nelems, flags);

    /* Create Bezier curve */
    curve = lwbeziercurve_construct(srid, FLAGS_SET_M(flags, 1), nelems - 1, pa);

    /* Cleanup */
    lwfree(points);
    ptarray_free(pa);
    pfree(elems);
    pfree(nulls);

    if (!curve) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("Failed to create Bezier curve")));
    }

    /* Return as serialized geometry */
    geom = geometry_serialize((LWGEOM *)curve);
    PG_RETURN_POINTER(geom);
}

/*
 * Create a NURBS curve from control points, weights, and knots
 * ST_NurbsCurve(geometry[], float8[], float8[]) -> NURBSCURVE
 */
Datum ST_NurbsCurve(PG_FUNCTION_ARGS)
{
    ArrayType *points_array, *weights_array, *knots_array;
    Datum *point_elems, *weight_elems, *knot_elems;
    bool *point_nulls, *weight_nulls, *knot_nulls;
    int npoints, nweights, nknots;
    GSERIALIZED *geom;
    LWGEOM *lwgeom;
    LWPOINT *lwpoint;
    POINT4D *points;
    double *weights, *knots;
    POINTARRAY *pa;
    LWNURBSCURVE *curve;
    uint32_t srid = 0;
    uint8_t flags = 0;
    uint32_t degree;
    int i;

    if (PG_ARGISNULL(0) || PG_ARGISNULL(1) || PG_ARGISNULL(2))
        PG_RETURN_NULL();

    points_array = PG_GETARG_ARRAYTYPE_P(0);
    weights_array = PG_GETARG_ARRAYTYPE_P(1);
    knots_array = PG_GETARG_ARRAYTYPE_P(2);

    /* Deconstruct arrays */
    deconstruct_array(points_array, get_fn_expr_argtype(fcinfo->flinfo, 0),
                     -1, false, 'd', &point_elems, &point_nulls, &npoints);
    deconstruct_array(weights_array, FLOAT8OID,
                     8, FLOAT8PASSBYVAL, 'd', &weight_elems, &weight_nulls, &nweights);
    deconstruct_array(knots_array, FLOAT8OID,
                     8, FLOAT8PASSBYVAL, 'd', &knot_elems, &knot_nulls, &nknots);

    /* Validate array sizes */
    if (npoints < 2) {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                       errmsg("NURBS curve requires at least 2 control points")));
    }

    if (nweights != npoints) {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                       errmsg("Number of weights must match number of points")));
    }

    degree = nknots - npoints - 1;
    if (degree < 1 || degree >= npoints) {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                       errmsg("Invalid knot vector size for NURBS curve")));
    }

    /* Extract points */
    points = lwalloc(npoints * sizeof(POINT4D));
    for (i = 0; i < npoints; i++) {
        if (point_nulls[i]) {
            ereport(ERROR, (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                           errmsg("Control points cannot be null")));
        }

        geom = (GSERIALIZED *)DatumGetPointer(point_elems[i]);
        lwgeom = lwgeom_from_gserialized(geom);

        if (lwgeom->type != POINTTYPE) {
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                           errmsg("All elements must be points")));
        }

        lwpoint = (LWPOINT *)lwgeom;
        if (i == 0) {
            srid = lwgeom->srid;
            flags = lwgeom->flags;
        }

        getPoint4d_p(lwpoint->point, 0, &points[i]);
        lwgeom_free(lwgeom);
    }

    /* Extract weights */
    weights = lwalloc(nweights * sizeof(double));
    for (i = 0; i < nweights; i++) {
        if (weight_nulls[i]) {
            ereport(ERROR, (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                           errmsg("Weights cannot be null")));
        }
        weights[i] = DatumGetFloat8(weight_elems[i]);
        if (weights[i] <= 0.0) {
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                           errmsg("All weights must be positive")));
        }
    }

    /* Extract knots */
    knots = lwalloc(nknots * sizeof(double));
    for (i = 0; i < nknots; i++) {
        if (knot_nulls[i]) {
            ereport(ERROR, (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                           errmsg("Knots cannot be null")));
        }
        knots[i] = DatumGetFloat8(knot_elems[i]);
    }

    /* Validate knot vector */
    if (!lwnurbs_validate_knots(knots, nknots, degree)) {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                       errmsg("Invalid knot vector")));
    }

    /* Create point array with weights */
    pa = lwnurbs_points_with_weights(points, weights, npoints, flags);

    /* Create NURBS curve */
    curve = lwnurbscurve_construct(srid, FLAGS_SET_M(flags, 1), degree, pa, nknots, knots);

    /* Cleanup */
    lwfree(points);
    lwfree(weights);
    lwfree(knots);
    ptarray_free(pa);
    pfree(point_elems);
    pfree(point_nulls);
    pfree(weight_elems);
    pfree(weight_nulls);
    pfree(knot_elems);
    pfree(knot_nulls);

    if (!curve) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("Failed to create NURBS curve")));
    }

    /* Return as serialized geometry */
    geom = geometry_serialize((LWGEOM *)curve);
    PG_RETURN_POINTER(geom);
}

/*
 * Evaluate a point on a curve at parameter t
 * ST_CurvePoint(curve, float8) -> POINT
 */
Datum ST_CurvePoint(PG_FUNCTION_ARGS)
{
    GSERIALIZED *geom_in;
    double t;
    LWGEOM *lwgeom;
    LWNURBSCURVE *curve;
    POINT4D result_point;
    LWPOINT *lwpoint;
    GSERIALIZED *geom_out;

    if (PG_ARGISNULL(0) || PG_ARGISNULL(1))
        PG_RETURN_NULL();

    geom_in = PG_GETARG_GSERIALIZED_P(0);
    t = PG_GETARG_FLOAT8(1);

    lwgeom = lwgeom_from_gserialized(geom_in);

    if (!IS_NURBS_TYPE(lwgeom->type)) {
        lwgeom_free(lwgeom);
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                       errmsg("Input geometry must be a NURBS or Bezier curve")));
    }

    curve = lwgeom_as_nurbscurve(lwgeom);

    /* Evaluate point at parameter t */
    lwnurbscurve_evaluate_point(curve, t, &result_point);

    /* Create result point */
    lwpoint = lwpoint_make3dz(curve->srid, result_point.x, result_point.y, result_point.z);

    lwgeom_free(lwgeom);

    geom_out = geometry_serialize((LWGEOM *)lwpoint);
    PG_RETURN_POINTER(geom_out);
}

/*
 * Convert curve to linestring with specified number of segments
 * ST_CurveToLinestring(curve, int4) -> LINESTRING
 */
Datum ST_CurveToLinestring(PG_FUNCTION_ARGS)
{
    GSERIALIZED *geom_in;
    int32 num_segments;
    LWGEOM *lwgeom;
    LWNURBSCURVE *curve;
    LWLINE *line;
    GSERIALIZED *geom_out;

    if (PG_ARGISNULL(0))
        PG_RETURN_NULL();

    geom_in = PG_GETARG_GSERIALIZED_P(0);
    num_segments = PG_ARGISNULL(1) ? 32 : PG_GETARG_INT32(1);

    if (num_segments < 1) num_segments = 32;

    lwgeom = lwgeom_from_gserialized(geom_in);

    /* Handle NURBS/Bezier curves */
    if (IS_NURBS_TYPE(lwgeom->type)) {
        curve = lwgeom_as_nurbscurve(lwgeom);
        line = lwnurbscurve_to_linestring(curve, num_segments);
    } else {
        /* For other geometry types, return as-is or convert appropriately */
        lwgeom_free(lwgeom);
        PG_RETURN_POINTER(geom_in);
    }

    lwgeom_free(lwgeom);

    if (!line) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("Failed to convert curve to linestring")));
    }

    geom_out = geometry_serialize((LWGEOM *)line);
    PG_RETURN_POINTER(geom_out);
}

/*
 * Check if a curve is valid
 * ST_IsValidCurve(curve) -> boolean
 */
Datum ST_IsValidCurve(PG_FUNCTION_ARGS)
{
    GSERIALIZED *geom;
    LWGEOM *lwgeom;
    LWNURBSCURVE *curve;
    bool is_valid;

    if (PG_ARGISNULL(0))
        PG_RETURN_BOOL(false);

    geom = PG_GETARG_GSERIALIZED_P(0);
    lwgeom = lwgeom_from_gserialized(geom);

    if (!IS_NURBS_TYPE(lwgeom->type)) {
        lwgeom_free(lwgeom);
        PG_RETURN_BOOL(false);
    }

    curve = lwgeom_as_nurbscurve(lwgeom);
    is_valid = lwnurbscurve_is_valid(curve);

    lwgeom_free(lwgeom);

    PG_RETURN_BOOL(is_valid);
}

/*
 * Calculate curve length
 * ST_CurveLength(curve, int4) -> float8
 */
Datum ST_CurveLength(PG_FUNCTION_ARGS)
{
    GSERIALIZED *geom;
    int32 num_segments;
    LWGEOM *lwgeom;
    LWNURBSCURVE *curve;
    double length;

    if (PG_ARGISNULL(0))
        PG_RETURN_NULL();

    geom = PG_GETARG_GSERIALIZED_P(0);
    num_segments = PG_ARGISNULL(1) ? 100 : PG_GETARG_INT32(1);

    lwgeom = lwgeom_from_gserialized(geom);

    if (IS_NURBS_TYPE(lwgeom->type)) {
        curve = lwgeom_as_nurbscurve(lwgeom);
        length = lwnurbscurve_length(curve, num_segments);
    } else {
        /* Use standard PostGIS length for other types */
        length = lwgeom_length(lwgeom);
    }

    lwgeom_free(lwgeom);

    PG_RETURN_FLOAT8(length);
}

/*
 * Get control points of a curve
 * ST_CurveControlPoints(curve) -> geometry[]
 */
Datum ST_CurveControlPoints(PG_FUNCTION_ARGS)
{
    GSERIALIZED *geom_in;
    LWGEOM *lwgeom;
    LWNURBSCURVE *curve;
    ArrayType *result;
    Datum *point_datums;
    POINT4D point;
    LWPOINT *lwpoint;
    GSERIALIZED *point_geom;
    int i;

    if (PG_ARGISNULL(0))
        PG_RETURN_NULL();

    geom_in = PG_GETARG_GSERIALIZED_P(0);
    lwgeom = lwgeom_from_gserialized(geom_in);

    if (!IS_NURBS_TYPE(lwgeom->type)) {
        lwgeom_free(lwgeom);
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                       errmsg("Input geometry must be a NURBS or Bezier curve")));
    }

    curve = lwgeom_as_nurbscurve(lwgeom);

    /* Create array of control points */
    point_datums = palloc(curve->num_points * sizeof(Datum));

    for (i = 0; i < curve->num_points; i++) {
        getPoint4d_p(curve->points, i, &point);

        /* Create point geometry (without weight) */
        lwpoint = lwpoint_make3dz(curve->srid, point.x, point.y,
                              FLAGS_GET_Z(curve->flags) ? point.z : 0.0);
        point_geom = geometry_serialize((LWGEOM *)lwpoint);
        point_datums[i] = PointerGetDatum(point_geom);
        lwpoint_free(lwpoint);
    }

    lwgeom_free(lwgeom);

    /* Construct PostgreSQL array */
    result = construct_array(point_datums, curve->num_points,
                            get_fn_expr_rettype(fcinfo->flinfo),
                            -1, false, 'd');

    pfree(point_datums);

    PG_RETURN_ARRAYTYPE_P(result);
}

/*
 * Get knot vector of a NURBS curve
 * ST_CurveKnots(curve) -> float8[]
 */
Datum ST_CurveKnots(PG_FUNCTION_ARGS)
{
    GSERIALIZED *geom_in;
    LWGEOM *lwgeom;
    LWNURBSCURVE *curve;
    ArrayType *result;
    Datum *knot_datums;
    int i;

    if (PG_ARGISNULL(0))
        PG_RETURN_NULL();

    geom_in = PG_GETARG_GSERIALIZED_P(0);
    lwgeom = lwgeom_from_gserialized(geom_in);

    if (!IS_NURBS_TYPE(lwgeom->type)) {
        lwgeom_free(lwgeom);
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                       errmsg("Input geometry must be a NURBS or Bezier curve")));
    }

    curve = lwgeom_as_nurbscurve(lwgeom);

    /* Bezier curves don't have explicit knot vectors */
    if (curve->type == BEZIERCURVETYPE || !curve->knots) {
        lwgeom_free(lwgeom);
        PG_RETURN_NULL();
    }

    /* Create array of knot values */
    knot_datums = palloc(curve->num_knots * sizeof(Datum));

    for (i = 0; i < curve->num_knots; i++) {
        knot_datums[i] = Float8GetDatum(curve->knots[i]);
    }

    lwgeom_free(lwgeom);

    /* Construct PostgreSQL array */
    result = construct_array(knot_datums, curve->num_knots, FLOAT8OID,
                            8, FLOAT8PASSBYVAL, 'd');

    pfree(knot_datums);

    PG_RETURN_ARRAYTYPE_P(result);
}
