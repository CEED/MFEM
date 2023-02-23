// Copyright (c) 2010-2021, Lawrence Livermore National Security, LLC. Produced
// at the Lawrence Livermore National Laboratory. All Rights reserved. See files
// LICENSE and NOTICE for details. LLNL-CODE-806117.
//
// This file is part of the MFEM library. For more information and source code
// availability visit https://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the BSD-3 license. We welcome feedback and contributions, see file
// CONTRIBUTING.md for details.

// 3D flow over a cylinder benchmark example

//#include "navier_solver.hpp"
#include "navier_3d_brink_workflow.hpp"
#include "MeshOptSolver.hpp"
#include <fstream>
#include <ctime>
#include <cstdlib> 
#include <unistd.h>
#include "ascii.hpp"

using namespace mfem;
using namespace navier;

s_NavierContext ctx;
 //ctx.order = 2;
 //ctx.kin_vis = 0.1;
// ctx.t_final = 1.0;
// ctx.dt = 1e-4;

static int BoxIterator = 0;

void vel(const Vector &x, double t, Vector &u)
{
   double xi = x(0);
   double yi = x(1);

   u(0) = 0.2; //1e-4 / 5.0e-4;
   u(1) = 0.0;
}

void vel_0(const Vector &x, double t, Vector &u)
{
   double xi = x(0);
   double yi = x(1);

   u(0) = 0.0; //1e-4 / 5.0e-4;
   u(1) = 0.0;
}

double pres(const Vector &x, double t)
{
   return 0.0;
}


double BoxFunction(const Vector &x)
{
   
   std::vector<double> rightval_x = {0.2, 0.4, 0.6, 1.2, 0.2, 0.4, 0.6, 1.2 , 0.2, 0.4, 0.6, 1.2 };
   std::vector<double> leftVal_x = {0.1, 0.3, 0.5, 1.1, 0.1, 0.3, 0.5, 1.1 , 0.1, 0.3, 0.5, 1.1 };

   std::vector<double> lowerVal_y  = {0.1, 0.1, 0.1, 0.1, 0.3, 0.3, 0.3, 0.3, 0.8, 0.8, 0.8, 0.8 };
   std::vector<double> upperVal_y = {0.2, 0.2, 0.2, 0.2, 0.4, 0.4, 0.4, 0.4, 0.9, 0.9, 0.9, 0.9 };

   double val = 0.0;
   if( x[0] > leftVal_x[BoxIterator] && x[0] < rightval_x[BoxIterator] &&
       x[1] > lowerVal_y[BoxIterator] && x[1] < upperVal_y[BoxIterator] )
   {
      val = 1.0;
   }
   return val;
}

