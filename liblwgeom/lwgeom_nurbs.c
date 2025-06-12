/*
 * PostGIS NURBS Support - Extended Geometry Implementation
 * File: liblwgeom/lwgeom_nurbs.c
 *
 * Implementation of native NURBS and Bezier curve geometry types for PostGIS.
 * These types preserve control points and curve parameters.
 *
 * Copyright (c) 2024 PostGIS contributors
 * License: GPL v2+
 */

#include "lwgeom_nurbs.h"

/*
 * Construct a NURBS curve geometry
 */
LWNURBSCURVE* lwnurbscurve_construct(uint32_t srid, lwflags_t flags, uint32_t degree,
                                    POINTARRAY *points, uint32_t num_knots, double *knots)
{
    LWNURBSCURVE *curve;

    /* Validate input parameters */
    if (degree < 1 || degree > NURBS_MAX_DEGREE) return NULL;
    if (!points || points->npoints < 2 || points->npoints > NURBS_MAX_POINTS) return NULL;
    if (degree >= points->npoints) return NULL;

    /* Allocate structure */
    curve = lwalloc(sizeof(LWNURBSCURVE));
    if (!curve) return NULL;

    /* Set basic properties */
    curve->type = (num_knots > 0) ? NURBSCURVETYPE : BEZIERCURVETYPE;
    curve->flags = flags;
    curve->srid = srid;
    curve->bbox = NULL;  /* Will be computed on demand */

    curve->degree = degree;
    curve->num_points = points->npoints;
    curve->points = ptarray_clone_deep(points);

    /* Handle knot vector */
    curve->num_knots = num_knots;
    if (num_knots > 0 && knots) {
        curve->knots = lwalloc(num_knots * sizeof(double));
        if (!curve->knots) {
            ptarray_free(curve->points);
            lwfree(curve);
            return NULL;
        }
        memcpy(curve->knots, knots, num_knots * sizeof(double));
    } else {
        curve->knots = NULL;
    }

    return curve;
}

/*
 * Construct a Bezier curve geometry
 */
LWBEZIERCURVE* lwbeziercurve_construct(uint32_t srid, lwflags_t flags, uint32_t degree,
                                      POINTARRAY *points)
{
    return lwnurbscurve_construct(srid, flags, degree, points, 0, NULL);
}

/*
 * Free NURBS curve memory
 */
void lwnurbscurve_free(LWNURBSCURVE *curve)
{
    if (curve) {
        if (curve->points) ptarray_free(curve->points);
        if (curve->knots) lwfree(curve->knots);
        if (curve->bbox) lwfree(curve->bbox);
        lwfree(curve);
    }
}

/*
 * Clone NURBS curve (shallow copy)
 */
LWNURBSCURVE* lwnurbscurve_clone(const LWNURBSCURVE *curve)
{
    LWNURBSCURVE *clone;

    if (!curve) return NULL;

    clone = lwalloc(sizeof(LWNURBSCURVE));
    memcpy(clone, curve, sizeof(LWNURBSCURVE));

    /* Shallow copy - share references */
    return clone;
}

/*
 * Clone NURBS curve (deep copy)
 */
LWNURBSCURVE* lwnurbscurve_clone_deep(const LWNURBSCURVE *curve)
{
    if (!curve) return NULL;

    return lwnurbscurve_construct(curve->srid, curve->flags, curve->degree,
                                 curve->points, curve->num_knots, curve->knots);
}

/*
 * Validate NURBS curve geometry
 */
bool lwnurbscurve_is_valid(const LWNURBSCURVE *curve)
{
    POINT4D point;
    int i;

    if (!curve || !curve->points) return false;

    /* Basic parameter validation */
    if (curve->degree < 1 || curve->num_points < 2) return false;
    if (curve->degree >= curve->num_points) return false;

    /* Validate control point weights (stored in M coordinate) */
    if (FLAGS_GET_M(curve->flags)) {
        for (i = 0; i < curve->num_points; i++) {
            getPoint4d_p(curve->points, i, &point);
            if (point.m <= 0.0) return false;
        }
    }

    /* NURBS-specific validation */
    if (curve->type == NURBSCURVETYPE) {
        if (curve->num_knots != curve->num_points + curve->degree + 1) return false;
        if (!curve->knots) return false;

        /* Validate knot ordering */
        for (i = 1; i < curve->num_knots; i++) {
            if (curve->knots[i] < curve->knots[i-1]) return false;
        }

        /* Validate clamping */
        for (i = 1; i <= curve->degree; i++) {
            if (fabs(curve->knots[i] - curve->knots[0]) > NURBS_EPSILON) return false;
            if (fabs(curve->knots[curve->num_knots - 1 - i] -
                    curve->knots[curve->num_knots - 1]) > NURBS_EPSILON) return false;
        }

        /* Validate normalization */
        if (fabs(curve->knots[0]) > NURBS_EPSILON) return false;
        if (fabs(curve->knots[curve->num_knots - 1] - 1.0) > NURBS_EPSILON) return false;
    }

    return true;
}

