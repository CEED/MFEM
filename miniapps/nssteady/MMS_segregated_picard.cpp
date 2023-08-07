// Include mfem and I/O
#include "mfem.hpp"
#include <fstream>
#include <iostream>

// Include for defining exact solution
#include <math.h>

// Include steady ns miniapp
#include "snavier_picard_segregated.hpp"


#ifdef M_PI
#define PI M_PI
#else
#define PI 3.14159265358979
#endif

using namespace mfem;

// Forward declarations of functions and global variables

double ns_coeff = 1.0;

double ComputeLift(ParGridFunction &p);

void   V_exact1(const Vector &X, Vector &v);
double P_exact1(const Vector &X);
std::function<void(const Vector &, Vector &)> RHS1(const double &kin_vis);

void   V_exact2(const Vector &X, Vector &v);
double P_exact2(const Vector &X);
std::function<void(const Vector &, Vector &)> RHS2(const double &kin_vis);

void   V_exact3(const Vector &X, Vector &v);
double P_exact3(const Vector &X);
std::function<void(const Vector &, Vector &)> RHS3(const double &kin_vis);

double pZero(const Vector &X);
void   vZero(const Vector &X, Vector &v);

// Test
int main(int argc, char *argv[])
{
   //
   /// 1. Initialize MPI and HYPRE.
   //
   int nprocs, myrank;
   MPI_Init(&argc, &argv);
   MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
   MPI_Comm_rank(MPI_COMM_WORLD, &myrank);
   Hypre::Init();


   //
   /// 2. Parse command-line options. 
   //
   int fun = 1;               // exact solution

   int porder = 1;            // fe
   int vorder = 2;

   int n = 10;                // mesh
   int dim = 2;
   int elem = 0;
   int ser_ref_levels = 0;
   int par_ref_levels = 0;

   double kin_vis = 0.01;     // kinematic viscosity

   double rel_tol = 1e-7;     // solvers
   double abs_tol = 1e-10;
   int tot_iter = 1000;
   int print_level = 0;

   bool stokes = false;       // if true solves stokes problem
   double alpha = 1.;         // steady-state scheme
   double gamma = 1.;         // relaxation

   bool paraview = false;     // postprocessing (paraview)
   bool visit    = true;      // postprocessing (VISit)
   
   bool verbose = false;
   const char *outFolder = "./";

   // TODO: check parsing and assign variables
   mfem::OptionsParser args(argc, argv);
   args.AddOption(&dim,
                     "-d",
                     "--dimension",
                     "Dimension of the problem (2 = 2d, 3 = 3d)");
   args.AddOption(&elem,
                     "-e",
                     "--element-type",
                     "Type of elements used (0: Quad/Hex, 1: Tri/Tet)");
   args.AddOption(&n,
                     "-n",
                     "--num-elements",
                     "Number of elements in uniform mesh.");
   args.AddOption(&ser_ref_levels,
                     "-rs",
                     "--refine-serial",
                     "Number of times to refine the mesh uniformly in serial.");
   args.AddOption(&par_ref_levels,
                     "-rp",
                     "--refine-parallel",
                     "Number of times to refine the mesh uniformly in parallel.");
   args.AddOption(&vorder, "-ov", "--order_vel",
                     "Finite element order for velocity (polynomial degree) or -1 for"
                     " isoparametric space.");
   args.AddOption(&porder, "-op", "--order_pres",
                     "Finite element order for pressure(polynomial degree) or -1 for"
                     " isoparametric space.");
   args.AddOption(&kin_vis,
                     "-kv",
                     "--kin-viscosity",
                     "Kinematic viscosity");
   args.AddOption(&rel_tol,
                  "-rel",
                  "--relative-tolerance",
                  "Relative tolerance for the Newton solve.");
   args.AddOption(&abs_tol,
                  "-abs",
                  "--absolute-tolerance",
                  "Absolute tolerance for the Outer solve.");
   args.AddOption(&tot_iter,
                  "-it",
                  "--linear-iterations",
                  "Maximum iterations for the linear solve.");
   args.AddOption(&outFolder,
                  "-o",
                  "--output-folder",
                  "Output folder.");
   args.AddOption(&paraview, "-p", "--paraview", "-no-p",
                  "--no-paraview",
                  "Enable Paraview output.");
   args.AddOption(&fun, "-f", "--test-function",
                     "Analytic function to test");    
   args.AddOption(&print_level,
                  "-pl",
                  "--print-level",
                  "Print level.");     
   args.AddOption(&stokes,
                     "-s",
                     "--stokes",
                     "-ns",
                     "--navier-stokes",
                     "Stokes solution.");
   args.AddOption(&gamma,
                     "-g",
                     "--gamma",
                     "Relaxation parameter");
   args.AddOption(&alpha,
                     "-a",
                     "--alpha",
                     "Parameter controlling linearization");


   args.Parse();
   if (!args.Good())
   {
      if (myrank == 0)
      {
         args.PrintUsage(std::cout);
      }
      MPI_Finalize();
      return 1;
   }
   if (myrank == 0)
   {
       args.PrintOptions(std::cout);
   }


   //
   /// 3. Read the (serial) mesh from the given mesh file on all processors.
   //
   Element::Type type;
   switch (elem)
   {
      case 0: // quad
         type = (dim == 2) ? Element::QUADRILATERAL: Element::HEXAHEDRON;
         break;
      case 1: // tri
         type = (dim == 2) ? Element::TRIANGLE: Element::TETRAHEDRON;
         break;
   }

   Mesh mesh;
   switch (dim)
   {
      case 2: // 2d
         mesh = Mesh::MakeCartesian2D(n,n,type,true);	
         break;
      case 3: // 3d
         mesh = Mesh::MakeCartesian3D(n,n,n,type,true);	
         break;
   }


   for (int l = 0; l < ser_ref_levels; l++)
   {
       mesh.UniformRefinement();
   }


   //
   /// 4. Define a parallel mesh by a partitioning of the serial mesh. 
   // Refine this mesh further in parallel to increase the resolution. Once the
   // parallel mesh is defined, the serial mesh can be deleted.
   //
   ParMesh *pmesh = new ParMesh(MPI_COMM_WORLD, mesh);
   mesh.Clear();
   {
       for (int l = 0; l < par_ref_levels; l++)
       {
          pmesh->UniformRefinement();
       }
   }


   //
   /// 5. Create solver
   // 
   SNavierPicardCGSolver* NSSolver = new SNavierPicardCGSolver(pmesh,vorder,porder,kin_vis,verbose);

   if ( stokes )
   {
      NSSolver->EnableStokes();
      ns_coeff = 0.0;
   }

   //
   /// 6. Define the coefficients (e.g. parameters, analytical solution/s) and compute pressure lift.
   //
   VectorFunctionCoefficient*    V_ex = nullptr;
   VectorFunctionCoefficient* f_coeff = nullptr;
   FunctionCoefficient*          P_ex = nullptr;
   switch (fun)
   {
   case 1:
      {
         V_ex = new VectorFunctionCoefficient(dim, V_exact1);
         P_ex = new FunctionCoefficient(P_exact1);
         f_coeff = new VectorFunctionCoefficient(dim, RHS1(kin_vis));
         break;
      }
   case 2:
      {
         V_ex = new VectorFunctionCoefficient(dim, V_exact2);
         P_ex = new FunctionCoefficient(P_exact2);
         f_coeff = new VectorFunctionCoefficient(dim, RHS2(kin_vis));
         break;
      }
   case 3:
      {
         V_ex = new VectorFunctionCoefficient(dim, V_exact3);
         P_ex = new FunctionCoefficient(P_exact3);
         f_coeff = new VectorFunctionCoefficient(dim, RHS3(kin_vis));
         break;
      }
   default:
      break;
   }

   //
   /// 7. Set parameters of the Fixed Point Solver
   // 
   //SolverParams sFP = {1e-6, 1e-10, 1000, 1};   // rtol, atol, maxIter, print level
   SolverParams sFP = {1e-6, 1e-10, 1000, 1}; 
   NSSolver->SetOuterSolver(sFP);

   NSSolver->SetAlpha(alpha, AlphaType::CONSTANT);
   NSSolver->SetGamma(gamma);

   //
   /// 8. Set parameters of the Linear Solvers
   //
   SolverParams s1 = {1e-6, 1e-10, 1000, 0}; 
   SolverParams s2 = {1e-6, 1e-10, 1000, 0}; 
   SolverParams s3 = {1e-6, 1e-10, 1000, 0}; 
   NSSolver->SetInnerSolvers(s1,s2,s3);


   //
   /// 9. Add boundary conditions (Velocity-Dirichlet, Traction) and forcing term/s
   //
   // Acceleration term
   Array<int> domain_attr(pmesh->attributes.Max());
   domain_attr = 1;
   NSSolver->AddAccelTerm(f_coeff,domain_attr);

   // Essential velocity bcs
   Array<int> ess_attr(pmesh->bdr_attributes.Max());
   ess_attr = 1;
   NSSolver->AddVelDirichletBC(V_ex, ess_attr);

   // Traction (neumann) bcs
   //Array<int> trac_attr(pmesh->bdr_attributes.Max());
   //NSSolver->AddTractionBC(coeff,attr);


   //
   /// 10. Set initial condition
   //
   //VectorFunctionCoefficient v_in(dim, vZero);
   //FunctionCoefficient p_in(pZero);
   //NSSolver->SetInitialConditionVel(v_in);
   //NSSolver->SetInitialConditionPres(p_in);
   //NSSolver->SetInitialConditionVel(*V_ex);
   //NSSolver->SetInitialConditionPres(*P_ex);

   //
   /// 11. Finalize Setup of solver
   //
   NSSolver->Setup();
   //DataCollection::Format forma = DataCollection::PARALLEL_FORMAT
   NSSolver->SetupOutput( outFolder, visit, paraview );

   // Export exact solution
   ParGridFunction* velocityExactPtr = new ParGridFunction(NSSolver->GetVFes());
   ParGridFunction* pressureExactPtr = new ParGridFunction(NSSolver->GetPFes());
   ParGridFunction*           rhsPtr = new ParGridFunction(NSSolver->GetVFes());
   velocityExactPtr->ProjectCoefficient(*V_ex);
   pressureExactPtr->ProjectCoefficient(*P_ex);
   rhsPtr->ProjectCoefficient(*f_coeff);

   ParaViewDataCollection paraview_dc("Results-Paraview-Exact", pmesh);
   paraview_dc.SetPrefixPath(outFolder);
   paraview_dc.SetLevelsOfDetail(vorder);
   paraview_dc.SetDataFormat(VTKFormat::BINARY);
   paraview_dc.SetHighOrderOutput(true);
   paraview_dc.SetCycle(0);
   paraview_dc.SetTime(0.0);
   paraview_dc.RegisterField("velocity_exact",velocityExactPtr);
   paraview_dc.RegisterField("pressure_exact",pressureExactPtr);
   paraview_dc.RegisterField("rhs",rhsPtr);
   paraview_dc.Save();


   // Compute lift for pressure solution
   double lift = ComputeLift(*pressureExactPtr);
   NSSolver->SetLift(lift);

   //
   /// 12. Solve the forward problem
   //
   NSSolver->FSolve(); 


   //
   /// 13. Return forward problem solution and output results
   //
   ParGridFunction* velocityPtr = &(NSSolver->GetVelocity());
   ParGridFunction* pressurePtr = &(NSSolver->GetPressure());


   // Free memory
   delete pmesh;
   delete NSSolver;
   delete velocityExactPtr;
   delete pressureExactPtr;
   delete rhsPtr;

   // Finalize Hypre and MPI
   HYPRE_Finalize();
   MPI_Finalize();

   return 0;
}


