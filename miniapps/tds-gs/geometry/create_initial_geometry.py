import matplotlib.pyplot as plt
import numpy as np

# SetFactory("OpenCASCADE");
# Point(1)={ 1.32, 0.0, 0 };
# Point(2)={ 1.319929875827836, 0.00856278624103039, 0 };
# Point(3)={ 1.3197195494079197, 0.01712345082879075, 0 };
# Point(4)={ 1.3193691589806256, 0.025679872635705904, 0 };
# ...
# Point(399)={ 1.3199058345166919, -0.009922589578139624, 0 };
# Point(400)={ 1.3199982311024445, -0.001359998583334122, 0 };
# 
# Line(401)={ 1, 2 };
# Line(402)={ 2, 3 };
# ...
# Line(799)={ 399, 400 };
# Line(800)={ 400, 1 };
#
# Line Loop(801)={ 
# 401,
# 402,
# ...
# 799,
# 800 };
# Plane Surface(802) = { 801 };

# separated_data = 'separated_file.data'
write_file = 'meshes/iter_gen_initial.geo'

def _VacuumVesselMetalWallCoordinates(return_coarse=3):
    """Return r and z coordinates for plasma-facing metal wall"""
    r = np.array([6.2670, 7.2830, 7.8990, 8.3060, 8.3950, 8.2700, 7.9040,
                  7.4000, 6.5870, 5.7530, 4.9040, 4.3110, 4.1260, 4.0760,
                  4.0460, 4.0460, 4.0670, 4.0970, 4.1780, 3.9579, 4.0034,
                  4.1742, 4.3257, 4.4408, 4.5066, 4.5157, 4.4670, 4.4064,
                  4.4062, 4.3773, 4.3115, 4.2457, 4.1799, 4.4918, 4.5687,
                  4.6456, 4.8215, 4.9982, 5.1496, 5.2529, 5.2628, 5.2727,
                  5.5650, 5.5650, 5.5650, 5.5650, 5.5720, 5.5720, 5.5720,
                  5.5720, 5.6008, 5.6842, 5.8150, 5.9821, 6.1710, 6.3655])

    z = np.array([-3.0460, -2.2570, -1.3420, -0.4210,  0.6330,  1.6810,
                   2.4640,  3.1790,  3.8940,  4.5320,  4.7120,  4.3240,
                   3.5820,  2.5660,  1.5490,  0.5330, -0.4840, -1.5000,
                  -2.5060, -2.5384, -2.5384, -2.5674, -2.6514, -2.7808,
                  -2.9410, -3.1139, -3.2801, -3.4043, -3.4048, -3.4799,
                  -3.6148, -3.7497, -3.8847, -3.9092, -3.8276, -3.7460,
                  -3.7090, -3.7414, -3.8382, -3.9852, -4.1244, -4.2636,
                  -4.5559, -4.4026, -4.2494, -4.0962, -3.9961, -3.9956,
                  -3.8960, -3.8950, -3.7024, -3.5265, -3.3823, -3.2822,
                  -3.2350, -3.2446])

    if return_coarse in (2, 3):
        z[18] += 0.2
    if return_coarse == 2:
        points_to_delete = [20, 28, 44, 46, 48]
        r = np.delete(r, points_to_delete, 0)
        z = np.delete(z, points_to_delete, 0)
    if return_coarse == 3:
        points_to_delete = [20, 21, 23, 25, 27, 28, 29, 30, 31, 34, 36, 38,
                            40, 43, 44, 45, 46, 47, 49, 50, 52, 54]
        r = np.delete(r, points_to_delete, 0)
        z = np.delete(z, points_to_delete, 0)
    return r, z


