// Copyright (c) 2010, Lawrence Livermore National Security, LLC. Produced at
// the Lawrence Livermore National Laboratory. LLNL-CODE-443211. All Rights
// reserved. See file COPYRIGHT for details.
//
// This file is part of the MFEM library. For more information and source code
// availability see http://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the GNU Lesser General Public License (as published by the Free
// Software Foundation) version 2.1 dated February 1999.
//
//   -----------------------------------------------------------------------
//       Stix1D_DH Miniapp: Cold Plasma Electromagnetic Simulation Code
//   -----------------------------------------------------------------------
//
//   Assumes that all sources and boundary conditions oscillate with the same
//   frequency although not necessarily in phase with one another.  This
//   assumption implies that we can factor out the time dependence which we
//   take to be of the form exp(-i omega t).  With these assumptions we can
//   write the Maxwell equations for D and H in the form:
//
//   -i omega D    = Curl H - J
//    i omega mu H = Curl epsilon^{-1} D
//
//   Which combine to yield:
//
//   Curl epsilon^{-1} Curl H - omega^2 mu H = Curl epsilon^{-1} J
//
//   In a cold plasma the dielectric tensor, epsilon, is complex-valued and
//   anisotropic.  The anisotropy aligns with the external magnetic field and
//   the values depend on the properties of the plasma including the masses and
//   charges of its constituent ion species.
//
//   For a magnetic field aligned with the z-axis the dielectric tensor has
//   the form:
//              | S  -iD 0 |
//    epsilon = |iD   S  0 |
//              | 0   0  P |
//
//   Where:
//      S = 1 - Sum_species omega_p^2 / (omega^2 - omega_c^2)
//      D = Sum_species omega_p^2 omega_c / (omega^2 - omega_c^2)
//      P = 1 - Sum_species omega_p^2 / omega^2
//
//   and:
//      omega_p is the plasma frequency
//      omega_c is the cyclotron frequency
//      omega   is the driving frequency
//
//   The plasma and cyclotron frequencies depend primarily on the properties
//   of the ion species.  We also include a complex-valued mass correction
//   which depends on the plasma temperature.
//
//   We discretize this equation with H(Curl) a.k.a Nedelec basis
//   functions.  The curl curl operator must be handled with
//   integration by parts which yields a surface integral:
//
//   (W, Curl epsilon^{-1} Curl H) = (Curl W, epsilon^{-1} Curl H)
//               + (W, n x (epsilon^{-1} Curl H))_{\Gamma}
//
//   or
//
//   (W, Curl epsilon^{-1} Curl H) = (Curl W, epsilon^{-1} Curl H)
//               - i omega (W, n x E)_{\Gamma}
//
//   Assuming J = 0 on the boundary
//
//   The non-linear sheath boundary condition can be used to set the
//   components of E that are tangent to the boundary. The governing
//   equations are:
//
//      E_t = - Grad Phi_{RF} (where Phi_{RF} is the sheath potential)
//      Phi_{RF} = i omega z_{sh} D_n
//
//   Where D_n is the normal component of D = epsilon E and z_{sh}
//   is the sheath impedance. The impedance z_{sh} is a function of the
//   plasma density, plasma temperature, ion charges, ion masses,
//   magnetic field strength, and the sheath potential itself. Clearly the
//   dependence on the potential is the source of the non-linearity.
//
//   The sheath boundary condition can be easily incorporated into the
//   weak form of the curl-curl operator:
//
//   (W, Curl epsilon^{-1} Curl H) = (Curl W, epsilon^{-1} Curl H)
//               + i omega (W, n x Grad Phi_{RF})_{\Gamma}
//
//   To compute Phi_{RF} we augment the Maxwell equations with the
//   relation between D_n and Phi_{RF}:
//
//      - i omega z_{sh} D_n + Phi_{RF} = 0
//
//   or
//
//      z_{sh} Curl H + Phi_{RF} = 0
//
// (By default the sources and fields are all zero)
//
// Compile with: make stix2d_dh
//
// Sample runs:
//   ./stix2d -rod '0 0 1 0 0 0.1' -o 3 -s 1 -rs 0 -maxit 1 -f 1e6
//
// Sample runs with partial assembly:
//   ./stix2d -rod '0 0 1 0 0 0.1' -o 3 -s 1 -rs 0 -maxit 1 -f 1e6 -pa
//
// Device sample runs:
//   ./stix2d -rod '0 0 1 0 0 0.1' -o 3 -s 1 -rs 0 -maxit 1 -f 1e6 -pa -d cuda
//
// Parallel sample runs:
//   mpirun -np 4 ./stix2d -rod '0 0 1 0 0 0.1' -dbcs '1' -w Z -o 3 -s 1 -rs 0 -maxit 1 -f 1e6
//

#include "cold_plasma_dielectric_coefs.hpp"
#include "cold_plasma_dielectric_dh_solver.hpp"
#include "../common/mesh_extras.hpp"
#include <fstream>
#include <iostream>
#include <cmath>
#include <complex>

using namespace std;
using namespace mfem;
using namespace mfem::common;
using namespace mfem::plasma;

// Admittance for Absorbing Boundary Condition
Coefficient * SetupRealAdmittanceCoefficient(const Mesh & mesh,
                                             const Array<int> & abcs);

// Storage for user-supplied, real-valued impedance
static Vector pw_eta_(0);      // Piecewise impedance values
static Vector pw_eta_inv_(0);  // Piecewise inverse impedance values

// Storage for user-supplied, complex-valued impedance
static Vector pw_eta_re_(0);      // Piecewise real impedance
static Vector pw_eta_inv_re_(0);  // Piecewise inverse real impedance
static Vector pw_eta_im_(0);      // Piecewise imaginary impedance
static Vector pw_eta_inv_im_(0);  // Piecewise inverse imaginary impedance

// Current Density Function
static Vector slab_params_
(0); // Amplitude of x, y, z current source, position in 1D, and size in 1D

void slab_current_source_r(const Vector &x, Vector &j);
void slab_current_source_i(const Vector &x, Vector &j);
void j_src_r(const Vector &x, Vector &j)
{
   if (slab_params_.Size() > 0)
   {
      slab_current_source_r(x, j);
   }
}
void j_src_i(const Vector &x, Vector &j)
{
   if (slab_params_.Size() > 0)
   {
      slab_current_source_i(x, j);
   }
}

// Electric Field Boundary Condition: The following function returns zero but
// any function could be used.
void e_bc_r(const Vector &x, Vector &E);
void e_bc_i(const Vector &x, Vector &E);

class ColdPlasmaPlaneWaveH: public VectorCoefficient
{
public:
   ColdPlasmaPlaneWaveH(char type,
                        double omega,
                        const Vector & B,
                        const Vector & number,
                        const Vector & charge,
                        const Vector & mass,
                        const Vector & temp,
                        int nuprof,
                        bool realPart);

   void SetCurrentSlab(double Jy, double xJ, double delta, double Lx)
   { Jy_ = Jy; xJ_ = xJ; dx_ = delta, Lx_ = Lx; }

   void SetPhaseShift(const Vector & beta)
   { beta_r_ = beta; beta_i_ = 0.0; }
   void SetPhaseShift(const Vector & beta_r,
                      const Vector & beta_i)
   { beta_r_ = beta_r; beta_i_ = beta_i; }

   void GetWaveVector(Vector & k_r, Vector & k_i) const
   { k_r = k_r_; k_i = k_i_; }

   void Eval(Vector &V, ElementTransformation &T,
             const IntegrationPoint &ip);

private:
   char type_;
   bool realPart_;
   int nuprof_;
   double omega_;
   double Bmag_;
   double Jy_;
   double xJ_;
   double dx_;
   double Lx_;
   complex<double> kappa_;
   Vector b_;   // Normalized vector in direction of B
   Vector bc_;  // Normalized vector perpendicular to b_, (by-bz,bz-bx,bx-by)
   Vector bcc_; // Normalized vector perpendicular to b_ and bc_
   Vector h_r_;
   Vector h_i_;
   Vector k_r_;
   Vector k_i_;
   Vector beta_r_;
   Vector beta_i_;

   // const Vector & B_;
   const Vector & numbers_;
   const Vector & charges_;
   const Vector & masses_;
   const Vector & temps_;

   complex<double> S_;
   complex<double> D_;
   complex<double> P_;
};

class ColdPlasmaPlaneWaveE: public VectorCoefficient
{
public:
   ColdPlasmaPlaneWaveE(char type,
                        double omega,
                        const Vector & B,
                        const Vector & number,
                        const Vector & charge,
                        const Vector & mass,
                        const Vector & temp,
                        int nuprof,
                        bool realPart);

   void SetCurrentSlab(double Jy, double xJ, double delta, double Lx)
   { Jy_ = Jy; xJ_ = xJ; dx_ = delta, Lx_ = Lx; }

   void SetPhaseShift(const Vector & beta)
   { beta_r_ = beta; beta_i_ = 0.0; }
   void SetPhaseShift(const Vector & beta_r,
                      const Vector & beta_i)
   { beta_r_ = beta_r; beta_i_ = beta_i; }

   void GetWaveVector(Vector & k_r, Vector & k_i) const
   { k_r = k_r_; k_i = k_i_; }

   void Eval(Vector &V, ElementTransformation &T,
             const IntegrationPoint &ip);

private:
   char type_;
   bool realPart_;
   int nuprof_;
   double omega_;
   double Bmag_;
   double Jy_;
   double xJ_;
   double dx_;
   double Lx_;
   complex<double> kappa_;
   Vector b_;   // Normalized vector in direction of B
   Vector bc_;  // Normalized vector perpendicular to b_, (by-bz,bz-bx,bx-by)
   Vector bcc_; // Normalized vector perpendicular to b_ and bc_
   Vector e_r_;
   Vector e_i_;
   Vector k_r_;
   Vector k_i_;
   Vector beta_r_;
   Vector beta_i_;

   // const Vector & B_;
   const Vector & numbers_;
   const Vector & charges_;
   const Vector & masses_;
   const Vector & temps_;

   complex<double> S_;
   complex<double> D_;
   complex<double> P_;
};

