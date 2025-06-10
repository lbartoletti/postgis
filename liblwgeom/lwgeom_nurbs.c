/*
 * PostGIS NURBS Support - Core Implementation
 * File: liblwgeom/lwgeom_nurbs.c
 *
 * Core algorithms for NURBS and Bezier curve operations.
 *
 * Copyright (c) 2024 Loïc Bartoletti
 * License: GPL v2+
 */

#include "lwgeom_nurbs.h"

/*
 * Construct a new NURBS curve
 */
LWNURBS* lwnurbs_construct(uint32_t srid, uint8_t flags, uint32_t degree,
                          uint32_t num_points, double *ordinates,
                          uint32_t num_knots, double *knots)
{
    LWNURBS *nurbs;
    int coord_size;
    int i;

    /* Validate input parameters */
    if (degree < 1 || degree > NURBS_MAX_DEGREE) return NULL;
    if (num_points < 2 || num_points > NURBS_MAX_POINTS) return NULL;
    if (degree >= num_points) return NULL;
    if (!ordinates) return NULL;

    /* Allocate structure */
    nurbs = lwalloc(sizeof(LWNURBS));
    if (!nurbs) return NULL;

    /* Set basic properties */
    nurbs->srid = srid;
    nurbs->flags = flags;
    nurbs->degree = degree;
    nurbs->num_points = num_points;
    nurbs->num_knots = num_knots;

    /* Determine curve type */
    nurbs->type = (num_knots > 0) ? NURBSTYPE : BEZIERTYPE;

    /* Copy control point ordinates */
    coord_size = NURBS_COORD_SIZE(flags);
    nurbs->ordinates = lwalloc(num_points * coord_size * sizeof(double));
    if (!nurbs->ordinates) {
        lwfree(nurbs);
        return NULL;
    }
    memcpy(nurbs->ordinates, ordinates, num_points * coord_size * sizeof(double));

    /* Check if rational (non-uniform weights) */
    nurbs->is_rational = false;
    for (i = coord_size - 1; i < num_points * coord_size; i += coord_size) {
        if (fabs(ordinates[i] - 1.0) > NURBS_EPSILON) {
            nurbs->is_rational = true;
            break;
        }
    }

    /* Copy knot vector if present */
    if (num_knots > 0 && knots) {
        nurbs->knots = lwalloc(num_knots * sizeof(double));
        if (!nurbs->knots) {
            lwfree(nurbs->ordinates);
            lwfree(nurbs);
            return NULL;
        }
        memcpy(nurbs->knots, knots, num_knots * sizeof(double));
    } else {
        nurbs->knots = NULL;
    }

    return nurbs;
}

/*
 * Free NURBS curve memory
 */
void lwnurbs_free(LWNURBS *nurbs)
{
    if (nurbs) {
        if (nurbs->ordinates) lwfree(nurbs->ordinates);
        if (nurbs->knots) lwfree(nurbs->knots);
        lwfree(nurbs);
    }
}

/*
 * Validate NURBS curve
 */
