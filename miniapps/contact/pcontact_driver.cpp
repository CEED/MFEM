//                               Parallel contact example
//
// Compile with: make pcontact_driver
// sample run
// mpirun -np 6 ./pcontact_driver -sr 2 -pr 2

#include "mfem.hpp"
#include <fstream>
#include <iostream>
#include "ipsolver/IPsolver.hpp"

using namespace std;
using namespace mfem;


int main(int argc, char *argv[])
{
   Mpi::Init();
   int myid = Mpi::WorldRank();
   int num_procs = Mpi::WorldSize();
   Hypre::Init();
   // 1. Parse command-line options.
   const char *mesh_file1 = "meshes/block1.mesh";
   const char *mesh_file2 = "meshes/rotatedblock2.mesh";
   int order = 1;
   int sref = 0;
   int pref = 0;
   Array<int> attr;
   Array<int> m_attr;
   bool visualization = true;
   bool paraview = false;
   double linsolvertol = 1e-12;
   int relax_type = 8;
   double optimizer_tol = 1e-10;
   bool elasticity_options = false;

   OptionsParser args(argc, argv);
   args.AddOption(&mesh_file1, "-m1", "--mesh1",
                  "First mesh file to use.");
   args.AddOption(&mesh_file2, "-m2", "--mesh2",
                  "Second mesh file to use.");
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
   args.AddOption(&relax_type, "-rt", "--relax-type",
                  "Selection of Smoother for AMG");
   args.AddOption(&elasticity_options, "-elast", "--elasticity-options",
                  "-no-elast",
                  "--no-elasticity-options",
                  "Enable or disable Elasticity options for the AMG preconditioner.");
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

   ElasticityProblem * prob1 = nullptr;
   ElasticityProblem * prob2 = nullptr;

   ParMesh * pmesh1 = nullptr;
   ParMesh * pmesh2 = nullptr;
   ParMesh * pmesh  = nullptr;

   bool own_mesh = true;
   if (elasticity_options) { own_mesh = true; }

   if (own_mesh)
   {
      Mesh * mesh1 = new Mesh(mesh_file1,1);
      Mesh * mesh2 = new Mesh(mesh_file2,1);

      for (int i = 0; i<sref; i++)
      {
         mesh1->UniformRefinement();
         mesh2->UniformRefinement();
      }

      int * part1 = mesh1->GeneratePartitioning(num_procs);
      int * part2 = mesh2->GeneratePartitioning(num_procs);

      Array<int> part_array1(part1, mesh1->GetNE());
      Array<int> part_array2(part2, mesh2->GetNE());

      for (int i = 0; i<mesh1->GetNE(); i++)
      {
         mesh1->SetAttribute(i,1);
      }
      mesh1->SetAttributes();
      for (int i = 0; i<mesh2->GetNE(); i++)
      {
         mesh2->SetAttribute(i,2);
      }
      mesh2->SetAttributes();

      Mesh * mesh_array[2];
      mesh_array[0] = mesh1;
      mesh_array[1] = mesh2;
      Mesh * mesh = new Mesh(mesh_array, 2);



      Array<int> part_array;
      part_array.Append(part_array1);
      part_array.Append(part_array2);

      pmesh = new ParMesh(MPI_COMM_WORLD, *mesh, part_array.GetData());
      MFEM_VERIFY(pmesh->GetNE(), "Empty partition");
      for (int i = 0; i<pref; i++)
      {
         pmesh->UniformRefinement();
      }

      Array<int> attr1(1); attr1 = 1;
      Array<int> attr2(1); attr2 = 2;


      pmesh1 = new ParMesh(MPI_COMM_WORLD,*mesh1, part1);
      pmesh2 = new ParMesh(MPI_COMM_WORLD,*mesh2, part2);

      for (int i = 0; i<pref; i++)
      {
         pmesh1->UniformRefinement();
         pmesh2->UniformRefinement();
      }

      MFEM_VERIFY(pmesh1->GetNE(), "Empty partition mesh1");
      MFEM_VERIFY(pmesh2->GetNE(), "Empty partition mesh2");

      prob1 = new ElasticityProblem(pmesh1,order);
      prob2 = new ElasticityProblem(pmesh2,order);
   }
   else
   {
      prob1 = new ElasticityProblem(MPI_COMM_WORLD, mesh_file1,sref,pref,order);
      prob2 = new ElasticityProblem(MPI_COMM_WORLD, mesh_file2,sref,pref,order);
   }
   cout << "constructed ElasticityProblem\n";

   Vector lambda1(prob1->GetMesh()->attributes.Max()); lambda1 = 57.6923076923;
   Vector mu1(prob1->GetMesh()->attributes.Max()); mu1 = 38.4615384615;
   Vector lambda2(prob2->GetMesh()->attributes.Max()); lambda2 = 57.6923076923;
   Vector mu2(prob2->GetMesh()->attributes.Max()); mu2 = 38.4615384615;

   prob1->SetLambda(lambda1); prob1->SetMu(mu1);
   prob2->SetLambda(lambda2); prob2->SetMu(mu2);

   ContactProblem contact(prob1,prob2);
   InteriorPointSolver optimizer(&contact);
   ParFiniteElementSpace *pfes = nullptr;
   if (pmesh)
   {
      pfes = new ParFiniteElementSpace(pmesh,prob1->GetFECol(),3,Ordering::byVDIM);
      pmesh->SetNodalFESpace(pfes);
      //if (elasticity_options)
      //{
      //   optimizer.SetFiniteElementSpace(pfes);
      //}
   }

   optimizer.SetTol(optimizer_tol);
   optimizer.SetMaxIter(30);

   //int linsolver = 2;
   //optimizer.SetLinearSolver(linsolver);
   //optimizer.SetLinearSolveTol(linsolvertol);
   //optimizer.SetLinearSolveRelaxType(relax_type);

   ParGridFunction x1 = prob1->GetDisplacementParGridFunction();
   ParGridFunction x2 = prob2->GetDisplacementParGridFunction();

   int ndofs1 = prob1->GetNumTDofs();
   int ndofs2 = prob2->GetNumTDofs();
   int gndofs1 = prob1->GetGlobalNumDofs();
   int gndofs2 = prob2->GetGlobalNumDofs();
   int ndofs = ndofs1 + ndofs2;

   Vector X1 = x1.GetTrueVector();
   Vector X2 = x2.GetTrueVector();

   Vector x0(ndofs); x0 = 0.0;
   x0.SetVector(X1,0);
   x0.SetVector(X2,X1.Size());

   Vector xf(ndofs); xf = 0.0;
   optimizer.SetLinearSolveTol(1.e-14);
   optimizer.Mult(x0, xf);

   double Einitial = contact.E(x0);
   double Efinal = contact.E(xf);
   //Array<int> & CGiterations = optimizer.GetCGIterNumbers();
   //if (Mpi::Root())
   //{
   //   mfem::out << endl;
   //   mfem::out << " Initial Energy objective     = " << Einitial << endl;
   //   mfem::out << " Final Energy objective       = " << Efinal << endl;
   //   mfem::out << " Global number of dofs        = " << gndofs1 + gndofs2 << endl;
   //   mfem::out << " Global number of constraints = " << numconstr << endl;
   //   mfem::out << " CG iteration numbers         = " ;
   //   CGiterations.Print(mfem::out, CGiterations.Size());
   //}

   MFEM_VERIFY(optimizer.GetConverged(),
               "Interior point solver did not converge.");

   if (visualization || paraview)
   {
      ParFiniteElementSpace * fes1 = dynamic_cast<ParFiniteElementSpace *>(prob1->GetFESpace());
      ParFiniteElementSpace * fes2 = dynamic_cast<ParFiniteElementSpace *>(prob2->GetFESpace());

      ParMesh * mesh1 = fes1->GetParMesh();
      ParMesh * mesh2 = fes2->GetParMesh();

      Vector X1_new(xf.GetData(),fes1->GetTrueVSize());
      Vector X2_new(&xf.GetData()[fes1->GetTrueVSize()],fes2->GetTrueVSize());

      //contact.ComputeGapFunctionAndDerivatives(X1_new, X2_new);
      //Vector gxf;
      //contact.g(xf, gxf);
      //for(int i = 0; i < gxf.Size(); i++)
      //{
      //  mfem::out << "g(xf)_i = " << gxf(i) << endl;
      //}

      ParGridFunction x1_gf(fes1);
      ParGridFunction x2_gf(fes2);

      x1_gf.SetFromTrueDofs(X1_new);
      x2_gf.SetFromTrueDofs(X2_new);

      mesh1->MoveNodes(x1_gf);
      mesh2->MoveNodes(x2_gf);


      ParGridFunction * xgf = nullptr;
      if (pmesh)
      {
         xgf = new ParGridFunction(pfes);
         *xgf = 0.0;
         // auto map1 = ParSubMesh::CreateTransferMap(x1_gf, *xgf);
         // auto map2 = ParSubMesh::CreateTransferMap(x2_gf, *xgf);
         // map1.Transfer(x1_gf,*xgf);
         // map2.Transfer(x2_gf,*xgf);
         xgf->SetVector(x1_gf,0);
         xgf->SetVector(x2_gf,x1_gf.Size());
         ParFiniteElementSpace * pfes1 = x1_gf.ParFESpace();
         ParFiniteElementSpace * pfes2 = x2_gf.ParFESpace();
         pmesh->MoveNodes(*xgf);
         
      }
      if (paraview)
      {
         ParaViewDataCollection paraview_dc1("QPContactBody1", mesh1);
         paraview_dc1.SetPrefixPath("ParaView");
         paraview_dc1.SetLevelsOfDetail(1);
         paraview_dc1.SetDataFormat(VTKFormat::BINARY);
         paraview_dc1.SetHighOrderOutput(true);
         paraview_dc1.SetCycle(0);
         paraview_dc1.SetTime(0.0);
         paraview_dc1.RegisterField("Body1", &x1_gf);
         paraview_dc1.Save();

         ParaViewDataCollection paraview_dc2("QPContactBody2", mesh2);
         paraview_dc2.SetPrefixPath("ParaView");
         paraview_dc2.SetLevelsOfDetail(1);
         paraview_dc2.SetDataFormat(VTKFormat::BINARY);
         paraview_dc2.SetHighOrderOutput(true);
         paraview_dc2.SetCycle(0);
         paraview_dc2.SetTime(0.0);
         paraview_dc2.RegisterField("Body2", &x2_gf);
         paraview_dc2.Save();

         if (pmesh)
         {
            ParaViewDataCollection paraview_dc("QPContact2Bodies", pmesh);
            paraview_dc.SetPrefixPath("ParaView");
            paraview_dc.SetLevelsOfDetail(1);
            paraview_dc.SetDataFormat(VTKFormat::BINARY);
            paraview_dc.SetHighOrderOutput(true);
            paraview_dc.SetCycle(0);
            paraview_dc.SetTime(0.0);
            paraview_dc.RegisterField("BothBodies", xgf);
            paraview_dc.Save();
         }
      }



      if (visualization)
      {
         char vishost[] = "localhost";
         int visport = 19916;

         {
            socketstream sol_sock(vishost, visport);
            sol_sock.precision(8);
            sol_sock << "parallel " << 2*num_procs << " " << myid << "\n"
                     << "solution\n" << *mesh1 << x1_gf << flush;
         }
         {
            socketstream sol_sock(vishost, visport);
            sol_sock.precision(8);
            sol_sock << "parallel " << 2*num_procs << " " << myid+num_procs << "\n"
                     << "solution\n" << *mesh2 << x2_gf << flush;
         }
         if (pmesh)
         {
            socketstream sol_sock1(vishost, visport);
            sol_sock1.precision(8);
            sol_sock1 << "parallel " << num_procs << " " << myid << "\n"
                      << "solution\n" << *pmesh << *xgf << flush;
         }
      }
      delete xgf;
   }

   delete pfes;
   delete prob2;
   delete prob1;
   delete pmesh2;
   delete pmesh1;

   return 0;
}