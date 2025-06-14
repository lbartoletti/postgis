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
	LWLINE *line = NULL;
	int32_t degree = PG_GETARG_INT32(0);
	GSERIALIZED *pcontrol_pts = PG_GETARG_GSERIALIZED_P(1);
	LWGEOM *control_geom = lwgeom_from_gserialized(pcontrol_pts);
	POINTARRAY *ctrl_pts = NULL;
	LWNURBSCURVE *nurbs = NULL;
	GSERIALIZED *result = NULL;

	if (!control_geom) {
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Invalid control points geometry")));
	}

	if (control_geom->type != LINETYPE) {
		lwgeom_free(control_geom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Control points must be a LINESTRING")));
	}

	if (degree < 1 || degree > 10) {
		lwgeom_free(control_geom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("NURBS degree must be between 1 and 10")));
	}

	line = (LWLINE*)control_geom;
	if (!line->points || line->points->npoints < degree + 1) {
		lwgeom_free(control_geom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Need at least %d control points for degree %d NURBS", degree + 1, degree)));
	}

	/* Clone les points ET preserve les flags correctement */
	ctrl_pts = ptarray_clone_deep(line->points);

	/* CORRECTION CRITIQUE: S'assurer que les flags sont cohérents */
	nurbs = lwnurbscurve_construct(control_geom->srid, degree, ctrl_pts, NULL, NULL, 0, 0);

	if (!nurbs) {
		lwgeom_free(control_geom);
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
			errmsg("Failed to construct NURBS curve")));
	}

	/* CORRECTION: Synchroniser les flags entre la géométrie et les points */
	if (nurbs->points) {
		nurbs->flags = nurbs->points->flags;
	}

	/* CORRECTION: Ne PAS calculer de bbox automatiquement pour éviter les problèmes */
	FLAGS_SET_BBOX(nurbs->flags, 0);
	nurbs->bbox = NULL;

	result = geometry_serialize((LWGEOM*)nurbs);

	lwgeom_free(control_geom);
	lwnurbscurve_free(nurbs);

	if (!result) {
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
			errmsg("Failed to serialize NURBS curve")));
	}

	PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(ST_NurbsCurveControlPoints);
Datum ST_NurbsCurveControlPoints(PG_FUNCTION_ARGS)
{
    GSERIALIZED *pgeom = PG_GETARG_GSERIALIZED_P(0);
    LWGEOM *lwgeom = lwgeom_from_gserialized(pgeom);
    LWNURBSCURVE *curve;
    POINTARRAY *ctrl_pts;
    LWLINE *line;
    GSERIALIZED *result;

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

    if (segments < 2 || segments > 10000) {
        lwgeom_free(lwgeom);
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
            errmsg("Number of segments must be between 2 and 10000")));
    }

    curve = (LWNURBSCURVE*)lwgeom;
    line = lwnurbscurve_stroke(curve, segments);

    result = geometry_serialize((LWGEOM*)line);

    lwline_free(line);
    lwgeom_free(lwgeom);

    PG_RETURN_POINTER(result);
}