void Update(ParFiniteElementSpace & H1FESpace,
            ParFiniteElementSpace & HCurlFESpace,
            ParFiniteElementSpace & HDivFESpace,
            ParFiniteElementSpace & L2FESpace,
            VectorCoefficient & BCoef,
            Coefficient & rhoCoef,
            Coefficient & TCoef,
            Coefficient & nueCoef,
            Coefficient & nuiCoef,
            int & size_h1,
            int & size_l2,
            Array<int> & density_offsets,
            Array<int> & temperature_offsets,
            BlockVector & density,
            BlockVector & temperature,
            ParGridFunction & BField,
            ParGridFunction & density_gf,
            ParGridFunction & temperature_gf,
            ParGridFunction & nue_gf,
            ParGridFunction & nui_gf);

//static double freq_ = 1.0e9;

// Mesh Size
static Vector mesh_dim_(0); // x, y, z dimensions of mesh

// Prints the program's logo to the given output stream
void display_banner(ostream & os);

int main(int argc, char *argv[])
{
   MPI_Session mpi(argc, argv);
   if (!mpi.Root()) { mfem::out.Disable(); mfem::err.Disable(); }

   display_banner(mfem::out);

   int logging = 1;

   // Parse command-line options.
   int order = 1;
   int maxit = 1;
   int sol = 2;
   int prec = 1;
   // int nspecies = 2;
   bool herm_conv = false;
   bool vis_u = false;
   bool visualization = true;
   bool visit = true;

   double freq = 1.0e6;
   const char * wave_type = " ";

   Vector BVec(3);
   BVec = 0.0; BVec(0) = 0.1;

   bool phase_shift = false;
   Vector kVec;
   Vector kReVec;
   Vector kImVec;

   Vector numbers;
   Vector charges;
   Vector masses;
   Vector temps;
   double nue = 0;
   double nui = 0;

   PlasmaProfile::Type dpt = PlasmaProfile::CONSTANT;
   PlasmaProfile::Type tpt = PlasmaProfile::CONSTANT;
   PlasmaProfile::Type nept = PlasmaProfile::CONSTANT;
   PlasmaProfile::Type nipt = PlasmaProfile::CONSTANT;
   BFieldProfile::Type bpt = BFieldProfile::CONSTANT;
   Vector dpp;
   Vector tpp;
   Vector bpp;
   Vector nepp;
   Vector nipp;
   int nuprof = 0;

   Array<int> abcs; // Absorbing BC attributes
   Array<int> sbca; // Sheath BC attributes
   Array<int> peca; // Perfect Electric Conductor BC attributes
   Array<int> dbca1; // Dirichlet BC attributes
   Array<int> dbca2; // Dirichlet BC attributes
   Array<int> dbcaw; // Dirichlet BC attributes for plane wave source
   Array<int> nbca1; // Neumann BC attributes
   Array<int> nbca2; // Neumann BC attributes
   Array<int> nbcaw; // Neumann BC attributes for plane wave source
   Vector dbcv1; // Dirichlet BC values
   Vector dbcv2; // Dirichlet BC values
   Vector nbcv1; // Neumann BC values
   Vector nbcv2; // Neumann BC values

   int num_elements = 10;

   SolverOptions solOpts;
   solOpts.maxIter = 1000;
   solOpts.kDim = 50;
   solOpts.printLvl = 1;
   solOpts.relTol = 1e-4;
   solOpts.euLvl = 1;

   bool logo = false;
   bool check_eps_inv = false;
   bool pa = false;
   const char *device_config = "cpu";

   OptionsParser args(argc, argv);
   args.AddOption(&logo, "-logo", "--print-logo", "-no-logo",
                  "--no-print-logo", "Print logo and exit.");
   args.AddOption(&order, "-o", "--order",
                  "Finite element order (polynomial degree).");
   // args.AddOption(&nspecies, "-ns", "--num-species",
   //               "Number of ion species.");
   args.AddOption(&freq, "-f", "--frequency",
                  "Frequency in Hertz (of course...)");
   args.AddOption((int*)&dpt, "-dp", "--density-profile",
                  "Density Profile Type (for ions): \n"
                  "0 - Constant, 1 - Constant Gradient, "
                  "2 - Hyprebolic Tangent, 3 - Elliptic Cosine.");
   args.AddOption(&dpp, "-dpp", "--density-profile-params",
                  "Density Profile Parameters:\n"
                  "   CONSTANT: density value\n"
                  "   GRADIENT: value, location, gradient (7 params)\n"
                  "   TANH:     value at 0, value at 1, skin depth, "
                  "location of 0 point, unit vector along gradient, "
                  "   ELLIPTIC_COS: value at -1, value at 1, "
                  "radius in x, radius in y, location of center.");
   args.AddOption((int*)&bpt, "-bp", "--Bfield-profile",
                  "BField Profile Type: \n"
                  "0 - Constant, 1 - Constant Gradient, "
                  "2 - Hyprebolic Tangent, 3 - Elliptic Cosine.");
   args.AddOption(&bpp, "-bpp", "--Bfield-profile-params",
                  "BField Profile Parameters:\n"
                  "  B_P: value at -1, value at 1, "
                  "radius in x, radius in y, location of center, Bz, placeholder.");
   args.AddOption((int*)&tpt, "-tp", "--temperature-profile",
                  "Temperature Profile Type: \n"
                  "0 - Constant, 1 - Constant Gradient, "
                  "2 - Hyperbolic Tangent, 3 - Elliptic Cosine.");
   args.AddOption(&tpp, "-tpp", "--temperature-profile-params",
                  "Temperature Profile Parameters: \n"
                  "   CONSTANT: temperature value \n"
                  "   GRADIENT: value, location, gradient (7 params)\n"
                  "   TANH:     value at 0, value at 1, skin depth, "
                  "location of 0 point, unit vector along gradient, "
                  "   ELLIPTIC_COS: value at -1, value at 1, "
                  "radius in x, radius in y, location of center.");
   args.AddOption((int*)&nept, "-nep", "--electron-collision-profile",
                  "Electron Collisions Profile Type: \n"
                  "0 - Constant, 1 - Constant Gradient, "
                  "2 - Hyperbolic Tangent, 3 - Elliptic Cosine.");
   args.AddOption(&nepp, "-nepp", "--electron-collisions-profile-params",
                  "Electron Collisions Profile Parameters: \n"
                  "   CONSTANT: temperature value \n"
                  "   GRADIENT: value, location, gradient (7 params)\n"
                  "   TANH:     value at 0, value at 1, skin depth, "
                  "location of 0 point, unit vector along gradient, "
                  "   ELLIPTIC_COS: value at -1, value at 1, "
                  "radius in x, radius in y, location of center.");
   args.AddOption((int*)&nipt, "-nip", "--ion-collision-profile",
                  "Ion Collisions Profile Type: \n"
                  "0 - Constant, 1 - Constant Gradient, "
                  "2 - Hyperbolic Tangent, 3 - Elliptic Cosine.");
   args.AddOption(&nipp, "-nipp", "--ion-collisions-profile-params",
                  "Ion Collisions Profile Parameters: \n"
                  "   CONSTANT: temperature value \n"
                  "   GRADIENT: value, location, gradient (7 params)\n"
                  "   TANH:     value at 0, value at 1, skin depth, "
                  "location of 0 point, unit vector along gradient, "
                  "   ELLIPTIC_COS: value at -1, value at 1, "
                  "radius in x, radius in y, location of center.");
   args.AddOption(&nuprof, "-nuprof", "--collisional-profile",
                  "Temperature Profile Type: \n"
                  "0 - Standard e-i Collision Freq, 1 - Custom Freq.");
   args.AddOption(&wave_type, "-w", "--wave-type",
                  "Wave type: 'R' - Right Circularly Polarized, "
                  "'L' - Left Circularly Polarized, "
                  "'O' - Ordinary, 'X' - Extraordinary, "
                  "'J' - Current Slab (in conjunction with -slab), "
                  "'Z' - Zero");
   args.AddOption(&BVec, "-B", "--magnetic-flux",
                  "Background magnetic flux vector");
   args.AddOption(&kVec, "-k-vec", "--phase-vector",
                  "Phase shift vector across periodic directions."
                  " For complex phase shifts input 3 real phase shifts "
                  "followed by 3 imaginary phase shifts");
   // args.AddOption(&numbers, "-num", "--number-densites",
   //               "Number densities of the various species");
   args.AddOption(&charges, "-q", "--charges",
                  "Charges of the various species "
                  "(in units of electron charge)");
   args.AddOption(&masses, "-m", "--masses",
                  "Masses of the various species (in amu)");
   args.AddOption(&prec, "-pc", "--precond",
                  "Preconditioner: 1 - Diagonal Scaling, 2 - ParaSails, "
                  "3 - Euclid, 4 - AMS");
   args.AddOption(&sol, "-s", "--solver",
                  "Solver: 1 - GMRES, 2 - FGMRES, 3 - MINRES"
#ifdef MFEM_USE_SUPERLU
                  ", 4 - SuperLU"
#endif
#ifdef MFEM_USE_STRUMPACK
                  ", 5 - STRUMPACK"
#endif
                 );
   args.AddOption(&solOpts.maxIter, "-sol-it", "--solver-iterations",
                  "Maximum number of solver iterations.");
   args.AddOption(&solOpts.kDim, "-sol-k-dim", "--solver-krylov-dimension",
                  "Krylov space dimension for GMRES and FGMRES.");
   args.AddOption(&solOpts.relTol, "-sol-tol", "--solver-tolerance",
                  "Relative tolerance for GMRES or FGMRES.");
   args.AddOption(&solOpts.printLvl, "-sol-prnt-lvl", "--solver-print-level",
                  "Logging level for solvers.");
   args.AddOption(&solOpts.euLvl, "-eu-lvl", "--euclid-level",
                  "Euclid factorization level for ILU(k).");
   args.AddOption(&pw_eta_, "-pwz", "--piecewise-eta",
                  "Piecewise values of Impedance (one value per abc surface)");
   args.AddOption(&pw_eta_re_, "-pwz-r", "--piecewise-eta-r",
                  "Piecewise values of Real part of Complex Impedance "
                  "(one value per abc surface)");
   args.AddOption(&pw_eta_im_, "-pwz-i", "--piecewise-eta-i",
                  "Piecewise values of Imaginary part of Complex Impedance "
                  "(one value per abc surface)");
   args.AddOption(&slab_params_, "-slab", "--slab_params",
                  "3D Vector Amplitude (Real x,y,z, Imag x,y,z), "
                  "1D Position, 1D Size");
   args.AddOption(&abcs, "-abcs", "--absorbing-bc-surf",
                  "Absorbing Boundary Condition Surfaces");
   args.AddOption(&sbca, "-sbcs", "--sheath-bc-surf",
                  "Sheath Boundary Condition Surfaces");
   args.AddOption(&peca, "-pecs", "--pec-bc-surf",
                  "Perfect Electrical Conductor Boundary Condition Surfaces");
   args.AddOption(&dbcaw, "-dbcs-pw", "--dirichlet-bc-pw-surf",
                  "Dirichlet Boundary Condition Surfaces Using Plane Wave");
   args.AddOption(&dbca1, "-dbcs1", "--dirichlet-bc-1-surf",
                  "Dirichlet Boundary Condition Surfaces Using Value 1");
   args.AddOption(&dbca2, "-dbcs2", "--dirichlet-bc-2-surf",
                  "Dirichlet Boundary Condition Surfaces Using Value 2");
   args.AddOption(&dbcv1, "-dbcv1", "--dirichlet-bc-1-vals",
                  "Dirichlet Boundary Condition Value 1 (v_x v_y v_z)"
                  " or (Re(v_x) Re(v_y) Re(v_z) Im(v_x) Im(v_y) Im(v_z))");
   args.AddOption(&dbcv2, "-dbcv2", "--dirichlet-bc-2-vals",
                  "Dirichlet Boundary Condition Value 2 (v_x v_y v_z)"
                  " or (Re(v_x) Re(v_y) Re(v_z) Im(v_x) Im(v_y) Im(v_z))");
   args.AddOption(&nbca1, "-nbcs1", "--neumann-bc-1-surf",
                  "Neumann Boundary Condition Surfaces Using Value 1");
   args.AddOption(&nbca2, "-nbcs2", "--neumann-bc-2-surf",
                  "Neumann Boundary Condition Surfaces Using Value 2");
   args.AddOption(&nbcaw, "-nbcs-pw", "--neumann-bc-pw-surf",
                  "Neumann Boundary Condition Surfaces Using Plane Wave");
   args.AddOption(&nbcv1, "-nbcv1", "--neumann-bc-1-vals",
                  "Neuamnn Boundary Condition (surface current) "
                  "Value 1 (v_x v_y v_z) or "
                  "(Re(v_x) Re(v_y) Re(v_z) Im(v_x) Im(v_y) Im(v_z))");
   args.AddOption(&nbcv2, "-nbcv2", "--neumann-bc-2-vals",
                  "Neumann Boundary Condition (surface current) "
                  "Value 2 (v_x v_y v_z) or "
                  "(Re(v_x) Re(v_y) Re(v_z) Im(v_x) Im(v_y) Im(v_z))");
   args.AddOption(&mesh_dim_, "-md", "--mesh_dimensions",
                  "The x, y, z mesh dimensions");
   args.AddOption(&num_elements, "-ne", "--num-elements",
                  "The number of mesh elements in x");
   args.AddOption(&maxit, "-maxit", "--max-amr-iterations",
                  "Max number of iterations in the main AMR loop.");
   args.AddOption(&herm_conv, "-herm", "--hermitian", "-no-herm",
                  "--no-hermitian", "Use convention for Hermitian operators.");
   args.AddOption(&vis_u, "-vis-u", "--visualize-energy", "-no-vis-u",
                  "--no-visualize-energy",
                  "Enable or disable visualization of energy density.");
   args.AddOption(&visualization, "-vis", "--visualization", "-no-vis",
                  "--no-visualization",
                  "Enable or disable GLVis visualization.");
   args.AddOption(&visit, "-visit", "--visit", "-no-visit", "--no-visit",
                  "Enable or disable VisIt visualization.");
   args.AddOption(&pa, "-pa", "--partial-assembly", "-no-pa",
                  "--no-partial-assembly", "Enable Partial Assembly.");
   args.AddOption(&device_config, "-d", "--device",
                  "Device configuration string, see Device::Configure().");
   args.Parse();
   if (!args.Good())
   {
      if (mpi.Root())
      {
         args.PrintUsage(cout);
      }
      return 1;
   }
   if (logo)
   {
      return 1;
   }
   Device device(device_config);
   if (mpi.Root())
   {
      device.Print();
   }
   if (numbers.Size() == 0)
   {
      numbers.SetSize(2);
      if (dpp.Size() == 0)
      {
         numbers[0] = 1.0e19;
         numbers[1] = 1.0e19;
      }
      else
      {
         switch (dpt)
         {
            case PlasmaProfile::CONSTANT:
               numbers[0] = dpp[0];
               numbers[1] = dpp[0];
               break;
            case PlasmaProfile::GRADIENT:
               numbers[0] = dpp[0];
               numbers[1] = dpp[0];
               break;
            case PlasmaProfile::TANH:
               numbers[0] = dpp[1];
               numbers[1] = dpp[1];
               break;
            case PlasmaProfile::ELLIPTIC_COS:
               numbers[0] = dpp[1];
               numbers[1] = dpp[1];
               break;
            default:
               numbers[0] = 1.0e19;
               numbers[1] = 1.0e19;
               break;
         }
      }
   }
   if (dpp.Size() == 0)
   {
      dpp.SetSize(1);
      dpp[0] = 1.0e19;
   }
   if (nepp.Size() == 0)
   {
      nepp.SetSize(1);
      nepp[0] = 0;
   }
   if (nipp.Size() == 0)
   {
      nipp.SetSize(1);
      nipp[0] = 0;
   }
   if (bpp.Size() == 0)
   {
      bpt = BFieldProfile::CONSTANT;
      bpp.SetSize(3);
      bpp[0] = 0.0; bpp[1] = 0.1; bpp[2] = 0.0;
   }
   if (bpt == BFieldProfile::CONSTANT)
   {
      BVec = bpp;
   }
   if (charges.Size() == 0)
   {
      charges.SetSize(2);
      charges[0] = -1.0;
      charges[1] =  1.0;
   }
   if (masses.Size() == 0)
   {
      masses.SetSize(2);
      masses[0] = me_u_;
      masses[1] = 2.01410178;
   }
   if (temps.Size() == 0)
   {
      temps.SetSize(2);
      if (tpp.Size() == 0)
      {
         tpp.SetSize(1);
         tpp[0] = 1.0e3;
         temps[0] = tpp[0];
         temps[1] = tpp[0];
      }
      else
      {
         switch (tpt)
         {
            case PlasmaProfile::CONSTANT:
               temps[0] = tpp[0];
               temps[1] = tpp[0];
               break;
            case PlasmaProfile::GRADIENT:
               temps[0] = tpp[0];
               temps[1] = tpp[0];
               break;
            case PlasmaProfile::TANH:
               temps[0] = tpp[1];
               temps[1] = tpp[1];
               break;
            case PlasmaProfile::ELLIPTIC_COS:
               temps[0] = tpp[1];
               temps[1] = tpp[1];
               break;
            default:
               temps[0] = 1.0e3;
               temps[1] = 1.0e3;
               break;
         }
      }
   }
   if (num_elements <= 0)
   {
      num_elements = 10;
   }
   if (mesh_dim_.Size() == 0)
   {
      mesh_dim_.SetSize(3);
      mesh_dim_ = 0.0;
   }
   else if (mesh_dim_.Size() < 3)
   {
      double d0 = mesh_dim_[0];
      double d1 = (mesh_dim_.Size() == 2) ? mesh_dim_[1] : 0.1 * d0;
      mesh_dim_.SetSize(3);
      mesh_dim_[0] = d0;
      mesh_dim_[1] = d1;
      mesh_dim_[2] = d1;
   }
   if (mesh_dim_[0] == 0.0)
   {
      mesh_dim_[0] = 1.0;
      mesh_dim_[1] = 0.1;
      mesh_dim_[2] = 0.1;
   }
   double omega = 2.0 * M_PI * freq;
   if (kVec.Size() != 0)
   {
      phase_shift = true;
   }

   if (mpi.Root())
   {
      args.PrintOptions(cout);
   }

   ComplexOperator::Convention conv =
      herm_conv ? ComplexOperator::HERMITIAN : ComplexOperator::BLOCK_SYMMETRIC;

   if (mpi.Root())
   {
      double lam0 = c0_ / freq;
      double Bmag = BVec.Norml2();
      std::complex<double> S = S_cold_plasma(omega, Bmag, nue, nui, numbers,
                                             charges, masses, temps, nuprof);
      std::complex<double> P = P_cold_plasma(omega, nue, numbers,
                                             charges, masses, temps, nuprof);
      std::complex<double> D = D_cold_plasma(omega, Bmag, nue, nui, numbers,
                                             charges, masses, temps, nuprof);
      std::complex<double> R = R_cold_plasma(omega, Bmag, nue, nui, numbers,
                                             charges, masses, temps, nuprof);
      std::complex<double> L = L_cold_plasma(omega, Bmag, nue, nui, numbers,
                                             charges, masses, temps, nuprof);

      cout << "\nConvenient Terms:\n";
      cout << "R = " << R << ",\tL = " << L << endl;
      cout << "S = " << S << ",\tD = " << D << ",\tP = " << P << endl;

      cout << "\nSpecies Properties (number, charge, mass):\n";
      for (int i=0; i<numbers.Size(); i++)
      {
         cout << numbers[i] << '\t' << charges[i] << '\t' << masses[i] << '\n';
      }
      cout << "\nPlasma and Cyclotron Frequencies by Species (GHz):\n";
      for (int i=0; i<numbers.Size(); i++)
      {
         cout << omega_p(numbers[i], charges[i], masses[i]) / (2.0e9 * M_PI)
              << '\t'
              << omega_c(Bmag, charges[i], masses[i]) / (2.0e9 * M_PI) << '\n';
      }

      cout << "\nWavelengths (meters):\n";
      cout << "   Free Space Wavelength: " << lam0 << '\n';
      complex<double> lamL = lam0 / sqrt(S-D);
      complex<double> lamR = lam0 / sqrt(S+D);
      complex<double> lamO = lam0 / sqrt(P);
      complex<double> lamX = lam0 * sqrt(S/(S*S-D*D));
      if (fabs(lamL.real()) > fabs(lamL.imag()))
      {
         cout << "   Oscillating L mode:    " << lamL << '\n';
      }
      else
      {
         cout << "   Decaying L mode:       " << lamL << '\n';
      }
      if (fabs(lamR.real()) > fabs(lamR.imag()))
      {
         cout << "   Oscillating R mode:    " << lamR << '\n';
      }
      else
      {
         cout << "   Decaying R mode:       " << lamR << '\n';
      }
      if (fabs(lamO.real()) > fabs(lamO.imag()))
      {
         cout << "   Oscillating O mode:    " << lamO << '\n';
      }
      else
      {
         cout << "   Decaying O mode:       " << lamO << '\n';
      }
      if (fabs(lamX.real()) > fabs(lamX.imag()))
      {
         cout << "   Oscillating X mode:    " << lamX << '\n';
      }
      else
      {
         cout << "   Decaying X mode:       " << lamX << '\n';
      }
      cout << endl;
   }

   // Read the (serial) mesh from the given mesh file on all processors.  We
   // can handle triangular, quadrilateral, tetrahedral, hexahedral, surface
   // and volume meshes with the same code.
   if ( mpi.Root() && logging > 0 ) { cout << "Building Mesh ..." << endl; }

   tic_toc.Clear();
   tic_toc.Start();

   Mesh * mesh = new Mesh;
   *mesh = Mesh::MakeCartesian3D(num_elements, 3, 3, Element::HEXAHEDRON,
                                 mesh_dim_(0), mesh_dim_(1), mesh_dim_(2));
   {
      Array<int> v2v(mesh->GetNV());
      for (int i=0; i<v2v.Size(); i++) { v2v[i] = i; }
      for (int i=0; i<=num_elements; i++)
      {
         v2v[ 3 * num_elements +  3 + i] = i; // y = sy, z = 0
         v2v[12 * num_elements + 12 + i] = i; // y =  0, z = sz
         v2v[15 * num_elements + 15 + i] = i; // y = sy, z = sz
      }
      for (int j=1; j<3; j++)
      {
         for (int i=0; i<=num_elements; i++)
         {
            v2v[(j + 12) * (num_elements +  1) + i] =
               j * (num_elements +  1) + i;
         }
      }
      for (int k=1; k<3; k++)
      {
         for (int i=0; i<=num_elements; i++)
         {
            v2v[(4 * k + 3) * (num_elements +  1) + i] =
               4 * k * (num_elements +  1) + i;
         }
      }

      Mesh * per_mesh = MakePeriodicMesh(mesh, v2v);
      delete mesh;
      mesh = per_mesh;
   }

   // Ensure that quad and hex meshes are treated as non-conforming.
   mesh->EnsureNCMesh();

   // Define a parallel mesh by a partitioning of the serial mesh. Refine
   // this mesh further in parallel to increase the resolution. Once the
   // parallel mesh is defined, the serial mesh can be deleted.
   if ( mpi.Root() && logging > 0 )
   { cout << "Building Parallel Mesh ..." << endl; }
   ParMesh pmesh(MPI_COMM_WORLD, *mesh);
   delete mesh;

   if (mpi.Root())
   {
      cout << "Starting initialization." << endl;
   }

   // If values for Voltage BCs were not set issue a warning and exit
   /*
   if ( ( vbcs.Size() > 0 && kbcs.Size() == 0 ) ||
        ( kbcs.Size() > 0 && vbcs.Size() == 0 ) ||
        ( vbcv.Size() < vbcs.Size() ) )
   {
      if ( mpi.Root() )
      {
         cout << "The surface current (K) boundary condition requires "
              << "surface current boundary condition surfaces (with -kbcs), "
              << "voltage boundary condition surface (with -vbcs), "
              << "and voltage boundary condition values (with -vbcv)."
              << endl;
      }
      return 3;
   }
   */
   /*
   double Bmag = BVec.Norml2();
   Vector BUnitVec(3);
   BUnitVec(0) = BVec(0)/Bmag;
   BUnitVec(1) = BVec(1)/Bmag;
   BUnitVec(2) = BVec(2)/Bmag;

   VectorConstantCoefficient BCoef(BVec);
   VectorConstantCoefficient BUnitCoef(BUnitVec);
   */
   // VectorConstantCoefficient kCoef(kVec);
   /*
   double ion_frac = 0.0;
   ConstantCoefficient rhoCoef1(rho1);
   ConstantCoefficient rhoCoef2(rhoCoef1.constant * (1.0 - ion_frac));
   ConstantCoefficient rhoCoef3(rhoCoef1.constant * ion_frac);
   ConstantCoefficient tempCoef(10.0 * q_);
   */
   H1_ParFESpace H1FESpace(&pmesh, order, pmesh.Dimension());
   ND_ParFESpace HCurlFESpace(&pmesh, order, pmesh.Dimension());
   RT_ParFESpace HDivFESpace(&pmesh, order, pmesh.Dimension());
   L2_ParFESpace L2FESpace(&pmesh, order, pmesh.Dimension());

   ParGridFunction BField(&HDivFESpace);

   ParGridFunction temperature_gf;
   ParGridFunction density_gf;
   ParGridFunction nue_gf(&H1FESpace);
   ParGridFunction nui_gf(&H1FESpace);

   PlasmaProfile nueCoef(nept, nepp);
   nue_gf.ProjectCoefficient(nueCoef);
   PlasmaProfile nuiCoef(nipt, nipp);
   nui_gf.ProjectCoefficient(nuiCoef);

   BFieldProfile BCoef(bpt, bpp, false);
   BFieldProfile BUnitCoef(bpt, bpp, true);

   BField.ProjectCoefficient(BCoef);

   int size_h1 = H1FESpace.GetVSize();
   int size_l2 = L2FESpace.GetVSize();

   Array<int> density_offsets(numbers.Size() + 1);
   Array<int> temperature_offsets(numbers.Size() + 2);

   density_offsets[0] = 0;
   temperature_offsets[0] = 0;
   temperature_offsets[1] = size_h1;

   for (int i=1; i<=numbers.Size(); i++)
   {
      density_offsets[i]     = density_offsets[i - 1] + size_l2;
      temperature_offsets[i + 1] = temperature_offsets[i] + size_h1;
   }

   BlockVector density(density_offsets);
   BlockVector temperature(temperature_offsets);

   if (mpi.Root())
   {
      cout << "Creating plasma profile." << endl;
   }

   PlasmaProfile tempCoef(tpt, tpp);
   PlasmaProfile rhoCoef(dpt, dpp);

   for (int i=0; i<=numbers.Size(); i++)
   {
      temperature_gf.MakeRef(&H1FESpace, temperature.GetBlock(i));
      temperature_gf.ProjectCoefficient(tempCoef);
   }

   for (int i=0; i<charges.Size(); i++)
   {
      density_gf.MakeRef(&L2FESpace, density.GetBlock(i));
      density_gf.ProjectCoefficient(rhoCoef);
   }
   /*
   for (int i=0; i<=nspecies; i++)
   {
      temperature_gf.MakeRef(&H1FESpace, temperature.GetBlock(i));
      temperature_gf.ProjectCoefficient(tempCoef);
   }
   */
   /*
   density_gf.MakeRef(&L2FESpace, density.GetBlock(0));
   density_gf.ProjectCoefficient(rhoCoef1);

   density_gf.MakeRef(&L2FESpace, density.GetBlock(1));
   density_gf.ProjectCoefficient(rhoCoef2);

   density_gf.MakeRef(&L2FESpace, density.GetBlock(2));
   density_gf.ProjectCoefficient(rhoCoef3);
   */

   if (mpi.Root())
   {
      cout << "Creating coefficients for Maxwell equations." << endl;
   }

   // Create a coefficient describing the magnetic permeability
   ConstantCoefficient muCoef(mu0_);

   // Create a coefficient describing the surface admittance
   Coefficient * etaInvCoef = SetupRealAdmittanceCoefficient(pmesh, abcs);

   // Create tensor coefficients describing the dielectric permittivity
   InverseDielectricTensor epsilonInv_real(BField, nue_gf, nui_gf,
                                           density, temperature,
                                           L2FESpace, H1FESpace,
                                           omega, charges, masses, nuprof,
                                           true);
   InverseDielectricTensor epsilonInv_imag(BField, nue_gf, nui_gf,
                                           density, temperature,
                                           L2FESpace, H1FESpace,
                                           omega, charges, masses, nuprof,
                                           false);
   SPDDielectricTensor epsilon_abs(BField, nue_gf, nui_gf,
                                   density, temperature,
                                   L2FESpace, H1FESpace,
                                   omega, charges, masses, nuprof);
   SheathImpedance z_r(BField, density, temperature,
                       L2FESpace, H1FESpace,
                       omega, charges, masses, true);
   SheathImpedance z_i(BField, density, temperature,
                       L2FESpace, H1FESpace,
                       omega, charges, masses, false);

   ColdPlasmaPlaneWaveH HReCoef(wave_type[0], omega, BVec,
                                numbers, charges, masses, temps, nuprof, true);
   ColdPlasmaPlaneWaveH HImCoef(wave_type[0], omega, BVec,
                                numbers, charges, masses, temps, nuprof, false);

   ColdPlasmaPlaneWaveE EReCoef(wave_type[0], omega, BVec,
                                numbers, charges, masses, temps, nuprof, true);
   ColdPlasmaPlaneWaveE EImCoef(wave_type[0], omega, BVec,
                                numbers, charges, masses, temps, nuprof, false);

   if (check_eps_inv)
   {
      DielectricTensor epsilon_real(BField, nue_gf, nui_gf,
                                    density, temperature,
                                    L2FESpace, H1FESpace,
                                    omega, charges, masses, nuprof, true);
      DielectricTensor epsilon_imag(BField, nue_gf, nui_gf,
                                    density, temperature,
                                    L2FESpace, H1FESpace,
                                    omega, charges, masses, nuprof, false);
      DenseMatrix epsInvRe(3,3);
      DenseMatrix epsInvIm(3,3);
      DenseMatrix epsRe(3,3);
      DenseMatrix epsIm(3,3);

      DenseMatrix IRe(3,3);
      DenseMatrix IIm(3,3);

      for (int i=0; i<pmesh.GetNE(); i++)
      {
         ElementTransformation *T = pmesh.GetElementTransformation(i);
         Geometry::Type g = pmesh.GetElementBaseGeometry(i);
         const IntegrationPoint &ip = Geometries.GetCenter(g);

         epsilonInv_real.Eval(epsInvRe, *T, ip);
         epsilonInv_imag.Eval(epsInvIm, *T, ip);

         epsilon_real.Eval(epsRe, *T, ip);
         epsilon_imag.Eval(epsIm, *T, ip);

         Mult(epsInvRe, epsRe, IRe);
         AddMult_a(-1.0, epsInvIm, epsIm, IRe);

         Mult(epsInvRe, epsIm, IIm);
         AddMult(epsInvIm, epsRe, IIm);

         IRe(0,0) -= 1.0;
         IRe(1,1) -= 1.0;
         IRe(2,2) -= 1.0;

         double nrmRe = IRe.MaxMaxNorm();
         double nrmIm = IIm.MaxMaxNorm();

         if (nrmRe + nrmIm > 1e-13)
         {
            cout << "element " << i << " on processor "
                 << mpi.WorldRank() << endl;
            IRe.Print(cout);
            IIm.Print(cout);
            cout << endl;
         }
      }
   }

   if (wave_type[0] != ' ')
   {
      Vector kr(3), ki(3);
      HReCoef.GetWaveVector(kr, ki);

      mfem::out << "Plane wave propagation vector: ("
                << complex<double>(kr(0),ki(0)) << ","
                << complex<double>(kr(1),ki(1)) << ","
                << complex<double>(kr(2),ki(2)) << ")" << endl;

      if (!phase_shift)
      {
         kVec.SetSize(6);
         kVec = 0.0;

         kVec[1] = kr[1];
         kVec[4] = ki[1];

         kVec[2] = kr[2];
         kVec[5] = ki[2];

         phase_shift = true;
      }

      kReVec.SetDataAndSize(&kVec[0], 3);
      kImVec.SetDataAndSize(&kVec[3], 3);

      mfem::out << "Setting phase shift of ("
                << complex<double>(kReVec[0],kImVec[0]) << ","
                << complex<double>(kReVec[1],kImVec[1])   << ","
                << complex<double>(kReVec[2],kImVec[2]) << ")" << endl;

      HReCoef.SetPhaseShift(kReVec, kImVec);
      HImCoef.SetPhaseShift(kReVec, kImVec);
      EReCoef.SetPhaseShift(kReVec, kImVec);
      EImCoef.SetPhaseShift(kReVec, kImVec);
   }
   else
   {
      if (phase_shift)
      {
         if (kVec.Size() >= 3)
         {
            kReVec.SetDataAndSize(&kVec[0], 3);
         }
         else
         {
            kReVec.SetSize(3);
            kReVec = 0.0;
         }
         if (kVec.Size() >= 6)
         {
            kImVec.SetDataAndSize(&kVec[3], 3);
         }
         else
         {
            kImVec.SetSize(3);
            kImVec = 0.0;
         }
      }
   }

   VectorConstantCoefficient kReCoef(kReVec);
   VectorConstantCoefficient kImCoef(kImVec);

   if (visualization && wave_type[0] != ' ')
   {
      if (mpi.Root())
      {
         cout << "Visualize input fields." << endl;
      }
      ParComplexGridFunction HField(&HCurlFESpace);
      HField.ProjectCoefficient(HReCoef, HImCoef);
      ParComplexGridFunction EField(&HCurlFESpace);
      EField.ProjectCoefficient(EReCoef, EImCoef);

      Vector zeroVec(3); zeroVec = 0.0;
      VectorConstantCoefficient zeroCoef(zeroVec);
      double max_Hr = HField.real().ComputeMaxError(zeroCoef);
      double max_Hi = HField.imag().ComputeMaxError(zeroCoef);
      double max_Er = EField.real().ComputeMaxError(zeroCoef);
      double max_Ei = EField.imag().ComputeMaxError(zeroCoef);
      /*
      ParComplexGridFunction ZCoef(&H1FESpace);
      // Array<int> ess_bdr(mesh->bdr_attributes.Size());
      // ess_bdr = 1;
      // ZCoef.ProjectBdrCoefficient(z_r, z_i, ess_bdr);
      ZCoef.ProjectCoefficient(z_r, z_i);
       */

      char vishost[] = "localhost";
      int  visport   = 19916;

      int Wx = 0, Wy = 0; // window position
      int Ww = 350, Wh = 350; // window size
      int offx = Ww+10, offy = Wh+45; // window offsets

      socketstream sock_Hr, sock_Hi, sock_Er, sock_Ei, /*sock_zr, sock_zi, */ sock_B;
      sock_Hr.precision(8);
      sock_Hi.precision(8);
      sock_Er.precision(8);
      sock_Ei.precision(8);
      sock_B.precision(8);
      // sock_zr.precision(8);
      // sock_zi.precision(8);

      ostringstream hr_keys, hi_keys;
      hr_keys << "aaAcPPPPvvv valuerange 0.0 " << max_Hr;
      hi_keys << "aaAcPPPPvvv valuerange 0.0 " << max_Hi;

      ostringstream er_keys, ei_keys;
      er_keys << "aaAcpppppvvv valuerange 0.0 " << max_Er;
      ei_keys << "aaAcpppppvvv valuerange 0.0 " << max_Ei;

      Wy += offy;
      VisualizeField(sock_Hr, vishost, visport,
                     HField.real(), "Exact Magnetic Field, Re(H)",
                     Wx, Wy, Ww, Wh, hr_keys.str().c_str());

      Wx += offx;
      VisualizeField(sock_Hi, vishost, visport,
                     HField.imag(), "Exact Magnetic Field, Im(H)",
                     Wx, Wy, Ww, Wh, hi_keys.str().c_str());

      Wx += offx;
      VisualizeField(sock_Er, vishost, visport,
                     EField.real(), "Exact Electric Field, Re(E)",
                     Wx, Wy, Ww, Wh, er_keys.str().c_str());
      Wx += offx;
      VisualizeField(sock_Ei, vishost, visport,
                     EField.imag(), "Exact Electric Field, Im(E)",
                     Wx, Wy, Ww, Wh, ei_keys.str().c_str());

      // Wx -= offx;
      // Wy += offy;

      /*
      VisualizeField(sock_B, vishost, visport,
                    BField, "Background Magnetic Field",
                    Wx, Wy, Ww, Wh);


      VisualizeField(sock_zr, vishost, visport,
                    ZCoef.real(), "Real Sheath Impedance",
                    Wx, Wy, Ww, Wh);

      VisualizeField(sock_zi, vishost, visport,
                    ZCoef.imag(), "Imaginary Sheath Impedance",
                    Wx, Wy, Ww, Wh);
      */
      /*
      for (int i=0; i<charges.Size(); i++)
      {
         Wx += offx;

         socketstream sock;
         sock.precision(8);

         stringstream oss;
         oss << "Density Species " << i;
         density_gf.MakeRef(&L2FESpace, density.GetBlock(i));
         VisualizeField(sock, vishost, visport,
                        density_gf, oss.str().c_str(),
                        Wx, Wy, Ww, Wh);
      }


        socketstream sock;
        sock.precision(8);

        temperature_gf.MakeRef(&H1FESpace, temperature.GetBlock(0));
        VisualizeField(sock, vishost, visport,
                         temperature_gf, "Temp",
                         Wx, Wy, Ww, Wh);
       */
   }

   if (mpi.Root())
   {
      cout << "Setup boundary conditions." << endl;
   }

   // Setup coefficients for Dirichlet BC
   /*
   Array<ComplexVectorCoefficientByAttr> dbcs(1);
   dbcs[0].attr = dbca;
   dbcs[0].real = &EReCoef;
   dbcs[0].imag = &EImCoef;
   */

   int dbcsSize = (peca.Size() > 0) + (dbca1.Size() > 0) + (dbca2.Size() > 0) +
                  (dbcaw.Size() > 0);

   Array<ComplexVectorCoefficientByAttr*> dbcs(dbcsSize);

   Vector zeroVec(3); zeroVec = 0.0;
   Vector dbc1ReVec;
   Vector dbc1ImVec;
   Vector dbc2ReVec;
   Vector dbc2ImVec;

   if (dbcv1.Size() >= 3)
   {
      dbc1ReVec.SetDataAndSize(&dbcv1[0], 3);
   }
   else
   {
      dbc1ReVec.SetDataAndSize(&zeroVec[0], 3);
   }
   if (dbcv1.Size() >= 6)
   {
      dbc1ImVec.SetDataAndSize(&dbcv1[3], 3);
   }
   else
   {
      dbc1ImVec.SetDataAndSize(&zeroVec[0], 3);
   }
   if (dbcv2.Size() >= 3)
   {
      dbc2ReVec.SetDataAndSize(&dbcv2[0], 3);
   }
   else
   {
      dbc2ReVec.SetDataAndSize(&zeroVec[0], 3);
   }
   if (dbcv2.Size() >= 6)
   {
      dbc2ImVec.SetDataAndSize(&dbcv2[3], 3);
   }
   else
   {
      dbc2ImVec.SetDataAndSize(&zeroVec[0], 3);
   }

   VectorConstantCoefficient zeroCoef(zeroVec);
   VectorConstantCoefficient dbc1ReCoef(dbc1ReVec);
   VectorConstantCoefficient dbc1ImCoef(dbc1ImVec);
   VectorConstantCoefficient dbc2ReCoef(dbc2ReVec);
   VectorConstantCoefficient dbc2ImCoef(dbc2ImVec);

   if (dbcsSize > 0)
   {
      int c = 0;
      if (peca.Size() > 0)
      {
         dbcs[c] = new ComplexVectorCoefficientByAttr;
         dbcs[c]->attr = peca;
         dbcs[c]->real = &zeroCoef;
         dbcs[c]->imag = &zeroCoef;
         c++;
      }
      if (dbca1.Size() > 0)
      {
         dbcs[c] = new ComplexVectorCoefficientByAttr;
         dbcs[c]->attr = dbca1;
         dbcs[c]->real = &dbc1ReCoef;
         dbcs[c]->imag = &dbc1ImCoef;
         c++;
      }
      if (dbca2.Size() > 0)
      {
         dbcs[c] = new ComplexVectorCoefficientByAttr;
         dbcs[c]->attr = dbca2;
         dbcs[c]->real = &dbc2ReCoef;
         dbcs[c]->imag = &dbc2ImCoef;
         c++;
      }
      if (dbcaw.Size() > 0)
      {
         dbcs[c] = new ComplexVectorCoefficientByAttr;
         dbcs[c]->attr = dbcaw;
         dbcs[c]->real = &HReCoef;
         dbcs[c]->imag = &HImCoef;
         c++;
      }
   }

   int nbcsSize = (nbca1.Size() > 0) + (nbca2.Size() > 0) + (nbcaw.Size() > 0);

   Array<ComplexVectorCoefficientByAttr*> nbcs(nbcsSize);

   Vector nbc1ReVec;
   Vector nbc1ImVec;
   Vector nbc2ReVec;
   Vector nbc2ImVec;

   if (nbcv1.Size() >= 3)
   {
      nbc1ReVec.SetDataAndSize(&nbcv1[0], 3);
   }
   else
   {
      nbc1ReVec.SetDataAndSize(&zeroVec[0], 3);
   }
   if (nbcv1.Size() >= 6)
   {
      nbc1ImVec.SetDataAndSize(&nbcv1[3], 3);
   }
   else
   {
      nbc1ImVec.SetDataAndSize(&zeroVec[0], 3);
   }
   if (nbcv2.Size() >= 3)
   {
      nbc2ReVec.SetDataAndSize(&nbcv2[0], 3);
   }
   else
   {
      nbc2ReVec.SetDataAndSize(&zeroVec[0], 3);
   }
   if (nbcv2.Size() >= 6)
   {
      nbc2ImVec.SetDataAndSize(&nbcv2[3], 3);
   }
   else
   {
      nbc2ImVec.SetDataAndSize(&zeroVec[0], 3);
   }

   VectorConstantCoefficient nbc1ReCoef(nbc1ReVec);
   VectorConstantCoefficient nbc1ImCoef(nbc1ImVec);
   VectorConstantCoefficient nbc2ReCoef(nbc2ReVec);
   VectorConstantCoefficient nbc2ImCoef(nbc2ImVec);

   if (nbcsSize > 0)
   {
      int c = 0;
      if (nbca1.Size() > 0)
      {
         nbcs[c] = new ComplexVectorCoefficientByAttr;
         nbcs[c]->attr = nbca1;
         nbcs[c]->real = &nbc1ReCoef;
         nbcs[c]->imag = &nbc1ImCoef;
         c++;
      }
      if (nbca2.Size() > 0)
      {
         nbcs[c] = new ComplexVectorCoefficientByAttr;
         nbcs[c]->attr = nbca2;
         nbcs[c]->real = &nbc2ReCoef;
         nbcs[c]->imag = &nbc2ImCoef;
         c++;
      }
      if (nbcaw.Size() > 0)
      {
         nbcs[c] = new ComplexVectorCoefficientByAttr;
         nbcs[c]->attr = nbcaw;
         nbcs[c]->real = &EReCoef;
         nbcs[c]->imag = &EImCoef;
         c++;
      }
   }

   Array<ComplexCoefficientByAttr*> sbcs((sbca.Size() > 0)? 1 : 0);
   if (sbca.Size() > 0)
   {
      sbcs[0] = new ComplexCoefficientByAttr;
      sbcs[0]->real = &z_r;
      sbcs[0]->imag = &z_i;
      sbcs[0]->attr = sbca;
      AttrToMarker(pmesh.bdr_attributes.Max(), sbcs[0]->attr,
                   sbcs[0]->attr_marker);
   }

   if (mpi.Root())
   {
      cout << "Creating Cold Plasma Dielectric solver." << endl;
   }

   // Create the cold plasma EM solver
   CPDSolverDH CPD(pmesh, order, omega,
                   (CPDSolverDH::SolverType)sol, solOpts,
                   (CPDSolverDH::PrecondType)prec,
                   conv, BUnitCoef,
                   epsilonInv_real, epsilonInv_imag, epsilon_abs,
                   muCoef, etaInvCoef,
                   (phase_shift) ? &kReCoef : NULL,
                   (phase_shift) ? &kImCoef : NULL,
                   abcs,
                   dbcs, nbcs, sbcs,
                   // e_bc_r, e_bc_i,
                   // EReCoef, EImCoef,
                   (slab_params_.Size() > 0) ?
                   j_src_r : NULL,
                   (slab_params_.Size() == 8) ?
                   j_src_i : NULL, vis_u, pa);

   // Initialize GLVis visualization
   if (visualization)
   {
      CPD.InitializeGLVis();
   }

   // Initialize VisIt visualization
   VisItDataCollection visit_dc("STIX1D-DH-AMR-Parallel", &pmesh);

   Array<ParComplexGridFunction*> auxFields;

   if ( visit )
   {
      CPD.RegisterVisItFields(visit_dc);

      auxFields.SetSize(2);
      auxFields[0] = new ParComplexGridFunction(&HCurlFESpace);
      auxFields[1] = new ParComplexGridFunction(&HCurlFESpace);

      auxFields[0]->ProjectCoefficient(HReCoef, HImCoef);
      auxFields[1]->ProjectCoefficient(EReCoef, EImCoef);

      visit_dc.RegisterField("Re_H_Exact", &auxFields[0]->real());
      visit_dc.RegisterField("Im_H_Exact", &auxFields[0]->imag());

      visit_dc.RegisterField("Re_E_Exact", &auxFields[1]->real());
      visit_dc.RegisterField("Im_E_Exact", &auxFields[1]->imag());
   }
   if (mpi.Root()) { cout << "Initialization done." << endl; }

   // The main AMR loop. In each iteration we solve the problem on the current
   // mesh, visualize the solution, estimate the error on all elements, refine
   // the worst elements and update all objects to work with the new mesh. We
   // refine until the maximum number of dofs in the Nedelec finite element
   // space reaches 10 million.
   const int max_dofs = 10000000;
   for (int it = 1; it <= maxit; it++)
   {
      if (mpi.Root())
      {
         cout << "\nAMR Iteration " << it << endl;
      }

      // Display the current number of DoFs in each finite element space
      CPD.PrintSizes();

      // Assemble all forms
      CPD.Assemble();

      // Solve the system and compute any auxiliary fields
      CPD.Solve();

      if (wave_type[0] != ' ')
      {
         // Compute error
         double glb_error_H = CPD.GetHFieldError(HReCoef, HImCoef);
         if (mpi.Root())
         {
            cout << "Global L2 Error in H field " << glb_error_H << endl;
         }

         double glb_error_E = CPD.GetEFieldError(EReCoef, EImCoef);
         if (mpi.Root())
         {
            cout << "Global L2 Error in E field " << glb_error_E << endl;
         }
      }
      /*
      // Compute error
      double glb_error = CPD.GetError(EReCoef, EImCoef);
      if (mpi.Root())
      {
         cout << "Global L2 Error " << glb_error << endl;
      }
      */

      // Determine the current size of the linear system
      int prob_size = CPD.GetProblemSize();

      // Write fields to disk for VisIt
      if ( visit )
      {
         CPD.WriteVisItFields(it);
      }

      // Send the solution by socket to a GLVis server.
      if (visualization)
      {
         CPD.DisplayToGLVis();
      }

      if (mpi.Root())
      {
         cout << "AMR iteration " << it << " complete." << endl;
      }

      // Check stopping criteria
      if (prob_size > max_dofs)
      {
         if (mpi.Root())
         {
            cout << "Reached maximum number of dofs, exiting..." << endl;
         }
         break;
      }
      if ( it == maxit )
      {
         break;
      }

      // Wait for user input. Ask every 10th iteration.
      char c = 'c';
      if (mpi.Root() && (it % 10 == 0))
      {
         cout << "press (q)uit or (c)ontinue --> " << flush;
         cin >> c;
      }
      MPI_Bcast(&c, 1, MPI_CHAR, 0, MPI_COMM_WORLD);

      if (c != 'c')
      {
         break;
      }

      // Estimate element errors using the Zienkiewicz-Zhu error estimator.
      Vector errors(pmesh.GetNE());
      CPD.GetErrorEstimates(errors);

      double local_max_err = errors.Max();
      double global_max_err;
      MPI_Allreduce(&local_max_err, &global_max_err, 1,
                    MPI_DOUBLE, MPI_MAX, pmesh.GetComm());

      // Refine the elements whose error is larger than a fraction of the
      // maximum element error.
      const double frac = 0.5;
      double threshold = frac * global_max_err;
      if (mpi.Root()) { cout << "Refining ..." << endl; }
      {
         pmesh.RefineByError(errors, threshold);
         /*
              Array<Refinement> refs;
              for (int i=0; i<pmesh.GetNE(); i++)
              {
                 if (errors[i] > threshold)
                 {
                    refs.Append(Refinement(i, 3));
                 }
              }
              if (refs.Size() > 0)
              {
                 pmesh.GeneralRefinement(refs);
              }
         */
      }

      // Update the magnetostatic solver to reflect the new state of the mesh.
      Update(H1FESpace, HCurlFESpace, HDivFESpace, L2FESpace, BCoef,
             rhoCoef, tempCoef, nueCoef, nuiCoef,
             size_h1, size_l2,
             density_offsets, temperature_offsets,
             density, temperature,
             BField, density_gf, temperature_gf, nue_gf, nui_gf);
      CPD.Update();

      if (pmesh.Nonconforming() && mpi.WorldSize() > 1 && false)
      {
         if (mpi.Root()) { cout << "Rebalancing ..." << endl; }
         pmesh.Rebalance();

         // Update again after rebalancing
         Update(H1FESpace, HCurlFESpace, HDivFESpace, L2FESpace, BCoef,
                rhoCoef, tempCoef, nueCoef, nuiCoef,
                size_h1, size_l2,
                density_offsets, temperature_offsets,
                density, temperature,
                BField, density_gf, temperature_gf, nue_gf, nui_gf);
         CPD.Update();
      }
   }

   // Send the solution by socket to a GLVis server.
   if (visualization)
   {
      CPD.DisplayAnimationToGLVis();
   }

   // delete epsCoef;
   // delete muInvCoef;
   // delete sigmaCoef;
   for (int i=0; i<auxFields.Size(); i++)
   {
      delete auxFields[i];
   }

   return 0;
}