def _VacuumVesselSecondWallCoordinates(return_coarse=4):
    """Return r and z coordinates for outer vacuum vessel wall."""
    r = np.array([6.2270, 6.4090, 6.5880, 6.7639, 6.9365, 7.1054, 7.2706,
                  7.4318, 7.5888, 7.7414, 7.8895, 8.0328, 8.1712, 8.3046,
                  8.4327, 8.5554, 8.6726, 8.7840, 8.8897, 8.9894, 9.0830,
                  9.1705, 9.2516, 9.3264, 9.3946, 9.4563, 9.5114, 9.5598,
                  9.6014, 9.6361, 9.6616, 9.6771, 9.6825, 9.6777, 9.6629,
                  9.6380, 9.6031, 9.5583, 9.5038, 9.4396, 9.3660, 9.2900,
                  9.2141, 9.1382, 9.0623, 8.9863, 8.9104, 8.8345, 8.7585,
                  8.6826, 8.6067, 8.5308, 8.4548, 8.3789, 8.3030, 8.2270,
                  8.1511, 8.0681, 7.9748, 7.8714, 7.7584, 7.6362, 7.5052,
                  7.3659, 7.2188, 7.0645, 6.9035, 6.7364, 6.5638, 6.3864,
                  6.2048, 6.0196, 5.8317, 5.6415, 5.4499, 5.2575, 5.0651,
                  4.8734, 4.6848, 4.5011, 4.3243, 4.1561, 3.9982, 3.8523,
                  3.7199, 3.6022, 3.5005, 3.4159, 3.3492, 3.3010, 3.2719,
                  3.2622, 3.2622, 3.2622, 3.2622, 3.2622, 3.2622, 3.2622,
                  3.2622, 3.2622, 3.2622, 3.2622, 3.2622, 3.2622, 3.2622,
                  3.2622, 3.2622, 3.2622, 3.2622, 3.2622, 3.2622, 3.2622,
                  3.2622, 3.2622, 3.2622, 3.2622, 3.2622, 3.2622, 3.2622,
                  3.2622, 3.2622, 3.2622, 3.2622, 3.2622, 3.2622, 3.2622,
                  3.2622, 3.2622, 3.2622, 3.2622, 3.2717, 3.3002, 3.3473,
                  3.4126, 3.4953, 3.5948, 3.7098, 3.8394, 3.9820, 4.1364,
                  4.3008, 4.4737, 4.6532, 4.8274, 5.0040, 5.1820, 5.3605,
                  5.5385, 5.7151, 5.8894, 6.0603, 6.2270])

    z = np.array([ 5.4684,  5.3873,  5.3000,  5.2064,  5.1067,  5.0011,
                   4.8897,  4.7726,  4.6499,  4.5218,  4.3885,  4.2501,
                   4.1068,  3.9588,  3.8062,  3.6492,  3.4881,  3.3229,
                   3.1540,  2.9815,  2.8056,  2.6266,  2.4447,  2.2600,
                   2.0728,  1.8833,  1.6919,  1.4986,  1.3037,  1.1076,
                   0.9153,  0.7220,  0.5282,  0.3343,  0.1410, -0.0513,
                  -0.2421, -0.4308, -0.6169, -0.7999, -0.9793, -1.1514,
                  -1.3235, -1.4956, -1.6677, -1.8398, -2.0119, -2.1840,
                  -2.3561, -2.5282, -2.7003, -2.8724, -3.0445, -3.2166,
                  -3.3887, -3.5608, -3.7329, -3.9066, -4.0749, -4.2373,
                  -4.3931, -4.5418, -4.6828, -4.8157, -4.9398, -5.0549,
                  -5.1604, -5.2559, -5.3412, -5.4158, -5.4796, -5.5323,
                  -5.5737, -5.6036, -5.6220, -5.6287, -5.6238, -5.6034,
                  -5.5637, -5.5052, -5.4285, -5.3343, -5.2237, -5.0977,
                  -4.9576, -4.8049, -4.6411, -4.4679, -4.2871, -4.1004,
                  -3.9099, -3.7173, -3.5200, -3.3226, -3.1253, -2.9280,
                  -2.7306, -2.5333, -2.3359, -2.1386, -1.9412, -1.7439,
                  -1.5466, -1.3492, -1.1519, -0.9545, -0.7572, -0.5599,
                  -0.3625, -0.1652,  0.0322,  0.2295,  0.4269,  0.6242,
                   0.8215,  1.0189,  1.2162,  1.4136,  1.6109,  1.8083,
                   2.0056,  2.2029,  2.4003,  2.5976,  2.7950,  2.9923,
                   3.1897,  3.3870,  3.5843,  3.7817,  3.9697,  4.1559,
                   4.3382,  4.5148,  4.6839,  4.8438,  4.9929,  5.1295,
                   5.2524,  5.3603,  5.4520,  5.5266,  5.5834,  5.6223,
                   5.6483,  5.6614,  5.6614,  5.6484,  5.6224,  5.5836,
                   5.5322,  5.4684])

    return r[:-1:return_coarse], z[:-1:return_coarse]