/*
 * Binomial coefficient calculation
 */
double lwnurbs_binomial_coefficient(int n, int k)
{
    int i;
    double result = 1.0;

    if (k > n || k < 0) return 0.0;
    if (k == 0 || k == n) return 1.0;

    if (k > n - k) k = n - k;

    for (i = 0; i < k; i++) {
        result = result * (n - i) / (i + 1);
    }

    return result;
}

/*
 * Bernstein polynomial for Bezier curves
 */
double lwnurbs_bernstein_polynomial(int n, int k, double t)
{
    double coeff = lwnurbs_binomial_coefficient(n, k);
    return coeff * pow(t, k) * pow(1.0 - t, n - k);
}

/*
 * B-spline basis function using Cox-de Boor recursion
 */
double lwnurbs_bspline_basis(int i, int p, double t, const double *knots, int num_knots)
{
    double left, right;
    double left_denom, right_denom;

    if (p == 0) {
        return (t >= knots[i] && t < knots[i + 1]) ? 1.0 : 0.0;
    }

    left = 0.0;
    right = 0.0;

    left_denom = knots[i + p] - knots[i];
    if (left_denom > NURBS_EPSILON) {
        left = (t - knots[i]) / left_denom *
               lwnurbs_bspline_basis(i, p - 1, t, knots, num_knots);
    }

    right_denom = knots[i + p + 1] - knots[i + 1];
    if (right_denom > NURBS_EPSILON) {
        right = (knots[i + p + 1] - t) / right_denom *
                lwnurbs_bspline_basis(i + 1, p - 1, t, knots, num_knots);
    }

    return left + right;
}

/*
 * Evaluate point on NURBS curve at parameter t
 */
void lwnurbscurve_evaluate_point(const LWNURBSCURVE *curve, double t, POINT4D *result)
{
    double px = 0.0, py = 0.0, pz = 0.0, weight_sum = 0.0;
    POINT4D point;
    double basis_value, weighted_basis;
    int i;
    bool has_z = FLAGS_GET_Z(curve->flags);
    bool has_m = FLAGS_GET_M(curve->flags);

    if (!curve || !result) return;

    /* Clamp parameter to [0,1] */
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;

    /* Evaluate curve based on type */
    if (curve->type == BEZIERCURVETYPE) {
        /* Bezier curve evaluation using Bernstein polynomials */
        for (i = 0; i < curve->num_points; i++) {
            getPoint4d_p(curve->points, i, &point);
            basis_value = lwnurbs_bernstein_polynomial(curve->degree, i, t);

            if (has_m && point.m != 1.0) {
                /* Rational curve */
                weighted_basis = basis_value * point.m;
                px += weighted_basis * point.x / point.m;
                py += weighted_basis * point.y / point.m;
                if (has_z) pz += weighted_basis * point.z / point.m;
                weight_sum += weighted_basis;
            } else {
                /* Non-rational curve */
                px += basis_value * point.x;
                py += basis_value * point.y;
                if (has_z) pz += basis_value * point.z;
            }
        }

        if (has_m && weight_sum > NURBS_EPSILON) {
            px /= weight_sum;
            py /= weight_sum;
            if (has_z) pz /= weight_sum;
        }

    } else if (curve->type == NURBSCURVETYPE) {
        /* NURBS curve evaluation using B-spline basis functions */
        for (i = 0; i < curve->num_points; i++) {
            getPoint4d_p(curve->points, i, &point);
            basis_value = lwnurbs_bspline_basis(i, curve->degree, t,
                                              curve->knots, curve->num_knots);

            weighted_basis = basis_value * (has_m ? point.m : 1.0);
            px += weighted_basis * point.x / (has_m ? point.m : 1.0);
            py += weighted_basis * point.y / (has_m ? point.m : 1.0);
            if (has_z) pz += weighted_basis * point.z / (has_m ? point.m : 1.0);
            weight_sum += weighted_basis;
        }

        if (weight_sum > NURBS_EPSILON) {
            px /= weight_sum;
            py /= weight_sum;
            if (has_z) pz /= weight_sum;
        }
    }

    /* Set result */
    result->x = px;
    result->y = py;
    result->z = has_z ? pz : 0.0;
    result->m = 0.0;
}

/*
 * Convert NURBS curve to linestring by sampling
 */