void Update(ParFiniteElementSpace & H1FESpace,
            ParFiniteElementSpace & HCurlFESpace,
            ParFiniteElementSpace & HDivFESpace,
            ParFiniteElementSpace & L2FESpace,
            VectorCoefficient & BCoef,
            Coefficient & rhoCoef,
            Coefficient & TCoef,
            Coefficient & nueCoef,
            Coefficient & nuiCoef,
            int & size_h1,
            int & size_l2,
            Array<int> & density_offsets,
            Array<int> & temperature_offsets,
            BlockVector & density,
            BlockVector & temperature,
            ParGridFunction & BField,
            ParGridFunction & density_gf,
            ParGridFunction & temperature_gf,
            ParGridFunction & nue_gf,
            ParGridFunction & nui_gf)
{
   H1FESpace.Update();
   HCurlFESpace.Update();
   HDivFESpace.Update();
   L2FESpace.Update();

   BField.Update();
   BField.ProjectCoefficient(BCoef);

   nue_gf.Update();
   nue_gf.ProjectCoefficient(nueCoef);
   nui_gf.Update();
   nui_gf.ProjectCoefficient(nuiCoef);

   size_l2 = L2FESpace.GetVSize();
   for (int i=1; i<density_offsets.Size(); i++)
   {
      density_offsets[i] = density_offsets[i - 1] + size_l2;
   }
   density.Update(density_offsets);
   for (int i=0; i<density_offsets.Size()-1; i++)
   {
      density_gf.MakeRef(&L2FESpace, density.GetBlock(i));
      density_gf.ProjectCoefficient(rhoCoef);
   }

   size_h1 = H1FESpace.GetVSize();
   for (int i=1; i<temperature_offsets.Size(); i++)
   {
      temperature_offsets[i] = temperature_offsets[i - 1] + size_h1;
   }
   temperature.Update(temperature_offsets);
   for (int i=0; i<temperature_offsets.Size()-1; i++)
   {
      temperature_gf.MakeRef(&H1FESpace, temperature.GetBlock(i));
      temperature_gf.ProjectCoefficient(TCoef);
   }
}