bool lwnurbs_is_valid(LWNURBS *nurbs)
{
    int coord_size;
    int i;

    if (!nurbs) return false;

    /* Basic parameter validation */
    if (nurbs->degree < 1 || nurbs->num_points < 2) return false;
    if (nurbs->degree >= nurbs->num_points) return false;

    /* Validate weights are positive */
    coord_size = NURBS_COORD_SIZE(nurbs->flags);
    for (i = coord_size - 1; i < nurbs->num_points * coord_size; i += coord_size) {
        if (nurbs->ordinates[i] <= 0.0) return false;
    }

    /* NURBS-specific validation */
    if (nurbs->type == NURBSTYPE) {
        if (nurbs->num_knots != nurbs->num_points + nurbs->degree + 1) return false;
        if (!nurbs->knots) return false;

        /* Validate knot ordering */
        for (i = 1; i < nurbs->num_knots; i++) {
            if (nurbs->knots[i] < nurbs->knots[i-1]) return false;
        }

        /* Validate clamping */
        for (i = 1; i <= nurbs->degree; i++) {
            if (fabs(nurbs->knots[i] - nurbs->knots[0]) > NURBS_EPSILON) return false;
            if (fabs(nurbs->knots[nurbs->num_knots - 1 - i] -
                    nurbs->knots[nurbs->num_knots - 1]) > NURBS_EPSILON) return false;
        }

        /* Validate normalization */
        if (fabs(nurbs->knots[0]) > NURBS_EPSILON) return false;
        if (fabs(nurbs->knots[nurbs->num_knots - 1] - 1.0) > NURBS_EPSILON) return false;
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

    /* Use the more efficient formula */
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
double lwnurbs_bspline_basis(int i, int p, double t, double *knots, int num_knots)
{
    double left, right;
    double left_denom, right_denom;

    /* Base case */
    if (p == 0) {
        return (t >= knots[i] && t < knots[i + 1]) ? 1.0 : 0.0;
    }

    /* Recursive calculation */
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
void lwnurbs_evaluate_point(LWNURBS *nurbs, double t, POINT4D *point)
{
    double px = 0.0, py = 0.0, pz = 0.0, weight_sum = 0.0;
    double cx, cy, cz, cw, basis_value, weighted_basis;
    int coord_size;
    int i, offset;

    if (!nurbs || !point) return;

    /* Clamp parameter to [0,1] */
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;

    coord_size = NURBS_COORD_SIZE(nurbs->flags);

    /* Evaluate curve */
    if (nurbs->type == BEZIERTYPE) {
        /* Bezier curve evaluation using Bernstein polynomials */
        for (i = 0; i < nurbs->num_points; i++) {
            offset = i * coord_size;
            cx = nurbs->ordinates[offset];
            cy = nurbs->ordinates[offset + 1];
            cz = FLAGS_GET_Z(nurbs->flags) ? nurbs->ordinates[offset + 2] : 0.0;
            cw = nurbs->ordinates[offset + coord_size - 1];

            basis_value = lwnurbs_bernstein_polynomial(nurbs->degree, i, t);

            if (nurbs->is_rational) {
                weighted_basis = basis_value * cw;
                px += weighted_basis * cx / cw;
                py += weighted_basis * cy / cw;
                if (FLAGS_GET_Z(nurbs->flags)) pz += weighted_basis * cz / cw;
                weight_sum += weighted_basis;
            } else {
                px += basis_value * cx;
                py += basis_value * cy;
                if (FLAGS_GET_Z(nurbs->flags)) pz += basis_value * cz;
            }
        }

        if (nurbs->is_rational && weight_sum > NURBS_EPSILON) {
            px /= weight_sum;
            py /= weight_sum;
            pz /= weight_sum;
        }

    } else if (nurbs->type == NURBSTYPE) {
        /* NURBS curve evaluation using B-spline basis functions */
        for (i = 0; i < nurbs->num_points; i++) {
            offset = i * coord_size;
            cx = nurbs->ordinates[offset];
            cy = nurbs->ordinates[offset + 1];
            cz = FLAGS_GET_Z(nurbs->flags) ? nurbs->ordinates[offset + 2] : 0.0;
            cw = nurbs->ordinates[offset + coord_size - 1];

            basis_value = lwnurbs_bspline_basis(i, nurbs->degree, t,
                                              nurbs->knots, nurbs->num_knots);

            weighted_basis = basis_value * cw;
            px += weighted_basis * cx / cw;
            py += weighted_basis * cy / cw;
            if (FLAGS_GET_Z(nurbs->flags)) pz += weighted_basis * cz / cw;
            weight_sum += weighted_basis;
        }

        if (weight_sum > NURBS_EPSILON) {
            px /= weight_sum;
            py /= weight_sum;
            pz /= weight_sum;
        }
    }

    /* Set result point */
    point->x = px;
    point->y = py;
    point->z = pz;
    point->m = 0.0;
}

/*
 * Convert NURBS curve to linestring by sampling
 */
LWLINE* lwnurbs_to_linestring(LWNURBS *nurbs, int num_segments)
{
    POINTARRAY *pa;
    POINT4D point;
    double step, t;
    int i;

    if (!nurbs || num_segments < 1) return NULL;

    /* Create point array */
    pa = ptarray_construct(FLAGS_GET_Z(nurbs->flags),
                          FLAGS_GET_M(nurbs->flags),
                          num_segments + 1);

    step = 1.0 / num_segments;

    /* Sample points along curve */
    for (i = 0; i <= num_segments; i++) {
        t = i * step;
        lwnurbs_evaluate_point(nurbs, t, &point);
        ptarray_set_point4d(pa, i, &point);
    }

    return lwline_construct(nurbs->srid, NULL, pa);
}

/*
 * Calculate approximate curve length
 */
double lwnurbs_length(LWNURBS *nurbs, int num_segments)
{
    POINT4D p1, p2;
    double length = 0.0;
    double step, t;
    int i;

    if (!nurbs || num_segments < 1) return 0.0;

    step = 1.0 / num_segments;
    lwnurbs_evaluate_point(nurbs, 0.0, &p1);

    for (i = 1; i <= num_segments; i++) {
        t = i * step;
        lwnurbs_evaluate_point(nurbs, t, &p2);

        length += sqrt(pow(p2.x - p1.x, 2) + pow(p2.y - p1.y, 2) +
                      (FLAGS_GET_Z(nurbs->flags) ? pow(p2.z - p1.z, 2) : 0));

        p1 = p2;
    }

    return length;
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

    /* Clamp at start (degree+1 zeros) */
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

    /* Clamp at end (degree+1 ones) */
    for (i = 0; i <= degree; i++) {
        knots[num_knots - 1 - i] = 1.0;
    }

    return knots;
}

/*
 * Validate knot vector
 */
bool lwnurbs_validate_knots(double *knots, uint32_t num_knots, uint32_t degree)
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
 * Helper: Create quadratic Bezier curve
 */
LWNURBS* lwnurbs_bezier_quadratic(uint32_t srid, POINT4D p1, POINT4D p2, POINT4D p3)
{
    double ordinates[9]; /* 3 points * 3 coords (x,y,w) */

    ordinates[0] = p1.x; ordinates[1] = p1.y; ordinates[2] = 1.0;
    ordinates[3] = p2.x; ordinates[4] = p2.y; ordinates[5] = 1.0;
    ordinates[6] = p3.x; ordinates[7] = p3.y; ordinates[8] = 1.0;

    return lwnurbs_construct(srid, 0, 2, 3, ordinates, 0, NULL);
}

/*
 * Helper: Create cubic Bezier curve
 */
LWNURBS* lwnurbs_bezier_cubic(uint32_t srid, POINT4D p1, POINT4D p2, POINT4D p3, POINT4D p4)
{
    double ordinates[12]; /* 4 points * 3 coords (x,y,w) */

    ordinates[0] = p1.x; ordinates[1] = p1.y; ordinates[2] = 1.0;
    ordinates[3] = p2.x; ordinates[4] = p2.y; ordinates[5] = 1.0;
    ordinates[6] = p3.x; ordinates[7] = p3.y; ordinates[8] = 1.0;
    ordinates[9] = p4.x; ordinates[10] = p4.y; ordinates[11] = 1.0;

    return lwnurbs_construct(srid, 0, 3, 4, ordinates, 0, NULL);
}

/*
 * Helper: Create NURBS circle approximation
 */
LWNURBS* lwnurbs_circle(uint32_t srid, POINT4D center, double radius)
{
    double ordinates[27]; /* 9 points * 3 coords */
    double knots[12] = {0, 0, 0, 0.25, 0.25, 0.5, 0.5, 0.75, 0.75, 1, 1, 1};
    double w = sqrt(2.0) / 2.0; /* Weight for rational circle */
    double cx = center.x, cy = center.y;

    /* 9 control points for rational quadratic NURBS circle */
    ordinates[0] = cx + radius; ordinates[1] = cy; ordinates[2] = 1.0;
    ordinates[3] = cx + radius; ordinates[4] = cy + radius; ordinates[5] = w;
    ordinates[6] = cx; ordinates[7] = cy + radius; ordinates[8] = 1.0;
    ordinates[9] = cx - radius; ordinates[10] = cy + radius; ordinates[11] = w;
    ordinates[12] = cx - radius; ordinates[13] = cy; ordinates[14] = 1.0;
    ordinates[15] = cx - radius; ordinates[16] = cy - radius; ordinates[17] = w;
    ordinates[18] = cx; ordinates[19] = cy - radius; ordinates[20] = 1.0;
    ordinates[21] = cx + radius; ordinates[22] = cy - radius; ordinates[23] = w;
    ordinates[24] = cx + radius; ordinates[25] = cy; ordinates[26] = 1.0;

    return lwnurbs_construct(srid, 0, 2, 9, ordinates, 12, knots);
}
