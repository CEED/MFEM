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

#include "../../config/config.hpp"

#ifdef MFEM_USE_MPI

#include "pdarcyform.hpp"

namespace mfem
{
ParDarcyForm::ParDarcyForm(ParFiniteElementSpace *fes_u,
                           ParFiniteElementSpace *fes_p, bool bsymmetrize)
   : DarcyForm(fes_u, fes_p, bsymmetrize), pfes_u(fes_u), pfes_p(fes_p)
{
   toffsets.SetSize(3);
   toffsets[0] = 0;
   toffsets[1] = pfes_u->GetTrueVSize();
   toffsets[2] = pfes_p->GetTrueVSize();
   toffsets.PartialSum();
}

BilinearForm *ParDarcyForm::GetFluxMassForm()
{
   if (!pM_u) { M_u = pM_u = new ParBilinearForm(pfes_u); }
   return pM_u;
}

BilinearForm *ParDarcyForm::GetPotentialMassForm()
{
   if (!pM_p) { M_p = pM_p = new ParBilinearForm(pfes_p); }
   return pM_p;
}

MixedBilinearForm *ParDarcyForm::GetFluxDivForm()
{
   if (!pB) { B = pB = new ParMixedBilinearForm(pfes_u, pfes_p); }
   return pB;
}

void ParDarcyForm::Assemble(int skip_zeros)
{
   if (pM_u)
   {
      {
         pM_u->Assemble(skip_zeros);
      }
   }
   /*else if (Mnl_u)
   {
      Mnl_u->Setup();
   }*/

   if (pB)
   {
      {
         pB->Assemble(skip_zeros);
      }
   }

   if (pM_p)
   {
      {
         pM_p->Assemble(skip_zeros);
      }
   }
   /*else if (Mnl_p)
   {
      Mnl_p->Setup();
   }*/
}

void ParDarcyForm::Finalize(int skip_zeros)
{
   AllocBlockOp();

   if (block_op)
   {
      if (M_u)
      {
         M_u->Finalize(skip_zeros);
      }
      /*else if (Mnl)
      {
         opM.Reset(Mnl, false);
      }*/

      if (M_p)
      {
         M_p->Finalize(skip_zeros);
      }

      if (B)
      {
         B->Finalize(skip_zeros);
      }
   }
}

void ParDarcyForm::ParallelAssembleInternal()
{
   if (pM_u) { pM_u->ParallelAssembleInternal(); }
   if (pM_p) { pM_p->ParallelAssembleInternal(); }
   if (pB) { pB->ParallelAssembleInternal(); }
}

void ParDarcyForm::FormLinearSystem(
   const Array<int> &ess_flux_tdof_list, BlockVector &x, BlockVector &b,
   OperatorHandle &A, Vector &X_, Vector &B_, int copy_interior)
{
   MFEM_VERIFY(assembly == AssemblyLevel::LEGACY,
               "Only legacy assembly is supported");

   // Finish the matrix assembly and perform BC elimination, storing the
   // eliminated part of the matrix.
   FormSystemMatrix(ess_flux_tdof_list, A);

   // Transform the system and perform the elimination in B, based on the
   // essential BC values from x. Restrict the BC part of x in X, and set the
   // non-BC part to zero. Since there is no good initial guess for the Lagrange
   // multipliers, set X = 0.0 for hybridization.
   {
      X_.SetSize(toffsets.Last());
      B_.SetSize(toffsets.Last());
      BlockVector X(X_, toffsets), B(B_, toffsets);

      // Variational restriction with P
      const Operator &P_u = *pfes_u->GetProlongationMatrix();
      const Operator &R_u = *pfes_u->GetRestrictionOperator();
      P_u.MultTranspose(b.GetBlock(0), B.GetBlock(0));
      R_u.Mult(x.GetBlock(0), X.GetBlock(0));

      const Operator &P_p = *pfes_p->GetProlongationMatrix();
      const Operator &R_p = *pfes_p->GetRestrictionOperator();
      P_p.MultTranspose(b.GetBlock(1), B.GetBlock(1));
      R_p.Mult(x.GetBlock(1), X.GetBlock(1));

      ParallelEliminateTDofsInRHS(ess_flux_tdof_list, X, B);
      if (!copy_interior)
      {
         X.GetBlock(0).SetSubVectorComplement(ess_flux_tdof_list, 0.0);
         X.GetBlock(1) = 0.;
      }
   }
}

void ParDarcyForm::FormSystemMatrix(const Array<int> &ess_flux_tdof_list,
                                    OperatorHandle &A)
{
   AllocBlockOp();

   if (block_op)
   {
      Array<int> ess_pot_tdof_list;//empty for discontinuous potentials

      if (pM_u)
      {
         pM_u->FormSystemMatrix(ess_flux_tdof_list, opM_u);
         block_op->SetDiagonalBlock(0, opM_u.Ptr());
      }
      /*else if (Mnl_u)
      {
         Operator *oper_M;
         Mnl_u->FormSystemOperator(ess_flux_tdof_list, oper_M);
         opM_u.Reset(oper_M);
         block_op->SetDiagonalBlock(0, opM_u.Ptr());
      }
      else if (Mnl)
      {
         Operator *oper_M;
         Mnl->FormSystemOperator(ess_flux_tdof_list, oper_M);
         opM.Reset(oper_M);
      }*/

      if (pM_p)
      {
         pM_p->FormSystemMatrix(ess_pot_tdof_list, opM_p);
         block_op->SetDiagonalBlock(1, opM_p.Ptr(), (bsym)?(-1.):(+1.));
      }
      /*else if (Mnl_p)
      {
         block_op->SetDiagonalBlock(1, Mnl_p, (bsym)?(-1.):(+1.));
      }*/

      if (pB)
      {
         pB->FormRectangularSystemMatrix(ess_flux_tdof_list, ess_pot_tdof_list, opB);

         ConstructBT(opB.Ptr());

         block_op->SetBlock(0, 1, opBt.Ptr(), (bsym)?(-1.):(+1.));
         block_op->SetBlock(1, 0, opB.Ptr(), (bsym)?(-1.):(+1.));
      }

   }

   A.Reset(block_op, false);
}

void ParDarcyForm::RecoverFEMSolution(const Vector &X_, const BlockVector &b,
                                      BlockVector &x)
{
   {
      BlockVector X(const_cast<Vector&>(X_), toffsets);
      if (pM_u)
      {
         pM_u->RecoverFEMSolution(X.GetBlock(0), b.GetBlock(0), x.GetBlock(0));
      }
      else
      {
         // Apply conforming prolongation
         const Operator &P_u = *pfes_u->GetProlongationMatrix();
         P_u.Mult(X.GetBlock(0), x.GetBlock(0));
      }

      if (pM_p)
      {
         pM_p->RecoverFEMSolution(X.GetBlock(1), b.GetBlock(1), x.GetBlock(1));
      }
      else
      {
         // Apply conforming prolongation
         const Operator &P_p = *pfes_p->GetProlongationMatrix();
         P_p.Mult(X.GetBlock(1), x.GetBlock(1));
      }
   }
}

void ParDarcyForm::ParallelEliminateTDofsInRHS(const Array<int> &tdofs_flux,
                                               const BlockVector &x, BlockVector &b)
{
   if (pB)
   {
      if (bsym)
      {
         //In the case of the symmetrized system, the sign is oppposite!
         Vector b_(fes_p->GetVSize());
         b_ = 0.;
         pB->ParallelEliminateTrialTDofsInRHS(tdofs_flux, x.GetBlock(0), b_);
         b.GetBlock(1) -= b_;
      }
      else
      {
         pB->ParallelEliminateTrialTDofsInRHS(tdofs_flux, x.GetBlock(0), b.GetBlock(1));
      }
   }
   if (pM_u)
   {
      pM_u->ParallelEliminateTDofsInRHS(tdofs_flux, x.GetBlock(0), b.GetBlock(0));
   }
   /*else if (Mnl_u && opM_u.Ptr())
   {
      opM_u.As<ConstrainedOperator>()->EliminateRHS(x.GetBlock(0), b.GetBlock(0));
   }
   else if (Mnl && opM.Ptr())
   {
      opM.As<ConstrainedOperator>()->EliminateRHS(x, b);
   }*/
}

void ParDarcyForm::Mult(const Vector &x, Vector &y) const
{
   const BlockVector xb(const_cast<Vector&>(x), offsets);
   BlockVector yb(y, offsets);

   if (pM_u) { pM_u->Mult(xb.GetBlock(0), yb.GetBlock(0)); }
   else { yb.GetBlock(0) = 0.; }

   if (pM_p)
   {
      pM_p->Mult(xb.GetBlock(1), yb.GetBlock(1));
      if (bsym) { yb.GetBlock(1).Neg(); }
   }
   else { yb.GetBlock(1) = 0.; }

   if (pB)
   {
      pB->AddMult(xb.GetBlock(1), yb.GetBlock(0), (bsym)?(-1.):(+1.));
      pB->AddMultTranspose(xb.GetBlock(0), yb.GetBlock(1), (bsym)?(-1.):(+1.));
   }
}

ParDarcyForm::~ParDarcyForm()
{
}

void ParDarcyForm::AllocBlockOp()
{
   delete block_op;
   block_op = new BlockOperator(toffsets);
}

const Operator *ParDarcyForm::ConstructBT(const HypreParMatrix *opB)
{
   opBt.Reset(opB->Transpose());
   return opBt.Ptr();
}
}

#endif // MFEM_USE_MPI
