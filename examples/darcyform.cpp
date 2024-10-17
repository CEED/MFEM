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

#include "darcyform.hpp"

namespace mfem
{

DarcyForm::DarcyForm(FiniteElementSpace *fes_u_, FiniteElementSpace *fes_p_,
                     bool bsymmetrize)
   : fes_u(fes_u_), fes_p(fes_p_), bsym(bsymmetrize)
{
   offsets.SetSize(3);
   offsets[0] = 0;
   offsets[1] = fes_u->GetVSize();
   offsets[2] = fes_p->GetVSize();
   offsets.PartialSum();

   width = height = offsets.Last();

   M_u = NULL;
   M_p = NULL;
   Mnl_u = NULL;
   Mnl_p = NULL;
   B = NULL;
   Mnl = NULL;

   assembly = AssemblyLevel::LEGACY;

   block_op = NULL;

   reduction = NULL;
   hybridization = NULL;
}

BilinearForm* DarcyForm::GetFluxMassForm()
{
   if (!M_u) { M_u = new BilinearForm(fes_u); }
   return M_u;
}

const BilinearForm* DarcyForm::GetFluxMassForm() const
{
   //MFEM_ASSERT(M_u, "Flux mass form not allocated!");
   return M_u;
}

BilinearForm* DarcyForm::GetPotentialMassForm()
{
   if (!M_p) { M_p = new BilinearForm(fes_p); }
   return M_p;
}

const BilinearForm* DarcyForm::GetPotentialMassForm() const
{
   //MFEM_ASSERT(M_p, "Potential mass form not allocated!");
   return M_p;
}

NonlinearForm *DarcyForm::GetFluxMassNonlinearForm()
{
   if (!Mnl_u) { Mnl_u = new NonlinearForm(fes_u); }
   return Mnl_u;
}

const NonlinearForm *DarcyForm::GetFluxMassNonlinearForm() const
{
   //MFEM_ASSERT(Mnl_u, "Flux mass nonlinear form not allocated!");
   return Mnl_u;
}

NonlinearForm* DarcyForm::GetPotentialMassNonlinearForm()
{
   if (!Mnl_p) { Mnl_p = new NonlinearForm(fes_p); }
   return Mnl_p;
}

const NonlinearForm* DarcyForm::GetPotentialMassNonlinearForm() const
{
   //MFEM_ASSERT(Mnl_p, "Potential mass nonlinear form not allocated!");
   return Mnl_p;
}

MixedBilinearForm* DarcyForm::GetFluxDivForm()
{
   if (!B) { B = new MixedBilinearForm(fes_u, fes_p); }
   return B;
}

const MixedBilinearForm* DarcyForm::GetFluxDivForm() const
{
   //MFEM_ASSERT(B, "Flux div form not allocated!");
   return B;
}

BlockNonlinearForm *DarcyForm::GetBlockNonlinearForm()
{
   if (!Mnl)
   {
      Array<FiniteElementSpace*> fes({fes_u, fes_p});
      Mnl = new BlockNonlinearForm(fes);
   }
   return Mnl;
}

const BlockNonlinearForm *DarcyForm::GetBlockNonlinearForm() const
{
   //MFEM_ASSERT(Mnl, "Block nonlinear form not allocated!");
   return Mnl;
}

void DarcyForm::SetAssemblyLevel(AssemblyLevel assembly_level)
{
   assembly = assembly_level;

   if (M_u) { M_u->SetAssemblyLevel(assembly); }
   if (M_p) { M_p->SetAssemblyLevel(assembly); }
   if (Mnl_u) { Mnl_u->SetAssemblyLevel(assembly); }
   if (Mnl_p) { Mnl_p->SetAssemblyLevel(assembly); }
   if (B) { B->SetAssemblyLevel(assembly); }
}

void DarcyForm::EnableReduction(const Array<int> &ess_flux_tdof_list,
                                DarcyReduction *reduction_)
{
   MFEM_ASSERT(!Mnl, "Reduction cannot be used with block nonlinear forms");
   MFEM_ASSERT((M_u || Mnl_u) && (M_p || Mnl_p),
               "Mass forms for the fluxes and potentials must be set prior to this call!");

   delete reduction;
   if (assembly != AssemblyLevel::LEGACY)
   {
      reduction = NULL;
      MFEM_WARNING("Reduction not supported for this assembly level");
      return;
   }
   reduction = reduction_;

   // Automatically load the flux mass integrators
   if (Mnl_u)
   {
      NonlinearFormIntegrator *flux_integ = NULL;
      auto dnlfi = Mnl_u->GetDNFI();
      if (dnlfi->Size())
      {
         SumNLFIntegrator *snlfi = new SumNLFIntegrator(false);
         for (NonlinearFormIntegrator *nlfi : *dnlfi)
         {
            snlfi->AddIntegrator(nlfi);
         }
         flux_integ = snlfi;
      }
      reduction->SetFluxMassNonlinearIntegrator(flux_integ);
   }

   // Automatically load the potential mass integrators
   if (Mnl_p)
   {
      NonlinearFormIntegrator *pot_integ = NULL;
      auto dnlfi = Mnl_p->GetDNFI();
      if (dnlfi->Size())
      {
         SumNLFIntegrator *snlfi = new SumNLFIntegrator(false);
         for (NonlinearFormIntegrator *nlfi : *dnlfi)
         {
            snlfi->AddIntegrator(nlfi);
         }
         pot_integ = snlfi;
      }
      reduction->SetPotMassNonlinearIntegrator(pot_integ);
   }

   reduction->Init(ess_flux_tdof_list);
}

void DarcyForm::EnableHybridization(FiniteElementSpace *constr_space,
                                    BilinearFormIntegrator *constr_flux_integ,
                                    const Array<int> &ess_flux_tdof_list)
{
   MFEM_ASSERT(M_u || Mnl_u || Mnl,
               "Mass form for the fluxes must be set prior to this call!");
   delete hybridization;
   if (assembly != AssemblyLevel::LEGACY)
   {
      delete constr_flux_integ;
      hybridization = NULL;
      MFEM_WARNING("Hybridization not supported for this assembly level");
      return;
   }
   hybridization = new DarcyHybridization(fes_u, fes_p, constr_space, bsym);

   // Automatically load the potential constraint operator from the face integrators
   if (M_p)
   {
      BilinearFormIntegrator *constr_pot_integ = NULL;
      auto fbfi = M_p->GetFBFI();
      if (fbfi->Size())
      {
         SumIntegrator *sbfi = new SumIntegrator(false);
         for (BilinearFormIntegrator *bfi : *fbfi)
         {
            sbfi->AddIntegrator(bfi);
         }
         constr_pot_integ = sbfi;
      }
      hybridization->SetConstraintIntegrators(constr_flux_integ, constr_pot_integ);
   }
   else if (Mnl_p)
   {
      NonlinearFormIntegrator *constr_pot_integ = NULL;
      auto fnlfi = Mnl_p->GetInteriorFaceIntegrators();
      if (fnlfi.Size())
      {
         SumNLFIntegrator *snlfi = new SumNLFIntegrator(false);
         for (NonlinearFormIntegrator *nlfi : fnlfi)
         {
            snlfi->AddIntegrator(nlfi);
         }
         constr_pot_integ = snlfi;
      }
      hybridization->SetConstraintIntegrators(constr_flux_integ, constr_pot_integ);
   }
   else
   {
      hybridization->SetConstraintIntegrators(constr_flux_integ,
                                              (BilinearFormIntegrator*)NULL);
   }

   // Automatically load the flux mass integrators
   if (Mnl_u)
   {
      NonlinearFormIntegrator *flux_integ = NULL;
      auto dnlfi = Mnl_u->GetDNFI();
      if (dnlfi->Size())
      {
         SumNLFIntegrator *snlfi = new SumNLFIntegrator(false);
         for (NonlinearFormIntegrator *nlfi : *dnlfi)
         {
            snlfi->AddIntegrator(nlfi);
         }
         flux_integ = snlfi;
      }
      hybridization->SetFluxMassNonlinearIntegrator(flux_integ);
   }

   // Automatically load the potential mass integrators
   if (Mnl_p)
   {
      NonlinearFormIntegrator *pot_integ = NULL;
      auto dnlfi = Mnl_p->GetDNFI();
      if (dnlfi->Size())
      {
         SumNLFIntegrator *snlfi = new SumNLFIntegrator(false);
         for (NonlinearFormIntegrator *nlfi : *dnlfi)
         {
            snlfi->AddIntegrator(nlfi);
         }
         pot_integ = snlfi;
      }
      hybridization->SetPotMassNonlinearIntegrator(pot_integ);
   }

   // Automatically load the block integrators
   if (Mnl)
   {
      BlockNonlinearFormIntegrator *block_integ = NULL;
      auto &dnlfi = Mnl->GetDomainIntegrators();
      block_integ = dnlfi[0];
      hybridization->SetBlockNonlinearIntegrator(block_integ, false);
   }

   // Automatically add the boundary flux constraint integrators
   if (B)
   {
      auto bfbfi_marker = B->GetBFBFI_Marker();
      hybridization->UseExternalBdrFluxConstraintIntegrators();

      for (Array<int> *bfi_marker : *bfbfi_marker)
      {
         if (bfi_marker)
         {
            hybridization->AddBdrFluxConstraintIntegrator(constr_flux_integ, *bfi_marker);
         }
         else
         {
            hybridization->AddBdrFluxConstraintIntegrator(constr_flux_integ);
         }
      }
   }

   // Automatically add the boundary potential constraint integrators
   if (M_p)
   {
      auto bfbfi = M_p->GetBFBFI();
      auto bfbfi_marker = M_p->GetBFBFI_Marker();
      hybridization->UseExternalBdrPotConstraintIntegrators();

      for (int i = 0; i < bfbfi->Size(); i++)
      {
         BilinearFormIntegrator *bfi = (*bfbfi)[i];
         Array<int> *bfi_marker = (*bfbfi_marker)[i];
         if (bfi_marker)
         {
            hybridization->AddBdrPotConstraintIntegrator(bfi, *bfi_marker);
         }
         else
         {
            hybridization->AddBdrPotConstraintIntegrator(bfi);
         }
      }
   }
   else if (Mnl_p)
   {
      auto bfnlfi = Mnl_p->GetBdrFaceIntegrators();
      auto bfnlfi_marker = Mnl_p->GetBdrFaceIntegratorsMarkers();
      hybridization->UseExternalBdrPotConstraintIntegrators();

      for (int i = 0; i < bfnlfi.Size(); i++)
      {
         NonlinearFormIntegrator *nlfi = bfnlfi[i];
         Array<int> *nlfi_marker = bfnlfi_marker[i];
         if (nlfi_marker)
         {
            hybridization->AddBdrPotConstraintIntegrator(nlfi, *nlfi_marker);
         }
         else
         {
            hybridization->AddBdrPotConstraintIntegrator(nlfi);
         }
      }
   }

   hybridization->Init(ess_flux_tdof_list);
}

void DarcyForm::Assemble(int skip_zeros)
{
   if (M_u)
   {
      if (hybridization)
      {
         DenseMatrix elmat;

         // Element-wise integration
         for (int i = 0; i < fes_u -> GetNE(); i++)
         {
            M_u->ComputeElementMatrix(i, elmat);
#ifndef MFEM_DARCY_HYBRIDIZATION_ELIM_BCS
            M_u->AssembleElementMatrix(i, elmat, skip_zeros);
#endif //!MFEM_DARCY_HYBRIDIZATION_ELIM_BCS
            hybridization->AssembleFluxMassMatrix(i, elmat);
         }
      }
      else if (reduction)
      {
         DenseMatrix elmat;

         // Element-wise integration
         for (int i = 0; i < fes_u -> GetNE(); i++)
         {
            M_u->ComputeElementMatrix(i, elmat);
#ifndef MFEM_DARCY_REDUCTION_ELIM_BCS
            M_u->AssembleElementMatrix(i, elmat, skip_zeros);
#endif //!MFEM_DARCY_REDUCTION_ELIM_BCS
            reduction->AssembleFluxMassMatrix(i, elmat);
         }
      }
      else
      {
         M_u->Assemble(skip_zeros);
      }
   }
   else if (Mnl_u)
   {
      Mnl_u->Setup();
   }

   if (B)
   {
      if (hybridization)
      {
         DenseMatrix elmat;

         // Element-wise integration
         for (int i = 0; i < fes_u -> GetNE(); i++)
         {
            B->ComputeElementMatrix(i, elmat);
#ifndef MFEM_DARCY_HYBRIDIZATION_ELIM_BCS
            B->AssembleElementMatrix(i, elmat, skip_zeros);
#endif //!MFEM_DARCY_HYBRIDIZATION_ELIM_BCS
            hybridization->AssembleDivMatrix(i, elmat);
         }
      }
      else if (reduction)
      {
         DenseMatrix elmat;

         // Element-wise integration
         for (int i = 0; i < fes_u -> GetNE(); i++)
         {
            B->ComputeElementMatrix(i, elmat);
#ifndef MFEM_DARCY_REDUCTION_ELIM_BCS
            B->AssembleElementMatrix(i, elmat, skip_zeros);
#endif //!MFEM_DARCY_REDUCTION_ELIM_BCS
            reduction->AssembleDivMatrix(i, elmat);
         }
      }
      else
      {
         B->Assemble(skip_zeros);
      }
   }

   if (M_p)
   {
      if (hybridization)
      {
         DenseMatrix elmat;

         // Element-wise integration
         for (int i = 0; i < fes_p -> GetNE(); i++)
         {
            M_p->ComputeElementMatrix(i, elmat);
#ifndef MFEM_DARCY_HYBRIDIZATION_ELIM_BCS
            M_p->AssembleElementMatrix(i, elmat, skip_zeros);
#endif //!MFEM_DARCY_HYBRIDIZATION_ELIM_BCS
            hybridization->AssemblePotMassMatrix(i, elmat);
         }

         AssemblePotHDGFaces(skip_zeros);
      }
      else if (reduction)
      {
         DenseMatrix elmat;

         // Element-wise integration
         for (int i = 0; i < fes_p -> GetNE(); i++)
         {
            M_p->ComputeElementMatrix(i, elmat);
#ifndef MFEM_DARCY_REDUCTION_ELIM_BCS
            M_p->AssembleElementMatrix(i, elmat, skip_zeros);
#endif //!MFEM_DARCY_REDUCTION_ELIM_BCS
            reduction->AssemblePotMassMatrix(i, elmat);
         }
      }
      else
      {
         M_p->Assemble(skip_zeros);
      }
   }
   else if (Mnl_p)
   {
      Mnl_p->Setup();
   }
}

void DarcyForm::Finalize(int skip_zeros)
{
   AllocBlockOp();

   if (block_op)
   {
      if (M_u)
      {
         M_u->Finalize(skip_zeros);
         block_op->SetDiagonalBlock(0, M_u);
      }
      else if (Mnl_u)
      {
         block_op->SetDiagonalBlock(0, Mnl_u);
      }
      else if (Mnl)
      {
         pM.Reset(Mnl, false);
      }

      if (M_p)
      {
         M_p->Finalize(skip_zeros);
         block_op->SetDiagonalBlock(1, M_p, (bsym)?(-1.):(+1.));
      }
      else if (Mnl_p)
      {
         block_op->SetDiagonalBlock(1, Mnl_p, (bsym)?(-1.):(+1.));
      }

      if (B)
      {
         B->Finalize(skip_zeros);

         if (!pBt.Ptr()) { ConstructBT(B); }

         block_op->SetBlock(0, 1, pBt.Ptr(), (bsym)?(-1.):(+1.));
         block_op->SetBlock(1, 0, B, (bsym)?(-1.):(+1.));
      }
   }

   if (hybridization)
   {
      hybridization->Finalize();
   }
   else if (reduction)
   {
      reduction->Finalize();
   }
}

void DarcyForm::FormLinearSystem(const Array<int> &ess_flux_tdof_list,
                                 BlockVector &x, BlockVector &b, OperatorHandle &A, Vector &X_, Vector &B_,
                                 int copy_interior)
{
   if (assembly != AssemblyLevel::LEGACY)
   {
      Array<int> ess_pot_tdof_list;//empty for discontinuous potentials

      //conforming

      if (M_u)
      {
         M_u->FormLinearSystem(ess_flux_tdof_list, x.GetBlock(0), b.GetBlock(0), pM_u,
                               X_, B_, copy_interior);
         block_op->SetDiagonalBlock(0, pM_u.Ptr());
      }
      else if (Mnl_u)
      {
         Operator *opM;
         Mnl_u->FormLinearSystem(ess_flux_tdof_list, x.GetBlock(0), b.GetBlock(0), opM,
                                 X_, B_, copy_interior);
         pM_u.Reset(opM);
         block_op->SetDiagonalBlock(0, pM_u.Ptr());
      }
      else if (Mnl)
      {
         Operator *opM;
         Mnl->FormLinearSystem(ess_flux_tdof_list, x, b, opM, X_, B_, copy_interior);
         pM.Reset(opM);
      }

      if (M_p)
      {
         M_p->FormLinearSystem(ess_pot_tdof_list, x.GetBlock(1), b.GetBlock(1), pM_p, X_,
                               B_, copy_interior);
         block_op->SetDiagonalBlock(1, pM_p.Ptr(), (bsym)?(-1.):(+1.));
      }
      else if (Mnl_p)
      {
         block_op->SetDiagonalBlock(1, Mnl_p, (bsym)?(-1.):(+1.));
      }

      if (B)
      {
         if (bsym)
         {
            //In the case of the symmetrized system, the sign is oppposite!
            Vector b_(fes_p->GetVSize());
            b_ = 0.;
            B->FormRectangularLinearSystem(ess_flux_tdof_list, ess_pot_tdof_list,
                                           x.GetBlock(0), b_, pB, X_, B_);
            b.GetBlock(1) -= b_;
         }
         else
         {
            B->FormRectangularLinearSystem(ess_flux_tdof_list, ess_pot_tdof_list,
                                           x.GetBlock(0), b.GetBlock(1), pB, X_, B_);
         }

         ConstructBT(pB.Ptr());

         block_op->SetBlock(0, 1, pBt.Ptr(), (bsym)?(-1.):(+1.));
         block_op->SetBlock(1, 0, pB.Ptr(), (bsym)?(-1.):(+1.));
      }

      if (Mnl && pM.Ptr())
      {
         A.Reset(new SumOperator(block_op, 1., pM.Ptr(), 1., false, false));
      }
      else
      {
         A.Reset(block_op, false);
      }

      X_.MakeRef(x, 0, x.Size());
      B_.MakeRef(b, 0, b.Size());

      return;
   }

   FormSystemMatrix(ess_flux_tdof_list, A);

   //conforming

   if (hybridization)
   {
      // Reduction to the Lagrange multipliers system
      EliminateVDofsInRHS(ess_flux_tdof_list, x, b);
      hybridization->ReduceRHS(b, B_);
      X_.SetSize(B_.Size());
      X_ = 0.0;
   }
   else if (reduction)
   {
      // Reduction to the Lagrange multipliers system
      EliminateVDofsInRHS(ess_flux_tdof_list, x, b);
      reduction->ReduceRHS(b, B_);
      X_.SetSize(B_.Size());
      X_ = 0.0;
   }
   else
   {
      // A, X and B point to the same data as mat, x and b
      EliminateVDofsInRHS(ess_flux_tdof_list, x, b);
      X_.MakeRef(x, 0, x.Size());
      B_.MakeRef(b, 0, b.Size());
      if (!copy_interior)
      {
         x.GetBlock(0).SetSubVectorComplement(ess_flux_tdof_list, 0.0);
         x.GetBlock(1) = 0.;
      }
   }
}

void DarcyForm::FormSystemMatrix(const Array<int> &ess_flux_tdof_list,
                                 OperatorHandle &A)
{
   AllocBlockOp();

   if (block_op)
   {
      Array<int> ess_pot_tdof_list;//empty for discontinuous potentials

      if (M_u)
      {
         M_u->FormSystemMatrix(ess_flux_tdof_list, pM_u);
         block_op->SetDiagonalBlock(0, pM_u.Ptr());
      }
      else if (Mnl_u)
      {
         Operator *opM;
         Mnl_u->FormSystemOperator(ess_flux_tdof_list, opM);
         pM_u.Reset(opM);
         block_op->SetDiagonalBlock(0, pM_u.Ptr());
      }
      else if (Mnl)
      {
         Operator *opM;
         Mnl->FormSystemOperator(ess_flux_tdof_list, opM);
         pM.Reset(opM);
      }

      if (M_p)
      {
         M_p->FormSystemMatrix(ess_pot_tdof_list, pM_p);
         block_op->SetDiagonalBlock(1, pM_p.Ptr(), (bsym)?(-1.):(+1.));
      }
      else if (Mnl_p)
      {
         block_op->SetDiagonalBlock(1, Mnl_p, (bsym)?(-1.):(+1.));
      }

      if (B)
      {
         B->FormRectangularSystemMatrix(ess_flux_tdof_list, ess_pot_tdof_list, pB);

         ConstructBT(pB.Ptr());

         block_op->SetBlock(0, 1, pBt.Ptr(), (bsym)?(-1.):(+1.));
         block_op->SetBlock(1, 0, pB.Ptr(), (bsym)?(-1.):(+1.));
      }
   }

   if (hybridization)
   {
      hybridization->Finalize();
      if (!Mnl_u && !Mnl_p && !Mnl)
      {
         A.Reset(&hybridization->GetMatrix(), false);
      }
      else
      {
         A.Reset(hybridization, false);
      }
   }
   else if (reduction)
   {
      reduction->Finalize();
      if (!Mnl_u && !Mnl_p && !Mnl)
      {
         A.Reset(&reduction->GetMatrix(), false);
      }
      else
      {
         A.Reset(reduction, false);
      }
   }
   else
   {
      if (Mnl && pM.Ptr())
      {
         A.Reset(new SumOperator(block_op, 1., pM.Ptr(), 1., false, false));
      }
      else
      {
         A.Reset(block_op, false);
      }
   }
}

void DarcyForm::RecoverFEMSolution(const Vector &X, const BlockVector &b,
                                   BlockVector &x)
{
   if (hybridization)
   {
      //conforming
      hybridization->ComputeSolution(b, X, x);
   }
   else if (reduction)
   {
      //conforming
      reduction->ComputeSolution(b, X, x);
   }
   else
   {
      BlockVector X_b(const_cast<Vector&>(X), offsets);
      if (M_u)
      {
         M_u->RecoverFEMSolution(X_b.GetBlock(0), b.GetBlock(0), x.GetBlock(0));
      }
      if (M_p)
      {
         M_p->RecoverFEMSolution(X_b.GetBlock(1), b.GetBlock(1), x.GetBlock(1));
      }
   }
}

void DarcyForm::EliminateVDofsInRHS(const Array<int> &vdofs_flux,
                                    const BlockVector &x, BlockVector &b)
{
#ifdef MFEM_DARCY_HYBRIDIZATION_ELIM_BCS
   if (hybridization)
   {
      hybridization->EliminateVDofsInRHS(vdofs_flux, x, b);
      return;
   }
#endif //MFEM_DARCY_HYBRIDIZATION_ELIM_BCS
#ifdef MFEM_DARCY_REDUCTION_ELIM_BCS
   if (reduction)
   {
      reduction->EliminateVDofsInRHS(vdofs_flux, x, b);
      return;
   }
#endif //MFEM_DARCY_REDUCTION_ELIM_BCS
   if (B)
   {
      if (bsym)
      {
         //In the case of the symmetrized system, the sign is oppposite!
         Vector b_(fes_p->GetVSize());
         b_ = 0.;
         B->EliminateTrialVDofsInRHS(vdofs_flux, x.GetBlock(0), b_);
         b.GetBlock(1) -= b_;
      }
      else
      {
         B->EliminateTrialVDofsInRHS(vdofs_flux, x.GetBlock(0), b.GetBlock(1));
      }
   }
   if (M_u)
   {
      M_u->EliminateVDofsInRHS(vdofs_flux, x.GetBlock(0), b.GetBlock(0));
   }
   else if (Mnl_u && pM_u.Ptr())
   {
      pM_u.As<ConstrainedOperator>()->EliminateRHS(x.GetBlock(0), b.GetBlock(0));
   }
   else if (Mnl && pM.Ptr())
   {
      pM.As<ConstrainedOperator>()->EliminateRHS(x, b);
   }

}

void DarcyForm::Mult(const Vector &x, Vector &y) const
{
   block_op->Mult(x, y);
   if (pM.Ptr()) { pM->AddMult(x, y); }
}

void DarcyForm::MultTranspose(const Vector &x, Vector &y) const
{
   block_op->MultTranspose(x, y);
   if (pM.Ptr()) { pM->AddMultTranspose(x, y); }
}

Operator &DarcyForm::GetGradient(const Vector &x) const
{
   if (!Mnl) { return *block_op; }

   pG.Reset(new SumOperator(block_op, 1., &Mnl->GetGradient(x), 1., false, false));
   return *pG.Ptr();
}

void DarcyForm::Update()
{
   if (M_u) { M_u->Update(); }
   if (M_p) { M_p->Update(); }
   if (Mnl_u) { Mnl_u->Update(); }
   if (Mnl_p) { Mnl_p->Update(); }
   if (B) { B->Update(); }
   if (Mnl) { Mnl->Update(); }

   pBt.Clear();

   if (reduction) { reduction->Reset(); }
   if (hybridization) { hybridization->Reset(); }
}

DarcyForm::~DarcyForm()
{
   if (M_u) { delete M_u; }
   if (M_p) { delete M_p; }
   if (Mnl_u) { delete Mnl_u; }
   if (Mnl_p) { delete Mnl_p; }
   if (B) { delete B; }
   if (Mnl) { delete Mnl; }

   delete block_op;

   delete reduction;
   delete hybridization;
}

void DarcyForm::AssemblePotHDGFaces(int skip_zeros)
{
   Mesh *mesh = fes_p->GetMesh();
   FaceElementTransformations *tr;
   DenseMatrix elmat1, elmat2;
   Array<int> vdofs1, vdofs2;

   if (hybridization->GetPotConstraintIntegrator())
   {
      int nfaces = mesh->GetNumFaces();
      for (int i = 0; i < nfaces; i++)
      {
         tr = mesh -> GetInteriorFaceTransformations (i);
         if (tr == NULL) { continue; }

         hybridization->ComputeAndAssemblePotFaceMatrix(i, elmat1, elmat2, vdofs1,
                                                        vdofs2);
#ifndef MFEM_DARCY_HYBRIDIZATION_ELIM_BCS
         M_p->SpMat().AddSubMatrix(vdofs1, vdofs1, elmat1, skip_zeros);
         M_p->SpMat().AddSubMatrix(vdofs2, vdofs2, elmat2, skip_zeros);
#endif //MFEM_DARCY_HYBRIDIZATION_ELIM_BCS
      }
   }

   auto &boundary_face_integs_marker = *hybridization->GetPotBCBFI_Marker();

   if (boundary_face_integs_marker.Size())
   {
      // Which boundary attributes need to be processed?
      Array<int> bdr_attr_marker(mesh->bdr_attributes.Size() ?
                                 mesh->bdr_attributes.Max() : 0);
      bdr_attr_marker = 0;
      for (int k = 0; k < boundary_face_integs_marker.Size(); k++)
      {
         if (boundary_face_integs_marker[k] == NULL)
         {
            bdr_attr_marker = 1;
            break;
         }
         Array<int> &bdr_marker = *boundary_face_integs_marker[k];
         MFEM_ASSERT(bdr_marker.Size() == bdr_attr_marker.Size(),
                     "invalid boundary marker for boundary face integrator #"
                     << k << ", counting from zero");
         for (int i = 0; i < bdr_attr_marker.Size(); i++)
         {
            bdr_attr_marker[i] |= bdr_marker[i];
         }
      }

      for (int i = 0; i < fes_p -> GetNBE(); i++)
      {
         const int bdr_attr = mesh->GetBdrAttribute(i);
         if (bdr_attr_marker[bdr_attr-1] == 0) { continue; }

         tr = mesh -> GetBdrFaceTransformations (i);
         if (tr != NULL)
         {
            hybridization->ComputeAndAssemblePotBdrFaceMatrix(i, elmat1, vdofs1);
#ifndef MFEM_DARCY_HYBRIDIZATION_ELIM_BCS
            M_p->SpMat().AddSubMatrix(vdofs1, vdofs1, elmat1, skip_zeros);
#endif //MFEM_DARCY_HYBRIDIZATION_ELIM_BCS
         }
      }
   }
}

void DarcyForm::AllocBlockOp()
{
   bool noblock = false;
#ifdef MFEM_DARCY_REDUCTION_ELIM_BCS
   noblock = noblock || reduction;
#endif //MFEM_DARCY_REDUCTION_ELIM_BCS
#ifdef MFEM_DARCY_HYBRIDIZATION_ELIM_BCS
   noblock = noblock || hybridization;
#endif //MFEM_DARCY_HYBRIDIZATION_ELIM_BCS

   if (!noblock)
   {
      delete block_op;
      block_op = new BlockOperator(offsets);
   }
}

const Operator *DarcyForm::ConstructBT(const MixedBilinearForm *B)
{
   pBt.Reset(Transpose(B->SpMat()));
   return pBt.Ptr();
}

const Operator* DarcyForm::ConstructBT(const Operator *opB)
{
   pBt.Reset(new TransposeOperator(opB));
   return pBt.Ptr();
}

}
