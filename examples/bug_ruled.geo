
// solved by setting the tolerance lower in sys3x3_with_tol (-> again,
// this arised from a wrong mean plane computation)

//lc = 0.00001;
lc = 0.001;
Point(61) = {0.058, -0.005, 0, lc};
Point(62) = {0.058, -0.005, 0.000625, lc};
Point(64) = {0.058625, -0.005, 0, lc};

Point(85) = {0.058, -0.006, 0, lc};
Point(86) = {0.058, -0.006, 0.000625, lc};
Point(88) = {0.058625, -0.006, 0, lc};
Line(1) = {86,62};
Line(2) = {88,64};
Circle(3) = {62,61,64};
Circle(4) = {86,85,88};
Line Loop(5) = {2,-3,-1,4};
Ruled Surface(6) = {5};
