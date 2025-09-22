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
 * Copyright 2012-2020 Oslandia <infos@oslandia.com>
 *
 **********************************************************************/

#include "SFCGAL/capi/sfcgal_c.h"

#include "postgres.h"
#include "fmgr.h"
#include "libpq/pqsignal.h"
#include "utils/builtins.h"
#include "utils/elog.h"
#include "utils/guc.h"

#include "lwgeom_pg.h"
#include "lwgeom_sfcgal.h"
#include "../postgis_config.h"
#include "../liblwgeom/liblwgeom.h"

/*
 * This is required for builds against pgsql
 */
PG_MODULE_MAGIC;

/* Prototypes */
#if POSTGIS_SFCGAL_VERSION >= 10400
Datum postgis_sfcgal_full_version(PG_FUNCTION_ARGS);
#endif
Datum postgis_sfcgal_version(PG_FUNCTION_ARGS);

Datum sfcgal_from_ewkt(PG_FUNCTION_ARGS);
Datum sfcgal_distance(PG_FUNCTION_ARGS);
Datum sfcgal_distance3D(PG_FUNCTION_ARGS);
Datum sfcgal_area(PG_FUNCTION_ARGS);
Datum sfcgal_area3D(PG_FUNCTION_ARGS);
Datum sfcgal_intersects(PG_FUNCTION_ARGS);
Datum sfcgal_intersects3D(PG_FUNCTION_ARGS);
Datum sfcgal_intersection(PG_FUNCTION_ARGS);
Datum sfcgal_intersection3D(PG_FUNCTION_ARGS);
Datum sfcgal_difference(PG_FUNCTION_ARGS);
Datum sfcgal_difference3D(PG_FUNCTION_ARGS);
Datum sfcgal_union(PG_FUNCTION_ARGS);
Datum sfcgal_union3D(PG_FUNCTION_ARGS);
Datum sfcgal_volume(PG_FUNCTION_ARGS);
Datum sfcgal_extrude(PG_FUNCTION_ARGS);
Datum sfcgal_straight_skeleton(PG_FUNCTION_ARGS);
Datum sfcgal_approximate_medial_axis(PG_FUNCTION_ARGS);
Datum sfcgal_is_planar(PG_FUNCTION_ARGS);
Datum sfcgal_orientation(PG_FUNCTION_ARGS);
Datum sfcgal_force_lhr(PG_FUNCTION_ARGS);
Datum sfcgal_constrained_delaunay_triangles(PG_FUNCTION_ARGS);
Datum sfcgal_triangulate(PG_FUNCTION_ARGS);
Datum sfcgal_tesselate(PG_FUNCTION_ARGS);
Datum sfcgal_minkowski_sum(PG_FUNCTION_ARGS);
Datum sfcgal_make_solid(PG_FUNCTION_ARGS);
Datum sfcgal_is_solid(PG_FUNCTION_ARGS);
Datum postgis_sfcgal_noop(PG_FUNCTION_ARGS);
Datum sfcgal_convexhull3D(PG_FUNCTION_ARGS);
Datum sfcgal_alphashape(PG_FUNCTION_ARGS);
Datum sfcgal_optimalalphashape(PG_FUNCTION_ARGS);
Datum sfcgal_simplify(PG_FUNCTION_ARGS);
Datum sfcgal_postgis_nurbs_curve_from_points(PG_FUNCTION_ARGS);
Datum sfcgal_postgis_nurbs_curve_evaluate(PG_FUNCTION_ARGS);
Datum sfcgal_postgis_nurbs_curve_to_linestring(PG_FUNCTION_ARGS);
Datum sfcgal_postgis_nurbs_curve_derivative(PG_FUNCTION_ARGS);
Datum sfcgal_postgis_nurbs_curve_interpolate(PG_FUNCTION_ARGS);
Datum sfcgal_postgis_nurbs_curve_approximate(PG_FUNCTION_ARGS);

GSERIALIZED *geometry_serialize(LWGEOM *lwgeom);
char *text_to_cstring(const text *textptr);
void _PG_init(void);
void _PG_fini(void);


static int __sfcgal_init = 0;


/* Module load callback */
void
_PG_init(void)
{
	/* install PostgreSQL handlers */
	pg_install_lwgeom_handlers();
	elog(DEBUG1, "PostGIS SFCGAL %s loaded", POSTGIS_VERSION);
}


/* Module unload callback */
void
_PG_fini(void)
{
	elog(NOTICE, "Goodbye from PostGIS SFCGAL %s", POSTGIS_VERSION);
}


void
sfcgal_postgis_init(void)
{
	if (!__sfcgal_init)
	{
		sfcgal_init();
		sfcgal_set_error_handlers((sfcgal_error_handler_t)(void *)lwpgnotice,
					  (sfcgal_error_handler_t)(void *)lwpgerror);
		sfcgal_set_alloc_handlers(lwalloc, lwfree);
		__sfcgal_init = 1;
	}
}

/* Conversion from GSERIALIZED* to SFCGAL::Geometry */
sfcgal_geometry_t *
POSTGIS2SFCGALGeometry(GSERIALIZED *pglwgeom)
{
	sfcgal_geometry_t *g;
	LWGEOM *lwgeom = lwgeom_from_gserialized(pglwgeom);

	if (!lwgeom)
		lwpgerror("POSTGIS2SFCGALGeometry: Unable to deserialize input");

	g = LWGEOM2SFCGAL(lwgeom);
	lwgeom_free(lwgeom);

	return g;
}

/* Conversion from GSERIALIZED* to SFCGAL::PreparedGeometry */
sfcgal_prepared_geometry_t *
POSTGIS2SFCGALPreparedGeometry(GSERIALIZED *pglwgeom)
{
	sfcgal_geometry_t *g;
	LWGEOM *lwgeom = lwgeom_from_gserialized(pglwgeom);

	if (!lwgeom)
		lwpgerror("POSTGIS2SFCGALPreparedGeometry: Unable to deserialize input");

	g = LWGEOM2SFCGAL(lwgeom);

	lwgeom_free(lwgeom);

	return sfcgal_prepared_geometry_create_from_geometry(g, gserialized_get_srid(pglwgeom));
}

/* Conversion from SFCGAL::Geometry to GSERIALIZED */
GSERIALIZED *
SFCGALGeometry2POSTGIS(const sfcgal_geometry_t *geom, int force3D, int32_t SRID)
{
	GSERIALIZED *result;
	LWGEOM *lwgeom = SFCGAL2LWGEOM(geom, force3D, SRID);

	if (lwgeom_needs_bbox(lwgeom) == LW_TRUE)
		lwgeom_add_bbox(lwgeom);

	result = geometry_serialize(lwgeom);
	lwgeom_free(lwgeom);

	return result;
}

/* Conversion from SFCGAL::PreparedGeometry to GSERIALIZED */
GSERIALIZED *
SFCGALPreparedGeometry2POSTGIS(const sfcgal_prepared_geometry_t *geom, int force3D)
{
	return SFCGALGeometry2POSTGIS(
	    sfcgal_prepared_geometry_geometry(geom), force3D, sfcgal_prepared_geometry_srid(geom));
}

