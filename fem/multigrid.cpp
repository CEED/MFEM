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

#include "multigrid.hpp"

namespace mfem
{

MultigridBase::MultigridBase()
   : cycleType(CycleType::VCYCLE), preSmoothingSteps(1), postSmoothingSteps(1),
     nrhs(0)
{}

MultigridBase::MultigridBase(const Array<Operator*>& operators_,
                             const Array<Solver*>& smoothers_,
                             const Array<bool>& ownedOperators_,
                             const Array<bool>& ownedSmoothers_)
   : Solver(operators_.Last()->Height(), operators_.Last()->Width()),
     cycleType(CycleType::VCYCLE), preSmoothingSteps(1), postSmoothingSteps(1),
     nrhs(0)
{
   operators_.Copy(operators);
   smoothers_.Copy(smoothers);
   ownedOperators_.Copy(ownedOperators);
   ownedSmoothers_.Copy(ownedSmoothers);
}

MultigridBase::~MultigridBase()
{
   for (int i = 0; i < operators.Size(); ++i)
   {
      if (ownedOperators[i])
      {
         delete operators[i];
      }
      if (ownedSmoothers[i])
      {
         delete smoothers[i];
      }
   }
   EraseVectors();
}

void MultigridBase::InitVectors() const
{
   if (X.NumRows() > 0 && X.NumCols() > 0) { EraseVectors(); }
   const int M = NumLevels();
   X.SetSize(M, nrhs);
   Y.SetSize(M, nrhs);
   R.SetSize(M, nrhs);
   for (int i = 0; i < M; ++i)
   {
      const int n = operators[i]->Height();
      for (int j = 0; j < nrhs; ++j)
      {
         if (i < M - 1)
         {
            X(i, j) = new Vector(n);
            Y(i, j) = new Vector(n);
         }
         R(i, j) = new Vector(n);
      }
   }
}

void MultigridBase::EraseVectors() const
{
   for (int i = 0; i < X.NumRows(); ++i)
   {
      for (int j = 0; j < X.NumCols(); ++j)
      {
         if (i < X.NumRows() - 1)
         {
            delete X(i, j);
            delete Y(i, j);
         }
         delete R(i, j);
      }
   }
}

void MultigridBase::AddLevel(Operator* op, Solver* smoother,
                             bool ownOperator, bool ownSmoother)
{
   height = op->Height();
   width = op->Width();
   operators.Append(op);
   smoothers.Append(smoother);
   ownedOperators.Append(ownOperator);
   ownedSmoothers.Append(ownSmoother);
}

void MultigridBase::SetCycleType(CycleType cycleType_, int preSmoothingSteps_,
                                 int postSmoothingSteps_)
{
   cycleType = cycleType_;
   preSmoothingSteps = preSmoothingSteps_;
   postSmoothingSteps = postSmoothingSteps_;
}

void MultigridBase::Mult(const Vector& x, Vector& y) const
{
   Array<const Vector*> X_(1);
   Array<Vector*> Y_(1);
   X_[0] = &x;
   Y_[0] = &y;
   ArrayMult(X_, Y_);
}

void MultigridBase::ArrayMult(const Array<const Vector*>& X_,
                              Array<Vector*>& Y_) const
{
   MFEM_ASSERT(operators.Size() > 0,
               "Multigrid solver does not have operators set!");
   MFEM_ASSERT(X_.Size() == Y_.Size(),
               "Number of columns mismatch in MultigridBase::Mult!");

   // Add capacity as necessary
   nrhs = X_.Size();
   if (X.NumCols() < nrhs) { InitVectors(); }

   // Perform a single cycle
   const int M = NumLevels();
   for (int j = 0; j < nrhs; ++j)
   {
      MFEM_ASSERT(X_[j] && Y_[j], "Missing Vector in MultigridBase::Mult!");
      X(M - 1, j) = const_cast<Vector*>(X_[j]);
      Y(M - 1, j) = Y_[j];
   }
   const bool zero = !iterative_mode;
   Cycle(M - 1, zero);
}

void MultigridBase::SmoothingStep(int level, bool zero, bool transpose) const
{
   // y = y + S (x - A y) or y = y + S^T (x - A y)

   // Note: 'zero' == true means that Y(level,*) are not initialized and we
   // should assume that the input they typically provide to this call is zeros.

   // We can't use the smoothers' iterative mode since we don't know if they
   // actually support it, so we always turn the iterative mode off to properly
   // use smoothers that do support it.
   smoothers[level]->iterative_mode = false;

   if (zero)
   {
      MFEM_ASSERT(!transpose, "internal error!");
      Array<Vector *> X_(X[level], nrhs), Y_(Y[level], nrhs);
      GetSmootherAtLevel(level)->ArrayMult(X_, Y_);
   }
   else
   {
      Array<Vector *> Y_(Y[level], nrhs), R_(R[level], nrhs);
      GetOperatorAtLevel(level)->ArrayMult(Y_, R_);
      for (int j = 0; j < nrhs; ++j)
      {
         // *R_[j] = *X(level, j) - *R_[j]
         subtract(*X(level, j), *R_[j], *R_[j]);
      }
      if (transpose)
      {
         GetSmootherAtLevel(level)->ArrayAddMultTranspose(R_, Y_);
      }
      else
      {
         GetSmootherAtLevel(level)->ArrayAddMult(R_, Y_);
      }
   }
}

void MultigridBase::Cycle(int level, bool zero) const
{
   // Note: 'zero' == true means that Y(level,*) are not initialized and we
   // should assume that the input they typically provide to this call is zeros.

   // Coarse solve
   if (level == 0)
   {
      SmoothingStep(0, zero, false);
      return;
   }

   // Pre-smooth
   for (int i = 0; i < preSmoothingSteps; ++i)
   {
      SmoothingStep(level, zero && (i == 0), false);
   }

   // Compute residual and restrict
   if (preSmoothingSteps == 0 && zero)
   {
      Array<Vector *> X_l(X[level], nrhs), X_lm1(X[level - 1], nrhs);
      GetProlongationAtLevel(level - 1)->ArrayMultTranspose(X_l, X_lm1);
   }
   else
   {
      Array<Vector *> Y_(Y[level], nrhs), R_(R[level], nrhs),
            X_(X[level - 1], nrhs);

      GetOperatorAtLevel(level)->ArrayMult(Y_, R_);
      for (int j = 0; j < nrhs; ++j)
      {
         // *R_[j] = *X(level, j) - *R_[j]
         subtract(*X(level, j), *R_[j], *R_[j]);
      }
      GetProlongationAtLevel(level - 1)->ArrayMultTranspose(R_, X_);
   }

   // Corrections
   Cycle(level - 1, true);
   if (cycleType == CycleType::WCYCLE)
   {
      // If the coarse solve at level 0 is "exact" solve, then we don't want to
      // repeat it.
      // To support multiple level 0 coarse-grid corrections, one can wrap that
      // smoother in an SLI solver and use that instead.
      if (level > 1) { Cycle(level - 1, false); }
   }

   // Prolongate and add
   {
      Array<Vector *> Y_lm1(Y[level - 1], nrhs), Y_l(Y[level], nrhs);
      if (preSmoothingSteps == 0 && zero)
      {
         GetProlongationAtLevel(level - 1)->ArrayMult(Y_lm1, Y_l);
      }
      else
      {
         GetProlongationAtLevel(level - 1)->ArrayAddMult(Y_lm1, Y_l);
      }
   }

   // Post-smooth
   for (int i = 0; i < postSmoothingSteps; ++i)
   {
      SmoothingStep(level, false, true);
   }
}

Multigrid::Multigrid()
   : MultigridBase()
{}

Multigrid::Multigrid(const Array<Operator*>& operators_,
                     const Array<Solver*>& smoothers_,
                     const Array<Operator*>& prolongations_,
                     const Array<bool>& ownedOperators_,
                     const Array<bool>& ownedSmoothers_,
                     const Array<bool>& ownedProlongations_)
   : MultigridBase(operators_, smoothers_, ownedOperators_, ownedSmoothers_)
{
   prolongations_.Copy(prolongations);
   ownedProlongations_.Copy(ownedProlongations);
}

Multigrid::~Multigrid()
{
   for (int i = 0; i < prolongations.Size(); ++i)
   {
      if (ownedProlongations[i])
      {
         delete prolongations[i];
      }
   }
}

GeometricMultigrid::
GeometricMultigrid(const FiniteElementSpaceHierarchy& fespaces_)
   : MultigridBase(), fespaces(fespaces_)
{}

GeometricMultigrid::~GeometricMultigrid()
{
   for (int i = 0; i < bfs.Size(); ++i)
   {
      delete bfs[i];
   }
   for (int i = 0; i < essentialTrueDofs.Size(); ++i)
   {
      delete essentialTrueDofs[i];
   }
}

void GeometricMultigrid::FormFineLinearSystem(Vector& x, Vector& b,
                                              OperatorHandle& A,
                                              Vector& X, Vector& B)
{
   bfs.Last()->FormLinearSystem(*essentialTrueDofs.Last(), x, b, A, X, B);
}

void GeometricMultigrid::RecoverFineFEMSolution(const Vector& X,
                                                const Vector& b, Vector& x)
{
   bfs.Last()->RecoverFEMSolution(X, b, x);
}

} // namespace mfem
