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
#include "utils/builtins.h"
#include "utils/array.h"
#include "../postgis_config.h"
#include "liblwgeom.h"
#include "lwgeom_pg.h"

Datum ST_MakeNurbsCurve(PG_FUNCTION_ARGS);
Datum ST_NurbsCurveControlPoints(PG_FUNCTION_ARGS);
Datum ST_NurbsCurveDegree(PG_FUNCTION_ARGS);
Datum ST_NurbsCurveToLineString(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(ST_MakeNurbsCurve);
Datum ST_MakeNurbsCurve(PG_FUNCTION_ARGS)
{
    int32_t degree = PG_GETARG_INT32(0);
    GSERIALIZED *control_geom = PG_GETARG_GSERIALIZED_P(1);
    ArrayType *weights_array = PG_ARGISNULL(2) ? NULL : PG_GETARG_ARRAYTYPE_P(2);
    ArrayType *knots_array = PG_ARGISNULL(3) ? NULL : PG_GETARG_ARRAYTYPE_P(3);

    LWGEOM *lwgeom = lwgeom_from_gserialized(control_geom);
    LWLINE *line;
    NURBSPOINT *points = NULL;
    //double *weights = NULL;
		double *knots = NULL;
    uint32_t npoints = 0, nknots = 0;
    LWNURBSCURVE *curve;
    GSERIALIZED *result;
    uint32_t i;

    if (lwgeom->type != LINETYPE) {
        lwgeom_free(lwgeom);
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
            errmsg("Control points must be a LINESTRING")));
    }

    line = (LWLINE*)lwgeom;
    npoints = line->points->npoints;

    if (npoints < degree + 1) {
        lwgeom_free(lwgeom);
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
            errmsg("Need at least %d control points for degree %d", degree + 1, degree)));
    }

    /* Convert POINTARRAY to NURBSPOINT array */
    points = lwalloc(sizeof(NURBSPOINT) * npoints);
    for (i = 0; i < npoints; i++) {
        POINT4D pt;
        getPoint4d_p(line->points, i, &pt);

        points[i].x = pt.x;
        points[i].y = pt.y;
        points[i].z = FLAGS_GET_Z(line->flags) ? pt.z : 0.0;
        points[i].m = FLAGS_GET_M(line->flags) ? pt.m : 0.0;
        points[i].weight = 1.0; /* Default weight */
    }

    /* Extract weights if provided */
    if (weights_array) {
        int ndims = ARR_NDIM(weights_array);
        int *dims = ARR_DIMS(weights_array);
        double *weight_data;

        if (ndims != 1 || dims[0] != npoints) {
            lwfree(points);
            lwgeom_free(lwgeom);
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                errmsg("Weights array must have exactly %d elements", npoints)));
        }

        weight_data = (double*)ARR_DATA_PTR(weights_array);
        for (i = 0; i < npoints; i++) {
            if (weight_data[i] <= 0.0) {
                lwfree(points);
                lwgeom_free(lwgeom);
                ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                    errmsg("All weights must be positive")));
            }
            points[i].weight = weight_data[i];
        }
    }

    /* Extract knots if provided */
    if (knots_array) {
        int ndims = ARR_NDIM(knots_array);
        int *dims = ARR_DIMS(knots_array);

        if (ndims != 1) {
            lwfree(points);
            lwgeom_free(lwgeom);
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                errmsg("Knots must be a one-dimensional array")));
        }

        nknots = dims[0];
        if (nknots != npoints + degree + 1) {
            lwfree(points);
            lwgeom_free(lwgeom);
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                errmsg("Knot vector must have exactly %d elements", npoints + degree + 1)));
        }

        knots = (double*)ARR_DATA_PTR(knots_array);
    }

    /* Create NURBS curve */
    curve = lwnurbscurve_construct(lwgeom->srid, degree, points, npoints, knots, nknots, 0.0, 0.0);
    curve->flags = lwgeom->flags;

    if (!lwnurbscurve_validate(curve)) {
        lwnurbscurve_free(curve);
        lwfree(points);
        lwgeom_free(lwgeom);
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
            errmsg("Invalid NURBS curve parameters")));
    }

    result = geometry_serialize((LWGEOM*)curve);

    lwnurbscurve_free(curve);
    lwfree(points);
    lwgeom_free(lwgeom);

    PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(ST_NurbsCurveControlPoints);
Datum ST_NurbsCurveControlPoints(PG_FUNCTION_ARGS)
{
    GSERIALIZED *pgeom = PG_GETARG_GSERIALIZED_P(0);
    LWGEOM *lwgeom = lwgeom_from_gserialized(pgeom);
    LWNURBSCURVE *curve;
    POINTARRAY *pa;
    LWLINE *line;
    GSERIALIZED *result;
    uint32_t i;

    if (lwgeom->type != NURBSCURVETYPE) {
        lwgeom_free(lwgeom);
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
            errmsg("Input geometry must be a NURBS curve")));
    }

    curve = (LWNURBSCURVE*)lwgeom;

    if (curve->npoints == 0) {
        lwgeom_free(lwgeom);
        PG_RETURN_NULL();
    }

    pa = ptarray_construct(FLAGS_GET_Z(curve->flags), FLAGS_GET_M(curve->flags), curve->npoints);

    for (i = 0; i < curve->npoints; i++) {
        POINT4D pt;
        pt.x = curve->points[i].x;
        pt.y = curve->points[i].y;
        pt.z = curve->points[i].z;
        pt.m = curve->points[i].m;
        ptarray_set_point4d(pa, i, &pt);
    }

    line = lwline_construct(curve->srid, NULL, pa);
    result = geometry_serialize((LWGEOM*)line);

    lwline_free(line);
    lwgeom_free(lwgeom);

    PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(ST_NurbsCurveDegree);
Datum ST_NurbsCurveDegree(PG_FUNCTION_ARGS)
{
    GSERIALIZED *pgeom = PG_GETARG_GSERIALIZED_P(0);
    LWGEOM *lwgeom = lwgeom_from_gserialized(pgeom);
    LWNURBSCURVE *curve;
    int32_t degree;

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

PG_FUNCTION_INFO_V1(ST_NurbsCurveToLineString);
Datum ST_NurbsCurveToLineString(PG_FUNCTION_ARGS)
{
    GSERIALIZED *pgeom = PG_GETARG_GSERIALIZED_P(0);
    int32_t segments = PG_NARGS() > 1 ? PG_GETARG_INT32(1) : 32;
    LWGEOM *lwgeom = lwgeom_from_gserialized(pgeom);
    LWNURBSCURVE *curve;
    LWLINE *line;
    GSERIALIZED *result;

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