/* Conversion from EWKT to GSERIALIZED */
PG_FUNCTION_INFO_V1(sfcgal_from_ewkt);
Datum
sfcgal_from_ewkt(PG_FUNCTION_ARGS)
{
	GSERIALIZED *result;
	sfcgal_prepared_geometry_t *g;
	text *wkttext = PG_GETARG_TEXT_P(0);
	char *cstring = text_to_cstring(wkttext);

	sfcgal_postgis_init();

	g = sfcgal_io_read_ewkt(cstring, strlen(cstring));

	result = SFCGALPreparedGeometry2POSTGIS(g, 0);
	sfcgal_prepared_geometry_delete(g);
	PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(sfcgal_area);
Datum
sfcgal_area(PG_FUNCTION_ARGS)
{
	GSERIALIZED *input;
	sfcgal_geometry_t *geom;
	double result;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	geom = POSTGIS2SFCGALGeometry(input);

	result = sfcgal_geometry_area(geom);
	sfcgal_geometry_delete(geom);

	PG_FREE_IF_COPY(input, 0);

	PG_RETURN_FLOAT8(result);
}

PG_FUNCTION_INFO_V1(sfcgal_area3D);
Datum
sfcgal_area3D(PG_FUNCTION_ARGS)
{
	GSERIALIZED *input;
	sfcgal_geometry_t *geom;
	double result;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	geom = POSTGIS2SFCGALGeometry(input);

	result = sfcgal_geometry_area_3d(geom);
	sfcgal_geometry_delete(geom);

	PG_FREE_IF_COPY(input, 0);

	PG_RETURN_FLOAT8(result);
}

PG_FUNCTION_INFO_V1(sfcgal_is_planar);
Datum
sfcgal_is_planar(PG_FUNCTION_ARGS)
{
	GSERIALIZED *input;
	sfcgal_geometry_t *geom;
	int result;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	geom = POSTGIS2SFCGALGeometry(input);

	result = sfcgal_geometry_is_planar(geom);
	sfcgal_geometry_delete(geom);

	PG_FREE_IF_COPY(input, 0);

	PG_RETURN_BOOL(result);
}

PG_FUNCTION_INFO_V1(sfcgal_orientation);
Datum
sfcgal_orientation(PG_FUNCTION_ARGS)
{
	GSERIALIZED *input;
	sfcgal_geometry_t *geom;
	int result;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	geom = POSTGIS2SFCGALGeometry(input);

	result = sfcgal_geometry_orientation(geom);
	sfcgal_geometry_delete(geom);

	PG_FREE_IF_COPY(input, 0);

	PG_RETURN_INT32(result);
}
PG_FUNCTION_INFO_V1(sfcgal_triangulate);
Datum
sfcgal_triangulate(PG_FUNCTION_ARGS)
{
	GSERIALIZED *input, *output;
	sfcgal_geometry_t *geom;
	sfcgal_geometry_t *result;
	srid_t srid;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	srid = gserialized_get_srid(input);
	geom = POSTGIS2SFCGALGeometry(input);
	PG_FREE_IF_COPY(input, 0);

	result = sfcgal_geometry_triangulate_2dz(geom);
	sfcgal_geometry_delete(geom);

	output = SFCGALGeometry2POSTGIS(result, 0, srid);
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
}

PG_FUNCTION_INFO_V1(sfcgal_tesselate);
Datum
sfcgal_tesselate(PG_FUNCTION_ARGS)
{
	GSERIALIZED *input, *output;
	sfcgal_geometry_t *geom;
	sfcgal_geometry_t *result;
	srid_t srid;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	srid = gserialized_get_srid(input);
	geom = POSTGIS2SFCGALGeometry(input);
	PG_FREE_IF_COPY(input, 0);

	result = sfcgal_geometry_tesselate(geom);
	sfcgal_geometry_delete(geom);

	output = SFCGALGeometry2POSTGIS(result, 0, srid);
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
}

PG_FUNCTION_INFO_V1(sfcgal_constrained_delaunay_triangles);
Datum
sfcgal_constrained_delaunay_triangles(PG_FUNCTION_ARGS)
{
	GSERIALIZED *input, *output;
	sfcgal_geometry_t *geom;
	sfcgal_geometry_t *result;
	srid_t srid;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	srid = gserialized_get_srid(input);
	geom = POSTGIS2SFCGALGeometry(input);
	PG_FREE_IF_COPY(input, 0);

	result = sfcgal_geometry_triangulate_2dz(geom);
	sfcgal_geometry_delete(geom);

	output = SFCGALGeometry2POSTGIS(result, 0, srid);
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
}

PG_FUNCTION_INFO_V1(sfcgal_force_lhr);
Datum
sfcgal_force_lhr(PG_FUNCTION_ARGS)
{
	GSERIALIZED *input, *output;
	sfcgal_geometry_t *geom;
	sfcgal_geometry_t *result;
	srid_t srid;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	srid = gserialized_get_srid(input);
	geom = POSTGIS2SFCGALGeometry(input);
	PG_FREE_IF_COPY(input, 0);

	result = sfcgal_geometry_force_lhr(geom);
	sfcgal_geometry_delete(geom);

	output = SFCGALGeometry2POSTGIS(result, 0, srid);
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
}

PG_FUNCTION_INFO_V1(sfcgal_straight_skeleton);
Datum
sfcgal_straight_skeleton(PG_FUNCTION_ARGS)
{
	GSERIALIZED *input, *output;
	sfcgal_geometry_t *geom;
	sfcgal_geometry_t *result;
	srid_t srid;
	bool use_m_as_distance;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	srid = gserialized_get_srid(input);
	geom = POSTGIS2SFCGALGeometry(input);
	PG_FREE_IF_COPY(input, 0);

	use_m_as_distance = PG_GETARG_BOOL(1);
	if ((POSTGIS_SFCGAL_VERSION < 10308) && use_m_as_distance)
	{
		lwpgnotice(
		    "The SFCGAL version this PostGIS binary "
		    "was compiled against (%d) doesn't support "
		    "'is_measured' argument in straight_skeleton "
		    "function (1.3.8+ required) "
		    "fallback to function not using m as distance.",
		    POSTGIS_SFCGAL_VERSION);
		use_m_as_distance = false;
	}

	if (use_m_as_distance)
	{
		result = sfcgal_geometry_straight_skeleton_distance_in_m(geom);
	}
	else
	{
		result = sfcgal_geometry_straight_skeleton(geom);
	}

	sfcgal_geometry_delete(geom);

	output = SFCGALGeometry2POSTGIS(result, 0, srid);
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
}

PG_FUNCTION_INFO_V1(sfcgal_approximate_medial_axis);
Datum
sfcgal_approximate_medial_axis(PG_FUNCTION_ARGS)
{
	GSERIALIZED *input, *output;
	sfcgal_geometry_t *geom;
	sfcgal_geometry_t *result;
	srid_t srid;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	srid = gserialized_get_srid(input);
	geom = POSTGIS2SFCGALGeometry(input);
	PG_FREE_IF_COPY(input, 0);

	result = sfcgal_geometry_approximate_medial_axis(geom);
	sfcgal_geometry_delete(geom);

	output = SFCGALGeometry2POSTGIS(result, 0, srid);
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
}

PG_FUNCTION_INFO_V1(sfcgal_intersects);
Datum
sfcgal_intersects(PG_FUNCTION_ARGS)
{
	GSERIALIZED *input0, *input1;
	sfcgal_geometry_t *geom0, *geom1;
	int result;

	sfcgal_postgis_init();

	input0 = PG_GETARG_GSERIALIZED_P(0);
	input1 = PG_GETARG_GSERIALIZED_P(1);
	geom0 = POSTGIS2SFCGALGeometry(input0);
	PG_FREE_IF_COPY(input0, 0);
	geom1 = POSTGIS2SFCGALGeometry(input1);
	PG_FREE_IF_COPY(input1, 1);

	result = sfcgal_geometry_intersects(geom0, geom1);
	sfcgal_geometry_delete(geom0);
	sfcgal_geometry_delete(geom1);

	PG_RETURN_BOOL(result);
}

PG_FUNCTION_INFO_V1(sfcgal_intersects3D);
Datum
sfcgal_intersects3D(PG_FUNCTION_ARGS)
{
	GSERIALIZED *input0, *input1;
	sfcgal_geometry_t *geom0, *geom1;
	int result;

	sfcgal_postgis_init();

	input0 = PG_GETARG_GSERIALIZED_P(0);
	input1 = PG_GETARG_GSERIALIZED_P(1);
	geom0 = POSTGIS2SFCGALGeometry(input0);
	PG_FREE_IF_COPY(input0, 0);
	geom1 = POSTGIS2SFCGALGeometry(input1);
	PG_FREE_IF_COPY(input1, 1);

	result = sfcgal_geometry_intersects_3d(geom0, geom1);
	sfcgal_geometry_delete(geom0);
	sfcgal_geometry_delete(geom1);

	PG_RETURN_BOOL(result);
}

PG_FUNCTION_INFO_V1(sfcgal_intersection);
Datum
sfcgal_intersection(PG_FUNCTION_ARGS)
{
	GSERIALIZED *input0, *input1, *output;
	sfcgal_geometry_t *geom0, *geom1;
	sfcgal_geometry_t *result;
	srid_t srid;

	sfcgal_postgis_init();

	input0 = PG_GETARG_GSERIALIZED_P(0);
	srid = gserialized_get_srid(input0);
	input1 = PG_GETARG_GSERIALIZED_P(1);
	geom0 = POSTGIS2SFCGALGeometry(input0);
	PG_FREE_IF_COPY(input0, 0);
	geom1 = POSTGIS2SFCGALGeometry(input1);
	PG_FREE_IF_COPY(input1, 1);

	result = sfcgal_geometry_intersection(geom0, geom1);
	sfcgal_geometry_delete(geom0);
	sfcgal_geometry_delete(geom1);

	output = SFCGALGeometry2POSTGIS(result, 0, srid);
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
}

PG_FUNCTION_INFO_V1(sfcgal_intersection3D);
Datum
sfcgal_intersection3D(PG_FUNCTION_ARGS)
{
	GSERIALIZED *input0, *input1, *output;
	sfcgal_geometry_t *geom0, *geom1;
	sfcgal_geometry_t *result;
	srid_t srid;

	sfcgal_postgis_init();

	input0 = PG_GETARG_GSERIALIZED_P(0);
	srid = gserialized_get_srid(input0);
	input1 = PG_GETARG_GSERIALIZED_P(1);
	geom0 = POSTGIS2SFCGALGeometry(input0);
	PG_FREE_IF_COPY(input0, 0);
	geom1 = POSTGIS2SFCGALGeometry(input1);
	PG_FREE_IF_COPY(input1, 1);

	result = sfcgal_geometry_intersection_3d(geom0, geom1);
	sfcgal_geometry_delete(geom0);
	sfcgal_geometry_delete(geom1);

	output = SFCGALGeometry2POSTGIS(result, 0, srid);
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
}
PG_FUNCTION_INFO_V1(sfcgal_distance);
Datum
sfcgal_distance(PG_FUNCTION_ARGS)
{
	GSERIALIZED *input0, *input1;
	sfcgal_geometry_t *geom0, *geom1;
	double result;

	sfcgal_postgis_init();

	input0 = PG_GETARG_GSERIALIZED_P(0);
	input1 = PG_GETARG_GSERIALIZED_P(1);
	geom0 = POSTGIS2SFCGALGeometry(input0);
	PG_FREE_IF_COPY(input0, 0);
	geom1 = POSTGIS2SFCGALGeometry(input1);
	PG_FREE_IF_COPY(input1, 1);

	result = sfcgal_geometry_distance(geom0, geom1);
	sfcgal_geometry_delete(geom0);
	sfcgal_geometry_delete(geom1);

	PG_RETURN_FLOAT8(result);
}

PG_FUNCTION_INFO_V1(sfcgal_distance3D);
Datum
sfcgal_distance3D(PG_FUNCTION_ARGS)
{
	GSERIALIZED *input0, *input1;
	sfcgal_geometry_t *geom0, *geom1;
	double result;

	sfcgal_postgis_init();

	input0 = PG_GETARG_GSERIALIZED_P(0);
	input1 = PG_GETARG_GSERIALIZED_P(1);
	geom0 = POSTGIS2SFCGALGeometry(input0);
	PG_FREE_IF_COPY(input0, 0);
	geom1 = POSTGIS2SFCGALGeometry(input1);
	PG_FREE_IF_COPY(input1, 1);

	result = sfcgal_geometry_distance_3d(geom0, geom1);
	sfcgal_geometry_delete(geom0);
	sfcgal_geometry_delete(geom1);

	PG_RETURN_FLOAT8(result);
}

PG_FUNCTION_INFO_V1(sfcgal_difference);
Datum
sfcgal_difference(PG_FUNCTION_ARGS)
{
	GSERIALIZED *input0, *input1, *output;
	sfcgal_geometry_t *geom0, *geom1;
	sfcgal_geometry_t *result;
	srid_t srid;

	sfcgal_postgis_init();

	input0 = (GSERIALIZED *)PG_DETOAST_DATUM(PG_GETARG_DATUM(0));
	srid = gserialized_get_srid(input0);
	input1 = (GSERIALIZED *)PG_DETOAST_DATUM(PG_GETARG_DATUM(1));
	geom0 = POSTGIS2SFCGALGeometry(input0);
	PG_FREE_IF_COPY(input0, 0);
	geom1 = POSTGIS2SFCGALGeometry(input1);
	PG_FREE_IF_COPY(input1, 1);

	result = sfcgal_geometry_difference(geom0, geom1);
	sfcgal_geometry_delete(geom0);
	sfcgal_geometry_delete(geom1);

	output = SFCGALGeometry2POSTGIS(result, 0, srid);
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
}

PG_FUNCTION_INFO_V1(sfcgal_difference3D);
Datum
sfcgal_difference3D(PG_FUNCTION_ARGS)
{
	GSERIALIZED *input0, *input1, *output;
	sfcgal_geometry_t *geom0, *geom1;
	sfcgal_geometry_t *result;
	srid_t srid;

	sfcgal_postgis_init();

	input0 = (GSERIALIZED *)PG_DETOAST_DATUM(PG_GETARG_DATUM(0));
	srid = gserialized_get_srid(input0);
	input1 = (GSERIALIZED *)PG_DETOAST_DATUM(PG_GETARG_DATUM(1));
	geom0 = POSTGIS2SFCGALGeometry(input0);
	PG_FREE_IF_COPY(input0, 0);
	geom1 = POSTGIS2SFCGALGeometry(input1);
	PG_FREE_IF_COPY(input1, 1);

	result = sfcgal_geometry_difference_3d(geom0, geom1);
	sfcgal_geometry_delete(geom0);
	sfcgal_geometry_delete(geom1);

	output = SFCGALGeometry2POSTGIS(result, 0, srid);
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
}

PG_FUNCTION_INFO_V1(sfcgal_union);
Datum
sfcgal_union(PG_FUNCTION_ARGS)
{
	GSERIALIZED *input0, *input1, *output;
	sfcgal_geometry_t *geom0, *geom1;
	sfcgal_geometry_t *result;
	srid_t srid;

	sfcgal_postgis_init();

	input0 = (GSERIALIZED *)PG_DETOAST_DATUM(PG_GETARG_DATUM(0));
	srid = gserialized_get_srid(input0);
	input1 = (GSERIALIZED *)PG_DETOAST_DATUM(PG_GETARG_DATUM(1));
	geom0 = POSTGIS2SFCGALGeometry(input0);
	PG_FREE_IF_COPY(input0, 0);
	geom1 = POSTGIS2SFCGALGeometry(input1);
	PG_FREE_IF_COPY(input1, 1);

	result = sfcgal_geometry_union(geom0, geom1);
	sfcgal_geometry_delete(geom0);
	sfcgal_geometry_delete(geom1);

	output = SFCGALGeometry2POSTGIS(result, 0, srid);
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
}

PG_FUNCTION_INFO_V1(sfcgal_union3D);
Datum
sfcgal_union3D(PG_FUNCTION_ARGS)
{
	GSERIALIZED *input0, *input1, *output;
	sfcgal_geometry_t *geom0, *geom1;
	sfcgal_geometry_t *result;
	srid_t srid;

	sfcgal_postgis_init();

	input0 = (GSERIALIZED *)PG_DETOAST_DATUM(PG_GETARG_DATUM(0));
	srid = gserialized_get_srid(input0);
	input1 = (GSERIALIZED *)PG_DETOAST_DATUM(PG_GETARG_DATUM(1));
	geom0 = POSTGIS2SFCGALGeometry(input0);
	PG_FREE_IF_COPY(input0, 0);
	geom1 = POSTGIS2SFCGALGeometry(input1);
	PG_FREE_IF_COPY(input1, 1);

	result = sfcgal_geometry_union_3d(geom0, geom1);
	sfcgal_geometry_delete(geom0);
	sfcgal_geometry_delete(geom1);

	output = SFCGALGeometry2POSTGIS(result, 0, srid);
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
}

PG_FUNCTION_INFO_V1(sfcgal_volume);
Datum
sfcgal_volume(PG_FUNCTION_ARGS)
{
	GSERIALIZED *input;
	sfcgal_geometry_t *geom;
	double result;

	sfcgal_postgis_init();

	input = (GSERIALIZED *)PG_DETOAST_DATUM(PG_GETARG_DATUM(0));
	geom = POSTGIS2SFCGALGeometry(input);

	result = sfcgal_geometry_volume(geom);
	sfcgal_geometry_delete(geom);

	PG_FREE_IF_COPY(input, 0);

	PG_RETURN_FLOAT8(result);
}

PG_FUNCTION_INFO_V1(sfcgal_minkowski_sum);
Datum
sfcgal_minkowski_sum(PG_FUNCTION_ARGS)
{
	GSERIALIZED *input0, *input1, *output;
	sfcgal_geometry_t *geom0, *geom1;
	sfcgal_geometry_t *result;
	srid_t srid;

	sfcgal_postgis_init();

	input0 = PG_GETARG_GSERIALIZED_P(0);
	srid = gserialized_get_srid(input0);
	input1 = PG_GETARG_GSERIALIZED_P(1);
	geom0 = POSTGIS2SFCGALGeometry(input0);
	PG_FREE_IF_COPY(input0, 0);
	geom1 = POSTGIS2SFCGALGeometry(input1);
	PG_FREE_IF_COPY(input1, 1);

	result = sfcgal_geometry_minkowski_sum(geom0, geom1);
	sfcgal_geometry_delete(geom0);
	sfcgal_geometry_delete(geom1);

	output = SFCGALGeometry2POSTGIS(result, 0, srid);
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
}

PG_FUNCTION_INFO_V1(sfcgal_extrude);
Datum
sfcgal_extrude(PG_FUNCTION_ARGS)
{
	GSERIALIZED *input, *output;
	sfcgal_geometry_t *geom;
	sfcgal_geometry_t *result;
	double dx, dy, dz;
	srid_t srid;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	srid = gserialized_get_srid(input);

	geom = POSTGIS2SFCGALGeometry(input);
	PG_FREE_IF_COPY(input, 0);

	dx = PG_GETARG_FLOAT8(1);
	dy = PG_GETARG_FLOAT8(2);
	dz = PG_GETARG_FLOAT8(3);

	result = sfcgal_geometry_extrude(geom, dx, dy, dz);
	sfcgal_geometry_delete(geom);

	output = SFCGALGeometry2POSTGIS(result, 0, srid);
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
}

PG_FUNCTION_INFO_V1(postgis_sfcgal_version);
Datum
postgis_sfcgal_version(PG_FUNCTION_ARGS)
{
	const char *ver = lwgeom_sfcgal_version();
	text *result = cstring_to_text(ver);
	PG_RETURN_POINTER(result);
}

#if POSTGIS_SFCGAL_VERSION >= 10400
PG_FUNCTION_INFO_V1(postgis_sfcgal_full_version);
Datum
postgis_sfcgal_full_version(PG_FUNCTION_ARGS)
{
	const char *ver = lwgeom_sfcgal_full_version();
	text *result = cstring_to_text(ver);
	PG_RETURN_POINTER(result);
}
#endif

PG_FUNCTION_INFO_V1(sfcgal_is_solid);
Datum
sfcgal_is_solid(PG_FUNCTION_ARGS)
{
	int result;
	GSERIALIZED *input = PG_GETARG_GSERIALIZED_P(0);
	LWGEOM *lwgeom = lwgeom_from_gserialized(input);
	PG_FREE_IF_COPY(input, 0);
	if (!lwgeom)
		elog(ERROR, "sfcgal_is_solid: Unable to deserialize input");

	result = lwgeom_is_solid(lwgeom);

	lwgeom_free(lwgeom);

	PG_RETURN_BOOL(result);
}

PG_FUNCTION_INFO_V1(sfcgal_make_solid);
Datum
sfcgal_make_solid(PG_FUNCTION_ARGS)
{
	GSERIALIZED *output;
	GSERIALIZED *input = PG_GETARG_GSERIALIZED_P(0);
	LWGEOM *lwgeom = lwgeom_from_gserialized(input);
	if (!lwgeom)
		elog(ERROR, "sfcgal_make_solid: Unable to deserialize input");

	FLAGS_SET_SOLID(lwgeom->flags, 1);

	output = geometry_serialize(lwgeom);
	lwgeom_free(lwgeom);
	PG_FREE_IF_COPY(input, 0);
	PG_RETURN_POINTER(output);
}

PG_FUNCTION_INFO_V1(postgis_sfcgal_noop);
Datum
postgis_sfcgal_noop(PG_FUNCTION_ARGS)
{
	GSERIALIZED *input, *output;
	LWGEOM *geom, *result;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	geom = lwgeom_from_gserialized(input);
	if (!geom)
		elog(ERROR, "sfcgal_noop: Unable to deserialize input");

	result = lwgeom_sfcgal_noop(geom);
	lwgeom_free(geom);
	if (!result)
		elog(ERROR, "sfcgal_noop: Unable to deserialize lwgeom");

	output = geometry_serialize(result);
	PG_FREE_IF_COPY(input, 0);
	PG_RETURN_POINTER(output);
}

PG_FUNCTION_INFO_V1(sfcgal_convexhull3D);
Datum
sfcgal_convexhull3D(PG_FUNCTION_ARGS)
{
	GSERIALIZED *input, *output;
	sfcgal_geometry_t *geom;
	sfcgal_geometry_t *result;
	srid_t srid;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	srid = gserialized_get_srid(input);
	geom = POSTGIS2SFCGALGeometry(input);
	PG_FREE_IF_COPY(input, 0);

	result = sfcgal_geometry_convexhull_3d(geom);
	sfcgal_geometry_delete(geom);

	output = SFCGALGeometry2POSTGIS(result, 0, srid);
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
}

PG_FUNCTION_INFO_V1(sfcgal_alphashape);
Datum
sfcgal_alphashape(PG_FUNCTION_ARGS)
{
#if POSTGIS_SFCGAL_VERSION < 10401
	lwpgerror(
	    "The SFCGAL version this PostGIS binary "
	    "was compiled against (%d) doesn't support "
	    "'sfcgal_geometry_alpha_shapes' function (1.4.1+ required)",
	    POSTGIS_SFCGAL_VERSION);
	PG_RETURN_NULL();
#else /* POSTGIS_SFCGAL_VERSION >= 10401 */
	GSERIALIZED *input, *output;
	sfcgal_geometry_t *geom;
	sfcgal_geometry_t *result;
	double alpha;
	bool allow_holes;
	srid_t srid;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	srid = gserialized_get_srid(input);
	geom = POSTGIS2SFCGALGeometry(input);
	PG_FREE_IF_COPY(input, 0);

	alpha = PG_GETARG_FLOAT8(1);
	allow_holes = PG_GETARG_BOOL(2);
	result = sfcgal_geometry_alpha_shapes(geom, alpha, allow_holes);
	sfcgal_geometry_delete(geom);

	output = SFCGALGeometry2POSTGIS(result, 0, srid);
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
#endif
}

PG_FUNCTION_INFO_V1(sfcgal_optimalalphashape);
Datum
sfcgal_optimalalphashape(PG_FUNCTION_ARGS)
{
#if POSTGIS_SFCGAL_VERSION < 10401
	lwpgerror(
	    "The SFCGAL version this PostGIS binary "
	    "was compiled against (%d) doesn't support "
	    "'sfcgal_geometry_optimal_alpha_shapes' function (1.4.1+ required)",
	    POSTGIS_SFCGAL_VERSION);
	PG_RETURN_NULL();
#else /* POSTGIS_SFCGAL_VERSION >= 10401 */
	GSERIALIZED *input, *output;
	sfcgal_geometry_t *geom;
	sfcgal_geometry_t *result;
	bool allow_holes;
	size_t nb_components;
	srid_t srid;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	srid = gserialized_get_srid(input);
	geom = POSTGIS2SFCGALGeometry(input);
	PG_FREE_IF_COPY(input, 0);

	allow_holes = PG_GETARG_BOOL(1);
	nb_components = (size_t)PG_GETARG_INT32(2);
	result = sfcgal_geometry_optimal_alpha_shapes(geom, allow_holes, nb_components);
	sfcgal_geometry_delete(geom);

	output = SFCGALGeometry2POSTGIS(result, 0, srid);
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
#endif
}

PG_FUNCTION_INFO_V1(sfcgal_ymonotonepartition);
Datum
sfcgal_ymonotonepartition(PG_FUNCTION_ARGS)
{
#if POSTGIS_SFCGAL_VERSION < 10500
	lwpgerror(
	    "The SFCGAL version this PostGIS binary "
	    "was compiled against (%d) doesn't support "
	    "'sfcgal_y_monotone_partition_2' function (1.5.0+ required)",
	    POSTGIS_SFCGAL_VERSION);
	PG_RETURN_NULL();
#else /* POSTGIS_SFCGAL_VERSION >= 10500 */
	GSERIALIZED *input, *output;
	sfcgal_geometry_t *geom;
	sfcgal_geometry_t *result;
	srid_t srid;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	srid = gserialized_get_srid(input);
	geom = POSTGIS2SFCGALGeometry(input);
	PG_FREE_IF_COPY(input, 0);

	result = sfcgal_y_monotone_partition_2(geom);
	sfcgal_geometry_delete(geom);

	output = SFCGALGeometry2POSTGIS(result, 0, srid);
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
#endif
}

PG_FUNCTION_INFO_V1(sfcgal_approxconvexpartition);
Datum
sfcgal_approxconvexpartition(PG_FUNCTION_ARGS)
{
#if POSTGIS_SFCGAL_VERSION < 10500
	lwpgerror(
	    "The SFCGAL version this PostGIS binary "
	    "was compiled against (%d) doesn't support "
	    "'sfcgal_approx_convex_partition_2' function (1.5.0+ required)",
	    POSTGIS_SFCGAL_VERSION);
	PG_RETURN_NULL();
#else /* POSTGIS_SFCGAL_VERSION >= 10500 */
	GSERIALIZED *input, *output;
	sfcgal_geometry_t *geom;
	sfcgal_geometry_t *result;
	srid_t srid;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	srid = gserialized_get_srid(input);
	geom = POSTGIS2SFCGALGeometry(input);
	PG_FREE_IF_COPY(input, 0);

	result = sfcgal_approx_convex_partition_2(geom);
	sfcgal_geometry_delete(geom);

	output = SFCGALGeometry2POSTGIS(result, 0, srid);
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
#endif
}

PG_FUNCTION_INFO_V1(sfcgal_greeneapproxconvexpartition);
Datum
sfcgal_greeneapproxconvexpartition(PG_FUNCTION_ARGS)
{
#if POSTGIS_SFCGAL_VERSION < 10500
	lwpgerror(
	    "The SFCGAL version this PostGIS binary "
	    "was compiled against (%d) doesn't support "
	    "'sfcgal_greene_approx_convex_partition_2' function (1.5.0+ required)",
	    POSTGIS_SFCGAL_VERSION);
	PG_RETURN_NULL();
#else /* POSTGIS_SFCGAL_VERSION >= 10500 */
	GSERIALIZED *input, *output;
	sfcgal_geometry_t *geom;
	sfcgal_geometry_t *result;
	srid_t srid;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	srid = gserialized_get_srid(input);
	geom = POSTGIS2SFCGALGeometry(input);
	PG_FREE_IF_COPY(input, 0);

	result = sfcgal_greene_approx_convex_partition_2(geom);
	sfcgal_geometry_delete(geom);

	output = SFCGALGeometry2POSTGIS(result, 0, srid);
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
#endif
}

PG_FUNCTION_INFO_V1(sfcgal_optimalconvexpartition);
Datum
sfcgal_optimalconvexpartition(PG_FUNCTION_ARGS)
{
#if POSTGIS_SFCGAL_VERSION < 10500
	lwpgerror(
	    "The SFCGAL version this PostGIS binary "
	    "was compiled against (%d) doesn't support "
	    "'sfcgal_optimal_convex_partition_2' function (1.5.0+ required)",
	    POSTGIS_SFCGAL_VERSION);
	PG_RETURN_NULL();
#else /* POSTGIS_SFCGAL_VERSION >= 10500 */
	GSERIALIZED *input, *output;
	sfcgal_geometry_t *geom;
	sfcgal_geometry_t *result;
	srid_t srid;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	srid = gserialized_get_srid(input);
	geom = POSTGIS2SFCGALGeometry(input);
	PG_FREE_IF_COPY(input, 0);

	result = sfcgal_optimal_convex_partition_2(geom);
	sfcgal_geometry_delete(geom);

	output = SFCGALGeometry2POSTGIS(result, 0, srid);
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
#endif
}

PG_FUNCTION_INFO_V1(sfcgal_extrudestraightskeleton);
Datum
sfcgal_extrudestraightskeleton(PG_FUNCTION_ARGS)
{
#if POSTGIS_SFCGAL_VERSION < 10500
	lwpgerror(
	    "The SFCGAL version this PostGIS binary "
	    "was compiled against (%d) doesn't support "
	    "'sfcgal_extrude_straigth_skeleton' function (1.5.0+ required)",
	    POSTGIS_SFCGAL_VERSION);
	PG_RETURN_NULL();
#else /* POSTGIS_SFCGAL_VERSION >= 10500 */


	GSERIALIZED *input, *output;
	sfcgal_geometry_t *geom;
	sfcgal_geometry_t *result;
	double building_height, roof_height;
	srid_t srid;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	srid = gserialized_get_srid(input);
#if POSTGIS_SFCGAL_VERSION < 20200
	if (gserialized_is_empty(input))
	{
		result = sfcgal_polyhedral_surface_create();
		output = SFCGALGeometry2POSTGIS(result, 0, srid);
		sfcgal_geometry_delete(result);

		PG_FREE_IF_COPY(input, 0);
		PG_RETURN_POINTER(output);
	}
#endif

	geom = POSTGIS2SFCGALGeometry(input);
	PG_FREE_IF_COPY(input, 0);

	roof_height = PG_GETARG_FLOAT8(1);
	building_height = PG_GETARG_FLOAT8(2);
	if (building_height <= 0.0)
	{
		result = sfcgal_geometry_extrude_straight_skeleton(geom, roof_height);
	}
	else
	{
		result = sfcgal_geometry_extrude_polygon_straight_skeleton(geom, building_height, roof_height);
	}
	sfcgal_geometry_delete(geom);

	output = SFCGALGeometry2POSTGIS(result, 0, srid);
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
#endif
}

PG_FUNCTION_INFO_V1(sfcgal_visibility_point);
Datum
sfcgal_visibility_point(PG_FUNCTION_ARGS)
{
#if POSTGIS_SFCGAL_VERSION < 10500
	lwpgerror(
	    "The SFCGAL version this PostGIS binary "
	    "was compiled against (%d) doesn't support "
	    "'sfcgal_visibility_point' function (1.5.0+ required)",
	    POSTGIS_SFCGAL_VERSION);
	PG_RETURN_NULL();
#else /* POSTGIS_SFCGAL_VERSION >= 10500 */
	GSERIALIZED *input0, *input1, *output;
	sfcgal_geometry_t *polygon, *point;
	sfcgal_geometry_t *result;
	srid_t srid;

	sfcgal_postgis_init();

	input0 = PG_GETARG_GSERIALIZED_P(0);
	srid = gserialized_get_srid(input0);
	input1 = PG_GETARG_GSERIALIZED_P(1);
	polygon = POSTGIS2SFCGALGeometry(input0);
	PG_FREE_IF_COPY(input0, 0);
	point = POSTGIS2SFCGALGeometry(input1);
	PG_FREE_IF_COPY(input1, 1);

#if POSTGIS_SFCGAL_VERSION < 20200
	if (gserialized_is_empty(input0) || gserialized_is_empty(input1))
	{
		result = sfcgal_polygon_create();
		output = SFCGALGeometry2POSTGIS(result, 0, srid);
		sfcgal_geometry_delete(result);

		PG_RETURN_POINTER(output);
	}
#endif

	result = sfcgal_geometry_visibility_point(polygon, point);
	sfcgal_geometry_delete(polygon);
	sfcgal_geometry_delete(point);

	output = SFCGALGeometry2POSTGIS(result, 0, srid);
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
#endif
}

PG_FUNCTION_INFO_V1(sfcgal_visibility_segment);
Datum
sfcgal_visibility_segment(PG_FUNCTION_ARGS)
{
#if POSTGIS_SFCGAL_VERSION < 10500
	lwpgerror(
	    "The SFCGAL version this PostGIS binary "
	    "was compiled against (%d) doesn't support "
	    "'sfcgal_visibility_segment' function (1.5.0+ required)",
	    POSTGIS_SFCGAL_VERSION);
	PG_RETURN_NULL();
#else /* POSTGIS_SFCGAL_VERSION >= 10500 */
	GSERIALIZED *input0, *input1, *input2, *output;
	sfcgal_geometry_t *polygon, *pointA, *pointB;
	sfcgal_geometry_t *result;
	srid_t srid;

	sfcgal_postgis_init();

	input0 = PG_GETARG_GSERIALIZED_P(0);
	srid = gserialized_get_srid(input0);
	input1 = PG_GETARG_GSERIALIZED_P(1);
	input2 = PG_GETARG_GSERIALIZED_P(2);
	polygon = POSTGIS2SFCGALGeometry(input0);
	PG_FREE_IF_COPY(input0, 0);
	pointA = POSTGIS2SFCGALGeometry(input1);
	PG_FREE_IF_COPY(input1, 1);
	pointB = POSTGIS2SFCGALGeometry(input2);
	PG_FREE_IF_COPY(input2, 2);

#if POSTGIS_SFCGAL_VERSION < 20200
	if (gserialized_is_empty(input0) || gserialized_is_empty(input1) || gserialized_is_empty(input2))
	{
		result = sfcgal_polygon_create();
		output = SFCGALGeometry2POSTGIS(result, 0, srid);
		sfcgal_geometry_delete(result);

		PG_RETURN_POINTER(output);
	}
#endif

	result = sfcgal_geometry_visibility_segment(polygon, pointA, pointB);
	sfcgal_geometry_delete(polygon);
	sfcgal_geometry_delete(pointA);
	sfcgal_geometry_delete(pointB);

	output = SFCGALGeometry2POSTGIS(result, 0, srid);
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
#endif
}

PG_FUNCTION_INFO_V1(sfcgal_rotate);
Datum
sfcgal_rotate(PG_FUNCTION_ARGS)
{
#if POSTGIS_SFCGAL_VERSION < 20000
	lwpgerror(
	    "The SFCGAL version this PostGIS binary was compiled against (%d) doesn't support "
	    "'sfcgal_geometry_rotate' function (requires SFCGAL 2.0.0+)",
	    POSTGIS_SFCGAL_VERSION);
	PG_RETURN_NULL();
#else /* POSTGIS_SFCGAL_VERSION >= 20000 */
	GSERIALIZED *input, *output;
	sfcgal_geometry_t *geom, *result;
	double angle;
	srid_t srid;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	angle = PG_GETARG_FLOAT8(1);
	srid = gserialized_get_srid(input);

	geom = POSTGIS2SFCGALGeometry(input);
	PG_FREE_IF_COPY(input, 0);

	result = sfcgal_geometry_rotate(geom, angle);
	sfcgal_geometry_delete(geom);

	output = SFCGALGeometry2POSTGIS(result, 0, srid);
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
#endif
}

PG_FUNCTION_INFO_V1(sfcgal_rotate_2d);
Datum
sfcgal_rotate_2d(PG_FUNCTION_ARGS)
{
#if POSTGIS_SFCGAL_VERSION < 20000
	lwpgerror(
	    "The SFCGAL version this PostGIS binary was compiled against (%d) doesn't support "
	    "'sfcgal_geometry_rotate_2d' function (requires SFCGAL 2.0.0+)",
	    POSTGIS_SFCGAL_VERSION);
	PG_RETURN_NULL();
#else /* POSTGIS_SFCGAL_VERSION >= 20000 */
	GSERIALIZED *input, *output;
	sfcgal_geometry_t *geom, *result;
	double angle, cx, cy;
	srid_t srid;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	angle = PG_GETARG_FLOAT8(1);
	cx = PG_GETARG_FLOAT8(2);
	cy = PG_GETARG_FLOAT8(3);
	srid = gserialized_get_srid(input);

	geom = POSTGIS2SFCGALGeometry(input);
	PG_FREE_IF_COPY(input, 0);

	result = sfcgal_geometry_rotate_2d(geom, angle, cx, cy);
	sfcgal_geometry_delete(geom);

	output = SFCGALGeometry2POSTGIS(result, 0, srid);
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
#endif
}

PG_FUNCTION_INFO_V1(sfcgal_rotate_3d);
Datum
sfcgal_rotate_3d(PG_FUNCTION_ARGS)
{
#if POSTGIS_SFCGAL_VERSION < 20000
	lwpgerror(
	    "The SFCGAL version this PostGIS binary was compiled against (%d) doesn't support "
	    "'sfcgal_geometry_rotate_3d' function (requires SFCGAL 2.0.0+)",
	    POSTGIS_SFCGAL_VERSION);
	PG_RETURN_NULL();
#else /* POSTGIS_SFCGAL_VERSION >= 20000 */
	GSERIALIZED *input, *output;
	sfcgal_geometry_t *geom, *result;
	double angle, ax, ay, az;
	srid_t srid;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	angle = PG_GETARG_FLOAT8(1);
	ax = PG_GETARG_FLOAT8(2);
	ay = PG_GETARG_FLOAT8(3);
	az = PG_GETARG_FLOAT8(4);
	srid = gserialized_get_srid(input);

	geom = POSTGIS2SFCGALGeometry(input);
	PG_FREE_IF_COPY(input, 0);

	result = sfcgal_geometry_rotate_3d(geom, angle, ax, ay, az);
	sfcgal_geometry_delete(geom);

	output = SFCGALGeometry2POSTGIS(result, 1, srid); // Force 3D output
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
#endif
}

PG_FUNCTION_INFO_V1(sfcgal_rotate_x);
Datum
sfcgal_rotate_x(PG_FUNCTION_ARGS)
{
#if POSTGIS_SFCGAL_VERSION < 20000
	lwpgerror(
	    "The SFCGAL version this PostGIS binary was compiled against (%d) doesn't support "
	    "'sfcgal_geometry_rotate_x' function (requires SFCGAL 2.0.0+)",
	    POSTGIS_SFCGAL_VERSION);
	PG_RETURN_NULL();
#else /* POSTGIS_SFCGAL_VERSION >= 20000 */
	GSERIALIZED *input, *output;
	sfcgal_geometry_t *geom, *result;
	double angle;
	srid_t srid;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	angle = PG_GETARG_FLOAT8(1);
	srid = gserialized_get_srid(input);

	geom = POSTGIS2SFCGALGeometry(input);
	PG_FREE_IF_COPY(input, 0);

	result = sfcgal_geometry_rotate_x(geom, angle);
	sfcgal_geometry_delete(geom);

	output = SFCGALGeometry2POSTGIS(result, 1, srid); // Force 3D output
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
#endif
}

PG_FUNCTION_INFO_V1(sfcgal_rotate_y);
Datum
sfcgal_rotate_y(PG_FUNCTION_ARGS)
{
#if POSTGIS_SFCGAL_VERSION < 20000
	lwpgerror(
	    "The SFCGAL version this PostGIS binary was compiled against (%d) doesn't support "
	    "'sfcgal_geometry_rotate_y' function (requires SFCGAL 2.0.0+)",
	    POSTGIS_SFCGAL_VERSION);
	PG_RETURN_NULL();
#else /* POSTGIS_SFCGAL_VERSION >= 20000 */
	GSERIALIZED *input, *output;
	sfcgal_geometry_t *geom, *result;
	double angle;
	srid_t srid;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	angle = PG_GETARG_FLOAT8(1);
	srid = gserialized_get_srid(input);

	geom = POSTGIS2SFCGALGeometry(input);
	PG_FREE_IF_COPY(input, 0);

	result = sfcgal_geometry_rotate_y(geom, angle);
	sfcgal_geometry_delete(geom);

	output = SFCGALGeometry2POSTGIS(result, 1, srid); // Force 3D output
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
#endif
}

PG_FUNCTION_INFO_V1(sfcgal_rotate_z);
Datum
sfcgal_rotate_z(PG_FUNCTION_ARGS)
{
#if POSTGIS_SFCGAL_VERSION < 20000
	lwpgerror(
	    "The SFCGAL version this PostGIS binary was compiled against (%d) doesn't support "
	    "'sfcgal_geometry_rotate_z' function (requires SFCGAL 2.0.0+)",
	    POSTGIS_SFCGAL_VERSION);
	PG_RETURN_NULL();
#else /* POSTGIS_SFCGAL_VERSION >= 20000 */
	GSERIALIZED *input, *output;
	sfcgal_geometry_t *geom, *result;
	double angle;
	srid_t srid;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	angle = PG_GETARG_FLOAT8(1);
	srid = gserialized_get_srid(input);

	geom = POSTGIS2SFCGALGeometry(input);
	PG_FREE_IF_COPY(input, 0);

	result = sfcgal_geometry_rotate_z(geom, angle);
	sfcgal_geometry_delete(geom);

	output = SFCGALGeometry2POSTGIS(result, 1, srid); // Force 3D output
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
#endif
}

PG_FUNCTION_INFO_V1(sfcgal_scale);
Datum
sfcgal_scale(PG_FUNCTION_ARGS)
{
#if POSTGIS_SFCGAL_VERSION < 20000
	lwpgerror(
	    "The SFCGAL version this PostGIS binary was compiled against (%d) doesn't support "
	    "'sfcgal_geometry_scale' function (requires SFCGAL 2.0.0+)",
	    POSTGIS_SFCGAL_VERSION);
	PG_RETURN_NULL();
#else /* POSTGIS_SFCGAL_VERSION >= 20000 */
	GSERIALIZED *input, *output;
	sfcgal_geometry_t *geom, *result;
	double scale_factor;
	srid_t srid;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	scale_factor = PG_GETARG_FLOAT8(1);
	srid = gserialized_get_srid(input);

	geom = POSTGIS2SFCGALGeometry(input);
	PG_FREE_IF_COPY(input, 0);

	result = sfcgal_geometry_scale(geom, scale_factor);
	sfcgal_geometry_delete(geom);

	output = SFCGALGeometry2POSTGIS(result, 0, srid);
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
#endif
}

PG_FUNCTION_INFO_V1(sfcgal_scale_3d);
Datum
sfcgal_scale_3d(PG_FUNCTION_ARGS)
{
#if POSTGIS_SFCGAL_VERSION < 20000
	lwpgerror(
	    "The SFCGAL version this PostGIS binary was compiled against (%d) doesn't support "
	    "'sfcgal_geometry_scale_3d' function (requires SFCGAL 2.0.0+)",
	    POSTGIS_SFCGAL_VERSION);
	PG_RETURN_NULL();
#else /* POSTGIS_SFCGAL_VERSION >= 20000 */
	GSERIALIZED *input, *output;
	sfcgal_geometry_t *geom, *result;
	double sx, sy, sz;
	srid_t srid;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	sx = PG_GETARG_FLOAT8(1);
	sy = PG_GETARG_FLOAT8(2);
	sz = PG_GETARG_FLOAT8(3);
	srid = gserialized_get_srid(input);

	geom = POSTGIS2SFCGALGeometry(input);
	PG_FREE_IF_COPY(input, 0);

	result = sfcgal_geometry_scale_3d(geom, sx, sy, sz);
	sfcgal_geometry_delete(geom);

	output = SFCGALGeometry2POSTGIS(result, 1, srid); // Force 3D output
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
#endif
}

PG_FUNCTION_INFO_V1(sfcgal_scale_3d_around_center);
Datum
sfcgal_scale_3d_around_center(PG_FUNCTION_ARGS)
{
#if POSTGIS_SFCGAL_VERSION < 20000
	lwpgerror(
	    "The SFCGAL version this PostGIS binary was compiled against (%d) doesn't support "
	    "'sfcgal_geometry_scale_3d_around_center' function (requires SFCGAL 2.0.0+)",
	    POSTGIS_SFCGAL_VERSION);
	PG_RETURN_NULL();
#else /* POSTGIS_SFCGAL_VERSION >= 20000 */
	GSERIALIZED *input, *output;
	sfcgal_geometry_t *geom, *result;
	double sx, sy, sz, cx, cy, cz;
	srid_t srid;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	sx = PG_GETARG_FLOAT8(1);
	sy = PG_GETARG_FLOAT8(2);
	sz = PG_GETARG_FLOAT8(3);
	cx = PG_GETARG_FLOAT8(4);
	cy = PG_GETARG_FLOAT8(5);
	cz = PG_GETARG_FLOAT8(6);
	srid = gserialized_get_srid(input);

	geom = POSTGIS2SFCGALGeometry(input);
	PG_FREE_IF_COPY(input, 0);

	result = sfcgal_geometry_scale_3d_around_center(geom, sx, sy, sz, cx, cy, cz);
	sfcgal_geometry_delete(geom);

	output = SFCGALGeometry2POSTGIS(result, 1, srid); // Force 3D output
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
#endif
}

PG_FUNCTION_INFO_V1(sfcgal_translate_2d);
Datum
sfcgal_translate_2d(PG_FUNCTION_ARGS)
{
#if POSTGIS_SFCGAL_VERSION < 20000
	lwpgerror(
	    "The SFCGAL version this PostGIS binary was compiled against (%d) doesn't support "
	    "'sfcgal_geometry_translate_2d' function (requires SFCGAL 2.0.0+)",
	    POSTGIS_SFCGAL_VERSION);
	PG_RETURN_NULL();
#else /* POSTGIS_SFCGAL_VERSION >= 20000 */
	GSERIALIZED *input, *output;
	sfcgal_geometry_t *geom, *result;
	double dx, dy;
	srid_t srid;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P_COPY(0);
	dx = PG_GETARG_FLOAT8(1);
	dy = PG_GETARG_FLOAT8(2);
	srid = gserialized_get_srid(input);

	geom = POSTGIS2SFCGALGeometry(input);

	result = sfcgal_geometry_translate_2d(geom, dx, dy);
	sfcgal_geometry_delete(geom);

	output = SFCGALGeometry2POSTGIS(result, 0, srid);
	sfcgal_geometry_delete(result);

	PG_FREE_IF_COPY(input, 0);

	PG_RETURN_POINTER(output);
#endif
}

PG_FUNCTION_INFO_V1(sfcgal_translate_3d);
Datum
sfcgal_translate_3d(PG_FUNCTION_ARGS)
{
#if POSTGIS_SFCGAL_VERSION < 20000
	lwpgerror(
	    "The SFCGAL version this PostGIS binary was compiled against (%d) doesn't support "
	    "'sfcgal_geometry_translate_3d' function (requires SFCGAL 2.0.0+)",
	    POSTGIS_SFCGAL_VERSION);
	PG_RETURN_NULL();
#else /* POSTGIS_SFCGAL_VERSION >= 20000 */
	GSERIALIZED *input, *output;
	sfcgal_geometry_t *geom, *result;
	double dx, dy, dz;
	srid_t srid;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P_COPY(0);
	dx = PG_GETARG_FLOAT8(1);
	dy = PG_GETARG_FLOAT8(2);
	dz = PG_GETARG_FLOAT8(3);
	srid = gserialized_get_srid(input);

	geom = POSTGIS2SFCGALGeometry(input);

	result = sfcgal_geometry_translate_3d(geom, dx, dy, dz);
	sfcgal_geometry_delete(geom);

	output = SFCGALGeometry2POSTGIS(result, 1, srid); // Force 3D output
	sfcgal_geometry_delete(result);

	PG_FREE_IF_COPY(input, 0);

	PG_RETURN_POINTER(output);
#endif
}

PG_FUNCTION_INFO_V1(sfcgal_straight_skeleton_partition);
Datum
sfcgal_straight_skeleton_partition(PG_FUNCTION_ARGS)
{
#if POSTGIS_SFCGAL_VERSION < 20000
	lwpgerror(
	    "The SFCGAL version this PostGIS binary was compiled against (%d) doesn't support "
	    "'sfcgal_geometry_straight_skeleton_partition' function (requires SFCGAL 2.0.0+)",
	    POSTGIS_SFCGAL_VERSION);
	PG_RETURN_NULL();
#else /* POSTGIS_SFCGAL_VERSION >= 20000 */
	GSERIALIZED *input, *output;
	sfcgal_geometry_t *geom, *result;
	srid_t srid;
	bool auto_orientation;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	auto_orientation = PG_GETARG_BOOL(1);
	srid = gserialized_get_srid(input);

	geom = POSTGIS2SFCGALGeometry(input);
	PG_FREE_IF_COPY(input, 0);

	result = sfcgal_geometry_straight_skeleton_partition(geom, auto_orientation);
	sfcgal_geometry_delete(geom);

	output = SFCGALGeometry2POSTGIS(result, 0, srid);
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
#endif
}

PG_FUNCTION_INFO_V1(sfcgal_buffer3d);
Datum
sfcgal_buffer3d(PG_FUNCTION_ARGS)
{
#if POSTGIS_SFCGAL_VERSION < 20000
	lwpgerror(
	    "The SFCGAL version this PostGIS binary was compiled against (%d) doesn't support "
	    "'sfcgal_geometry_buffer3d' function (requires SFCGAL 2.0.0+)",
	    POSTGIS_SFCGAL_VERSION);
	PG_RETURN_NULL();
#else /* POSTGIS_SFCGAL_VERSION >= 20000 */
	GSERIALIZED *input, *output;
	sfcgal_geometry_t *geom = NULL, *result;
	double radius;
	int segments;
	int buffer_type_int;
	sfcgal_buffer3d_type_t buffer_type;
	srid_t srid;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	radius = PG_GETARG_FLOAT8(1);
	segments = PG_GETARG_INT32(2);
	buffer_type_int = PG_GETARG_INT32(3);
	srid = gserialized_get_srid(input);

	if (buffer_type_int < 0 || buffer_type_int > 2)
		ereport(ERROR, (errmsg("Invalid buffer type")));

	buffer_type = (sfcgal_buffer3d_type_t)buffer_type_int;

	if (gserialized_is_empty(input))
	{
		result = sfcgal_polyhedral_surface_create();
	}
	else
	{

		geom = POSTGIS2SFCGALGeometry(input);
		PG_FREE_IF_COPY(input, 0);
		result = sfcgal_geometry_buffer3d(geom, radius, segments, buffer_type);
		sfcgal_geometry_delete(geom);
	}

	output = SFCGALGeometry2POSTGIS(result, 1, srid); // force 3d output
	sfcgal_geometry_delete(result);
	PG_RETURN_POINTER(output);
#endif
}

PG_FUNCTION_INFO_V1(sfcgal_simplify);
Datum
sfcgal_simplify(PG_FUNCTION_ARGS)
{
#if POSTGIS_SFCGAL_VERSION < 20100
	lwpgerror(
	    "The SFCGAL version this PostGIS binary was compiled against (%d) doesn't support "
	    "'sfcgal_geometry_simplify' function (requires SFCGAL 2.1.0+)",
	    POSTGIS_SFCGAL_VERSION);
	PG_RETURN_NULL();
#else /* POSTGIS_SFCGAL_VERSION >= 20100 */
	GSERIALIZED *input, *output;
	sfcgal_geometry_t *geom, *result;
	double threshold;
	bool preserveTopology;
	srid_t srid;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	threshold = PG_GETARG_FLOAT8(1);
	preserveTopology = PG_GETARG_BOOL(2);
	srid = gserialized_get_srid(input);

	geom = POSTGIS2SFCGALGeometry(input);
	PG_FREE_IF_COPY(input, 0);

	result = sfcgal_geometry_simplify(geom, threshold, preserveTopology);
	sfcgal_geometry_delete(geom);

	output = SFCGALGeometry2POSTGIS(result, 0, srid);
	sfcgal_geometry_delete(result);

	PG_RETURN_POINTER(output);
#endif
}

PG_FUNCTION_INFO_V1(sfcgal_alphawrapping_3d);
/**
 * Compute a 3D alpha-wrapping (alpha shape) of the input geometry and return it as a 3D PostGIS geometry.
 *
 * If the input is empty, returns an empty polyhedral surface. The function preserves the input SRID and
 * always returns a forced-3D GSERIALIZED geometry. Requires SFCGAL >= 2.1.0 (PostGIS SFCGAL version 20100+);
 * when compiled against an older SFCGAL the function returns NULL.
 *
 * @param input GSERIALIZED* input geometry to wrap.
 * @param relative_alpha Integer controlling the alpha parameter relative to the input scale (higher values produce coarser wraps).
 * @param relative_offset Integer offset applied when computing the alpha parameter.
 * @return Datum pointer to a GSERIALIZED 3D geometry containing the alpha-wrapped result (caller receives a PostgreSQL Datum).
 */
Datum
sfcgal_alphawrapping_3d(PG_FUNCTION_ARGS)
{
#if POSTGIS_SFCGAL_VERSION < 20100
	lwpgerror(
	    "The SFCGAL version this PostGIS binary was compiled against (%d) doesn't support "
	    "'sfcgal_geometry_alphawrapping3d' function (requires SFCGAL 2.1.0+)",
	    POSTGIS_SFCGAL_VERSION);
	PG_RETURN_NULL();
#else /* POSTGIS_SFCGAL_VERSION >= 20100 */
	GSERIALIZED *input, *output;
	sfcgal_geometry_t *geom = NULL, *result;
	size_t relative_alpha;
	size_t relative_offset;
	srid_t srid;

	sfcgal_postgis_init();

	input = PG_GETARG_GSERIALIZED_P(0);
	relative_alpha = (size_t)PG_GETARG_INT32(1);
	relative_offset = (size_t)PG_GETARG_INT32(2);
	srid = gserialized_get_srid(input);

	if (gserialized_is_empty(input))
	{
		result = sfcgal_polyhedral_surface_create();
	}
	else
	{

		geom = POSTGIS2SFCGALGeometry(input);
		PG_FREE_IF_COPY(input, 0);
		result = sfcgal_geometry_alpha_wrapping_3d(geom, relative_alpha, relative_offset);
		sfcgal_geometry_delete(geom);
	}

	output = SFCGALGeometry2POSTGIS(result, 1, srid); // force 3d output
	sfcgal_geometry_delete(result);
	PG_RETURN_POINTER(output);
#endif
}

/* NURBS curve support functions using native SFCGAL NURBS API */


/* CG_NurbsCurveFromPoints - Create NURBS curve from control points */
PG_FUNCTION_INFO_V1(sfcgal_postgis_nurbs_curve_from_points);
/**
 * Create a PostGIS NURBS curve from a sequence of control points.
 *
 * Takes a serialized PostGIS geometry of control points (LINESTRING or MULTIPOINT)
 * and an integer degree, constructs a NURBS curve using SFCGAL, and returns the
 * resulting PostGIS NURBS geometry (GSERIALIZED) preserving the input SRID.
 *
 * Detailed behavior:
 * - The first argument must be a GSERIALIZED LINESTRING or MULTIPOINT containing
 *   the control points. For MULTIPOINT, each member must contain a single point.
 * - The second argument is the NURBS degree and must be between 1 and 10
 *   (inclusive). At least degree+1 control points are required.
 * - Point dimensionality (XY, XYZ, XYM, XYZM) is preserved when creating SFCGAL
 *   points from the input coordinates.
 * - Requires SFCGAL NURBS support (compiled with SFCGAL 2.3.0+); if unavailable,
 *   the function returns NULL at runtime in the compiled binary path guarded by
 *   the build-time version check.
 *
 * Errors:
 * - Raises ERROR for invalid parameter values (invalid degree, wrong geometry
 *   type, empty MULTIPOINT, insufficient control points) and for internal
 *   failures during NURBS creation or conversion back to PostGIS.
 *
 * Return:
 * - GSERIALIZED pointer to the created NURBS curve on success.
 */
Datum
sfcgal_postgis_nurbs_curve_from_points(PG_FUNCTION_ARGS)
{
#if POSTGIS_SFCGAL_VERSION < 20300
	lwpgerror(
		"The SFCGAL version this PostGIS binary was compiled against (%d) doesn't support "
		"'sfcgal_nurbs_curve_from_points' function (requires SFCGAL 2.3.0+)",
		POSTGIS_SFCGAL_VERSION);
	PG_RETURN_NULL();
#else /* POSTGIS_SFCGAL_VERSION >= 20300 */
	GSERIALIZED *input, *output;
	LWGEOM *lwgeom;
	LWLINE *line;
	int32_t degree;
	sfcgal_geometry_t **points;
	sfcgal_geometry_t *sfcgal_nurbs;
	LWNURBSCURVE *result_nurbs;
	uint32_t i;
	POINT4D pt;
	srid_t srid;
	POINTARRAY *pa;
	uint32_t npoints;

	sfcgal_postgis_init();

	if (PG_ARGISNULL(0) || PG_ARGISNULL(1))
		PG_RETURN_NULL();

	input = PG_GETARG_GSERIALIZED_P(0);
	degree = PG_GETARG_INT32(1);
	srid = gserialized_get_srid(input);

	/* Validate degree */
	if (degree < 1 || degree > 10)
	{
		PG_FREE_IF_COPY(input, 0);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("NURBS degree must be between 1 and 10")));
	}

	/* Pre-validate that we'll have enough control points */
	lwgeom = lwgeom_from_gserialized(input);
	if (lwgeom && lwgeom->type == LINETYPE)
	{
		LWLINE *temp_line = (LWLINE*)lwgeom;
		if (temp_line->points && temp_line->points->npoints < degree + 1)
		{
			lwgeom_free(lwgeom);
			PG_FREE_IF_COPY(input, 0);
			ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					errmsg("Need at least %d control points for degree %d NURBS",
						degree + 1, degree)));
		}
	}
	/* Continue with existing validation... */
	if (!lwgeom || (lwgeom->type != LINETYPE && lwgeom->type != MULTIPOINTTYPE))
	{
		if (lwgeom) lwgeom_free(lwgeom);
		PG_FREE_IF_COPY(input, 0);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("Control points must be a LINESTRING or MULTIPOINT")));
	}

	/* Get point array based on geometry type */
	if (lwgeom->type == LINETYPE)
	{
		line = (LWLINE*)lwgeom;
		pa = line->points;
		npoints = pa->npoints;
	}
	else /* MULTIPOINTTYPE */
	{
		LWMPOINT *mpoint = (LWMPOINT*)lwgeom;
		if (!mpoint->ngeoms)
		{
			lwgeom_free(lwgeom);
			PG_FREE_IF_COPY(input, 0);
			ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					errmsg("MULTIPOINT must contain at least one point")));
		}

		/* Create a temporary point array from multipoint */
		pa = ptarray_construct_empty(FLAGS_GET_Z(lwgeom->flags), FLAGS_GET_M(lwgeom->flags), mpoint->ngeoms);
		for (i = 0; i < mpoint->ngeoms; i++)
		{
			LWPOINT *pt_geom = mpoint->geoms[i];
			if (pt_geom && pt_geom->point && pt_geom->point->npoints == 1)
			{
				getPoint4d_p(pt_geom->point, 0, &pt);
				ptarray_append_point(pa, &pt, LW_TRUE);
			}
		}
		npoints = pa->npoints;
	}

	if (!pa || npoints < degree + 1)
	{
		if (lwgeom->type == MULTIPOINTTYPE && pa) ptarray_free(pa);
		lwgeom_free(lwgeom);
		PG_FREE_IF_COPY(input, 0);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("Need at least %d control points for degree %d NURBS",
					degree + 1, degree)));
	}

	/* Convert to SFCGAL points */
	points = (sfcgal_geometry_t**)palloc(sizeof(sfcgal_geometry_t*) * npoints);

	for (i = 0; i < npoints; i++)
	{
		getPoint4d_p(pa, i, &pt);
		if (FLAGS_GET_Z(lwgeom->flags) && FLAGS_GET_M(lwgeom->flags))
			points[i] = sfcgal_point_create_from_xyzm(pt.x, pt.y, pt.z, pt.m);
		else if (FLAGS_GET_Z(lwgeom->flags))
			points[i] = sfcgal_point_create_from_xyz(pt.x, pt.y, pt.z);
		else if (FLAGS_GET_M(lwgeom->flags))
			points[i] = sfcgal_point_create_from_xym(pt.x, pt.y, pt.m);
		else
			points[i] = sfcgal_point_create_from_xy(pt.x, pt.y);
	}

	/* Create NURBS curve using SFCGAL */
	sfcgal_nurbs = sfcgal_nurbs_curve_create_from_points(
		(const sfcgal_geometry_t**)points, npoints, degree,
		SFCGAL_KNOT_METHOD_UNIFORM);

	/* Clean up SFCGAL points */
	for (i = 0; i < npoints; i++)
	{
		sfcgal_geometry_delete(points[i]);
	}
	pfree(points);

	/* Clean up temporary point array for MULTIPOINT */
	if (lwgeom->type == MULTIPOINTTYPE && pa)
		ptarray_free(pa);

	if (!sfcgal_nurbs)
	{
		lwgeom_free(lwgeom);
		PG_FREE_IF_COPY(input, 0);
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
				errmsg("Failed to create NURBS curve with SFCGAL")));
	}

	/* Convert back to PostGIS NURBS */
	result_nurbs = (LWNURBSCURVE*)SFCGAL2LWGEOM(sfcgal_nurbs, 0, srid);
	sfcgal_geometry_delete(sfcgal_nurbs);

	if (!result_nurbs)
	{
		lwgeom_free(lwgeom);
		PG_FREE_IF_COPY(input, 0);
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
				errmsg("Failed to convert SFCGAL NURBS to PostGIS")));
	}

	output = geometry_serialize((LWGEOM*)result_nurbs);

	lwnurbscurve_free(result_nurbs);
	lwgeom_free(lwgeom);
	PG_FREE_IF_COPY(input, 0);

	PG_RETURN_POINTER(output);
