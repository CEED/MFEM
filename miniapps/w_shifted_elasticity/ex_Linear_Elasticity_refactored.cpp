// Sample run:  
// mpirun -np 1 ./ex_Linear_Elasticity_WSBM -m ./square01_tri.mesh -bulkModCoef 166.66666666667 -shearModCoef 76.9230769231 -emb -gS 1 -rs 2 -tO 2 -do 2 -gPenCoef 1.0 -nST 2 -mumps -level-set
//              -bulkModCoef is the flag for setting the bulk modulus.
//              -shearModCoef is the flag for setting the shear modulus. 
//              -emb is the flag, when added, for activating the embedded data structure of the code.
//              -gS is the flag for setting the type of geometry for the level set to use.
//              -do is the flag for setting the polynomial order for the displacement.
//              -gPenCoef is the flag for setting the penalty parameter for the strain ghost penalty.
//              -nST is the flag for setting the number of terms to add in the strain ghost penalty.
//              -mumps is the flag, when added, for using a mumps solver.
//              -level-set is the flag, when added, for using a level set for computing the distance and normal vectors otherwise an analytical shape is used instead.    
//
// Description: This code implements WSBM for the linear elasticity operator. This Neumann conditions are embedded
//              while any Dirichlet condition has to be body-fitted. So far we assume one level set to define
//              the geometry to be embedded, but the code can be easily modifed to handle multiple level sets.
//              The body-fitted Dirichlet conditions are enforced weakly with a fixed penalty parameter equal to
//              40.0*C_I, where C_I is the trace inequality constant. Code also works for pure body-fitted calculation.

#include "mfem.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>
#include "linear_elasticity_solver.hpp"

using namespace std;
using namespace mfem;

// Exact solution, E, and r.h.s., f. See below for implementation.
void uFun_ex(const Vector & x, Vector & u);
void fFun(const Vector & x, Vector & f );
void traction_ex(const Vector & x, DenseMatrix & tN );

void uFun_ex3D(const Vector & x, Vector & u);
void fFun3D(const Vector & x, Vector & f );

double pi = 3.141592653589793e0;