int main(int argc, char *argv[])
{
   MPI_Session mpi(argc, argv);

   //const char *mesh_file = "bar3d.msh";
   int run_id = 0;

   int serial_refinements = 1;

   double tForce_Magnitude = 1.0;


   OptionsParser args(argc, argv);
   // args.AddOption(&mesh_file, "-m", "--mesh",
   //                "Mesh file to use.");
   args.AddOption(&run_id, "-r", "--runID",
                  "Run ID.");

   args.AddOption(&tForce_Magnitude, "-fo", "--forceMag",
                  "Force Magnitude.");

   args.AddOption(&serial_refinements, "-ref", "--refinements",
                  "refinements.");

   args.Parse();

   bool tPerturbed = false;
   double PerturbationSize = 0.01;
   bool LoadSolVecFromFile = false;
   enum DensityCoeff::PatternType tGeometry = DensityCoeff::PatternType::Spheres;
   enum DensityCoeff::ProjectionType tProjectionType = DensityCoeff::ProjectionType::zero_one;
  
   double tLengthScale = 2.0e-2;
   double tThreshold = 0.3;
   double tDensity = 1.0e3;
   double tRefVelocity = 5.0e-4; 
   double tKinViscosity = 1.0e-6; 
   double ReynoldsNumber = tLengthScale * tRefVelocity / tKinViscosity;

   double tPoriosity = 0.1; 
   double tPermeability = 1.0e-3; 
   double tDa = tPoriosity / tPermeability; 
   double tBrinkmann = 10000; 

   s_NavierContext ctx;
   ctx.order = 2;
   ctx.kin_vis = 1.0 / ReynoldsNumber;
   ctx.t_final = 1.0;
   ctx.dt = 1e-4;

   //mesh->EnsureNCMesh(true);

   Mesh mesh("Flow2D_full.msh");

   for (int i = 0; i < serial_refinements; ++i)
   {
      mesh.UniformRefinement();
   }

   if (mpi.Root())
   {
      std::cout << "Number of elements: " << mesh.GetNE() << std::endl;
   }


   auto *pmesh = new ParMesh(MPI_COMM_WORLD, mesh);
   //delete mesh;
   if (mpi.Root())
   {
      std::cout << "Mesh of elements: " << pmesh->GetNE() << std::endl;
   }

   if( true )
   {

      mfem::ParaViewDataCollection mPvdc("2D_FullMesh", pmesh);
      mPvdc.SetCycle(0);
      mPvdc.SetTime(0.0);
      mPvdc.Save();
   }
 
   //----------------------------------------------------------
   {
      DensityCoeff* DensCoeff = new DensityCoeff;
      DensCoeff->SetThreshold(tThreshold);
      DensCoeff->SetPatternType(tGeometry);
      DensCoeff->SetProjectionType(tProjectionType);

      NavierSolver* mFlowsolver = new NavierSolver(pmesh, ctx.order, ctx.kin_vis);
      mFlowsolver->EnablePA(true);
      mFlowsolver->EnableNI(true);

      Array<int> domain_attr(pmesh->attributes.Max());
      domain_attr = 1;
      BrinkPenalAccel* Bp = new BrinkPenalAccel(pmesh->Dimension() );
      Bp->SetDensity(DensCoeff);
      Bp->SetBrinkmannPenalization(tBrinkmann);
      Bp->SetVel(mFlowsolver->GetCurrentVelocity());
      Bp->SetParams( 0.0, 0.0, 0.0, 0.0);
      mFlowsolver->AddAccelTerm(Bp,domain_attr);

      Array<int> attrVel(pmesh->bdr_attributes.Max());
      attrVel[1] = 1;
      mFlowsolver->AddVelDirichletBC(vel, attrVel);

      Array<int> attrVel1(pmesh->bdr_attributes.Max());
      attrVel1[0] = 1;
      mFlowsolver->AddVelDirichletBC(vel_0, attrVel1);

      Array<int> attrPres(pmesh->bdr_attributes.Max());
      attrPres[2] = 1;
      attrPres[3] = 1;
      mFlowsolver->AddPresDirichletBC(pres, attrPres);

      mFlowsolver->Setup(ctx.dt);


         std::cout<<"setup  done: "<<std::endl;
      ParGridFunction *u_gf = mFlowsolver->GetCurrentVelocity();
      ParGridFunction *p_gf = mFlowsolver->GetCurrentPressure();
      ParGridFunction *d_gf = new ParGridFunction(*p_gf);
      ParGridFunction *u_gf_dim = new ParGridFunction(*u_gf);
      ParGridFunction *p_gf_dim = new ParGridFunction(*p_gf);
      *u_gf_dim *= 1e-4;
      *p_gf_dim *= 1e-5;
      //DensCoeff->SetProjectionType(DensityCoeff::ProjectionType::continuous);
      d_gf->ProjectCoefficient(*DensCoeff);
      DensCoeff->SetProjectionType(DensityCoeff::ProjectionType::zero_one);

      ParaViewDataCollection* mPvdc = new ParaViewDataCollection("2DFull", pmesh);
      mPvdc->SetDataFormat(VTKFormat::BINARY32);
      mPvdc->SetHighOrderOutput(true);
      mPvdc->SetLevelsOfDetail(ctx.order);
      mPvdc->SetCycle(0);
      mPvdc->SetTime(0.0);
      mPvdc->RegisterField("velocity", u_gf);
      mPvdc->RegisterField("pressure", p_gf);
      mPvdc->RegisterField("velocity_dim", u_gf_dim);
      mPvdc->RegisterField("pressure_dim", p_gf_dim);
      mPvdc->RegisterField("density",  d_gf);
      mPvdc->Save();

      double t = 0.0;
      double dt = ctx.dt;
      double t_final = ctx.t_final;
      bool last_step = false;

      for (int step = 0; !last_step; ++step)
      {
         if (t + dt >= t_final - dt / 2)
         {
            last_step = true;
         }
         
         mFlowsolver->Step(t, dt, step);

         Bp->SetVel(mFlowsolver->GetCurrentVelocity());

         //mFlowsolver->GetCurrentVelocity()->norm2();
         //mBp->SetVel(flowsolver.GetProvisionalVelocity());

         if (step % 200 == 0)
         {
            mPvdc->SetCycle(step);
            mPvdc->SetTime(t);
            mPvdc->Save();
         }

         if (mpi.Root())
         {
            printf("%11s %11s\n", "Time", "dt");
            printf("%.5E %.5E\n", t, dt);
            fflush(stdout);
         }
      }

      mFlowsolver->PrintTimingData();

      std::string uOutputNameGF = "FinalVelGF";
      std::string pOutputNameGF = "FinalPresGF";

      u_gf->Save( uOutputNameGF.c_str() );
      p_gf->Save( pOutputNameGF.c_str() );

      double BoxVal = 0.0;

      int NumBox = 12;

      for( int Ik = 0; Ik < NumBox; Ik++)
      {

         mfem::Coefficient * tPreasusreCoeff = new mfem::GridFunctionCoefficient( p_gf);
         mfem::Coefficient * tIndicatorCoeff = new mfem::FunctionCoefficient( BoxFunction );
         mfem::Coefficient * tFinalCoeff = new mfem::ProductCoefficient( *tPreasusreCoeff, *tIndicatorCoeff);
   
         mfem::ParLinearForm BoxQILinearForm(p_gf->ParFESpace());
         BoxQILinearForm.AddDomainIntegrator( new mfem::DomainLFIntegrator( *tFinalCoeff ) );
         BoxQILinearForm.Assemble();
         BoxQILinearForm.ParallelAssemble();

         mfem::ParGridFunction OneGridGunction(p_gf->ParFESpace()); OneGridGunction =1.0;
         BoxVal = BoxQILinearForm * OneGridGunction;
         double TotalBoxVal = 0.0;

         MPI_Allreduce(
            &BoxVal,
            &TotalBoxVal, 
            1, 
            MPI_DOUBLE, 
            MPI_SUM,
            MPI_COMM_WORLD);

         if (mpi.Root())
         {
            std::cout<<"--------------------------------------------------"<<std::endl;
            std::cout<<"BoxVal Preassure_star: "<< Ik<<" | "<<TotalBoxVal<< " | Preassure: "<< TotalBoxVal* tDensity * std::pow(tRefVelocity ,2)<<std::endl;
            std::cout<<"--------------------------------------------------"<<std::endl;
         }

         BoxIterator = Ik + 1;
      }

      // FiniteElementSpace *fes = u_gf->FESpace();
      // int vdim = fes->GetVDim();

      // initilaize integradl of velozity vector
      // Vector tVelVal(vdim);
      // tVelVal = 0.0;

      // double tVolume = 0.0;

      // for (int e = 0; e < fes->GetNE(); ++e)
      // {
      //    const FiniteElement *fe = fes->GetFE(e);
      //    const IntegrationRule &ir = IntRules.Get(fe->GetGeomType(),
      //                                          fe->GetOrder());
      //    ElementTransformation *tr = fes->GetElementTransformation(e);

      //    for (int i = 0; i < ir.GetNPoints(); ++i)
      //    {
      //       const IntegrationPoint &ip = ir.IntPoint(i);
      //       tr->SetIntPoint(&ip);


      //       double w = tr->Weight() * ip.weight;

      //       Vector tVal;

      //       u_gf->GetVectorValue( e, ip, tVal);

      //       tVal *= w;
      //       tVelVal += tVal;

      //       tVolume += w;
      //    }

      // }

      // double tTotalVol;
      // MPI_Allreduce(&tVolume, &tTotalVol, 1, MPI_DOUBLE, MPI_SUM,
      //             pmesh->GetComm());

      // for( int Ik = 0; Ik < vdim; Ik ++)
      // {
      //    double tVal = tVelVal(Ik); 
      //    double tTotalVal;

      //    MPI_Allreduce(
      //       &tVal,
      //       &tTotalVal, 
      //       1, 
      //       MPI_DOUBLE, 
      //       MPI_SUM,
      //       pmesh->GetComm());

      //       tVelVal(Ik) = tTotalVal / tTotalVol;
      // }

      // if (mpi.Root())
      // {
      //    std::string tString = "./OutputFile_";

      //    Ascii tAsciiWriter( tString, FileMode::NEW );

      //    for( int Ik = 0; Ik < vdim; Ik ++)
      //    {
      //       tAsciiWriter.print(stringify( tVelVal(Ik) ));
      //    }
         
      //    tAsciiWriter.save();
      // }
      delete mPvdc;
      delete d_gf;
      delete DensCoeff;
      delete mFlowsolver;
   }


   delete pmesh;

   return 0;
}