#endif
}

/* CG_NurbsCurveToLineString - Convert NURBS curve to LineString using SFCGAL */
PG_FUNCTION_INFO_V1(sfcgal_postgis_nurbs_curve_to_linestring);
/**
 * Tessellate a PostGIS NURBS curve into a LineString.
 *
 * Converts an input NURBS geometry (GSERIALIZED) to an SFCGAL NURBS, tessellates it
 * into a LineString with the requested number of segments, and returns the result
 * as a GSERIALIZED LineString preserving the input SRID.
 *
 * Parameters:
 * - input (arg 0): a non-NULL GSERIALIZED representing a NURBS curve (LINE type must be NURBSCURVETYPE).
 * - segments (arg 1, optional): number of segments to use for tessellation (default 32).
 *   Must be between 2 and 10000.
 *
 * Returns:
 * - A newly-allocated GSERIALIZED LineString containing the tessellated curve (caller receives a PostgreSQL pointer Datum).
 *
 * Errors (ereport(ERROR)):
 * - If SFCGAL NURBS support is unavailable (requires SFCGAL 2.3.0+), the function returns NULL at compile-time and reports an error.
 * - If the input is NULL, returns NULL.
 * - If segments is out of bounds (not in [2,10000]), raises ERRCODE_INVALID_PARAMETER_VALUE.
 * - If the input is not a NURBS curve or conversion between PostGIS and SFCGAL formats fails, raises an appropriate error (ERRCODE_INVALID_PARAMETER_VALUE or ERRCODE_INTERNAL_ERROR).
 */
