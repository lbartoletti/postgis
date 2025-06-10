/*
 * PostGIS NURBS Support - PostgreSQL Functions
 * File: postgis/lwgeom_nurbs_functions.c
 *
 * PostgreSQL interface functions for NURBS operations.
 *
 * Copyright (c) 2024 Loïc Bartoletti
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

/*
 * Create a Bezier curve from control points
 * ST_BezierCurve(geometry[])
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
    LWNURBS *nurbs;
    LWLINE *line;
    uint32_t srid = 0;
    int i;

    if (PG_ARGISNULL(0))
        PG_RETURN_NULL();

    array = PG_GETARG_ARRAYTYPE_P(0);

    /* Deconstruct array */
    deconstruct_array(array, get_fn_expr_argtype(fcinfo->flinfo, 0),
                     -1, false, 'd', &elems, &nulls, &nelems);

    if (nelems < 2 || nelems > 4) {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                       errmsg("Bezier curve requires 2-4 control points")));
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
        if (i == 0) srid = lwgeom->srid;

        getPoint4d_p(lwpoint->point, 0, &points[i]);
        lwgeom_free(lwgeom);
    }

    /* Create appropriate Bezier curve */
    if (nelems == 3) {
        nurbs = lwnurbs_bezier_quadratic(srid, points[0], points[1], points[2]);
    } else if (nelems == 4) {
        nurbs = lwnurbs_bezier_cubic(srid, points[0], points[1], points[2], points[3]);
    } else {
        /* Linear or higher-order Bezier - use general constructor */
        double *ordinates = lwalloc(nelems * 3 * sizeof(double));
        for (i = 0; i < nelems; i++) {
            ordinates[i * 3] = points[i].x;
            ordinates[i * 3 + 1] = points[i].y;
            ordinates[i * 3 + 2] = 1.0; /* Unit weight */
        }
        nurbs = lwnurbs_construct(srid, 0, nelems - 1, nelems, ordinates, 0, NULL);
        lwfree(ordinates);
    }

    lwfree(points);
    pfree(elems);
    pfree(nulls);

    if (!nurbs) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("Failed to create Bezier curve")));
    }

    /* Convert to linestring for now - in future we could store as custom type */
    line = lwnurbs_to_linestring(nurbs, 32);
    lwnurbs_free(nurbs);

    if (!line) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("Failed to convert curve to linestring")));
    }

    geom = geometry_serialize((LWGEOM *)line);
    PG_RETURN_POINTER(geom);
}

/*
 * Create a NURBS curve from control points, weights, and knots
 * ST_NurbsCurve(geometry[], float8[], float8[])
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
    double *weights, *knots, *ordinates;
    LWNURBS *nurbs;
    LWLINE *line;
    uint32_t srid = 0;
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
        if (i == 0) srid = lwgeom->srid;

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

    /* Build ordinates array */
    ordinates = lwalloc(npoints * 3 * sizeof(double));
    for (i = 0; i < npoints; i++) {
        ordinates[i * 3] = points[i].x * weights[i];
        ordinates[i * 3 + 1] = points[i].y * weights[i];
        ordinates[i * 3 + 2] = weights[i];
    }

    /* Create NURBS curve */
    nurbs = lwnurbs_construct(srid, 0, degree, npoints, ordinates, nknots, knots);

    /* Cleanup */
    lwfree(points);
    lwfree(weights);
    lwfree(knots);
    lwfree(ordinates);
    pfree(point_elems);
    pfree(point_nulls);
    pfree(weight_elems);
    pfree(weight_nulls);
    pfree(knot_elems);
    pfree(knot_nulls);

    if (!nurbs) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("Failed to create NURBS curve")));
    }

    /* Convert to linestring */
    line = lwnurbs_to_linestring(nurbs, 64);
    lwnurbs_free(nurbs);

    if (!line) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("Failed to convert curve to linestring")));
    }

    geom = geometry_serialize((LWGEOM *)line);
    PG_RETURN_POINTER(geom);
}

/*
 * Evaluate a point on a curve at parameter t
 * ST_CurvePoint(geometry, float8) - Placeholder for now
 */
Datum ST_CurvePoint(PG_FUNCTION_ARGS)
{
    ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                   errmsg("ST_CurvePoint not yet implemented")));
    PG_RETURN_NULL();
}

/*
 * Convert any curve to linestring with specified number of segments
 * ST_CurveToLinestring(geometry, int4) - Placeholder for now
 */
Datum ST_CurveToLinestring(PG_FUNCTION_ARGS)
{
    GSERIALIZED *geom_in;
    int32 num_segments;

    if (PG_ARGISNULL(0))
        PG_RETURN_NULL();

    geom_in = PG_GETARG_GSERIALIZED_P(0);
    num_segments = PG_ARGISNULL(1) ? 32 : PG_GETARG_INT32(1);

    if (num_segments < 1) num_segments = 32;

    /* For now, just return the input geometry */
    /* In future, decode NURBS metadata and convert properly */
    PG_RETURN_POINTER(geom_in);
}

/*
 * Check if a curve is valid
 * ST_IsValidCurve(geometry) - Placeholder for now
 */
Datum ST_IsValidCurve(PG_FUNCTION_ARGS)
{
    if (PG_ARGISNULL(0))
        PG_RETURN_BOOL(false);

    /* For now, assume all geometries are valid curves */
    PG_RETURN_BOOL(true);
}

/*
 * Calculate curve length
 * ST_CurveLength(geometry, int4) - Placeholder for now
 */
Datum ST_CurveLength(PG_FUNCTION_ARGS)
{
    GSERIALIZED *geom;
    LWGEOM *lwgeom;
    double length;

    if (PG_ARGISNULL(0))
        PG_RETURN_NULL();

    geom = PG_GETARG_GSERIALIZED_P(0);
    lwgeom = lwgeom_from_gserialized(geom);

    /* For now, use standard PostGIS length calculation */
    length = lwgeom_length(lwgeom);
    lwgeom_free(lwgeom);

    PG_RETURN_FLOAT8(length);
}