def _VacuumVesselFirstWallCoordinates(return_coarse=5):
    """Return r and z coordinates for inner vacuum vessel wall"""
    r = np.array([3.5396, 3.5396, 3.5396, 3.5396, 3.5396, 3.5396, 3.5396,
                  3.5396, 3.5396, 3.5396, 3.5396, 3.5396, 3.5396, 3.5396,
                  3.5396, 3.5396, 3.5396, 3.5396, 3.5396, 3.5514, 3.5869,
                  3.6453, 3.7260, 3.8276, 3.9489, 4.0878, 4.2425, 4.4107,
                  4.5899, 4.7775, 4.9708, 5.1670, 5.3631, 5.5564, 5.7440,
                  5.9232, 6.0913, 6.2474, 6.4034, 6.5595, 6.7156, 6.8716,
                  7.0315, 7.1867, 7.3371, 7.4825, 7.6226, 7.7572, 7.8861,
                  8.0092, 8.1262, 8.2370, 8.3413, 8.4391, 8.5302, 8.6145,
                  8.6918, 8.7619, 8.8249, 8.8764, 8.9166, 8.9453, 8.9638,
                  8.9768, 8.9844, 8.9865, 8.9832, 8.9744, 8.9602, 8.9364,
                  8.8985, 8.8468, 8.7817, 8.7023, 8.6228, 8.5433, 8.4638,
                  8.3844, 8.3049, 8.2254, 8.1459, 8.0665, 7.9870, 7.9075,
                  7.8280, 7.7486, 7.6691, 7.5896, 7.5102, 7.4307, 7.3458,
                  7.2486, 7.1396, 7.0195, 6.8888, 6.7483, 6.5988, 6.4411,
                  6.2762, 6.1048, 5.9281, 5.7469, 5.5623, 5.3753, 5.1869,
                  4.9897, 4.7949, 4.6054, 4.4242, 4.2538, 4.0969, 3.9558,
                  3.8327, 3.7293, 3.6472, 3.5877, 3.5516, 3.5396, 3.5396,
                  3.5396, 3.5396, 3.5396, 3.5396, 3.5396, 3.5396, 3.5396,
                  3.5396, 3.5396, 3.5396, 3.5396, 3.5396, 3.5396, 3.5396,
                  3.5396, 3.5396])

    z = np.array([ 0.0307,  0.2278,  0.4250,  0.6221,  0.8192,  1.0164,
                   1.2135,  1.4106,  1.6078,  1.8049,  2.0020,  2.1992,
                   2.3963,  2.5935,  2.7906,  2.9877,  3.1849,  3.3820,
                   3.5791,  3.7753,  3.9686,  4.1562,  4.3354,  4.5035,
                   4.6582,  4.7971,  4.9183,  5.0200,  5.1006,  5.1590,
                   5.1944,  5.2062,  5.1943,  5.1589,  5.1004,  5.0197,
                   4.9180,  4.8102,  4.7024,  4.5946,  4.4868,  4.3790,
                   4.2640,  4.1429,  4.0158,  3.8830,  3.7446,  3.6009,
                   3.4521,  3.2984,  3.1400,  2.9772,  2.8102,  2.6393,
                   2.4647,  2.2867,  2.1056,  1.9216,  1.7351,  1.5529,
                   1.3679,  1.1809,  1.0073,  0.8333,  0.6590,  0.4845,
                   0.3100,  0.1357, -0.0382, -0.2088, -0.3768, -0.5411,
                  -0.7006, -0.8743, -1.0480, -1.2217, -1.3954, -1.5691,
                  -1.7428, -1.9166, -2.0903, -2.2640, -2.4377, -2.6114,
                  -2.7851, -2.9588, -3.1325, -3.3062, -3.4799, -3.6536,
                  -3.8222, -3.9840, -4.1380, -4.2835, -4.4196, -4.5456,
                  -4.6608, -4.7645, -4.8561, -4.9352, -5.0013, -5.0540,
                  -5.0931, -5.1183, -5.1295, -5.1217, -5.0899, -5.0345,
                  -4.9564, -4.8567, -4.7369, -4.5989, -4.4446, -4.2765,
                  -4.0970, -3.9088, -3.7148, -3.5178, -3.3206, -3.1235,
                  -2.9264, -2.7292, -2.5321, -2.3349, -2.1378, -1.9407,
                  -1.7435, -1.5464, -1.3493, -1.1521, -0.9550, -0.7579,
                  -0.5607, -0.3636, -0.1665])

    return r[::return_coarse], z[::return_coarse]


