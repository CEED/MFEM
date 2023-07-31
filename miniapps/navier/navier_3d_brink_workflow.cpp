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

#include "navier_3d_brink_workflow.hpp"
#include "ascii.hpp"

using namespace mfem;
using namespace navier;

Navier3dBrinkWorkflow::Navier3dBrinkWorkflow( const MPI_Session & aMPI, ParMesh * pmesh, const struct s_NavierContext & aCtk ) : 
mMPI(aMPI),
mPMesh(pmesh),
mCtk(aCtk)
{}

Navier3dBrinkWorkflow::~Navier3dBrinkWorkflow() 
{
   delete mDensCoeff;
   delete mFlowsolver;
   delete mPvdc;
   //delete mBp;
}


void Navier3dBrinkWorkflow::SetDensityCoeff(    
   enum DensityCoeff::PatternType aGeometry,
   enum DensityCoeff::ProjectionType aProjectionType)
{
   mDensCoeff = new DensityCoeff;

   mDensCoeff->SetThreshold(meta);
   mDensCoeff->SetAngle(mnz);
   mDensCoeff->SetPatternType(aGeometry);

   mDensCoeff->SetProjectionType(aProjectionType);
}


void Navier3dBrinkWorkflow::SetupFlowSolver()
{
   //   Create the flow solver.
   mFlowsolver = new NavierSolver(mPMesh, mCtk.order, mCtk.kin_vis);
   mFlowsolver->EnablePA(true);
   mFlowsolver->EnableNI(true);
}

void Navier3dBrinkWorkflow::SetInitialConditions( std::function<void(const Vector &, double, Vector &)> TDF, bool LoadSolVecFromFile, double BrinmannPen )
{
   ParGridFunction *u_ic = mFlowsolver->GetCurrentVelocity();

   if( !LoadSolVecFromFile )
   {
      VectorFunctionCoefficient u_excoeff(mPMesh->Dimension(), TDF);
      u_ic->ProjectCoefficient(u_excoeff);
   }
   else
   {
      std::string tStringIn = "VelGF";
      int n = 6;
      std::string tWorlsRank = std::to_string( mMPI.WorldRank());
    
      int precision = n - tWorlsRank.size();
      std::string s = std::string(precision, '0').append(tWorlsRank);

      tStringIn= tStringIn +"."+s;

      std::ifstream inp(tStringIn);
      ParGridFunction tLoadGF(mPMesh, inp);

      *u_ic = tLoadGF;

      //std::cout<<"u_ic: "<<u_ic->Norml2()<<std::endl;

      //u_ic->Print();
   }
   


   // Add Dirichlet boundary conditions to velocity space restricted to
   // selected attributes on the mesh.
   //Array<int> attr(pmesh->bdr_attributes.Max());
   // Inlet is attribute 1.
   //attr[0] = 1;
   // Walls is attribute 2.
   //attr[1] = 1;
   //flowsolver.AddVelDirichletBC(vel, attr);

   Array<int> domain_attr(mPMesh->attributes.Max());
   domain_attr = 1;
   mBp = new BrinkPenalAccel(mPMesh->Dimension() );
   mBp->SetDensity(mDensCoeff);
   mBp->SetBrinkmannPenalization(BrinmannPen);
   mBp->SetVel(mFlowsolver->GetCurrentVelocity());
   mBp->SetParams( mnx, mny, mnz, ma);
   mFlowsolver->AddAccelTerm(mBp,domain_attr);



   mFlowsolver->Setup(mCtk.dt);


}

void Navier3dBrinkWorkflow::SetupOutput( )
{
   ParGridFunction *u_gf = mFlowsolver->GetCurrentVelocity();
   ParGridFunction *p_gf = mFlowsolver->GetCurrentPressure();
   ParGridFunction *d_gf = new ParGridFunction(*p_gf);
   mDensCoeff->SetProjectionType(DensityCoeff::ProjectionType::continuous);
   d_gf->ProjectCoefficient(*mDensCoeff);
   mDensCoeff->SetProjectionType(DensityCoeff::ProjectionType::zero_one);



   if( mVisualization )
   {
      mPvdc = new ParaViewDataCollection("2dfoc1", mPMesh);
      mPvdc->SetDataFormat(VTKFormat::BINARY32);
      mPvdc->SetHighOrderOutput(true);
      mPvdc->SetLevelsOfDetail(mCtk.order);
      mPvdc->SetCycle(0);
      mPvdc->SetTime(0.0);
      mPvdc->RegisterField("velocity", u_gf);
      mPvdc->RegisterField("pressure", p_gf);
      mPvdc->RegisterField("density",  d_gf);
      mPvdc->Save();
   }
}

