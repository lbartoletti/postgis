/*
 * PostGIS NURBS Support - Header File
 * File: liblwgeom/lwgeom_nurbs.h
 *
 * NURBS (Non-Uniform Rational B-Spline) and Bezier curve support for PostGIS.
 * Integrated directly into liblwgeom for native PostGIS support.
 *
 * Copyright (c) 2024 Loïc Bartoletti
 * License: GPL v2+
 */

#ifndef LWGEOM_NURBS_H
#define LWGEOM_NURBS_H

#include "liblwgeom_internal.h"
#include <math.h>

/* NURBS curve structure */
typedef struct {
    uint8_t type;           /* NURBSTYPE or BEZIERTYPE */
    uint8_t flags;          /* Dimensionality flags */
    uint32_t srid;          /* Spatial reference ID */
    uint32_t degree;        /* Curve degree (order - 1) */
    uint32_t num_points;    /* Number of control points */
    uint32_t num_knots;     /* Number of knots (0 for Bezier) */
    double *ordinates;      /* Control points [x,y,[z],w, ...] */
    double *knots;          /* Knot vector (NULL for Bezier) */
    bool is_rational;       /* Non-uniform weights flag */
} LWNURBS;

/* Type identifiers */
#define NURBSTYPE 100
#define BEZIERTYPE 101

/* Coordinate size calculation */
#define NURBS_COORD_SIZE(flags) (2 + (FLAGS_GET_Z(flags) ? 1 : 0) + 1) /* x,y,[z],w */

/* Mathematical constants */
#define NURBS_EPSILON 1e-10
#define NURBS_MAX_DEGREE 10
#define NURBS_MAX_POINTS 1000

/*
 * Core NURBS functions
 */

/* Construction and destruction */
extern LWNURBS* lwnurbs_construct(uint32_t srid, uint8_t flags, uint32_t degree,
                                 uint32_t num_points, double *ordinates,
                                 uint32_t num_knots, double *knots);
extern void lwnurbs_free(LWNURBS *nurbs);

/* Validation */
extern bool lwnurbs_is_valid(LWNURBS *nurbs);

/* Geometry operations */
extern void lwnurbs_evaluate_point(LWNURBS *nurbs, double t, POINT4D *point);
extern LWLINE* lwnurbs_to_linestring(LWNURBS *nurbs, int num_segments);
extern double lwnurbs_length(LWNURBS *nurbs, int num_segments);

/* Utility functions */
extern double* lwnurbs_uniform_knots(uint32_t num_points, uint32_t degree);
extern bool lwnurbs_validate_knots(double *knots, uint32_t num_knots, uint32_t degree);

/* Mathematical functions */
extern double lwnurbs_binomial_coefficient(int n, int k);
extern double lwnurbs_bernstein_polynomial(int n, int k, double t);
extern double lwnurbs_bspline_basis(int i, int p, double t, double *knots, int num_knots);

/* Helper constructors */
extern LWNURBS* lwnurbs_bezier_quadratic(uint32_t srid, POINT4D p1, POINT4D p2, POINT4D p3);
extern LWNURBS* lwnurbs_bezier_cubic(uint32_t srid, POINT4D p1, POINT4D p2, POINT4D p3, POINT4D p4);
extern LWNURBS* lwnurbs_circle(uint32_t srid, POINT4D center, double radius);

#endif /* LWGEOM_NURBS_H */