const char * banner[6] =
{
   R"(  _________ __   __        ____     ___________    ___ ___  )",
   R"( /   _____//  |_|__|__  __/_   | __| _/\______ \  /   |   \ )",
   R"( \_____  \\   __\  \  \/  /|   |/ __ |  |    |  \/    ~    \)",
   R"( /        \|  | |  |>    < |   / /_/ |  |    `   \    Y    /)",
   R"(/_______  /|__| |__/__/\_ \|___\____ | /_______  /\___|_  / )",
   R"(        \/               \/         \/         \/       \/  )"
};

// Print the stix2d ascii logo to the given ostream
void display_banner(ostream & os)
{
   for (int i=0; i<6; i++)
   {
      os << banner[i] << endl;
   }
   os << endl
      << "* Thomas H. Stix was a pioneer in the use of radio frequency"
      << " waves to heat" << endl
      << "  terrestrial plasmas to solar temperatures. He made important"
      << " contributions" << endl
      << "  to experimental and theoretic plasma physics. In the Stix"
      << " application, the" << endl
      << "  plasma dielectric for the wave equation is formulated using"
      << " the \"Stix\"" << endl
      << "  notation, \"S, D, P\"." << endl<< endl << flush;
}

// The Admittance is an optional coefficient defined on boundary surfaces which
// can be used in conjunction with absorbing boundary conditions.
Coefficient *
SetupRealAdmittanceCoefficient(const Mesh & mesh, const Array<int> & abcs)
{
   Coefficient * coef = NULL;

   if ( pw_eta_.Size() > 0 )
   {
      MFEM_VERIFY(pw_eta_.Size() == abcs.Size(),
                  "Each impedance value must be associated with exactly one "
                  "absorbing boundary surface.");

      pw_eta_inv_.SetSize(mesh.bdr_attributes.Size());

      if ( abcs[0] == -1 )
      {
         pw_eta_inv_ = 1.0 / pw_eta_[0];
      }
      else
      {
         pw_eta_inv_ = 0.0;

         for (int i=0; i<pw_eta_.Size(); i++)
         {
            pw_eta_inv_[abcs[i]-1] = 1.0 / pw_eta_[i];
         }
      }
      coef = new PWConstCoefficient(pw_eta_inv_);
   }

   return coef;
}

