SetFactory("OpenCASCADE");

//first wall
Point(1)={ 6.267000, -3.046000, 0 };
Point(2)={ 7.283000, -2.257000, 0 };
Point(3)={ 7.899000, -1.342000, 0 };
Point(4)={ 8.306000, -0.421000, 0 };
Point(5)={ 8.395000, 0.633000, 0 };
Point(6)={ 8.270000, 1.681000, 0 };
Point(7)={ 7.904000, 2.464000, 0 };
Point(8)={ 7.400000, 3.179000, 0 };
Point(9)={ 6.587000, 3.894000, 0 };
Point(10)={ 5.753000, 4.532000, 0 };
Point(11)={ 4.904000, 4.712000, 0 };
Point(12)={ 4.311000, 4.324000, 0 };
Point(13)={ 4.126000, 3.582000, 0 };
Point(14)={ 4.076000, 2.566000, 0 };
Point(15)={ 4.046000, 1.549000, 0 };
Point(16)={ 4.046000, 0.533000, 0 };
Point(17)={ 4.067000, -0.484000, 0 };
Point(18)={ 4.097000, -1.500000, 0 };
Point(19)={ 4.178000, -2.306000, 0 };
Point(20)={ 3.957900, -2.538400, 0 };
Point(21)={ 4.325700, -2.651400, 0 };
Point(22)={ 4.506600, -2.941000, 0 };
Point(23)={ 4.467000, -3.280100, 0 };
Point(24)={ 4.179900, -3.884700, 0 };
Point(25)={ 4.491800, -3.909200, 0 };
Point(26)={ 4.645600, -3.746000, 0 };
Point(27)={ 4.998200, -3.741400, 0 };
Point(28)={ 5.252900, -3.985200, 0 };
Point(29)={ 5.272700, -4.263600, 0 };
Point(30)={ 5.565000, -4.555900, 0 };
Point(31)={ 5.572000, -3.896000, 0 };
Point(32)={ 5.684200, -3.526500, 0 };
Point(33)={ 5.982100, -3.282200, 0 };
Point(34)={ 6.365500, -3.244600, 0 };

Line(35)={ 1, 2 };
Line(36)={ 2, 3 };
Line(37)={ 3, 4 };
Line(38)={ 4, 5 };
Line(39)={ 5, 6 };
Line(40)={ 6, 7 };
Line(41)={ 7, 8 };
Line(42)={ 8, 9 };
Line(43)={ 9, 10 };
Line(44)={ 10, 11 };
Line(45)={ 11, 12 };
Line(46)={ 12, 13 };
Line(47)={ 13, 14 };
Line(48)={ 14, 15 };
Line(49)={ 15, 16 };
Line(50)={ 16, 17 };
Line(51)={ 17, 18 };
Line(52)={ 18, 19 };
Line(53)={ 19, 20 };
Line(54)={ 20, 21 };
Line(55)={ 21, 22 };
Line(56)={ 22, 23 };
Line(57)={ 23, 24 };
Line(58)={ 24, 25 };
Line(59)={ 25, 26 };
Line(60)={ 26, 27 };
Line(61)={ 27, 28 };
Line(62)={ 28, 29 };
Line(63)={ 29, 30 };
Line(64)={ 30, 31 };
Line(65)={ 31, 32 };
Line(66)={ 32, 33 };
Line(67)={ 33, 34 };
Line(68)={ 34, 1 };

Line Loop(69)={
35,
36,
37,
38,
39,
40,
41,
42,
43,
44,
45,
46,
47,
48,
49,
50,
51,
52,
53,
54,
55,
56,
57,
58,
59,
60,
61,
62,
63,
64,
65,
66,
67,
68 };
Plane Surface(70) = { 69 };

//solenoids
Point(35) = {0.946000, -5.415000, 0};
Point(36) = {2.446000, -5.415000, 0};
Point(37) = {0.946000, -3.606700, 0};
Point(38) = {2.446000, -3.606700, 0};
Point(39) = {0.946000, -1.798300, 0};
Point(40) = {2.446000, -1.798300, 0};
Point(41) = {0.946000, 1.818300, 0};
Point(42) = {2.446000, 1.818300, 0};
Point(43) = {0.946000, 3.626700, 0};
Point(44) = {2.446000, 3.626700, 0};
Point(45) = {0.946000, 5.435000, 0};
Point(46) = {2.446000, 5.435000, 0};
Line(70) = {35, 37};
Line(71) = {37, 39};
Line(72) = {39, 41};
Line(73) = {41, 43};
Line(74) = {43, 45};
Line(75) = {45, 46};
Line(76) = {46, 44};
Line(77) = {44, 42};
Line(78) = {42, 40};
Line(79) = {40, 38};
Line(80) = {38, 36};
Line(81) = {36, 35};
Line(82) = {38, 37};
Line(83) = {40, 39};
Line(84) = {42, 41};
Line(85) = {44, 43};
Curve Loop(70) = {81, 70, -82, 80};
Plane Surface(71) = {70};
Curve Loop(71) = {82, 71, -83, 79};
Plane Surface(72) = {71};
Curve Loop(72) = {83, 72, -84, 78};
Plane Surface(73) = {72};
Curve Loop(73) = {84, 73, -85, 77};
Plane Surface(74) = {73};
Curve Loop(74) = {85, 74, 75, 76};
Plane Surface(75) = {74};

// coils
Rectangle(76) = {2.943100, 6.824100, 0, 2.000000, 1.500000, 0};
Rectangle(77) = {7.285100, 5.789800, 0, 2.000000, 1.500000, 0};
Rectangle(78) = {10.991900, 2.525200, 0, 2.000000, 1.500000, 0};
Rectangle(79) = {10.963000, -2.983600, 0, 2.000000, 1.500000, 0};
Rectangle(80) = {7.390800, -7.476900, 0, 2.000000, 1.500000, 0};
Rectangle(81) = {3.334000, -8.216500, 0, 2.000000, 1.500000, 0};

//outer boundary
Point(71) = {0.0, 16.000000, 0, 1.0};
Point(72) = {0.0, 0.000000, 0, 1.0};
Point(73) = {0.0, -16.000000, 0, 1.0};
Circle(157) = {71, 72, 73};
Line(158) = {71, 73};

Curve Loop(81) = {158, -157};
Curve Loop(82) = {70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81};
Curve Loop(83) = {89, 86, 87, 88};
Curve Loop(84) = {93, 90, 91, 92};
Curve Loop(85) = {97, 94, 95, 96};
Curve Loop(86) = {101, 98, 99, 100};
Curve Loop(87) = {103, 104, 105, 102};
Curve Loop(88) = {107, 108, 109, 106};
Curve Loop(89) = {51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50};
Plane Surface(82) = {81, 82, 83, 84, 85, 86, 87, 88, 89};

//physical attributes
Physical Surface("interior", 2000) = {82};
Physical Surface("coil1", 832) = {71};
Physical Surface("coil2", 833) = {72};
Physical Surface("coil3", 834) = {73};
Physical Surface("coil4", 835) = {74};
Physical Surface("coil5", 836) = {75};
Physical Surface("coil6", 837) = {76};
Physical Surface("coil7", 838) = {77};
Physical Surface("coil8", 839) = {78};
Physical Surface("coil9", 840) = {79};
Physical Surface("coil10", 841) = {80};
Physical Surface("coil11", 842) = {81};
Physical Surface("limiter", 1000) = {70};

//physical boundary
Physical Curve("boundary", 831) = {157};
Physical Curve("axis", 900) = {158};
Mesh.MshFileVersion = 2.2;