Datum
sfcgal_postgis_nurbs_curve_to_linestring(PG_FUNCTION_ARGS)
{
#if POSTGIS_SFCGAL_VERSION < 20300
	lwpgerror(
		"The SFCGAL version this PostGIS binary was compiled against (%d) doesn't support "
		"'sfcgal_nurbs_curve_to_linestring' function (requires SFCGAL 2.3.0+)",
		POSTGIS_SFCGAL_VERSION);
	PG_RETURN_NULL();
#else /* POSTGIS_SFCGAL_VERSION >= 20300 */
	GSERIALIZED *input, *output;
	LWGEOM *lwgeom;
	LWNURBSCURVE *nurbs;
	sfcgal_geometry_t *sfcgal_nurbs, *sfcgal_line;
	uint32_t segments;
	srid_t srid;

	sfcgal_postgis_init();

	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();

	input = PG_GETARG_GSERIALIZED_P(0);
	segments = PG_NARGS() > 1 ? PG_GETARG_INT32(1) : 32;
	srid = gserialized_get_srid(input);

	/* Validate segment count */
	if (segments < 2 || segments > 10000)
	{
		PG_FREE_IF_COPY(input, 0);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("Number of segments must be between 2 and 10000")));
	}

	/* Extract NURBS curve */
	lwgeom = lwgeom_from_gserialized(input);
	if (!lwgeom || lwgeom->type != NURBSCURVETYPE)
	{
		if (lwgeom) lwgeom_free(lwgeom);
		PG_FREE_IF_COPY(input, 0);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("Input geometry must be a NURBS curve")));
	}

	nurbs = (LWNURBSCURVE*)lwgeom;

	/* Convert PostGIS NURBS to SFCGAL NURBS */
	sfcgal_nurbs = LWGEOM2SFCGAL((LWGEOM*)nurbs);
	if (!sfcgal_nurbs)
	{
		lwgeom_free(lwgeom);
		PG_FREE_IF_COPY(input, 0);
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
				errmsg("Failed to convert NURBS to SFCGAL format")));
	}

	/* Convert NURBS to LineString using SFCGAL */
	sfcgal_line = sfcgal_nurbs_curve_to_linestring(sfcgal_nurbs, segments);
	sfcgal_geometry_delete(sfcgal_nurbs);

	if (!sfcgal_line)
	{
		lwgeom_free(lwgeom);
		PG_FREE_IF_COPY(input, 0);
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
				errmsg("Failed to tessellate NURBS curve with SFCGAL")));
	}

	/* Convert result back to PostGIS */
	output = SFCGALGeometry2POSTGIS(sfcgal_line, 0, srid);
	sfcgal_geometry_delete(sfcgal_line);

	lwgeom_free(lwgeom);
	PG_FREE_IF_COPY(input, 0);

	PG_RETURN_POINTER(output);