int main(int argc, char *argv[])
{
  StopWatch chrono;
  Mpi::Init(argc, argv);
  int num_procs = Mpi::WorldSize();
  int myid = Mpi::WorldRank();
  Hypre::Init();
  bool verbose = (myid == 0);


   // 1. Parse command line options
  //  const char *mesh_file = "./mesh_1.exo";
  //  const char *mesh_file = "./square01_tri.mesh";
  const char *mesh_file = "./square01_quad.mesh";

  int displacementOrder = 1;
  int ser_ref_levels = 0;
  const char *device_config = "cpu";
  double shearModCoefficient = 0.0;
  double bulkModCoefficient = 0.0;
  bool visualization = false;
  bool useEmbedded = false;
  int geometricShape = 0;
  int nTerms = 1; 
  bool mumps_solver = false;
  int numberStrainTerms = 1;
  double ghostPenaltyCoefficient = 1.0;
  bool useAnalyticalShape = true;
  int problemType = 1;
  
  OptionsParser args(argc, argv);
  args.AddOption(&mesh_file, "-m", "--mesh", "Mesh file to use.");
  args.AddOption(&problemType, "-pT", "--problemType", "Select the problem type.");
  args.AddOption(&displacementOrder, "-do", "--displacementOrder", "Finite element displacement polynomial degree");
  args.AddOption(&ser_ref_levels, "-rs", "--refine-serial",
		 "Number of times to refine the mesh uniformly in serial.");
  args.AddOption(&shearModCoefficient, "-shearModCoef", "--shearModulusCoefficient",
		 "Value of shear modulus.");
  args.AddOption(&bulkModCoefficient, "-bulkModCoef", "--bulkModulusCoefficient",
		 "Value of bulk modulus.");
  args.AddOption(&visualization, "-vis", "--visualization", "-no-vis",
		 "--no-visualization",
		 "Enable or disable GLVis visualization.");
  args.AddOption(&useEmbedded, "-emb", "--use-embedded", "-no-emb",
		 "--no-embedded",
		 "Use Embedded when there is surface that will be embedded in a pre-existing mesh");
  args.AddOption(&geometricShape, "-gS", "--geometricShape",
		 "Shape of the embedded geometry that will be embedded");
  args.AddOption(&nTerms, "-tO", "--taylorOrder",
		 "Number of terms in the Taylor expansion");
  args.AddOption(&numberStrainTerms, "-nST", "--numberStrainTerms",
		 "Number of terms in the strain ghost penalty operator.");
  args.AddOption(&ghostPenaltyCoefficient, "-gPenCoef", "--ghost-penalty-coefficient",
		 "Ghost penalty scaling.");
  args.AddOption(&useAnalyticalShape, "-analyticalShape", "--use-analytical-shape", "-level-set",
		 "--use-level-set",
		 "Use analytical shape for computing distance and normals, otherwise a level set is used.");
  
#ifdef MFEM_USE_MUMPS
   args.AddOption(&mumps_solver, "-mumps", "--mumps-solver", "-no-mumps",
                  "--no-mumps-solver", "Use the MUMPS Solver.");
#endif

   args.ParseCheck();
   Device device(device_config);

   // 2. Read the mesh from the given mesh file, and refine once uniformly.
   Mesh *mesh;
   mesh = new Mesh(mesh_file, true, true);
   for (int lev = 0; lev < ser_ref_levels; lev++) { mesh->UniformRefinement(); }

   int dim = mesh->Dimension();

   ParMesh *pmesh = new ParMesh(MPI_COMM_WORLD, *mesh);

   delete mesh;

   // 5. Define the coefficients, analytical solution, and rhs of the PDE.
   VectorFunctionCoefficient fcoeff(dim, fFun);
   VectorFunctionCoefficient ucoeff(dim, uFun_ex);
   ShiftedMatrixFunctionCoefficient traction_shifted(dim, traction_ex);

   VectorFunctionCoefficient fcoeff3D(dim, fFun3D);
   VectorFunctionCoefficient ucoeff3D(dim, uFun_ex3D);
   
   mfem::LinearElasticitySolver* ssolv=new mfem::LinearElasticitySolver(pmesh, displacementOrder, useEmbedded, geometricShape, nTerms, numberStrainTerms, ghostPenaltyCoefficient, mumps_solver, useAnalyticalShape, visualization);
   ssolv->AddMaterial(shearModCoefficient,bulkModCoefficient);
   if ( problemType == 1){
     ssolv->SetVolForce(fcoeff);
     ssolv->AddDisplacementBC(1,ucoeff);
     ssolv->AddDisplacementBC(2,ucoeff);
     ssolv->AddDisplacementBC(3,ucoeff);
     ssolv->AddDisplacementBC(4,ucoeff);
     ssolv->AddShiftedNormalStressBC(traction_shifted);
     ssolv->SetExactDisplacementSolution(ucoeff);
   }
   else if (problemType == 2){
     ssolv->SetVolForce(fcoeff3D);
     ssolv->AddDisplacementBC(1,ucoeff3D);
     ssolv->AddDisplacementBC(2,ucoeff3D);
     ssolv->AddDisplacementBC(3,ucoeff3D);
     ssolv->AddDisplacementBC(4,ucoeff3D);
     ssolv->AddDisplacementBC(5,ucoeff3D);
     ssolv->AddDisplacementBC(6,ucoeff3D);
     ssolv->SetExactDisplacementSolution(ucoeff3D);
   }
   ssolv->SetNewtonSolver(1.0e-12,0.0,10000000,1);
   ssolv->FSolve();
   ssolv->ComputeL2Errors();
   ssolv->VisualizeFields();
   
   delete ssolv;
   
   return 0;
}


void fFun(const Vector & x, Vector & f )
{
  // assumes kappa = 500/3 and mu = 76.9230769231
  f(0) = -(0.2*(759.200338546*cos(pi*(x(0)+0.5))*sin(pi*(x(1)+0.5))) + 115.384615385*((pi*pi*cos((pi*(x(0)+0.5))/7.0)*cos((pi*(x(1)+0.5))/3.0))/210.0 + (pi*pi*cos(pi*(x(0)+0.5))*sin(pi*(x(1)+0.5)))/10.0) +(759.200338546*(cos((pi*(x(0)+0.5))/7.0)*cos((pi*(x(1)+0.5))/3.0)-21.0*cos(pi*x(1))*sin(pi*x(0))))/210.0);
  f(1) = -(-(759.200338546*(sin((pi*(x(0)+0.5))/7.0)*sin((pi*(x(1)+0.5))/3.0)+49*cos(pi*x(0))*sin(pi*x(1))))/490 -(1.0/45.0)*(759.200338546*sin((pi*(x(0)+0.5))/7.0)*sin((pi*(x(1)+0.5))/3.0)) - 115.384615385*((pi*pi*sin((pi*(x(0)+0.5))/7.0)*sin((pi*(x(1)+0.5))/3.0))/90.0 - (pi*pi*cos(pi*(x(1)+0.5))*sin(pi*(x(0)+0.5)))/10.0));
}