def main():
    r0 = [1.696, 1.696, 1.696, 1.696, 1.696,
          3.9431, 8.2851, 11.9919, 11.9630, 8.3908, 4.3340]
    z0 = [-5.415, -3.6067, -1.7983, 1.8183, 3.6267,
          7.5741, 6.5398, 3.2752, -2.2336, -6.7269, -7.4665]

    r_sn = 1.5
    z_sn = 1
    r_c = 2
    z_c = 1.5

    dr = [ r_sn, r_sn, r_sn, r_sn, r_sn,
           r_c, r_c, r_c, r_c, r_c, r_c]
    dz = [ z_sn, z_sn, z_sn, z_sn, z_sn,
           z_c, z_c, z_c, z_c, z_c, z_c]
    rr = [r+d/2 for r, d in zip(r0, dr)]
    zr = [z+d/2 for z, d in zip(z0, dz)]
    rl = [r-d/2 for r, d in zip(r0, dr)]
    zl = [z-d/2 for z, d in zip(z0, dz)]

    zl[:5] = [-5.415, -3.6067, -1.7983, 1.8183, 3.6267]
    zr[:5] = [-3.6067, -1.7983, 1.8183, 3.6267, 5.435]

    dz = [a-b for a, b in zip(zr, zl)]

    R = 16.5


    rlim, zlim = _VacuumVesselMetalWallCoordinates()

    stuff = 'SetFactory("OpenCASCADE");\n\n'
    count = 1

    box_dim = [3, 10, -6, 6]
    box_dim = [3+.5, 10-.5, -6+.5, 6-.5]
    stuff += "Rectangle(%d) = {%f, %f, 0, %f, %f, 0};\n" % (count, box_dim[0], box_dim[2],
                                                            box_dim[1] - box_dim[0],
                                                            box_dim[3] - box_dim[2])
    first_line = count + 4
    key = first_line

    # solenoids
    count_ = first_line
    for i in range(5):
        stuff += "Point(%d) = {%f, %f, 0};\n" % (count_, rl[i], zl[i])
        count_ += 1
        stuff += "Point(%d) = {%f, %f, 0};\n" % (count_, rr[i], zl[i])
        count_ += 1
    stuff += "Point(%d) = {%f, %f, 0};\n" % (count_, rl[i], zr[i])
    count_ += 1
    stuff += "Point(%d) = {%f, %f, 0};\n" % (count_, rr[i], zr[i])
    count_ += 1

    count_ = first_line
    line_count = key + 1
    cv1 = [57, 59, 61, 63, 65, 67, 68, 66, 64, 62, 60, 58, 60, 62, 64, 66]
    cv2 = [59, 61, 63, 65, 67, 68, 66, 64, 62, 60, 58, 57, 59, 61, 63, 65]
    for i in range(len(cv1)):
        stuff += "Line(%d) = {%d, %d};\n" % (i+line_count, cv1[i] - 57 + count_, cv2[i] - 57 + count_)

    array = [[81, 70, -82, 80],
             [82, 71, -83, 79],
             [83, 72, -84, 78],
             [84, 73, -85, 77],
             [85, 74, 75, 76]]
    for i in range(5):
        stuff += "Curve Loop(%d) = {%d, %d, %d, %d};\n" % (i + line_count,
                                                           np.sign(array[i][0])*(abs(array[i][0]) - 70 + line_count),
                                                           np.sign(array[i][1])*(abs(array[i][1]) - 70 + line_count),
                                                           np.sign(array[i][2])*(abs(array[i][2]) - 70 + line_count),
                                                           np.sign(array[i][3])*(abs(array[i][3]) - 70 + line_count))
        stuff += "Plane Surface(%d) = {%d};\n" % (i + line_count + 1, i + line_count)

    count = line_count + 6
    for i in range(5, len(rl)):
        stuff += "Rectangle(%d) = {%f, %f, 0, %f, %f, 0};\n" % (count, rl[i], zl[i], dr[i], dz[i])
        count += 1

    count = 41
    stuff += "Point(%d) = {0.0, %f, 0, 1.0};\n" % (count, R)
    stuff += "Point(%d) = {0.0, %f, 0, 1.0};\n" % (count+1, 0)
    stuff += "Point(%d) = {0.0, %f, 0, 1.0};\n" % (count+2, -R)
    stuff += "Circle(157) = {%d, %d, %d};\n" % (count, count+1, count+2)

    stuff += "\n"

    stuff += "Line(158) = {%d, %d};\n" % (count, count + 2)

    stuff += "Curve Loop(82) = {157, -158};\n"
    stuff += "Curve Loop(83) = {24, 25, 22, 23};\n"
    stuff += "Curve Loop(84) = {28, 29, 26, 27};\n"
    stuff += "Curve Loop(85) = {32, 33, 30, 31};\n"
    stuff += "Curve Loop(86) = {36, 37, 34, 35};\n"
    stuff += "Curve Loop(87) = {39, 40, 41, 38};\n"
    stuff += "Curve Loop(88) = {42, 43, 44, 45};\n"
    stuff += "Curve Loop(89) = {17, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};\n"
    stuff += "Curve Loop(90) = {4, 1, 2, 3};\n"
    stuff += "Plane Surface(18) = {82, 83, 84, 85, 86, 87, 88, 89, 90};\n"


    stuff += 'Physical Surface("interior", 2000) = {18};\n'
    for d in range(1, 12):
        stuff += 'Physical Surface("coil%d", %d) = {%d};\n' % (d, 831+d, 6+d)
    stuff += 'Physical Surface("exact", 1100) = {1};\n'
    stuff += 'Physical Curve("boundary", 831) = {157};\n'
    stuff += 'Physical Curve("axis", 900) = {158};\n'
    stuff += 'Physical Curve("box", 832) = {3, 2, 1, 4};\n'
    # stuff += 'Recursive Delete {\n'
    # stuff += '  Surface{1};\n'
    # stuff += '}\n'

    with open(write_file, 'w') as gid:
        gid.write(stuff)


    
if __name__ == "__main__":
    main()