#endif
}

/* CG_NurbsCurveEvaluate - Evaluate NURBS curve at parameter using SFCGAL */
PG_FUNCTION_INFO_V1(sfcgal_postgis_nurbs_curve_evaluate);
/**
 * Evaluate a NURBS curve at a given parameter and return the resulting point.
 *
 * Converts a PostGIS NURBS curve to SFCGAL format, evaluates it at the provided
 * parameter value, and returns the evaluated point as a GSERIALIZED PostGIS
 * geometry preserving the input SRID.
 *
 * Requires SFCGAL 2.3.0+; if the build-time SFCGAL version is older, the function
 * returns NULL. Errors if the input is not a NURBS curve or if conversion/
 * evaluation fails.
 *
 * @param 0 GSERIALIZED* GSERIALIZED representation of a PostGIS NURBS curve.
 * @param 1 float8 Parameter value at which to evaluate the NURBS curve.
 * @return GSERIALIZED* GSERIALIZED Point geometry representing the evaluated
 *         location (preserves input SRID).
 */
Datum
sfcgal_postgis_nurbs_curve_evaluate(PG_FUNCTION_ARGS)
{
#if POSTGIS_SFCGAL_VERSION < 20300
	lwpgerror(
		"The SFCGAL version this PostGIS binary was compiled against (%d) doesn't support "
		"'sfcgal_nurbs_curve_evaluate' function (requires SFCGAL 2.3.0+)",
		POSTGIS_SFCGAL_VERSION);
	PG_RETURN_NULL();
#else /* POSTGIS_SFCGAL_VERSION >= 20300 */
	GSERIALIZED *input, *output;
	LWGEOM *lwgeom;
	LWNURBSCURVE *nurbs;
	sfcgal_geometry_t *sfcgal_nurbs, *sfcgal_point;
	double parameter;
	srid_t srid;

	sfcgal_postgis_init();

	if (PG_ARGISNULL(0) || PG_ARGISNULL(1))
		PG_RETURN_NULL();

	input = PG_GETARG_GSERIALIZED_P(0);
	parameter = PG_GETARG_FLOAT8(1);
	srid = gserialized_get_srid(input);

	/* Extract NURBS curve */
	lwgeom = lwgeom_from_gserialized(input);
	if (!lwgeom || lwgeom->type != NURBSCURVETYPE)
	{
		if (lwgeom) lwgeom_free(lwgeom);
		PG_FREE_IF_COPY(input, 0);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("Input geometry must be a NURBS curve")));
	}

	nurbs = (LWNURBSCURVE*)lwgeom;

	/* Convert PostGIS NURBS to SFCGAL NURBS */
	sfcgal_nurbs = LWGEOM2SFCGAL((LWGEOM*)nurbs);
	if (!sfcgal_nurbs)
	{
		lwgeom_free(lwgeom);
		PG_FREE_IF_COPY(input, 0);
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
				errmsg("Failed to convert NURBS to SFCGAL format")));
	}

	/* Evaluate point on curve using SFCGAL */
	sfcgal_point = sfcgal_nurbs_curve_evaluate(sfcgal_nurbs, parameter);
	sfcgal_geometry_delete(sfcgal_nurbs);

	if (!sfcgal_point)
	{
		lwgeom_free(lwgeom);
		PG_FREE_IF_COPY(input, 0);
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
				errmsg("Failed to evaluate NURBS curve at parameter %g", parameter)));
	}

	/* Convert result back to PostGIS */
	output = SFCGALGeometry2POSTGIS(sfcgal_point, 0, srid);
	sfcgal_geometry_delete(sfcgal_point);

	lwgeom_free(lwgeom);
	PG_FREE_IF_COPY(input, 0);

	PG_RETURN_POINTER(output);
