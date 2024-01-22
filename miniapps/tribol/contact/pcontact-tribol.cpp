//                               Parallel contact example
//
// Compile with: make pcontact_driver
// sample run
// mpirun -np 6 ./pcontact_driver -sr 2 -pr 2

#include "mfem.hpp"
#include <fstream>
#include <iostream>
#include "ipsolver/ParIPsolver.hpp"

using namespace std;
using namespace mfem;


int main(int argc, char *argv[])
{
   Mpi::Init();
   int myid = Mpi::WorldRank();
   int num_procs = Mpi::WorldSize();
   Hypre::Init();
   // 1. Parse command-line options.
   const char *mesh_file = "meshes/merged.mesh";
   int order = 1;
   int sref = 0;
   int pref = 0;
   Array<int> attr;
   Array<int> m_attr;
   bool visualization = true;
   bool paraview = false;
   double linsolvertol = 1e-6;
   int relax_type = 8;
   double optimizer_tol = 1e-6;
   int optimizer_maxit = 10;

   OptionsParser args(argc, argv);
   args.AddOption(&mesh_file, "-m", "--mesh",
                  "Mesh file to use.");
   args.AddOption(&attr, "-at", "--attributes-surf",
                  "Attributes of boundary faces on contact surface for mesh 2.");
   args.AddOption(&sref, "-sr", "--serial-refinements",
                  "Number of uniform refinements.");
   args.AddOption(&pref, "-pr", "--parallel-refinements",
                  "Number of uniform refinements.");
   args.AddOption(&linsolvertol, "-stol", "--solver-tol",
                  "Linear Solver Tolerance.");
   args.AddOption(&optimizer_tol, "-otol", "--optimizer-tol",
                  "Interior Point Solver Tolerance.");
   args.AddOption(&optimizer_maxit, "-omaxit", "--optimizer-maxit",
                  "Interior Point Solver maximum number of iterations.");
   args.AddOption(&relax_type, "-rt", "--relax-type",
                  "Selection of Smoother for AMG");
   args.AddOption(&visualization, "-vis", "--visualization", "-no-vis",
                  "--no-visualization",
                  "Enable or disable GLVis visualization.");
   args.AddOption(&paraview, "-paraview", "--paraview", "-no-paraview",
                  "--no-paraview",
                  "Enable or disable ParaView visualization.");
   args.Parse();
   if (!args.Good())
   {
      if (myid == 0)
      {
         args.PrintUsage(cout);
      }
      return 1;
   }
   if (myid == 0)
   {
      args.PrintOptions(cout);
   }

   Mesh * mesh = new Mesh(mesh_file,1);
   for (int i = 0; i<sref; i++)
   {
      mesh->UniformRefinement();
   }

   ParMesh * pmesh = new ParMesh(MPI_COMM_WORLD,*mesh);

   for (int i = 0; i<pref; i++)
   {
      pmesh->UniformRefinement();
   }

   MFEM_VERIFY(pmesh->GetNE(), "Empty partition pmesh");

   Array<int> ess_bdr_attr;
   ess_bdr_attr.Append(2);
   ess_bdr_attr.Append(6);
   ParElasticityProblem * prob = new ParElasticityProblem(pmesh,ess_bdr_attr,
                                                          order);

   Vector lambda(prob->GetMesh()->attributes.Max()); lambda = 57.6923076923;
   Vector mu(prob->GetMesh()->attributes.Max()); mu = 38.4615384615;
   prob->SetLambda(lambda); prob->SetMu(mu);

#ifdef MFEM_USE_TRIBOL
   ParContactProblemTribol contact_tribol(prob);
   QPOptParContactProblemTribol qpopt(&contact_tribol);
   int numconstr = contact_tribol.GetGlobalNumConstraints();
   ParInteriorPointSolver optimizer(&qpopt);
   optimizer.SetTol(optimizer_tol);
   optimizer.SetMaxIter(optimizer_maxit);

   int linsolver = 2;
   optimizer.SetLinearSolver(linsolver);
   optimizer.SetLinearSolveTol(linsolvertol);
   optimizer.SetLinearSolveRelaxType(relax_type);
   ParGridFunction x = prob->GetDisplacementGridFunction();
   Vector x0 = x.GetTrueVector();
   int ndofs = x0.Size();
   Vector xf(ndofs); xf = 0.0;
   optimizer.Mult(x0, xf);
   double Einitial = contact_tribol.E(x0);
   double Efinal = contact_tribol.E(xf);
   Array<int> & CGiterations = optimizer.GetCGIterNumbers();
   int gndofs = prob->GetGlobalNumDofs();
   if (Mpi::Root())
   {
      mfem::out << endl;
      mfem::out << " Initial Energy objective       = " << Einitial << endl;
      mfem::out << " Final Energy objective         = " << Efinal << endl;
      mfem::out << " Global number of dofs          = " << gndofs << endl;
      mfem::out << " Global number of constraints   = " << numconstr << endl;
      mfem::out << " Optimizer number of iterations = " << CGiterations.Size() << endl
                ;
      mfem::out << " CG iteration numbers           = " ;
      CGiterations.Print(mfem::out, CGiterations.Size());
   }

   // MFEM_VERIFY(optimizer.GetConverged(),
   //             "Interior point solver did not converge.");


   if (visualization || paraview)
   {
      ParFiniteElementSpace * fes = prob->GetFESpace();
      ParMesh * pmesh = fes->GetParMesh();

      Vector X_new(xf.GetData(),fes->GetTrueVSize());

      ParGridFunction x_gf(fes);

      x_gf.SetFromTrueDofs(X_new);

      pmesh->MoveNodes(x_gf);

      if (paraview)
      {
         ParaViewDataCollection paraview_dc("QPContactBodyTribol", pmesh);
         paraview_dc.SetPrefixPath("ParaView");
         paraview_dc.SetLevelsOfDetail(1);
         paraview_dc.SetDataFormat(VTKFormat::BINARY);
         paraview_dc.SetHighOrderOutput(true);
         paraview_dc.SetCycle(0);
         paraview_dc.SetTime(0.0);
         paraview_dc.RegisterField("Body", &x_gf);
         paraview_dc.Save();
      }


      if (visualization)
      {
         char vishost[] = "localhost";
         int visport = 19916;

         socketstream sol_sock(vishost, visport);
         sol_sock.precision(8);
         sol_sock << "parallel " << num_procs << " " << myid << "\n"
                  << "solution\n" << *pmesh << x_gf << flush;
      }
   }
#endif

   delete prob;
   delete pmesh;
   delete mesh;
   return 0;
}