void uFun_ex(const Vector & x, Vector & u)
{
  u(0) = -0.1*cos(pi*(x(0)+0.5))*sin(pi*(x(1)+0.5));
  u(1) = 0.1*sin(pi*(x(0)+0.5)/7.0)*sin(pi*(x(1)+0.5)/3.0);
}

void traction_ex(const Vector & x, DenseMatrix & tN )
{
  double kappa = 500.0/3.0;
  double mu = 76.9230769231;
  double sigma_xx = (pi/5.0)*mu*sin(pi*(x(0)+0.5))*sin(pi*(x(1)+0.5))+(kappa-(2.0/3.0)*mu)*((pi/10.0)*sin(pi*(x(0)+0.5))*sin(pi*(x(1)+0.5))+(pi/30.0)*sin((pi/7.0)*(x(0)+0.5))*cos((pi/3.0)*(x(1)+0.5)));
  double sigma_xy = mu*(-(pi/10.0)*cos(pi*(x(0)+0.5))*cos(pi*(x(1)+0.5))+(pi/70.0)*cos((pi/7.0)*(x(0)+0.5))*sin((pi/3.0)*(x(1)+0.5)));
  double sigma_yx = mu*(-(pi/10.0)*cos(pi*(x(0)+0.5))*cos(pi*(x(1)+0.5))+(pi/70.0)*cos((pi/7.0)*(x(0)+0.5))*sin((pi/3.0)*(x(1)+0.5)));
  double sigma_yy = (pi/15.0)*mu*sin((pi/7.0)*(x(0)+0.5))*cos((pi/3.0)*(x(1)+0.5))+(kappa-(2.0/3.0)*mu)*((pi/10.0)*sin(pi*(x(0)+0.5))*sin(pi*(x(1)+0.5))+(pi/30.0)*sin((pi/7.0)*(x(0)+0.5))*cos((pi/3.0)*(x(1)+0.5)));
  tN = 0.0; 
  tN(0,0) = sigma_xx;
  tN(0,1) = sigma_xy;
  tN(1,0) = sigma_yx;
  tN(1,1) = sigma_yy;
}


void fFun3D(const Vector & x, Vector & f )
{
  // assumes kappa = 500/3 and mu = 76.9230769231
  double kappa = 500.0/3.0;
  double mu = 76.9230769231;

  f(0) = -(384.615384615*(pi*x(1)*0.025*cos(pi*x(0)*x(1)*x(2)*0.5)+(pi*pi/420.0)*cos(pi*x(0)/7.0)*cos(pi*x(1)/3.0)*cos(pi*x(2)/5.0)-pi*pi*0.0125*x(0)*x(1)*x(1)*x(2)*sin(pi*x(0)*x(1)*x(2)*0.5))+211.538461538*0.2*pi*pi*cos(pi*x(0))*cos(pi*x(2))*sin(pi*x(1)));
  
  f(1) = -(384.615384615*(pi*x(0)*0.025*cos(pi*x(0)*x(1)*x(2)*0.5)-pi*pi*0.0125*x(0)*x(0)*x(1)*x(2)*sin(pi*x(0)*x(1)*x(2)*0.5)+0.05*pi*pi*cos(pi*x(1))*cos(pi*x(2))*sin(pi*x(0)))-pi*pi*3.45613117042*sin(pi*x(0)/7.0)*sin(pi*x(1)/3.0)*cos(pi*x(2)/5.0));

  f(2) = (269.230769231*pi*pi*0.025*x(0)*x(0)*x(1)*x(1)*sin(pi*x(0)*x(1)*x(2)*0.5)+0.025*mu*pi*pi*x(0)*x(0)*x(2)*x(2)*sin(pi*x(0)*x(1)*x(2)*0.5)+0.025*mu*pi*pi*x(1)*x(1)*x(2)*x(2)*sin(pi*x(0)*x(1)*x(2)*0.5)+1.28205128205*pi*pi*sin(pi*x(0)/7.0)*cos(pi*x(1)/3.0)*sin(pi*x(2)/5.0)+19.2307692308*pi*pi*sin(pi*x(0))*sin(pi*x(1))*sin(pi*x(2)));

}

void uFun_ex3D(const Vector & x, Vector & u)
{
  u(0) = -0.1*cos(pi*x(0))*sin(pi*x(1))*cos(pi*x(2));
  u(1) = 0.1*sin(pi*x(0)/7.0)*sin(pi*x(1)/3.0)*cos(pi*x(2)/5.0);
  u(2) = 0.1*sin(pi*x(0)*x(1)*x(2)/2.0);
}