void slab_current_source_r(const Vector &x, Vector &j)
{
   MFEM_ASSERT(x.Size() == 3, "current source requires 3D space.");

   j.SetSize(x.Size());
   j = 0.0;

   bool cmplx = slab_params_.Size() == 8;

   int o = 3 + (cmplx ? 3 : 0);

   double x0 = slab_params_(o+0);
   double dx = slab_params_(o+1);

   if (x[0] >= x0-0.5*dx && x[0] <= x0+0.5*dx)
   {
      j(0) = slab_params_(0);
      j(1) = slab_params_(1);
      j(2) = slab_params_(2);
   }
}

void slab_current_source_i(const Vector &x, Vector &j)
{
   MFEM_ASSERT(x.Size() == 3, "current source requires 3D space.");

   j.SetSize(x.Size());
   j = 0.0;

   bool cmplx = slab_params_.Size() == 8;

   int o = 3 + (cmplx ? 3 : 0);

   double x0 = slab_params_(o+0);
   double dx = slab_params_(o+1);

   if (x[0] >= x0-0.5*dx && x[0] <= x0+0.5*dx)
   {
      if (cmplx)
      {
         j(0) = slab_params_(3);
         j(1) = slab_params_(4);
         j(2) = slab_params_(5);
      }
   }
}