void Navier3dBrinkWorkflow::Perform( )
{
   double t = 0.0;
   double dt = mCtk.dt;
   double t_final = mCtk.t_final;
   bool last_step = false;



    for (int step = 0; !last_step; ++step)
    {


       if (t + dt >= t_final - dt / 2)
       {
          last_step = true;
       }

       mFlowsolver->Step(t, dt, step);

       mBp->SetVel(mFlowsolver->GetCurrentVelocity());

       //mFlowsolver->GetCurrentVelocity()->norm2();
   //    //mBp->SetVel(flowsolver.GetProvisionalVelocity());
      if( mVisualization )
      {
         if (step % 50000 == 0)
         {
           mPvdc->SetCycle(step);
            mPvdc->SetTime(t);
            mPvdc->Save();
         }
      }
         if (mMPI.Root())
         {
            printf("%11s %11s\n", "Time", "dt");
            printf("%.5E %.5E\n", t, dt);
            fflush(stdout);
         }
    }

    mFlowsolver->PrintTimingData();
}

void Navier3dBrinkWorkflow::Postprocess(const int & runID)
{
   ParGridFunction * u = mFlowsolver->GetCurrentVelocity();

   std::string tOutputNameGF = "VelGF";

   u->Save( tOutputNameGF.c_str() );

   FiniteElementSpace *fes = u->FESpace();
   int vdim = fes->GetVDim();

   // initilaize integradl of velozity vector
   Vector tVelVal(vdim);
   tVelVal = 0.0;

   mAverageVel.SetSize(vdim);

   double tVolume = 0.0;

   for (int e = 0; e < fes->GetNE(); ++e)
   {
      const FiniteElement *fe = fes->GetFE(e);
      const IntegrationRule &ir = IntRules.Get(fe->GetGeomType(),
                                               fe->GetOrder());
      ElementTransformation *tr = fes->GetElementTransformation(e);

      for (int i = 0; i < ir.GetNPoints(); ++i)
      {
         const IntegrationPoint &ip = ir.IntPoint(i);
         tr->SetIntPoint(&ip);

         double w = tr->Weight() * ip.weight;

         Vector tVal;

         u->GetVectorValue( e, ip, tVal);

         tVal *= w;
         tVelVal += tVal;

         tVolume += w;
      }

   }

   std::cout<<" Volume = "<< tVolume<< std::endl;

   double tTotalVol;
   MPI_Allreduce(&tVolume, &tTotalVol, 1, MPI_DOUBLE, MPI_SUM,
                 mPMesh->GetComm());

   for( int Ik = 0; Ik < vdim; Ik ++)
   {
      double tVal = tVelVal(Ik); 
      double tTotalVal;

      MPI_Allreduce(
         &tVal,
         &tTotalVal, 
         1, 
         MPI_DOUBLE, 
         MPI_SUM,
         mPMesh->GetComm());

         tVelVal(Ik) = tTotalVal / tTotalVol;
         mAverageVel(Ik) = tVelVal(Ik);
   }

   if (mMPI.Root())
   {
      std::string tString = "./OutputFile_" + std::to_string(runID);

      Ascii tAsciiWriter( tString, FileMode::NEW );

      tAsciiWriter.print(stringify( mnx ));
      tAsciiWriter.print(stringify( mny ));
      tAsciiWriter.print(stringify( mnz ));
      tAsciiWriter.print(stringify( ma ));
      tAsciiWriter.print(stringify( meta ));

      for( int Ik = 0; Ik < vdim; Ik ++)
      {
         tAsciiWriter.print(stringify( tVelVal(Ik) ));
      }
         
      tAsciiWriter.save();
   }

}



