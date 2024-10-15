// Copyright (c) 2010-2024, Lawrence Livermore National Security, LLC. Produced
// at the Lawrence Livermore National Laboratory. All Rights reserved. See files
// LICENSE and NOTICE for details. LLNL-CODE-806117.
//
// This file is part of the MFEM library. For more information and source code
// availability visit https://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the BSD-3 license. We welcome feedback and contributions, see file
// CONTRIBUTING.md for details.

#include "incompressible_navier_solver.hpp"
#include "../../general/forall.hpp"
#include <fstream>
#include <iomanip>

using namespace mfem;
using namespace incompressible_navier;

IncompressibleNavierSolver::NavierSolver(ParMesh *mesh, int velorder, int porder, real_t kin_vis)
   : pmesh(mesh), velorder(velorder), porder(porder), kin_vis(kin_vis),
     gll_rules(0, Quadrature1D::GaussLobatto)
{
   vfec   = new H1_FECollection(velorder, pmesh->Dimension());
   psifec = new H1_FECollection(velorder);
   pfec   = new H1_FECollection(porder);
   vfes   = new ParFiniteElementSpace(pmesh, vfec, pmesh->Dimension());
   psifes s= new ParFiniteElementSpace(pmesh, pfec);
   pfes   = new ParFiniteElementSpace(pmesh, pfec);

   // Check if fully periodic mesh
   if (!(pmesh->bdr_attributes.Size() == 0))
   {
      vel_ess_attr.SetSize(pmesh->bdr_attributes.Max());
      vel_ess_attr = 0;

      pres_ess_attr.SetSize(pmesh->bdr_attributes.Max());
      pres_ess_attr = 0;
   }

   int vfes_truevsize = vfes->GetTrueVSize();
   int pfes_truevsize = pfes->GetTrueVSize();

   un.SetSize(vfes_truevsize);
   un = 0.0;

   PrintInfo();
}

void IncompressibleNavierSolver::Setup(real_t dt)
{
   if (verbose && pmesh->GetMyRank() == 0)
   {
      mfem::out << "Setup" << std::endl;
      if (partial_assembly)
      {
         mfem::out << "Using Partial Assembly" << std::endl;
      }
      else
      {
         mfem::out << "Using Full Assembly" << std::endl;
      }
   }

   vfes->GetEssentialTrueDofs(vel_ess_attr, vel_ess_tdof);
   pfes->GetEssentialTrueDofs(pres_ess_attr, pres_ess_tdof);

   Array<int> empty;

   // GLL integration rule (Numerical Integration)
   const IntegrationRule &ir_ni = gll_rules.Get(vfes->GetFE(0)->GetGeomType(),
                                                2 * order - 1);

   //-------------------------------------------------------------------------

   velBForm = new ParBilinearForm(vfes);
   auto *vmass_blfi = new VectorMassIntegrator();       //FIXEM add -1/dt coefficient
   auto *vdiff_blfi = new VectorDiffusionIntegrator();   //FIXEM add nu coefficient

   if (numerical_integ)
   {
       vmass_blfi->SetIntRule(&ir_ni); 
       vdiff_blfi->SetIntRule(&ir_ni);
   }
   velBForm->AddDomainIntegrator(vmass_blfi);
   velBForm->AddDomainIntegrator(vdiff_blfi);
   if (partial_assembly)
   {
      velBForm->SetAssemblyLevel(AssemblyLevel::PARTIAL);
   }

   velBForm->Assemble();
   //velBForm->FormSystemMatrix(empty, Mv);

   //-------------------------------------------------------------------------

   psiBForm = new ParBilinearForm(psifes);
   auto *psidiff_blfi = new DiffusionIntegrator;

   if (numerical_integ)
   {
       psidiff_blfi->SetIntRule(&ir_ni); 
   }
   psiBForm->AddDomainIntegrator(psidiff_blfi);
   if (partial_assembly)
   {
      psiBForm->SetAssemblyLevel(AssemblyLevel::PARTIAL);
   }

   psiBForm->Assemble();
   //psiBForm->FormSystemMatrix(empty, Mv);

   //-------------------------------------------------------------------------

   pBForm = new ParBilinearForm(pfes);
   auto *pmass_blfi = new MassIntegrator;

   if (numerical_integ)
   {
       pmass_blfi->SetIntRule(&ir_ni); 
   }
   pBForm->AddDomainIntegrator(pmass_blfi);
   if (partial_assembly)
   {
      pBForm->SetAssemblyLevel(AssemblyLevel::PARTIAL);
   }

   pBForm->Assemble();
   //pBForm->FormSystemMatrix(empty, Mv);

   //-------------------------------------------------------------------------
}

void IncompressibleNavierSolver::UpdateTimestepHistory(real_t dt)
{

}

void IncompressibleNavierSolver::Step(real_t &time, real_t dt, int current_step,
                        bool provisional)
{


}



void IncompressibleNavierSolver::EliminateRHS(Operator &A,
                                ConstrainedOperator &constrainedA,
                                const Array<int> &ess_tdof_list,
                                Vector &x,
                                Vector &b,
                                Vector &X,
                                Vector &B,
                                int copy_interior)
{

}

real_t IncompressibleNavierSolver::ComputeCFL(ParGridFunction &u, real_t dt)
{
   
   return 0;
}

void IncompressibleNavierSolver::AddVelDirichletBC(VectorCoefficient *coeff, Array<int> &attr)
{

}

void IncompressibleNavierSolver::AddVelDirichletBC(VecFuncT *f, Array<int> &attr)
{

}

IncompressibleNavierSolver::~NavierSolver()
{

   delete velBForm;
   delete psiBForm;
   delete pBForm;

   delete vfec;
   delete psifec;
   delete pfec;
   delete vfes;
   delete psifes;
   delete pfes;
}
