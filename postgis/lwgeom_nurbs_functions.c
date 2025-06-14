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

	ctrl_pts = ptarray_clone_deep(line->points);

	nurbs = lwnurbscurve_construct(control_geom->srid, degree, ctrl_pts, NULL, NULL, 0, 0);
	lwgeom_free(control_geom);

	if (!nurbs) {
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
			errmsg("Failed to construct NURBS curve")));
	}

	// DEBUG: Vérifier le type
	ereport(NOTICE, (errmsg("NURBS created with type: %d", nurbs->type)));

	/* S'assurer que la bbox n'est PAS calculée automatiquement */
	// Commenter cette ligne pour éviter le calcul de bbox
	// if (lwgeom_needs_bbox((LWGEOM*)nurbs))
	//     lwgeom_add_bbox((LWGEOM*)nurbs);

	ereport(NOTICE, (errmsg("Before serialize: NURBS type=%d, srid=%d",
                       nurbs->type, nurbs->srid)));
	result = geometry_serialize((LWGEOM*)nurbs);

	// DEBUG: Vérifier le type après sérialisation
	if (result) {
    LWGEOM *test_geom = lwgeom_from_gserialized(result);
    ereport(NOTICE, (errmsg("After serialize: type=%d",
                           test_geom ? test_geom->type : -1)));
    if (test_geom) lwgeom_free(test_geom);
}

	lwnurbscurve_free(nurbs);

	if (!result) {
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
			errmsg("Failed to serialize NURBS curve")));
	}

	PG_RETURN_POINTER(result);
}