#endif
}

/* CG_NurbsCurveDerivative - Compute derivative of NURBS curve using SFCGAL */
PG_FUNCTION_INFO_V1(sfcgal_postgis_nurbs_curve_derivative);
/**
 * Compute the derivative of a NURBS curve at a given parameter.
 *
 * Accepts a PostGIS NURBS curve, a parameter value (double), and a derivative order (int).
 * Returns a Point geometry (GSERIALIZED) representing the derivative vector at the parameter;
 * the result preserves the input SRID. Requires SFCGAL >= 2.3.0.
 *
 * Parameters:
 * - arg0: a NURBS curve geometry (LINE or NURBS curve LWGEOM); must be a NURBS curve.
 * - arg1: parameter value along the curve (double).
 * - arg2: derivative order (1–3).
 *
 * Errors:
 * - Throws ERROR if the input is not a NURBS curve.
 * - Throws ERROR if derivative order is outside the range 1..3.
 * - Throws ERROR on internal conversion or computation failures.
 *
 * If the build-time SFCGAL version is older than 2.3.0, the function returns NULL.
 */
Datum
sfcgal_postgis_nurbs_curve_derivative(PG_FUNCTION_ARGS)
{
#if POSTGIS_SFCGAL_VERSION < 20300
	lwpgerror(
		"The SFCGAL version this PostGIS binary was compiled against (%d) doesn't support "
		"'sfcgal_nurbs_curve_derivative' function (requires SFCGAL 2.3.0+)",
		POSTGIS_SFCGAL_VERSION);
	PG_RETURN_NULL();
#else /* POSTGIS_SFCGAL_VERSION >= 20300 */
	GSERIALIZED *input, *output;
	LWGEOM *lwgeom;
	LWNURBSCURVE *nurbs;
	sfcgal_geometry_t *sfcgal_nurbs, *sfcgal_point;
	double parameter;
	uint32_t order;
	srid_t srid;

	sfcgal_postgis_init();

	if (PG_ARGISNULL(0) || PG_ARGISNULL(1) || PG_ARGISNULL(2))
		PG_RETURN_NULL();

	input = PG_GETARG_GSERIALIZED_P(0);
	parameter = PG_GETARG_FLOAT8(1);
	order = PG_GETARG_INT32(2);
	srid = gserialized_get_srid(input);

	if (order < 1 || order > 3)
	{
		PG_FREE_IF_COPY(input, 0);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("Derivative order must be between 1 and 3")));
	}

	/* Extract NURBS curve */
	lwgeom = lwgeom_from_gserialized(input);
	if (!lwgeom || lwgeom->type != NURBSCURVETYPE)
	{
		if (lwgeom) lwgeom_free(lwgeom);
		PG_FREE_IF_COPY(input, 0);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("Input geometry must be a NURBS curve")));
	}

	nurbs = (LWNURBSCURVE*)lwgeom;

	/* Convert PostGIS NURBS to SFCGAL NURBS */
	sfcgal_nurbs = LWGEOM2SFCGAL((LWGEOM*)nurbs);
	if (!sfcgal_nurbs)
	{
		lwgeom_free(lwgeom);
		PG_FREE_IF_COPY(input, 0);
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
				errmsg("Failed to convert NURBS to SFCGAL format")));
	}

	/* Compute derivative using SFCGAL */
	sfcgal_point = sfcgal_nurbs_curve_derivative(sfcgal_nurbs, parameter, order);
	sfcgal_geometry_delete(sfcgal_nurbs);

	if (!sfcgal_point)
	{
		lwgeom_free(lwgeom);
		PG_FREE_IF_COPY(input, 0);
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
				errmsg("Failed to compute derivative of NURBS curve")));
	}

	/* Convert result back to PostGIS */
	output = SFCGALGeometry2POSTGIS(sfcgal_point, 0, srid);
	sfcgal_geometry_delete(sfcgal_point);

	lwgeom_free(lwgeom);
	PG_FREE_IF_COPY(input, 0);

	PG_RETURN_POINTER(output);