LWLINE* lwnurbscurve_to_linestring(const LWNURBSCURVE *curve, int num_segments)
{
    POINTARRAY *pa;
    POINT4D point;
    double step, t;
    int i;

    if (!curve || num_segments < 1) return NULL;

    /* Create point array */
    pa = ptarray_construct(FLAGS_GET_Z(curve->flags),
                          FLAGS_GET_M(curve->flags),
                          num_segments + 1);

    step = 1.0 / num_segments;

    /* Sample points along curve */
    for (i = 0; i <= num_segments; i++) {
        t = i * step;
        lwnurbscurve_evaluate_point(curve, t, &point);
        ptarray_set_point4d(pa, i, &point);
    }

    return lwline_construct(curve->srid, NULL, pa);
}

/*
 * Calculate approximate curve length
 */
double lwnurbscurve_length(const LWNURBSCURVE *curve, int num_segments)
{
    POINT4D p1, p2;
    double length = 0.0;
    double step, t;
    int i;
    bool has_z;

    if (!curve || num_segments < 1) return 0.0;

    has_z = FLAGS_GET_Z(curve->flags);
    step = 1.0 / num_segments;

    lwnurbscurve_evaluate_point(curve, 0.0, &p1);

    for (i = 1; i <= num_segments; i++) {
        t = i * step;
        lwnurbscurve_evaluate_point(curve, t, &p2);

        length += sqrt(pow(p2.x - p1.x, 2) + pow(p2.y - p1.y, 2) +
                      (has_z ? pow(p2.z - p1.z, 2) : 0));

        p1 = p2;
    }

    return length;
}

/*
 * Compute bounding box of NURBS curve
 */
GBOX* lwnurbscurve_compute_bbox(const LWNURBSCURVE *curve)
{
    GBOX *bbox;
    POINT4D point;
    double step, t;
    int i, num_samples = 100;  /* Sample 100 points for bbox estimation */

    if (!curve) return NULL;

    bbox = gbox_new(curve->flags);

    /* Sample points along curve to estimate bounding box */
    step = 1.0 / (num_samples - 1);

    for (i = 0; i < num_samples; i++) {
        t = i * step;
        lwnurbscurve_evaluate_point(curve, t, &point);

        if (i == 0) {
            bbox->xmin = bbox->xmax = point.x;
            bbox->ymin = bbox->ymax = point.y;
            if (FLAGS_GET_Z(curve->flags)) {
                bbox->zmin = bbox->zmax = point.z;
            }
            if (FLAGS_GET_M(curve->flags)) {
                bbox->mmin = bbox->mmax = point.m;
            }
        } else {
            bbox->xmin = FP_MIN(bbox->xmin, point.x);
            bbox->xmax = FP_MAX(bbox->xmax, point.x);
            bbox->ymin = FP_MIN(bbox->ymin, point.y);
            bbox->ymax = FP_MAX(bbox->ymax, point.y);
            if (FLAGS_GET_Z(curve->flags)) {
                bbox->zmin = FP_MIN(bbox->zmin, point.z);
                bbox->zmax = FP_MAX(bbox->zmax, point.z);
            }
            if (FLAGS_GET_M(curve->flags)) {
                bbox->mmin = FP_MIN(bbox->mmin, point.m);
                bbox->mmax = FP_MAX(bbox->mmax, point.m);
            }
        }
    }

    return bbox;
}

/*
 * Generate uniform clamped knot vector
 */
double* lwnurbs_uniform_knots(uint32_t num_points, uint32_t degree)
{
    double *knots;
    uint32_t num_knots;
    uint32_t internal_knots;
    double step;
    int i;

    num_knots = num_points + degree + 1;
    internal_knots = num_knots - 2 * (degree + 1);

    knots = lwalloc(num_knots * sizeof(double));
    if (!knots) return NULL;

    /* Clamp at start */
    for (i = 0; i <= degree; i++) {
        knots[i] = 0.0;
    }

    /* Internal knots uniformly spaced */
    if (internal_knots > 0) {
        step = 1.0 / (internal_knots + 1);
        for (i = 0; i < internal_knots; i++) {
            knots[degree + 1 + i] = (i + 1) * step;
        }
    }

    /* Clamp at end */
    for (i = 0; i <= degree; i++) {
        knots[num_knots - 1 - i] = 1.0;
    }

    return knots;
}

/*
 * Validate knot vector
 */
bool lwnurbs_validate_knots(const double *knots, uint32_t num_knots, uint32_t degree)
{
    int i;

    if (!knots || num_knots == 0) return false;

    /* Check ordering */
    for (i = 1; i < num_knots; i++) {
        if (knots[i] < knots[i-1]) return false;
    }

    /* Check normalization */
    if (fabs(knots[0]) > NURBS_EPSILON) return false;
    if (fabs(knots[num_knots - 1] - 1.0) > NURBS_EPSILON) return false;

    /* Check clamping */
    for (i = 1; i <= degree; i++) {
        if (fabs(knots[i] - knots[0]) > NURBS_EPSILON) return false;
        if (fabs(knots[num_knots - 1 - i] - knots[num_knots - 1]) > NURBS_EPSILON) return false;
    }

    return true;
}

