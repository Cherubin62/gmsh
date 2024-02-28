// rectangle shape geometry
dx = 2;
dy = 2.5;
p = 1;

Point(1) = {-dx, -dy, 0, p};
Point(2) = {+dx, -dy, 0, p};
Point(3) = {+dx, +dy, 0, p};
Point(4) = {-dx, +dy, 0, p};

Point(5) = {+3.5*dx, 0, 0, p};
Point(6) = {-3.5*dx, 0, 0, p};

Line(1) = {1, 2};
// Line(2) ={3,2};
Circle(2) = {3, 5, 2};
Line(3) = {3, 4};
// Line(4) ={1,4};
Circle(4) = {1, 6, 4};


Curve Loop(1) = {1, -2, 3, -4};
Plane Surface(1) = {1};

/*
Physical Point("start_0") = {1};
Physical Point("end_0") = {2};
*/

/*
Physical Point("start_1") = {3};
Physical Point("end_1") = {4};
*/
/*
Physical Point("start_2") = {1};
Physical Point("end_2") = {3};
*/
/*
Physical Point("start_4") = {1};
Physical Point("end_4") = {4};
*/


Physical Point("vertices") = {1,2,3,4};
Physical Curve("edges") = {1,2,3,4};
Physical Surface("surface") = {1};
