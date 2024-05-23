// Copyright (c) 2010-2023, Lawrence Livermore National Security, LLC. Produced
// at the Lawrence Livermore National Laboratory. All Rights reserved. See files
// LICENSE and NOTICE for details. LLNL-CODE-806117.
//
// This file is part of the MFEM library. For more information and source code
// availability visit https://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the BSD-3 license. We welcome feedback and contributions, see file
// CONTRIBUTING.md for details.
//
//    ------------------------------------------------------------------
//      Fitting of Selected Mesh Nodes to Specified Physical Positions
//    ------------------------------------------------------------------
//
// This example fits a selected set of the mesh nodes to given physical
// positions while maintaining a valid mesh with good quality.
//
// Sample runs:
//   mpirun -np 4 tmop-tangent -rs 1 -m square01.mesh -o 1 -gS 1
//   mpirun -np 4 tmop-tangent -rs 1 -m rectangle01.mesh -o 1 -gS 1
// NOTE: Need to change the width, depth and coordinates of the center in the Square.cpp file depending on the geometry.

#include "mfem.hpp"
#include "parametrized_tmop_integrator.hpp"

using namespace mfem;
using namespace std;

char vishost[] = "localhost";
int  visport   = 19916;
int  wsize     = 350;

int main (int argc, char *argv[])
{
   // Initialize MPI.
   Mpi::Init();
   int myid = Mpi::WorldRank();
   const char *mesh_file = "square01.mesh";
   int rs_levels     = 1;
   int mesh_poly_deg = 2;
   int quad_order    = 5;
   bool glvis        = true;
   int geometricShape = 1;
   // Parse command-line options.
   OptionsParser args(argc, argv);
   args.AddOption(&mesh_file, "-m", "--mesh",
		 "Mesh file to use.");
   args.AddOption(&rs_levels, "-rs", "--refine-serial",
		 "Number of times to refine the mesh uniformly in serial.");
   args.AddOption(&mesh_poly_deg, "-o", "--order",
		 "Polynomial degree of mesh finite element space.");
   args.AddOption(&quad_order, "-qo", "--quad_order",
		 "Order of the quadrature rule.");
   args.AddOption(&glvis, "-vis", "--visualization", "-no-vis",
		 "--no-visualization",
		 "Enable or disable GLVis visualization.");
   args.AddOption(&geometricShape, "-gS", "--geometricShape",
		 "Shape of parametrized geometry");
   args.Parse();
   if (!args.Good())
   {
      if (myid == 0) { args.PrintUsage(cout); }
      return 1;
   }
   if (myid == 0) { args.PrintOptions(cout); }

   // Read and refine the mesh.
   Mesh *mesh = new Mesh(mesh_file, 1, 1, false);
   for (int lev = 0; lev < rs_levels; lev++) { mesh->UniformRefinement(); }
   ParMesh pmesh(MPI_COMM_WORLD, *mesh);
   delete mesh;
   const int dim = pmesh.Dimension();

   // Setup mesh curvature and GridFunction that stores the coordinates.
   FiniteElementCollection *fec_mesh;
   if (mesh_poly_deg <= 0)
   {
      fec_mesh = new QuadraticPosFECollection;
      mesh_poly_deg = 2;
   }
   else { fec_mesh = new H1_FECollection(mesh_poly_deg, dim); }
   ParFiniteElementSpace pfes_mesh(&pmesh, fec_mesh, dim);
   pmesh.SetNodalFESpace(&pfes_mesh);
   ParGridFunction coord(&pfes_mesh);
   pmesh.SetNodalGridFunction(&coord);
   ParGridFunction x0(coord);

   // Move the mesh nodes to have non-trivial problem.
   const int N = coord.Size() / 2;
   for (int i = 0; i < N; i++)
   {
      if (coord(i) < 1.0 - 1e-12)
      {
         double t = coord(i+N);
         coord(i) = coord(i) - 2 * t * 0.2 * coord(i);
      }
   }

   // Mark which nodes to move tangentially (attribute 1).
   Array<bool> fit_marker(pfes_mesh.GetNDofs());
   ParGridFunction fit_marker_vis_gf(&pfes_mesh);
   Array<int> vdofs;
   fit_marker = false;
   fit_marker_vis_gf = 0.0;
   for (int e = 0; e < pmesh.GetNBE(); e++)
   {
      const int attr = pmesh.GetBdrElement(e)->GetAttribute();
      if (attr != 1) { continue; }

      const int nd = pfes_mesh.GetBE(e)->GetDof();
      pfes_mesh.GetBdrElementVDofs(e, vdofs);
      for (int j = 0; j < nd; j++)
      {
         int j_x = vdofs[j];
         fit_marker[pfes_mesh.VDofToDof(j_x)] = true;
         fit_marker_vis_gf(j_x) = 1.0;
      }
   }

   // Visualize the selected nodes and their target positions.
   if (glvis)
   {
      socketstream vis1;
      common::VisualizeField(vis1, "localhost", 19916, fit_marker_vis_gf,
			     "Target positions (DOFS with value 1)",
			     0, 0, 400, 400, (dim == 2) ? "Rjm" : "");
   }

   // Fix the remaining boundaries.
   int n = 0;
   for (int i = 0; i < pmesh.GetNBE(); i++)
   {
      const int nd   = pfes_mesh.GetBE(i)->GetDof();
      const int attr = pmesh.GetBdrElement(i)->GetAttribute();
      if (attr == 1) { n += nd; }
      else           { n += nd * dim; }
   }
   Array<int> ess_vdofs(n);
   n = 0;
   for (int i = 0; i < pmesh.GetNBE(); i++)
   {
      const int nd = pfes_mesh.GetBE(i)->GetDof();
      const int attr = pmesh.GetBdrElement(i)->GetAttribute();
      pfes_mesh.GetBdrElementVDofs(i, vdofs);
      if (attr == 1)
      {
         // Fix y components.
         for (int j = 0; j < nd; j++)
         { ess_vdofs[n++] = vdofs[j+nd]; }
      }
      else
      {
         // Fix all components.
         for (int j = 0; j < vdofs.Size(); j++)
         { ess_vdofs[n++] = vdofs[j]; }
      }
   }

   AnalyticalSurface analyticalSurface(geometricShape, pfes_mesh, coord, pmesh);
   analyticalSurface.ConvertPhysicalCoordinatesToParametric(coord);

   // TMOP setup.
   TMOP_QualityMetric *metric;
   if (dim == 2) { metric = new TMOP_Metric_002; }
   else          { metric = new TMOP_Metric_302; }
   TargetConstructor target(TargetConstructor::IDEAL_SHAPE_UNIT_SIZE,
                            pfes_mesh.GetComm());
   auto integ = new ParametrizedTMOP_Integrator(metric, &target, nullptr);
   integ->EnableTangentialMovement(fit_marker, analyticalSurface);

   // Linear solver.
   MINRESSolver minres(pfes_mesh.GetComm());
   minres.SetMaxIter(100);
   minres.SetRelTol(1e-8);
   minres.SetAbsTol(0.0);

   // Nonlinear solver.
   ParNonlinearForm a(&pfes_mesh);
   a.SetEssentialVDofs(ess_vdofs);
   a.AddDomainIntegrator(integ);
   const IntegrationRule &ir =
    IntRules.Get(pfes_mesh.GetFE(0)->GetGeomType(), quad_order);
   TMOPNewtonSolver solver(pfes_mesh.GetComm(), ir, 0);
   solver.SetOperator(a);
   solver.SetPreconditioner(minres);
   solver.SetPrintLevel(1);
   solver.SetMaxIter(10000);
   solver.SetRelTol(1e-5);
   solver.SetAbsTol(0.0);

   // Solve.
   Vector b(0);
   coord.SetTrueVector();
   solver.Mult(b, coord.GetTrueVector());
   coord.SetFromTrueVector();
   analyticalSurface.ConvertParametricCoordinatesToPhysical(coord);
   if(glvis)
   {
      socketstream vis2;
      common::VisualizeMesh(vis2, "localhost", 19916, pmesh, "Final mesh",
                            400, 0, 400, 400, "me");
   }

   delete metric;
   return 0;
}