void e_bc_r(const Vector &x, Vector &E)
{
   E.SetSize(3);
   E = 0.0;

}

void e_bc_i(const Vector &x, Vector &E)
{
   E.SetSize(3);
   E = 0.0;
}

ColdPlasmaPlaneWaveH::ColdPlasmaPlaneWaveH(char type,
                                           double omega,
                                           const Vector & B,
                                           const Vector & number,
                                           const Vector & charge,
                                           const Vector & mass,
                                           const Vector & temp,
                                           int nuprof,
                                           bool realPart)
   : VectorCoefficient(3),
     type_(type),
     realPart_(realPart),
     nuprof_(nuprof),
     omega_(omega),
     Bmag_(B.Norml2()),
     Jy_(0.0),
     xJ_(0.5),
     dx_(0.05),
     Lx_(1.0),
     kappa_(0.0),
     b_(B),
     bc_(3),
     bcc_(3),
     h_r_(3),
     h_i_(3),
     k_r_(3),
     k_i_(3),
     beta_r_(3),
     beta_i_(3),
     numbers_(number),
     charges_(charge),
     masses_(mass),
     temps_(temp)
{
   b_ *= 1.0 / Bmag_;

   {
      double bx = b_(0);
      double by = b_(1);
      double bz = b_(2);

      bc_(0) = by - bz;
      bc_(1) = bz - bx;
      bc_(2) = bx - by;

      bcc_(0) = by*by + bz*bz - bx*(by + bz);
      bcc_(1) = bz*bz + bx*bx - by*(bz + bx);
      bcc_(2) = bx*bx + by*by - bz*(bx + by);

      bc_  *= 1.0 / bc_.Norml2();
      bcc_ *= 1.0 / bcc_.Norml2();
   }

   beta_r_ = 0.0;
   beta_i_ = 0.0;

   double nue = 0;
   double nui = 0;

   S_ = S_cold_plasma(omega_, Bmag_, nue, nui, numbers_, charges_, masses_,
                      temps_, nuprof_);
   D_ = D_cold_plasma(omega_, Bmag_, nue, nui, numbers_, charges_, masses_,
                      temps_, nuprof_);
   P_ = P_cold_plasma(omega_, nue, numbers_, charges_, masses_, temps_,
                      nuprof_);

   switch (type_)
   {
      case 'L':
      {
         kappa_ = omega_ * sqrt(S_ - D_) / c0_;
         if (kappa_.imag() < 0.0) { kappa_ *= -1.0; }

         k_r_.Set(kappa_.real(), b_);
         k_i_.Set(kappa_.imag(), b_);

         complex<double> h = sqrt((S_ - D_) * (epsilon0_ / mu0_));

         h_r_.Set(-M_SQRT1_2 * h.real(), bcc_);
         h_r_.Add(-M_SQRT1_2 * h.imag(), bc_);
         h_i_.Set( M_SQRT1_2 * h.real(), bc_);
         h_i_.Add(-M_SQRT1_2 * h.imag(), bcc_);
      }
      break;
      case 'R':
      {
         kappa_ = omega_ * sqrt(S_ + D_) / c0_;
         if (kappa_.imag() < 0.0) { kappa_ *= -1.0; }

         k_r_.Set(kappa_.real(), b_);
         k_i_.Set(kappa_.imag(), b_);

         complex<double> h = sqrt((S_ + D_) * (epsilon0_ / mu0_));

         h_r_.Set(-M_SQRT1_2 * h.real(), bcc_);
         h_r_.Add( M_SQRT1_2 * h.imag(), bc_);
         h_i_.Set(-M_SQRT1_2 * h.real(), bc_);
         h_i_.Add(-M_SQRT1_2 * h.imag(), bcc_);
      }
      break;
      case 'O':
      {
         kappa_ = omega_ * sqrt(P_) / c0_;
         if (kappa_.imag() < 0.0) { kappa_ *= -1.0; }

         k_r_.Set(kappa_.real(), bc_);
         k_i_.Set(kappa_.imag(), bc_);

         complex<double> h = sqrt(P_ * (epsilon0_ / mu0_));

         h_r_.Set(h.real(), bcc_);
         h_i_.Set(h.imag(), bcc_);
      }
      break;
      case 'X':
      {
         kappa_ = omega_ * sqrt(S_ - D_ * D_ / S_) / c0_;
         if (kappa_.imag() < 0.0) { kappa_ *= -1.0; }

         k_r_.Set(kappa_.real(), bc_);
         k_i_.Set(kappa_.imag(), bc_);

         complex<double> h = S_ * sqrt(S_ - D_ * D_ / S_);
         h *= sqrt((epsilon0_ / mu0_) / (S_ * S_ + D_ * D_));

         h_r_.Set(-h.real(), b_);
         h_i_.Set(-h.imag(), b_);
      }
      break;
      case 'J':
         // MFEM_VERIFY(fabs(B_[2]) == Bmag_,
         //             "Current slab require a magnetic field in the z-direction.");
         break;
   }
}

