-- Tests for NURBS curve SFCGAL functions using native SFCGAL NURBS API

-- Test CG_NurbsCurveFromPoints - Create NURBS curve from control points
SELECT 't1', ST_GeometryType(CG_NurbsCurveFromPoints(
    'LINESTRING(0 0, 5 10, 10 0)'::geometry, 2
));

-- Test CG_NurbsCurveFromPoints - Create NURBS curve from control points (MultiPoint)
SELECT 't1mp', ST_GeometryType(CG_NurbsCurveFromPoints(
    'MULTIPOINT(0 0, 5 10, 10 0)'::geometry, 2
));

-- Test CG_NurbsCurveFromPoints with higher degree
SELECT 't2', ST_GeometryType(CG_NurbsCurveFromPoints(
    'LINESTRING(0 0, 5 5, 10 0, 15 -5)'::geometry, 3
));

-- Test CG_NurbsCurveToLineString conversion
SELECT 't3', ST_GeometryType(CG_NurbsCurveToLineString(
    CG_NurbsCurveFromPoints('LINESTRING(0 0, 5 10, 10 0)'::geometry, 2),
    8
));

-- Test CG_NurbsCurveToLineString with default segments
SELECT 't4', ST_GeometryType(CG_NurbsCurveToLineString(
    CG_NurbsCurveFromPoints('LINESTRING(0 0, 5 10, 10 0)'::geometry, 2)
));

-- Test CG_NurbsCurveEvaluate at parameter
SELECT 't5', ST_GeometryType(CG_NurbsCurveEvaluate(
    CG_NurbsCurveFromPoints('LINESTRING(0 0, 5 10, 10 0)'::geometry, 2),
    0.5
));

-- Test CG_NurbsCurveDerivative - first derivative (tangent)
SELECT 't6', ST_GeometryType(CG_NurbsCurveDerivative(
    CG_NurbsCurveFromPoints('LINESTRING(0 0, 5 10, 10 0)'::geometry, 2),
    0.5, 1
));

-- Test CG_NurbsCurveInterpolate through data points
SELECT 't7', ST_GeometryType(CG_NurbsCurveInterpolate(
    'LINESTRING(0 0, 2 5, 5 3, 8 1)'::geometry, 3
));

-- Test CG_NurbsCurveApproximate with tolerance
SELECT 't8', ST_GeometryType(CG_NurbsCurveApproximate(
    'LINESTRING(0 0, 1 2, 2 3, 3 2, 4 1, 5 0)'::geometry, 3, 0.1
));

-- Test 3D NURBS curve support
SELECT 't9', ST_GeometryType(CG_NurbsCurveFromPoints(
    'LINESTRING Z (0 0 0, 5 10 5, 10 0 0)'::geometry, 2
));

-- Test 3D NURBS curve evaluation
SELECT 't10', (ST_CoordDim(CG_NurbsCurveEvaluate(
    CG_NurbsCurveFromPoints('LINESTRING Z (0 0 0, 5 10 5, 10 0 0)'::geometry, 2),
    0.5
)) = 3) AS evaluation_preserves_3d;

-- Test interpolation with 3D points
SELECT 't11', (ST_CoordDim(CG_NurbsCurveInterpolate(
    'LINESTRING Z (0 0 0, 2 5 2, 5 3 1, 8 1 0)'::geometry, 3
)) = 3) AS interpolation_supports_3d;

-- Test approximation with M coordinates
SELECT 't12', ST_GeometryType(CG_NurbsCurveFromPoints(
    'LINESTRING M (0 0 1, 5 10 2, 10 0 3)'::geometry, 2
));

-- Error handling tests
-- Test error handling with invalid degree
SELECT 't13', CG_NurbsCurveFromPoints('LINESTRING(0 0, 5 10, 10 0)'::geometry, 0);

-- Test error handling with insufficient control points
SELECT 't14', CG_NurbsCurveFromPoints('LINESTRING(0 0, 5 10)'::geometry, 3);

-- Test error handling with invalid geometry types for control points
SELECT 't15', CG_NurbsCurveFromPoints('POINT(0 0)'::geometry, 2);

-- Test error handling with invalid segment count
SELECT 't16', CG_NurbsCurveToLineString(
    CG_NurbsCurveFromPoints('LINESTRING(0 0, 5 10, 10 0)'::geometry, 2),
    1
);

-- Test derivative computation at boundary
SELECT 't17', ST_GeometryType(CG_NurbsCurveDerivative(
    CG_NurbsCurveFromPoints('LINESTRING(0 0, 5 5, 10 0)'::geometry, 2),
    0.0, 1
));