double ComputeLift(ParGridFunction &p)
{
   ConstantCoefficient onecoeff(1.0);
   ParLinearForm* mass_lf = new ParLinearForm(p.ParFESpace());
   auto *dlfi = new DomainLFIntegrator(onecoeff);
   mass_lf->AddDomainIntegrator(dlfi);
   mass_lf->Assemble();
   ParGridFunction one_gf(p.ParFESpace());
   one_gf.ProjectCoefficient(onecoeff);

   double volume = mass_lf->operator()(one_gf);

   double integ = mass_lf->operator()(p);

   return integ/volume;   // CHECK should we scale by volume
}


// Exact solutions
void V_exact1(const Vector &X, Vector &v)
{
   const int dim = X.Size();

   double x = X[0];
   double y = X[1];
   if( dim == 3) {
      double z = X[2];
   }

   v = 0.0;

   v(0) = -cos(M_PI * x) * sin(M_PI * y);
   v(1) = sin(M_PI * x) * cos(M_PI * y);

   if( dim == 3) { v(2) = 0; }
}

std::function<void(const Vector &, Vector &)> RHS1(const double &kin_vis)
{
   return [kin_vis](const Vector &X, Vector &v)
   {
      const int dim = X.Size();

      double x = X[0];
      double y = X[1];
      
      if( dim == 3) {
         double z = X[2];
      }
      
      v = 0.0;

      v(0) = 1.0
             - 2.0 * kin_vis * M_PI * M_PI * cos(M_PI * x) * sin(M_PI * y)
             - ns_coeff * ( M_PI * sin( M_PI * x) * cos( M_PI * x) );
      v(1) = 1.0
             + 2.0 * kin_vis * M_PI * M_PI * cos(M_PI * y) * sin(M_PI * x)
             - ns_coeff * ( M_PI * sin( M_PI * y) * cos( M_PI * y) );
      if( dim == 3) {
         v(2) = 0;
      }
   };
}