void ColdPlasmaPlaneWaveH::Eval(Vector &V, ElementTransformation &T,
                                const IntegrationPoint &ip)
{
   V.SetSize(3);

   double x_data[3];
   Vector x(x_data, 3);
   T.Transform(ip, x);

   complex<double> i = complex<double>(0.0,1.0);

   switch (type_)
   {
      case 'L': // Left Circularly Polarized, propagating along B
      case 'R': // Right Circularly Polarized, propagating along B
      case 'O': // Ordinary wave propagating perpendicular to B
      case 'X': // eXtraordinary wave propagating perpendicular to B
      {
         complex<double> kx = 0.0;
         for (int d=0; d<3; d++)
         {
            kx += (k_r_[d] - beta_r_[d] + i * (k_i_[d] - beta_i_[d])) * x[d];
         }
         complex<double> phase = exp(i * kx);
         double phase_r = phase.real();
         double phase_i = phase.imag();

         if (realPart_)
         {
            for (int d=0; d<3; d++)
            {
               V[d] = h_r_[d] * phase_r - h_i_[d] * phase_i;
            }
         }
         else
         {
            for (int d=0; d<3; d++)
            {
               V[d] = h_r_[d] * phase_i + h_i_[d] * phase_r;
            }
         }
      }
      break;
      case 'J':  // Slab of current density perpendicular to propagation
      {
         if (true/* k_.Size() == 0 */)
         {
            complex<double> kE = omega_ * sqrt(S_ - D_ * D_ / S_) / c0_;

            complex<double> skL = sin(kE * Lx_);
            complex<double> E0 = i * Jy_ /
                                 (omega_ * epsilon0_ * skL *
                                  (S_ * S_ - D_ * D_));

            complex<double> Ex = i * D_ * E0;
            complex<double> Ey = S_ * E0;

            if (x[0] <= xJ_ - 0.5 * dx_)
            {
               complex<double> skLJ = sin(kE * (Lx_ - xJ_));
               complex<double> skd  = sin(kE * 0.5 * dx_);
               complex<double> skx  = sin(kE * x[0]);

               Ex *= -2.0 * skLJ * skd * skx;
               Ey *= -2.0 * skLJ * skd * skx;
            }
            else if (x[0] <= xJ_ + 0.5 * dx_)
            {
               complex<double> ck1  = cos(kE * (Lx_ - xJ_ - 0.5 * dx_));
               complex<double> ck2  = cos(kE * (xJ_ - 0.5 * dx_));
               complex<double> skx  = sin(kE * x[0]);
               complex<double> skLx = sin(kE * (Lx_ - x[0]));

               Ex *= skL - ck1 * skx - ck2 * skLx;
               Ey *= skL - ck1 * skx - ck2 * skLx;
            }
            else
            {
               complex<double> skJ  = sin(kE * xJ_);
               complex<double> skd  = sin(kE * 0.5 * dx_);
               complex<double> skLx = sin(kE * (Lx_ - x[0]));

               Ex *= -2.0 * skJ * skd * skLx;
               Ey *= -2.0 * skJ * skd * skLx;
            }

            if (realPart_)
            {
               V[0] = Ex.real();
               V[1] = Ey.real();
               V[2] = 0.0;
            }
            else
            {
               V[0] = Ex.imag();
               V[1] = Ey.imag();
               V[2] = 0.0;
            }
         }
         else
         {
            // General phase shift
            V = 0.0; // For now...
         }
      }
      break;
      case 'Z':
         V = 0.0;
         break;
   }
}

