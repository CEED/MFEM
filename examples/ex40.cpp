#include "mfem.hpp"
#include "ex40.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>

using namespace std;
using namespace mfem;

// Choice for the problem setup. The fluid velocity, initial condition and
// inflow boundary condition are chosen based on this parameter.
int problem;

// Initial condition
real_t u0_function(const Vector &x);


// Mesh bounding box
Vector bb_min, bb_max;

void Limit(GridFunction &u, GridFunction &uavg, IntegrationRule &solpts,
           IntegrationRule &samppts, ElementOptimizer * opt, int dim);

void Test(ElementOptimizer &opt, GridFunction &u, int nspts);

int main(int argc, char *argv[])
{
   // 1. Parse command-line options.
   problem = 0;
   const char *mesh_file = "../data/periodic-hexagon.mesh";
   int ref_levels = 4;
   int order = 3;
   const char *device_config = "cpu";
   int ode_solver_type = 4;
   bool visualization = true;
   bool visit = false;
   bool paraview = false;
   bool binary = false;

   int precision = 8;
   cout.precision(precision);

   OptionsParser args(argc, argv);
   args.AddOption(&mesh_file, "-m", "--mesh",
                  "Mesh file to use.");
   args.AddOption(&ref_levels, "-r", "--refine",
                  "Number of times to refine the mesh uniformly.");
   args.AddOption(&order, "-o", "--order",
                  "Order (degree) of the finite elements.");
   args.AddOption(&visualization, "-vis", "--visualization", "-no-vis",
                  "--no-visualization",
                  "Enable or disable GLVis visualization.");
   args.AddOption(&visit, "-visit", "--visit-datafiles", "-no-visit",
                  "--no-visit-datafiles",
                  "Save data files for VisIt (visit.llnl.gov) visualization.");
   args.AddOption(&paraview, "-paraview", "--paraview-datafiles", "-no-paraview",
                  "--no-paraview-datafiles",
                  "Save data files for ParaView (paraview.org) visualization.");

   args.Parse();
   if (!args.Good())
   {
      args.PrintUsage(cout);
      return 1;
   }
   args.PrintOptions(cout);

   Device device(device_config);
   device.Print();

   // 2. Read the mesh from the given mesh file. We can handle geometrically
   //    periodic meshes in this code.
   Mesh mesh(mesh_file, 1, 1);
   int dim = mesh.Dimension();


   // 4. Refine the mesh to increase the resolution. In this example we do
   //    'ref_levels' of uniform refinement, where 'ref_levels' is a
   //    command-line parameter. If the mesh is of NURBS type, we convert it to
   //    a (piecewise-polynomial) high-order mesh.
   for (int lev = 0; lev < ref_levels; lev++)
   {
      mesh.UniformRefinement();
   }
   if (mesh.NURBSext)
   {
      mesh.SetCurvature(max(order, 1));
   }
   mesh.GetBoundingBox(bb_min, bb_max, max(order, 1));

   // 5. Define the discontinuous DG finite element space of the given
   //    polynomial order on the refined mesh.
   DG_FECollection fec(order, dim, BasisType::GaussLobatto);
   FiniteElementSpace fes(&mesh, &fec);

   cout << "Number of unknowns: " << fes.GetVSize() << endl;

   // 6. Set up and assemble the bilinear and linear forms corresponding to the
   //    DG discretization. The DGTraceIntegrator involves integrals over mesh
   //    interior faces.
   FunctionCoefficient u0(u0_function);

   // 7. Define the initial conditions, save the corresponding grid function to
   //    a file and (optionally) save data in the VisIt format and initialize
   //    GLVis visualization.
   GridFunction u(&fes);
   u.ProjectCoefficient(u0);

   {
      ofstream omesh("ex40.mesh");
      omesh.precision(precision);
      mesh.Print(omesh);
      ofstream osol("ex40-init.gf");
      osol.precision(precision);
      u.Save(osol);
   }
   

   // 8. Setup FE space and grid function for element-wise mean.
   L2_FECollection uavg_fec(0, dim);
   FiniteElementSpace uavg_fes(&mesh, &uavg_fec);
   GridFunction uavg(&uavg_fes);

   // 9. Generate modal basis transformation and pre-compute Vandermonde matrix.
   Geometry::Type gtype = mesh.GetElementGeometry(0);
   ModalBasis MB = ModalBasis(fec, gtype, order, dim);

   // 10 .Setup spatial optimization algorithmic for constraint functionals.
   ElementOptimizer opt = ElementOptimizer(&MB, dim);
   Test(opt, u, 10); 

   // 11. Perform limiting based on sampling points (quadrature points) and solution points.
   IntegrationRule samppts = IntRules.Get(gtype, 2*order);
   Limit(u, uavg, MB.solpts, samppts, &opt, dim);

   Test(opt, u, 10);

   socketstream sout;
   if (visualization)
   {
      char vishost[] = "localhost";
      int  visport   = 19916;
      sout.open(vishost, visport);
      if (!sout)
      {
         cout << "Unable to connect to GLVis server at "
              << vishost << ':' << visport << endl;
         visualization = false;
         cout << "GLVis visualization disabled.\n";
      }
      else
      {
         sout.precision(precision);
         sout << "solution\n" << mesh << u;
         sout << "pause\n";
         sout << flush;
         cout << "GLVis visualization paused."
              << " Press space (in the GLVis window) to resume it.\n";
      }
   }

   // 12. Save the final solution. This output can be viewed later using GLVis:
   //    "glvis -m ex40.mesh -g ex40-final.gf".
   {
      ofstream osol("ex40-final.gf");
      osol.precision(precision);
      u.Save(osol);
   }

   return 0;
}

