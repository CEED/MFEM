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
   int num_procs = Mpi::WorldSize();
   int myid = Mpi::WorldRank();
   Hypre::Init();
   // 1. Parse command-line options.
   const char *mesh_file1 = "meshes/block1.mesh";
   const char *mesh_file2 = "meshes/rotatedblock2.mesh";
   // const char *mesh_file2 = "meshes/block2.mesh";
   int order = 1;
   int sref = 0;
   int pref = 0;
   Array<int> attr;
   Array<int> m_attr;

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

   ParElasticityProblem prob1(MPI_COMM_WORLD,mesh_file1,sref,pref,order); 
   ParElasticityProblem prob2(MPI_COMM_WORLD,mesh_file2,sref,pref,order); 

   ParContactProblem contact(&prob1,&prob2);
   QPOptParContactProblem qpopt(&contact);

   ParInteriorPointSolver optimizer(&qpopt);

   optimizer.SetTol(1e-6);
   optimizer.SetMaxIter(50);
   int linsolver = 2;
   optimizer.SetLinearSolver(linsolver);

   ParGridFunction x1 = prob1.GetDisplacementGridFunction();
   ParGridFunction x2 = prob2.GetDisplacementGridFunction();

   int ndofs1 = prob1.GetNumTDofs();
   int ndofs2 = prob2.GetNumTDofs();
   int ndofs = ndofs1 + ndofs2;

   Vector X1 = x1.GetTrueVector();
   Vector X2 = x2.GetTrueVector();

   Vector x0(ndofs); x0 = 0.0;
   x0.SetVector(X1,0);
   x0.SetVector(X2,X1.Size());

   Vector xf(ndofs); xf = 0.0;
   optimizer.Mult(x0, xf);

   MFEM_VERIFY(optimizer.GetConverged(), "Interior point solver did not converge.");
   double Einitial = contact.E(x0);
   double Efinal = contact.E(xf);

   if (Mpi::Root())
   {
      cout << "Energy objective at initial point = " << Einitial << endl;
      cout << "Energy objective at QP optimizer = " << Efinal << endl;
   }

   ParFiniteElementSpace * fes1 = prob1.GetFESpace();
   ParFiniteElementSpace * fes2 = prob2.GetFESpace();
   
   ParMesh * mesh1 = fes1->GetParMesh();
   ParMesh * mesh2 = fes2->GetParMesh();

   Vector X1_new(xf.GetData(),fes1->GetTrueVSize());
   Vector X2_new(&xf.GetData()[fes1->GetTrueVSize()],fes2->GetTrueVSize());

   ParGridFunction x1_gf(fes1);
   ParGridFunction x2_gf(fes2);

   x1_gf.SetFromTrueDofs(X1_new);
   x2_gf.SetFromTrueDofs(X2_new);

   mesh1->MoveNodes(x1_gf);
   mesh2->MoveNodes(x2_gf);

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

   return 0;
}
