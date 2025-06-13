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
#include "liblwgeom_internal.h"
#include "lwgeom_pg.h"

Datum ST_MakeNurbsCurve(PG_FUNCTION_ARGS);
Datum ST_NurbsToLine(PG_FUNCTION_ARGS);

/**
 * ST_MakeNurbsCurve(degree integer, control_points geometry)
 * Create a NURBS curve from control points
 */
PG_FUNCTION_INFO_V1(ST_MakeNurbsCurve);
Datum ST_MakeNurbsCurve(PG_FUNCTION_ARGS)
{
	int32_t degree;
	GSERIALIZED *pcontrol_pts;
	LWGEOM *control_geom;
	LWMPOINT *control_mpt = NULL;
	POINTARRAY *ctrl_pts;
	LWNURBSCURVE *nurbs;
	GSERIALIZED *result;
	int32_t srid;

	/* Get degree */
	degree = PG_GETARG_INT32(0);
	if (degree < 1 || degree > 10) {
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("NURBS degree must be between 1 and 10")));
	}

	/* Get control points */
	pcontrol_pts = PG_GETARG_GSERIALIZED_P(1);
	control_geom = lwgeom_from_gserialized(pcontrol_pts);
	srid = control_geom->srid;

	/* Convert to MULTIPOINT if necessary */
	if (control_geom->type == POINTTYPE) {
		control_mpt = lwmpoint_construct_empty(srid,
		                                     lwgeom_has_z(control_geom),
		                                     lwgeom_has_m(control_geom));
		lwmpoint_add_lwpoint(control_mpt, (LWPOINT*)control_geom);
		control_geom = (LWGEOM*)control_mpt;
	} else if (control_geom->type == MULTIPOINTTYPE) {
		control_mpt = (LWMPOINT*)control_geom;
	} else if (control_geom->type == LINETYPE) {
		/* Extract points from linestring */
		LWLINE *line = (LWLINE*)control_geom;
		ctrl_pts = ptarray_clone_deep(line->points);

		nurbs = lwnurbscurve_construct(srid, degree, ctrl_pts, NULL, NULL, 0, 0);
		lwgeom_free(control_geom);

		if (!nurbs) {
			ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
				errmsg("Failed to construct NURBS curve")));
		}

		result = geometry_serialize((LWGEOM*)nurbs);
		lwnurbscurve_free(nurbs);
		PG_RETURN_POINTER(result);
	} else {
		lwgeom_free(control_geom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Control points must be POINT, MULTIPOINT or LINESTRING geometry")));
	}

	/* Extract point array from multipoint */
	ctrl_pts = ptarray_construct_empty(lwgeom_has_z(control_geom),
	                                  lwgeom_has_m(control_geom),
	                                  control_mpt->ngeoms);

	for (uint32_t i = 0; i < control_mpt->ngeoms; i++) {
		POINT4D pt;
		if (getPoint4d_p(control_mpt->geoms[i]->point, 0, &pt)) {
			ptarray_append_point(ctrl_pts, &pt, LW_FALSE);
		}
	}

	if (ctrl_pts->npoints < 2) {
		ptarray_free(ctrl_pts);
		lwgeom_free(control_geom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("NURBS curve requires at least 2 control points")));
	}

	/* Create NURBS curve */
	nurbs = lwnurbscurve_construct(srid, degree, ctrl_pts, NULL, NULL, 0, 0);
	lwgeom_free(control_geom);

	if (!nurbs) {
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
			errmsg("Failed to construct NURBS curve")));
	}

	result = geometry_serialize((LWGEOM*)nurbs);
	lwnurbscurve_free(nurbs);

	PG_RETURN_POINTER(result);
}

/**
 * ST_NurbsToLine(nurbscurve geometry, segments integer)
 * Convert NURBS curve to linestring
 */
PG_FUNCTION_INFO_V1(ST_NurbsToLine);
Datum ST_NurbsToLine(PG_FUNCTION_ARGS)
{
	GSERIALIZED *pnurbs;
	int32_t segments;
	LWGEOM *lwgeom;
	LWNURBSCURVE *nurbs;
	LWLINE *line;
	GSERIALIZED *result;

	pnurbs = PG_GETARG_GSERIALIZED_P(0);
	segments = PG_ARGISNULL(1) ? 32 : PG_GETARG_INT32(1);

	if (segments < 2)
		segments = 32;

	lwgeom = lwgeom_from_gserialized(pnurbs);
	if (lwgeom->type != NURBSCURVETYPE) {
		lwgeom_free(lwgeom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Geometry must be a NURBS curve")));
	}

	nurbs = (LWNURBSCURVE*)lwgeom;
	line = lwnurbscurve_to_linestring(nurbs, segments);

	lwgeom_free(lwgeom);

	if (!line) {
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
			errmsg("Failed to convert NURBS curve to linestring")));
	}

	result = geometry_serialize((LWGEOM*)line);
	lwline_free(line);

	PG_RETURN_POINTER(result);
}

/**
 * ST_MakeBezier(control_points geometry)
 * Create a Bezier curve (NURBS without knots)
 */
PG_FUNCTION_INFO_V1(ST_MakeBezier);
Datum ST_MakeBezier(PG_FUNCTION_ARGS)
{
	GSERIALIZED *pcontrol_pts;
	LWGEOM *control_geom;
	LWLINE *line;
	uint32_t degree;

	pcontrol_pts = PG_GETARG_GSERIALIZED_P(0);
	control_geom = lwgeom_from_gserialized(pcontrol_pts);

	if (control_geom->type != LINETYPE) {
		lwgeom_free(control_geom);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("Control points must be a LINESTRING")));
	}

	line = (LWLINE*)control_geom;
	degree = line->points->npoints - 1;

	/* Call ST_MakeNurbsCurve with appropriate degree */
	PG_RETURN_DATUM(DirectFunctionCall2(ST_MakeNurbsCurve,
	                                   Int32GetDatum(degree),
	                                   PG_GETARG_DATUM(0)));
}