/*
 * Create POINTARRAY with weights in M coordinate
 */
POINTARRAY* lwnurbs_points_with_weights(const POINT4D *points, const double *weights,
                                       uint32_t num_points, lwflags_t flags)
{
    POINTARRAY *pa;
    POINT4D point;
    int i;

    /* Force M coordinate for weights */
    FLAGS_SET_M(flags, 1);

    pa = ptarray_construct(FLAGS_GET_Z(flags), FLAGS_GET_M(flags), num_points);
    if (!pa) return NULL;

    for (i = 0; i < num_points; i++) {
        point = points[i];
        point.m = weights ? weights[i] : 1.0;
        ptarray_set_point4d(pa, i, &point);
    }

    return pa;
}

/*
 * Helper: Create quadratic Bezier curve
 */
LWBEZIERCURVE* lwbeziercurve_quadratic(uint32_t srid, POINT4D p1, POINT4D p2, POINT4D p3)
{
    POINT4D points[3] = {p1, p2, p3};
    POINTARRAY *pa = lwnurbs_points_with_weights(points, NULL, 3, 0);
	lwflags_t flags = 0;
	FLAGS_SET_M(flags, 1);
    LWBEZIERCURVE *curve = lwbeziercurve_construct(srid, flags, 2, pa);
    ptarray_free(pa);
    return curve;
}

/*
 * Helper: Create cubic Bezier curve
 */
LWBEZIERCURVE* lwbeziercurve_cubic(uint32_t srid, POINT4D p1, POINT4D p2, POINT4D p3, POINT4D p4)
{
    POINT4D points[4] = {p1, p2, p3, p4};
    POINTARRAY *pa = lwnurbs_points_with_weights(points, NULL, 4, 0);
	lwflags_t flags = 0;
	FLAGS_SET_M(flags, 1);
    LWBEZIERCURVE *curve = lwbeziercurve_construct(srid, flags, 3, pa);
    ptarray_free(pa);
    return curve;
}

/*
 * Helper: Create NURBS circle approximation
 */
LWNURBSCURVE* lwnurbscurve_circle(uint32_t srid, POINT4D center, double radius)
{
    POINT4D points[9];
    double weights[9] = {1.0, M_SQRT1_2, 1.0, M_SQRT1_2, 1.0, M_SQRT1_2, 1.0, M_SQRT1_2, 1.0};
    double knots[12] = {0, 0, 0, 0.25, 0.25, 0.5, 0.5, 0.75, 0.75, 1, 1, 1};
    POINTARRAY *pa;
    LWNURBSCURVE *curve;
    double cx = center.x, cy = center.y;

    /* 9 control points for rational quadratic NURBS circle */
    points[0] = (POINT4D){cx + radius, cy, center.z, 1.0};
    points[1] = (POINT4D){cx + radius, cy + radius, center.z, M_SQRT1_2};
    points[2] = (POINT4D){cx, cy + radius, center.z, 1.0};
    points[3] = (POINT4D){cx - radius, cy + radius, center.z, M_SQRT1_2};
    points[4] = (POINT4D){cx - radius, cy, center.z, 1.0};
    points[5] = (POINT4D){cx - radius, cy - radius, center.z, M_SQRT1_2};
    points[6] = (POINT4D){cx, cy - radius, center.z, 1.0};
    points[7] = (POINT4D){cx + radius, cy - radius, center.z, M_SQRT1_2};
    points[8] = (POINT4D){cx + radius, cy, center.z, 1.0};

    pa = lwnurbs_points_with_weights(points, weights, 9, FLAGS_GET_Z(center.z != 0.0) ? 1 : 0);

    lwflags_t flags = 0;
    FLAGS_SET_M(flags, FLAGS_GET_Z(center.z != 0.0) ? 1 : 0);
    curve = lwnurbscurve_construct(srid, flags, 2, pa, 12, knots);
    ptarray_free(pa);

    return curve;
}

/*
 * Convert NURBS curve to generic LWGEOM
 */
LWGEOM* lwgeom_from_nurbscurve(const LWNURBSCURVE *curve)
{
    return (LWGEOM*)curve;
}

/*
 * Cast LWGEOM to NURBS curve (with type checking)
 */
LWNURBSCURVE* lwgeom_as_nurbscurve(const LWGEOM *lwgeom)
{
    if (!lwgeom || !IS_NURBS_TYPE(lwgeom->type)) return NULL;
    return (LWNURBSCURVE*)lwgeom;
}