-- Test higher order derivative
SELECT 't18', ST_GeometryType(CG_NurbsCurveDerivative(
    CG_NurbsCurveFromPoints('LINESTRING(0 0, 2 8, 5 5, 8 2, 10 0)'::geometry, 3),
    0.5, 2
));

-- Test approximation with max control points limit
SELECT 't19', ST_GeometryType(CG_NurbsCurveApproximate(
    'LINESTRING(0 0, 1 1, 2 4, 3 9, 4 16, 5 25, 6 36, 7 49, 8 64, 9 81, 10 100)'::geometry,
    3, 1.0, 5
));

-- Test parameter validation for CG_NurbsCurveEvaluate
SELECT 't20', CG_NurbsCurveEvaluate(
    CG_NurbsCurveFromPoints('LINESTRING(0 0, 5 10, 10 0)'::geometry, 2),
    1.5
);

-- WKT NURBS Curve Tests - Test reading and manipulating NURBS curves from WKT format

-- Test complex NURBS curve with degree 3
SELECT 't21', ST_GeometryType(
    'NURBSCURVE ((0.0 2.0,2.0 4.0,4.0 4.0,5.0 2.0,5.0 0.0,2.5 -3.0,0.0 -5.0,-2.5 -3.0,-5.0 0.0,-5.0 2.0,-4.0 4.0,-2.0 4.0,0.0 2.0),3)'::geometry
);

-- Test converting complex NURBS curve to LineString
SELECT 't22', ST_GeometryType(CG_NurbsCurveToLineString(
    'NURBSCURVE ((0.0 2.0,2.0 4.0,4.0 4.0,5.0 2.0,5.0 0.0,2.5 -3.0,0.0 -5.0,-2.5 -3.0,-5.0 0.0,-5.0 2.0,-4.0 4.0,-2.0 4.0,0.0 2.0),3)'::geometry,
    16
));

-- Test simple NURBS curve degree 2
SELECT 't23', ST_GeometryType(
    'NURBSCURVE ((0.00 0.00,5.00 10.00,10.00 0.00),2)'::geometry
);

-- Test NURBS curve with weights
SELECT 't24', ST_GeometryType(
    'NURBSCURVE ((0.00 0.00,5.00 10.00,10.00 0.00),(1.00,2.00,1.00),2)'::geometry
);

-- Test NURBS curve degree 2 with 4 control points
SELECT 't25', ST_GeometryType(
    'NURBSCURVE ((0.00 0.00,3.00 6.00,6.00 3.00,9.00 0.00),2)'::geometry
);

-- Test 3D NURBS curve with weights
SELECT 't26', ST_GeometryType(
    'NURBSCURVE Z ((0.00 0.00 0.00,5.00 10.00 5.00,10.00 0.00 0.00),(1.00,2.00,1.00),2)'::geometry
);

-- Test quarter circle approximation using weighted NURBS
SELECT 't27', ST_GeometryType(
    'NURBSCURVE ((1.00 0.00,1.00 1.00,0.00 1.00),(1.00,0.71,1.00),2)'::geometry
);

-- Test higher degree NURBS curve (degree 3, multiple segments)
SELECT 't28', ST_GeometryType(
    'NURBSCURVE ((0.00 0.00,3.00 2.00,6.00 -2.00,9.00 0.00,12.00 2.00,15.00 -2.00,18.00 0.00),3)'::geometry
);

-- Test conversion of weighted NURBS to LineString
SELECT 't29', ST_GeometryType(CG_NurbsCurveToLineString(
    'NURBSCURVE ((0.00 0.00,5.00 10.00,10.00 0.00),(1.00,2.00,1.00),2)'::geometry,
    10
));

-- Test evaluation of point on NURBS curve from WKT
SELECT 't30', ST_GeometryType(CG_NurbsCurveEvaluate(
    'NURBSCURVE ((0.00 0.00,5.00 10.00,10.00 0.00),2)'::geometry,
    0.5
));

-- Test derivative computation on WKT NURBS curve
SELECT 't31', ST_GeometryType(CG_NurbsCurveDerivative(
    'NURBSCURVE ((0.00 0.00,3.00 6.00,6.00 3.00,9.00 0.00),2)'::geometry,
    0.25, 1
));

-- Test 3D coordinate preservation
SELECT 't32', (ST_CoordDim(
    'NURBSCURVE Z ((0.00 0.00 0.00,5.00 10.00 5.00,10.00 0.00 0.00),(1.00,2.00,1.00),2)'::geometry
) = 3) AS preserves_3d_coordinates;