void Limit(GridFunction &u, GridFunction &uavg, IntegrationRule &solpts, 
           IntegrationRule &samppts, ElementOptimizer * opt, int dim) {
   Vector x0(dim), xi(dim);
   Vector u_elem = Vector();

   // Compute element-wise averages
   u.GetElementAverages(uavg);

   // Loop through elements and limit if necessary
   for (int i = 0; i < u.FESpace()->GetNE(); i++) {
      // Get local element DOF values
      u.GetElementDofValues(i, u_elem);

      real_t alpha = 0.0;
      bool skip_opt = false;

      // Loop through constraint functionals
      for (int j = 0; j < opt->ncon; j++) {
         opt->SetCostFunction(j);

         // Check if element-wise mean is on constaint boundary
         if (opt->g(uavg(i)) < opt->eps) {
            // Set maximum limiting factor and skip optimization
            skip_opt = true;
            alpha = 1.0;
            break;
         }
      }

      if (!skip_opt) {
         // Set element-wise solution and convert to modal form
         opt->MB->SetSolution(u_elem);

         // Loop through constraint functionals
         for (int j = 0; j < opt->ncon; j++) {
            // Set constraint functional and calculate element-wise mean terms
            opt->SetCostFunction(j);
            opt->SetGbar(uavg(i));

            // Compute discrete minimum (hstar) and location (x0) over nodal points
            real_t hstar = infinity();

            // Loop through solution nodes
            for (int k = 0; k < solpts.GetNPoints(); k++) {
               real_t hi = opt->h(u_elem(k));
               if (hi < hstar) {
                  hstar = hi;
                  solpts.IntPoint(k).Get(xi, dim);
                  x0 = xi;
               }
            }
            // Loop through other sampling nodes (typically quadrature nodes)
            for (int k = 0; k < samppts.GetNPoints(); k++) {
               samppts.IntPoint(k).Get(xi, dim);
               // Compute solution using modal basis
               real_t ui = opt->MB->Eval(xi); 
               real_t hi = opt->h(ui);
               if (hi < hstar) {
                  hstar = hi;
                  x0 = xi;
               }
            }

            // Use optimizer to find minima of h(u(x)) within element 
            // using x0 as the starting point
            real_t hss = opt->Optimize(x0);

            // Track maximum limiting factor
            alpha = max(alpha, -hss);
         }
      }

      // Perform convex limiting towards element-wise mean using maximum limiting factor
      for (int j = 0; j < u_elem.Size(); j++) {
         u_elem(j) = (1 - alpha)*u_elem(j) + alpha*uavg(i);
      }
      u.SetElementDofValues(i, u_elem); 
   } 
}

// Initial condition for solid body rotation problem, consisting of a notched cylinder,
// sharp cone, and Cosinusoidal hump.
real_t u0_function(const Vector &x) {
   int dim = x.Size();

   // Map to the reference [-1,1] domain
   Vector X(dim);
   for (int i = 0; i < dim; i++)
   {
      real_t center = (bb_min[i] + bb_max[i]) * 0.5;
      X(i) = 2 * (x(i) - center) / (bb_max[i] - bb_min[i]);
   }

   constexpr real_t r2 = pow(0.3, 2.0);
   // Notched cylinder
   if ((pow(X(0), 2.0) + pow(X(1) - 0.5, 2.0) <= r2) && !(abs(X(0)) < 0.05 && abs(X(1) - 0.45) < 0.25)) {
      return 1.0;
   }
   // Cosinusoidal hump
   else if (pow(X(0) + 0.5, 2.0) + pow(X(1), 2.0) <= r2) {
      return 0.25*(1 + cos(M_PI*sqrt(pow(X(0) + 0.5, 2.0) + pow(X(1), 2.0))/0.3));
   }
   // Sharp cone
   else if (pow(X(0), 2.0) + pow(X(1) + 0.5, 2.0) <= r2) {
      return 1 - sqrt(pow(X(0), 2.0) + pow(X(1) + 0.5, 2.0))/0.3;
   }
   else {
      return 0.0;
   }
}

// Constraint functionals for enforcing maximum principle: u(x, t) \in [0,1]
inline real_t g1(real_t u) {return u;}
inline real_t g2(real_t u) {return 1.0 - u;}


void Test(ElementOptimizer &opt, GridFunction &u, int nspts) {
   real_t umin = infinity();
   real_t umax = -infinity();
   Vector xs(2);
   Vector u_elem = Vector();
   for (int i = 0; i < u.FESpace()->GetNE(); i++) {
      // Get local element DOF values
      u.GetElementDofValues(i, u_elem);

      // Set element-wise solution and convert to modal form
      opt.MB->SetSolution(u_elem);

      for (int j = 0; j < nspts; j++) {
         real_t xx = (j)/(nspts-1);
         xs(0) = xx;
         for (int k = 0; k < nspts; k++) {
            real_t yy = (k)/(nspts-1);
            xs(1) = yy;
            real_t uu = opt.MB->Eval(xs);
            umin = min(umin, uu);
            umax = max(umax, uu);
         }
      }
   }
   cout << umin << " " << umax << endl;
}