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

#ifndef _LWGEOM_NURBS_H
#define _LWGEOM_NURBS_H 1

#include "liblwgeom.h"

/* Forward declaration */
typedef struct wkb_parse_state_t wkb_parse_state;

/**
 * NURBS curve constants
 */
#define NURBS_MIN_DEGREE 1
#define NURBS_MAX_DEGREE 20
#define NURBS_MIN_POINTS 2
#define NURBS_DEFAULT_SAMPLES 32
#define NURBS_TOLERANCE 1e-10

/**
 * LWNURBSCURVE structure following PostGIS conventions
 */
typedef struct
{
	GBOX *bbox;
	POINTARRAY *ctrl_pts;  /* Control points */
	double *weights;       /* Weights for rational curves */
	double *knots;         /* Knot vector */
	int32_t srid;
	lwflags_t flags;
	uint8_t type;          /* NURBSCURVETYPE */
	char pad[1];           /* Padding to 24 bytes (unused) */
	uint32_t degree;       /* Curve degree */
	uint32_t nweights;     /* Number of weights */
	uint32_t nknots;       /* Number of knots */
}
LWNURBSCURVE;

/* Construction and destruction */
extern LWNURBSCURVE *lwnurbs_construct(int32_t srid, GBOX *bbox, uint32_t degree,
                                      POINTARRAY *ctrl_pts, double *weights,
                                      uint32_t nweights, double *knots, uint32_t nknots);
extern LWNURBSCURVE *lwnurbs_construct_empty(int32_t srid, char hasz, char hasm);
extern void lwnurbs_free(LWNURBSCURVE *nurbs);
extern void lwnurbs_release(LWNURBSCURVE *nurbs);

/* Core NURBS algorithms */
extern double bspline_basis(int i, int p, double t, const double *knots, int nknots);
extern int lwnurbs_interpolate_point(const LWNURBSCURVE *nurbs, double t, POINT4D *pt);
extern LWLINE *lwnurbs_to_linestring(const LWNURBSCURVE *nurbs, uint32_t segments);
extern double lwnurbs_length(const LWNURBSCURVE *nurbs);

/* Validation and utilities */
extern int lwnurbs_is_valid(const LWNURBSCURVE *nurbs);
extern int lwnurbs_is_empty(const LWNURBSCURVE *nurbs);
extern int lwnurbs_is_closed(const LWNURBSCURVE *nurbs);

/* Clone and copy operations */
extern LWNURBSCURVE *lwnurbs_clone(const LWNURBSCURVE *nurbs);
extern LWNURBSCURVE *lwnurbs_clone_deep(const LWNURBSCURVE *nurbs);

/* Bounding box calculation */
extern void lwnurbs_compute_bbox_p(LWNURBSCURVE *nurbs);

/* Knot vector utilities */
extern double *lwnurbs_uniform_knots(uint32_t degree, uint32_t nctrl);
extern double *lwnurbs_clamped_knots(uint32_t degree, uint32_t nctrl);

/* WKB serialization - only declare if wkb_parse_state is available */
#ifdef LIBLWGEOM_INTERNAL_H
extern uint8_t *lwnurbs_to_wkb_buf(const LWNURBSCURVE *nurbs, uint8_t *buf,
                                  uint8_t variant);
extern LWNURBSCURVE *lwnurbs_from_wkb_state(wkb_parse_state *s);
extern size_t lwnurbs_to_wkb_size(const LWNURBSCURVE *nurbs, uint8_t variant);
#endif

/* WKT serialization */
extern char *lwnurbs_to_wkt(const LWNURBSCURVE *nurbs, uint8_t variant,
                           int precision, size_t *size_out);

/* Accessor functions */
extern uint32_t lwnurbs_get_degree(const LWNURBSCURVE *nurbs);
extern uint32_t lwnurbs_get_npoints(const LWNURBSCURVE *nurbs);
extern int lwnurbs_startpoint(const LWNURBSCURVE *nurbs, POINT4D *pt);
extern int lwnurbs_endpoint(const LWNURBSCURVE *nurbs, POINT4D *pt);

/* Type conversion */
extern LWGEOM *lwnurbs_as_lwgeom(const LWNURBSCURVE *nurbs);
extern LWNURBSCURVE *lwgeom_as_lwnurbs(const LWGEOM *lwgeom);

#endif /* _LWGEOM_NURBS_H */