ColdPlasmaPlaneWaveE::ColdPlasmaPlaneWaveE(char type,
                                           double omega,
                                           const Vector & B,
                                           const Vector & number,
                                           const Vector & charge,
                                           const Vector & mass,
                                           const Vector & temp,
                                           int nuprof,
                                           bool realPart)
   : VectorCoefficient(3),
     type_(type),
     realPart_(realPart),
     nuprof_(nuprof),
     omega_(omega),
     Bmag_(B.Norml2()),
     Jy_(0.0),
     xJ_(0.5),
     dx_(0.05),
     Lx_(1.0),
     kappa_(0.0),
     b_(B),
     bc_(3),
     bcc_(3),
     e_r_(3),
     e_i_(3),
     k_r_(3),
     k_i_(3),
     beta_r_(3),
     beta_i_(3),
     numbers_(number),
     charges_(charge),
     masses_(mass),
     temps_(temp)
{
   b_ *= 1.0 / Bmag_;

   {
      double bx = b_(0);
      double by = b_(1);
      double bz = b_(2);

      bc_(0) = by - bz;
      bc_(1) = bz - bx;
      bc_(2) = bx - by;

      bcc_(0) = by*by + bz*bz - bx*(by + bz);
      bcc_(1) = bz*bz + bx*bx - by*(bz + bx);
      bcc_(2) = bx*bx + by*by - bz*(bx + by);

      bc_  *= 1.0 / bc_.Norml2();
      bcc_ *= 1.0 / bcc_.Norml2();
   }

   beta_r_ = 0.0;
   beta_i_ = 0.0;

   double nue = 0;
   double nui = 0;

   S_ = S_cold_plasma(omega_, Bmag_, nue, nui, numbers_, charges_, masses_,
                      temps_, nuprof_);
   D_ = D_cold_plasma(omega_, Bmag_, nue, nui, numbers_, charges_, masses_,
                      temps_, nuprof_);
   P_ = P_cold_plasma(omega_, nue, numbers_, charges_, masses_, temps_,
                      nuprof_);

   switch (type_)
   {
      case 'L':
      {
         kappa_ = omega_ * sqrt(S_ - D_) / c0_;
         if (kappa_.imag() < 0.0) { kappa_ *= -1.0; }

         k_r_.Set(kappa_.real(), b_);
         k_i_.Set(kappa_.imag(), b_);

         e_r_.Set(M_SQRT1_2, bc_);
         e_i_.Set(M_SQRT1_2, bcc_);
      }
      break;
      case 'R':
      {
         kappa_ = omega_ * sqrt(S_ + D_) / c0_;
         if (kappa_.imag() < 0.0) { kappa_ *= -1.0; }

         k_r_.Set(kappa_.real(), b_);
         k_i_.Set(kappa_.imag(), b_);

         e_r_.Set( M_SQRT1_2, bc_);
         e_i_.Set(-M_SQRT1_2, bcc_);
      }
      break;
      case 'O':
      {
         kappa_ = omega_ * sqrt(P_) / c0_;
         if (kappa_.imag() < 0.0) { kappa_ *= -1.0; }

         k_r_.Set(kappa_.real(), bc_);
         k_i_.Set(kappa_.imag(), bc_);

         e_r_.Set(1.0, b_);
         e_i_ = 0.0;
      }
      break;
      case 'X':
      {
         kappa_ = omega_ * sqrt(S_ - D_ * D_ / S_) / c0_;
         if (kappa_.imag() < 0.0) { kappa_ *= -1.0; }

         k_r_.Set(kappa_.real(), bc_);
         k_i_.Set(kappa_.imag(), bc_);

         complex<double> den = sqrt(S_ * S_ + D_ * D_);
         complex<double> ec  = D_ / den;
         complex<double> ecc = S_ / den;

         e_r_.Set(ecc.real(), bcc_);
         e_r_.Add(ec.imag(), bc_);
         e_i_.Set(-ec.real(), bc_);
         e_i_.Add(ecc.imag(), bcc_);
      }
      break;
      case 'J':
         // MFEM_VERIFY(fabs(B_[2]) == Bmag_,
         //           "Current slab require a magnetic field in the z-direction.");
         break;
   }
}

void ColdPlasmaPlaneWaveE::Eval(Vector &V, ElementTransformation &T,
                                const IntegrationPoint &ip)
{
   V.SetSize(3);

   double x_data[3];
   Vector x(x_data, 3);
   T.Transform(ip, x);

   complex<double> i = complex<double>(0.0,1.0);

   switch (type_)
   {
      case 'L': // Left Circularly Polarized, propagating along B
      case 'R': // Right Circularly Polarized, propagating along B
      case 'O': // Ordinary wave propagating perpendicular to B
      case 'X': // eXtraordinary wave propagating perpendicular to B
      {
         complex<double> kx = 0.0;
         for (int d=0; d<3; d++)
         {
            kx += (k_r_[d] - beta_r_[d] + i * (k_i_[d] - beta_i_[d])) * x[d];
         }
         complex<double> phase = exp(i * kx);
         double phase_r = phase.real();
         double phase_i = phase.imag();

         if (realPart_)
         {
            for (int d=0; d<3; d++)
            {
               V[d] = e_r_[d] * phase_r - e_i_[d] * phase_i;
            }
         }
         else
         {
            for (int d=0; d<3; d++)
            {
               V[d] = e_r_[d] * phase_i + e_i_[d] * phase_r;
            }
         }
      }
      break;
      case 'J':  // Slab of current density perpendicular to propagation
      {
         /*
          if (k_.Size() == 0)
               {
                  complex<double> kE = omega_ * sqrt(S_ - D_ * D_ / S_) / c0_;

                  complex<double> skL = sin(kE * Lx_);
                  complex<double> E0 = i * Jy_ /
                                       (omega_ * epsilon0_ * skL *
                                        (S_ * S_ - D_ * D_));

                  complex<double> Ex = i * D_ * E0;
                  complex<double> Ey = S_ * E0;

                  if (x[0] <= xJ_ - 0.5 * dx_)
                  {
                     complex<double> skLJ = sin(kE * (Lx_ - xJ_));
                     complex<double> skd  = sin(kE * 0.5 * dx_);
                     complex<double> skx  = sin(kE * x[0]);

                     Ex *= -2.0 * skLJ * skd * skx;
                     Ey *= -2.0 * skLJ * skd * skx;
                  }
                  else if (x[0] <= xJ_ + 0.5 * dx_)
                  {
                     complex<double> ck1  = cos(kE * (Lx_ - xJ_ - 0.5 * dx_));
                     complex<double> ck2  = cos(kE * (xJ_ - 0.5 * dx_));
                     complex<double> skx  = sin(kE * x[0]);
                     complex<double> skLx = sin(kE * (Lx_ - x[0]));

                     Ex *= skL - ck1 * skx - ck2 * skLx;
                     Ey *= skL - ck1 * skx - ck2 * skLx;
                  }
                  else
                  {
                     complex<double> skJ  = sin(kE * xJ_);
                     complex<double> skd  = sin(kE * 0.5 * dx_);
                     complex<double> skLx = sin(kE * (Lx_ - x[0]));

                     Ex *= -2.0 * skJ * skd * skLx;
                     Ey *= -2.0 * skJ * skd * skLx;
                  }

                  if (realPart_)
                  {
                     V[0] = Ex.real();
                     V[1] = Ey.real();
                     V[2] = 0.0;
                  }
                  else
                  {
                     V[0] = Ex.imag();
                     V[1] = Ey.imag();
                     V[2] = 0.0;
                  }
               }
               else
               {
                  // General phase shift
                  V = 0.0; // For now...
               }
         */
      }
      break;
      case 'Z':
         V = 0.0;
         break;
   }
}