double P_exact1(const Vector &X)
{
   double x = X[0];
   double y = X[1];

   double p = x + y - 1.0;;

   return p;
}



void V_exact2(const Vector &X, Vector &v)
{
   const int dim = X.Size();

   double x = X[0];
   double y = X[1];
   if( dim == 3) {
      double z = X[2];
   }

   v = 0.0;

   v(0) = pow(sin(M_PI*x),2) * sin(M_PI*y) * cos(M_PI*y);
   v(1) = - pow(sin(M_PI*y),2) * sin(M_PI*x) * cos(M_PI*x);
   if( dim == 3) { v(2) = 0; }
}

std::function<void(const Vector &, Vector &)> RHS2(const double &kin_vis)
{
   return [kin_vis](const Vector &X, Vector &v)
   {
      const int dim = X.Size();

      double x = X[0];
      double y = X[1];
      
      if( dim == 3) {
         double z = X[2];
      }
      
      v = 0.0;

      v(0) =  y * (2*x - 1) * (y - 1)
              - kin_vis * M_PI * M_PI * 2 * sin(M_PI*y) * cos(M_PI*y) *(pow(cos(M_PI*x),2) - 3*pow(sin(M_PI*x),2))
              + ns_coeff * ( M_PI * cos(M_PI * x) * pow(sin(M_PI * x),3) * pow(sin(M_PI*y),2) );
      v(1) =  x * (2*y - 1) * (x - 1)
              + kin_vis * M_PI * M_PI * 2 * sin(M_PI*x) * cos(M_PI*x) *(pow(cos(M_PI*y),2) - 3*pow(sin(M_PI*y),2))
              + ns_coeff * ( M_PI * cos(M_PI * y) * pow(sin(M_PI * y),3) * pow(sin(M_PI*x),2) );

      if( dim == 3) {
         v(2) = 0;
      }
   };
}