#endif
}

/* CG_NurbsCurveInterpolate - Create interpolating NURBS curve using SFCGAL */
PG_FUNCTION_INFO_V1(sfcgal_postgis_nurbs_curve_interpolate);
/**
 * Create an interpolating NURBS curve from an input linestring of data points.
 *
 * Given a LINESTRING of data points and an integer degree, constructs an
 * interpolating NURBS curve using SFCGAL and returns it as a PostGIS
 * serialized geometry (GSERIALIZED). The function preserves the input SRID.
 *
 * Requirements and behavior:
 * - Requires SFCGAL 2.3.0+; if compiled against an older SFCGAL the function
 *   returns NULL.
 * - The input geometry must be a LINESTRING. Null inputs return NULL.
 * - The degree must be between 1 and 10 (inclusive).
 * - The LINESTRING must contain at least (degree + 1) points.
 * - Supports 2D, 2.5D (Z), measure (M) and 3D (XYZM) point coordinates and converts
 *   points to the appropriate SFCGAL point type before interpolation.
 *
 * @param input_linestring A LINESTRING of data points to interpolate (first
 *        function argument). Must not be NULL for execution.
 * @param degree Degree of the NURBS curve (second function argument); integer
 *        in range [1, 10].
 * @return A pointer to a GSERIALIZED representing the resulting PostGIS NURBS
 *         curve (must be freed by caller as per PostGIS memory conventions).
 * @throws ERROR if input is not a LINESTRING, if degree is out of range, if
 *         there are insufficient data points, or if SFCGAL fails to create or
 *         convert the NURBS curve.
 */
