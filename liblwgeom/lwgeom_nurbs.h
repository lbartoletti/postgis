/*
 * PostGIS NURBS Support - Extended Geometry Types
 * File: liblwgeom/lwgeom_nurbs.h
 *
 * Extends PostGIS with native NURBS and Bezier curve geometry types.
 * Preserves control points and curve parameters for SQL/MM compliance.
 *
 * Copyright (c) 2024 PostGIS contributors
 * License: GPL v2+
 */

#ifndef LWGEOM_NURBS_H
#define LWGEOM_NURBS_H

#include "liblwgeom_internal.h"
#include <math.h>

/* New geometry types for NURBS support */
#define NURBSCURVETYPE 50
#define BEZIERCURVETYPE 51

/* NURBS curve structure - extends LWGEOM */
typedef struct {
    uint8_t type;           /* NURBSCURVETYPE or BEZIERCURVETYPE */
    uint8_t flags;          /* Dimensionality flags */
    uint32_t srid;          /* Spatial reference ID */
    GBOX *bbox;             /* Bounding box */

    uint32_t degree;        /* Curve degree (order - 1) */
    uint32_t num_points;    /* Number of control points */
    POINTARRAY *points;     /* Control points with weights in M coordinate */

    /* NURBS-specific data */
    uint32_t num_knots;     /* Number of knots (0 for Bezier) */
    double *knots;          /* Knot vector (NULL for Bezier) */
} LWNURBSCURVE;

/* Alias for clarity */
typedef LWNURBSCURVE LWBEZIERCURVE;

/* Mathematical constants */
#define NURBS_EPSILON 1e-10
#define NURBS_MAX_DEGREE 20
#define NURBS_MAX_POINTS 1000

/*
 * Core NURBS geometry functions
 */

/* Construction and destruction */
extern LWNURBSCURVE* lwnurbscurve_construct(uint32_t srid, lwflags_t flags, uint32_t degree,
                                           POINTARRAY *points, uint32_t num_knots, double *knots);
extern LWNURBSCURVE* lwbeziercurve_construct(uint32_t srid, lwflags_t flags, uint32_t degree,
                                            POINTARRAY *points);
extern void lwnurbscurve_free(LWNURBSCURVE *curve);

/* Cloning and copying */
extern LWNURBSCURVE* lwnurbscurve_clone(const LWNURBSCURVE *curve);
extern LWNURBSCURVE* lwnurbscurve_clone_deep(const LWNURBSCURVE *curve);

/* Validation */
extern bool lwnurbscurve_is_valid(const LWNURBSCURVE *curve);

/* Geometry operations */
extern void lwnurbscurve_evaluate_point(const LWNURBSCURVE *curve, double t, POINT4D *point);
extern LWLINE* lwnurbscurve_to_linestring(const LWNURBSCURVE *curve, int num_segments);
extern double lwnurbscurve_length(const LWNURBSCURVE *curve, int num_segments);
extern GBOX* lwnurbscurve_compute_bbox(const LWNURBSCURVE *curve);

/* Serialization support */
extern size_t lwnurbscurve_serialized_size(const LWNURBSCURVE *curve);
extern uint8_t* lwnurbscurve_serialize(const LWNURBSCURVE *curve);
extern LWNURBSCURVE* lwnurbscurve_deserialize(uint8_t *srl);

/* Type checking macros */
#define IS_NURBS_TYPE(type) ((type) == NURBSCURVETYPE || (type) == BEZIERCURVETYPE)
#define NURBS_GETTYPE(flags) (FLAGS_GET_Z(flags) ? 4 : 2)

/* Utility functions */
extern double* lwnurbs_uniform_knots(uint32_t num_points, uint32_t degree);
extern bool lwnurbs_validate_knots(const double *knots, uint32_t num_knots, uint32_t degree);
extern POINTARRAY* lwnurbs_points_with_weights(const POINT4D *points, const double *weights,
                                              uint32_t num_points, lwflags_t flags);

/* Mathematical functions */
extern double lwnurbs_binomial_coefficient(int n, int k);
extern double lwnurbs_bernstein_polynomial(int n, int k, double t);
extern double lwnurbs_bspline_basis(int i, int p, double t, const double *knots, int num_knots);

/* Helper constructors */
extern LWBEZIERCURVE* lwbeziercurve_quadratic(uint32_t srid, POINT4D p1, POINT4D p2, POINT4D p3);
extern LWBEZIERCURVE* lwbeziercurve_cubic(uint32_t srid, POINT4D p1, POINT4D p2, POINT4D p3, POINT4D p4);
extern LWNURBSCURVE* lwnurbscurve_circle(uint32_t srid, POINT4D center, double radius);

/* LWGEOM interface functions */
extern LWGEOM* lwgeom_from_nurbscurve(const LWNURBSCURVE *curve);
extern LWNURBSCURVE* lwgeom_as_nurbscurve(const LWGEOM *lwgeom);

#endif /* LWGEOM_NURBS_H */