double P_exact2(const Vector &X)
{
   double x = X[0];
   double y = X[1];

   double p = x*y*(1-x)*(1-y);

   return p;
}


void V_exact3(const Vector &X, Vector &v)
{
   const int dim = X.Size();

   double x = X[0];
   double y = X[1];
   if( dim == 3) {
      double z = X[2];
   }

   v = 0.0;

   v(0) = -sin(x) * cos(y);
   v(1) =  cos(x) * sin(y);

   if( dim == 3) { v(2) = 0; }
}

std::function<void(const Vector &, Vector &)> RHS3(const double &kin_vis)
{
   return [kin_vis](const Vector &X, Vector &v)
   {
      const int dim = X.Size();

      double x = X[0];
      double y = X[1];
      
      if( dim == 3) {
         double z = X[2];
      }
      
      v = 0.0;

      v(0) = cos(x)
             - 2*kin_vis*cos(y)*sin(x)
             + ns_coeff * ( cos(x)*sin(x) );
      v(1) = cos(y)
             + 2*kin_vis*cos(x)*sin(y)
             + ns_coeff * ( cos(y)*sin(y) );
      if( dim == 3) {
         v(2) = 0;
      }
   };
}

double P_exact3(const Vector &X)
{
   double x = X[0];
   double y = X[1];

   double p = sin(x) + sin(y);

   return p;
}



void vZero(const Vector &X, Vector &v)
{
   v = 0.0;
}

double pZero(const Vector &X)
{
   return 0;
}