Datum
sfcgal_postgis_nurbs_curve_interpolate(PG_FUNCTION_ARGS)
{
#if POSTGIS_SFCGAL_VERSION < 20300
	lwpgerror(
		"The SFCGAL version this PostGIS binary was compiled against (%d) doesn't support "
		"'sfcgal_nurbs_curve_interpolate' function (requires SFCGAL 2.3.0+)",
		POSTGIS_SFCGAL_VERSION);
	PG_RETURN_NULL();
#else /* POSTGIS_SFCGAL_VERSION >= 20300 */
	GSERIALIZED *input, *output;
	LWGEOM *lwgeom;
	LWLINE *line;
	sfcgal_geometry_t **points;
	sfcgal_geometry_t *sfcgal_nurbs;
	LWNURBSCURVE *result_nurbs;
	int32_t degree;
	uint32_t i;
	POINT4D pt;
	srid_t srid;

	sfcgal_postgis_init();

	if (PG_ARGISNULL(0) || PG_ARGISNULL(1))
		PG_RETURN_NULL();

	input = PG_GETARG_GSERIALIZED_P(0);
	degree = PG_GETARG_INT32(1);
	srid = gserialized_get_srid(input);

	/* Validate degree */
	if (degree < 1 || degree > 10)
	{
		PG_FREE_IF_COPY(input, 0);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("NURBS degree must be between 1 and 10")));
	}

	/* Extract data points */
	lwgeom = lwgeom_from_gserialized(input);
	if (!lwgeom || lwgeom->type != LINETYPE)
	{
		if (lwgeom) lwgeom_free(lwgeom);
		PG_FREE_IF_COPY(input, 0);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("Data points must be a LINESTRING")));
	}

	line = (LWLINE*)lwgeom;
	if (!line->points || line->points->npoints < degree + 1)
	{
		lwgeom_free(lwgeom);
		PG_FREE_IF_COPY(input, 0);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("Need at least %d data points for degree %d interpolation",
					degree + 1, degree)));
	}

	/* Convert to SFCGAL points */
	points = (sfcgal_geometry_t**)palloc(sizeof(sfcgal_geometry_t*) * line->points->npoints);

	for (i = 0; i < line->points->npoints; i++)
	{
		getPoint4d_p(line->points, i, &pt);
		if (FLAGS_GET_Z(line->flags) && FLAGS_GET_M(line->flags))
			points[i] = sfcgal_point_create_from_xyzm(pt.x, pt.y, pt.z, pt.m);
		else if (FLAGS_GET_Z(line->flags))
			points[i] = sfcgal_point_create_from_xyz(pt.x, pt.y, pt.z);
		else if (FLAGS_GET_M(line->flags))
			points[i] = sfcgal_point_create_from_xym(pt.x, pt.y, pt.m);
		else
			points[i] = sfcgal_point_create_from_xy(pt.x, pt.y);
	}

	/* Create interpolating NURBS curve using SFCGAL */
	sfcgal_nurbs = sfcgal_nurbs_curve_interpolate(
		(const sfcgal_geometry_t**)points, line->points->npoints, degree,
		SFCGAL_KNOT_METHOD_CHORD_LENGTH, SFCGAL_END_CONDITION_CLAMPED);

	/* Clean up SFCGAL points */
	for (i = 0; i < line->points->npoints; i++)
	{
		sfcgal_geometry_delete(points[i]);
	}
	pfree(points);

	if (!sfcgal_nurbs)
	{
		lwgeom_free(lwgeom);
		PG_FREE_IF_COPY(input, 0);
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
				errmsg("Failed to create interpolating NURBS curve with SFCGAL")));
	}

	/* Convert back to PostGIS NURBS */
	result_nurbs = (LWNURBSCURVE*)SFCGAL2LWGEOM(sfcgal_nurbs, 0, srid);
	sfcgal_geometry_delete(sfcgal_nurbs);

	if (!result_nurbs)
	{
		lwgeom_free(lwgeom);
		PG_FREE_IF_COPY(input, 0);
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
				errmsg("Failed to convert SFCGAL NURBS to PostGIS")));
	}

	output = geometry_serialize((LWGEOM*)result_nurbs);

	lwnurbscurve_free(result_nurbs);
	lwgeom_free(lwgeom);
	PG_FREE_IF_COPY(input, 0);

	PG_RETURN_POINTER(output);
#endif
}


/* CG_NurbsCurveApproximate - Create approximating NURBS curve using SFCGAL */
PG_FUNCTION_INFO_V1(sfcgal_postgis_nurbs_curve_approximate);
/**
 * Approximate a NURBS curve from input data points.
 *
 * Builds an approximating NURBS curve from a LINESTRING of data points using SFCGAL's
 * approximation routine and returns the result as a PostGIS `GSERIALIZED` NURBS geometry.
 *
 * Parameters:
 * @param input LINESTRING of data points (GSERIALIZED). Points may be 2D, 2D+M, 3D, or 3D+M.
 * @param degree Desired NURBS degree (integer). Must be between 1 and 10.
 * @param tolerance Approximation tolerance (double).
 * @param max_control_points Optional maximum number of control points (integer, default 100).
 *
 * Returns:
 * @return A newly allocated `GSERIALIZED *` containing the approximating NURBS curve.
 *
 * Error conditions:
 * - Raises an ERROR if `degree` is out of range (1..10).
 * - Raises an ERROR if `input` is not a LINESTRING or does not contain at least `degree + 1` points.
 * - Raises an ERROR if SFCGAL fails to produce an approximating NURBS curve.
 *
 * Notes:
 * - Requires SFCGAL >= 2.3.0; if compiled against an older SFCGAL, the function returns NULL.
 * - The returned GSERIALIZED preserves the input SRID.
 */
Datum
sfcgal_postgis_nurbs_curve_approximate(PG_FUNCTION_ARGS)
{
#if POSTGIS_SFCGAL_VERSION < 20300
	lwpgerror(
		"The SFCGAL version this PostGIS binary was compiled against (%d) doesn't support "
		"'sfcgal_nurbs_curve_approximate' function (requires SFCGAL 2.3.0+)",
		POSTGIS_SFCGAL_VERSION);
	PG_RETURN_NULL();
#else /* POSTGIS_SFCGAL_VERSION >= 20300 */
	GSERIALIZED *input, *output;
	LWGEOM *lwgeom;
	LWLINE *line;
	int32_t degree;
	float8 tolerance;
	int32_t max_control_points = 100; /* default */
	sfcgal_geometry_t **points;
	sfcgal_geometry_t *sfcgal_nurbs;
	LWNURBSCURVE *result_nurbs;
	uint32_t i;
	POINT4D pt;
	srid_t srid;

	sfcgal_postgis_init();

	if (PG_ARGISNULL(0) || PG_ARGISNULL(1) || PG_ARGISNULL(2))
		PG_RETURN_NULL();

	input = PG_GETARG_GSERIALIZED_P(0);
	degree = PG_GETARG_INT32(1);
	tolerance = PG_GETARG_FLOAT8(2);
	if (PG_NARGS() > 3 && !PG_ARGISNULL(3))
		max_control_points = PG_GETARG_INT32(3);
	srid = gserialized_get_srid(input);

	/* Validate degree */
	if (degree < 1 || degree > 10)
	{
		PG_FREE_IF_COPY(input, 0);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("NURBS degree is %d, must be between 1 and 10", degree)));
	}

	/* Convert to lwgeom */
	lwgeom = lwgeom_from_gserialized(input);
	if (lwgeom->type != LINETYPE)
	{
		lwgeom_free(lwgeom);
		PG_FREE_IF_COPY(input, 0);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("Data points must be a LINESTRING")));
	}

	line = (LWLINE*)lwgeom;
	if (!line->points || line->points->npoints < degree + 1)
	{
		lwgeom_free(lwgeom);
		PG_FREE_IF_COPY(input, 0);
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("Need at least %d data points for degree %d approximation",
					degree + 1, degree)));
	}

	/* Convert to SFCGAL points */
	points = (sfcgal_geometry_t**)palloc(sizeof(sfcgal_geometry_t*) * line->points->npoints);

	for (i = 0; i < line->points->npoints; i++)
	{
		getPoint4d_p(line->points, i, &pt);
		if (FLAGS_GET_Z(line->flags) && FLAGS_GET_M(line->flags))
			points[i] = sfcgal_point_create_from_xyzm(pt.x, pt.y, pt.z, pt.m);
		else if (FLAGS_GET_Z(line->flags))
			points[i] = sfcgal_point_create_from_xyz(pt.x, pt.y, pt.z);
		else if (FLAGS_GET_M(line->flags))
			points[i] = sfcgal_point_create_from_xym(pt.x, pt.y, pt.m);
		else
			points[i] = sfcgal_point_create_from_xy(pt.x, pt.y);
	}

	/* Create approximating NURBS curve */
	sfcgal_nurbs = sfcgal_nurbs_curve_approximate((const sfcgal_geometry_t **)points, line->points->npoints, degree,
													tolerance, max_control_points);

	/* Clean up points */
	for (i = 0; i < line->points->npoints; i++)
		sfcgal_geometry_delete(points[i]);
	pfree(points);

	if (!sfcgal_nurbs)
	{
		lwgeom_free(lwgeom);
		PG_FREE_IF_COPY(input, 0);
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
				errmsg("SFCGAL NURBS curve approximation failed")));
	}

	/* Convert result back to PostGIS */
	result_nurbs = (LWNURBSCURVE*)SFCGAL2LWGEOM(sfcgal_nurbs, 0, srid);
	sfcgal_geometry_delete(sfcgal_nurbs);

	output = geometry_serialize(lwnurbscurve_as_lwgeom(result_nurbs));
	lwnurbscurve_free(result_nurbs);
	lwgeom_free(lwgeom);
	PG_FREE_IF_COPY(input, 0);

	PG_RETURN_POINTER(output);
#endif
}
