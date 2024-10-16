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

#include "darcyop.hpp"
#include "../general/tic_toc.hpp"

namespace mfem
{

FEOperator::FEOperator(const Array<int> &ess_flux_tdofs_list_,
                       DarcyForm *darcy_, LinearForm *g_, LinearForm *f_, LinearForm *h_,
                       const Array<Coefficient*> &coeffs_, SolverType stype_, bool btime_)
   : TimeDependentOperator(0, 0., IMPLICIT),
     ess_flux_tdofs_list(ess_flux_tdofs_list_), darcy(darcy_), g(g_), f(f_), h(h_),
     coeffs(coeffs_), solver_type(stype_), btime(btime_)
{
   offsets = ConstructOffsets(*darcy);
   width = height = offsets.Last();

   if (darcy->GetHybridization())
   {
      trace_space = darcy->GetHybridization()->ConstraintFESpace();
   }

   if (btime)
   {
      BilinearForm *Mt = const_cast<BilinearForm*>(
                            (const_cast<const DarcyForm*>(darcy))->GetPotentialMassForm());
      NonlinearForm *Mtnl = const_cast<NonlinearForm*>(
                               (const_cast<const DarcyForm*>(darcy))->GetPotentialMassNonlinearForm());
      idtcoeff = new FunctionCoefficient([&](const Vector &) { return idt; });
      if (Mt) { Mt->AddDomainIntegrator(new MassIntegrator(*idtcoeff)); }
      if (Mtnl)
      {
         Mtnl->AddDomainIntegrator(new MassIntegrator(*idtcoeff));
         if (trace_space)
         {
            //hybridization must be reconstructed, since the non-linear
            //potential mass must be passed to it
            darcy->EnableHybridization(trace_space,
                                       new NormalTraceJumpIntegrator(),
                                       ess_flux_tdofs_list);
         }
      }
      Mt0 = new BilinearForm(darcy->PotentialFESpace());
      Mt0->AddDomainIntegrator(new MassIntegrator(*idtcoeff));
   }
}

FEOperator::~FEOperator()
{
   delete solver;
   delete prec;
   delete S;
   delete Mt0;
   delete idtcoeff;
}

Array<int> FEOperator::ConstructOffsets(const DarcyForm &darcy)
{
   if (!darcy.GetHybridization())
   {
      return darcy.GetOffsets();
   }

   Array<int> offsets(4);
   offsets[0] = 0;
   offsets[1] = darcy.FluxFESpace()->GetVSize();
   offsets[2] = darcy.PotentialFESpace()->GetVSize();
   offsets[3] = darcy.GetHybridization()->ConstraintFESpace()->GetVSize();
   offsets.PartialSum();

   return offsets;
}

void FEOperator::ImplicitSolve(const real_t dt, const Vector &x_v, Vector &dx_v)
{
   //form the linear system

   BlockVector rhs(g->GetData(), darcy->GetOffsets());
   BlockVector x(dx_v.GetData(), darcy->GetOffsets());
   dx_v = x_v;

   //set time

   for (Coefficient *coeff : coeffs)
   {
      coeff->SetTime(t);
   }

   //assemble rhs

   StopWatch chrono;
   chrono.Clear();
   chrono.Start();

   g->Assemble();
   f->Assemble();
   if (h) { h->Assemble(); }

   //check if the operator has to be reassembled

   bool reassemble = (idt != 1./dt);

   if (reassemble)
   {
      idt = 1./dt;

      //reset the operator

      darcy->Update();

      //assemble the system

      darcy->Assemble();
      if (Mt0)
      {
         Mt0->Update();
         Mt0->Assemble();
         //Mt0->Finalize();

      }
   }

   if (Mt0)
   {
      GridFunction t_h;
      t_h.MakeRef(darcy->PotentialFESpace(), x.GetBlock(1), 0);
      Mt0->AddMult(t_h, *f, -1.);
   }

   //form the reduced system

   OperatorHandle op;
   Vector X, RHS;
   if (trace_space)
   {
      X.MakeRef(dx_v, offsets[2], trace_space->GetVSize());
      RHS.MakeRef(*h, 0, trace_space->GetVSize());
   }

   darcy->FormLinearSystem(ess_flux_tdofs_list, x, rhs,
                           op, X, RHS);


   chrono.Stop();
   std::cout << "Assembly took " << chrono.RealTime() << "s.\n";

   if (reassemble)
   {
      // 10. Construct the preconditioner and solver

      chrono.Clear();
      chrono.Start();

      constexpr int maxIter(1000);
      constexpr real_t rtol(1.e-6);
      constexpr real_t atol(1.e-10);

      bool pa = (darcy->GetAssemblyLevel() != AssemblyLevel::LEGACY);

      // We do not want to initialize any new forms here, only obtain
      // the existing ones, so we const cast the DarcyForm
      const DarcyForm *cdarcy = const_cast<const DarcyForm*>(darcy);

      const BilinearForm *Mq = cdarcy->GetFluxMassForm();
      const NonlinearForm *Mqnl = cdarcy->GetFluxMassNonlinearForm();
      const BlockNonlinearForm *Mnl = cdarcy->GetBlockNonlinearForm();
      const MixedBilinearForm *B = cdarcy->GetFluxDivForm();
      const BilinearForm *Mt = cdarcy->GetPotentialMassForm();
      const NonlinearForm *Mtnl = cdarcy->GetPotentialMassNonlinearForm();

      if (trace_space)
      {
         if (Mqnl || Mtnl || Mnl)
         {
            darcy->GetHybridization()->SetLocalNLSolver(
               DarcyHybridization::LSsolveType::Newton,
               maxIter, rtol * 1e-2, atol, -1);
            lsolver_str = "Newton";

            IterativeSolver *lin_solver = NULL;
            switch (solver_type)
            {
               case SolverType::LBFGS:
                  prec = NULL;
                  solver = new LBFGSSolver();
                  solver_str = "LBFGS";
                  break;
               case SolverType::LBB:
                  prec = NULL;
                  solver = new LBBSolver();
                  solver_str = "LBB";
                  break;
               case SolverType::Newton:
                  lin_solver = new GMRESSolver();
                  lin_solver->SetAbsTol(atol);
                  lin_solver->SetRelTol(rtol * 1e-2);
                  lin_solver->SetMaxIter(maxIter);
                  lin_solver->SetPrintLevel(0);
                  prec = lin_solver;
                  prec_str = "GMRES";
                  solver = new NewtonSolver();
                  solver_str = "Newton";
                  break;
            }
         }
         else
         {
            prec = new GSSmoother(static_cast<SparseMatrix&>(*op));
            prec_str = "GS";
            solver = new GMRESSolver();
            solver_str = "GMRES";
         }

         solver->SetAbsTol(atol);
         solver->SetRelTol(rtol);
         solver->SetMaxIter(maxIter);
         solver->SetOperator(*op);
         if (prec) { solver->SetPreconditioner(*prec); }
         solver->SetPrintLevel(btime?0:1);
      }
      else
      {
         // Construct the operators for preconditioner
         //
         //                 P = [ diag(M)         0         ]
         //                     [  0       B diag(M)^-1 B^T ]
         //
         //     Here we use Symmetric Gauss-Seidel to approximate the inverse of the
         //     temperature Schur Complement
         SparseMatrix *MinvBt = NULL;
         Vector Md(offsets[1] - offsets[0]);

         const Array<int> &block_offsets = darcy->GetOffsets();
         auto *darcyPrec = new BlockDiagonalPreconditioner(block_offsets);
         prec = darcyPrec;
         darcyPrec->owns_blocks = true;
         Solver *invM, *invS;

         if (pa)
         {
            Mq->AssembleDiagonal(Md);
            auto Md_host = Md.HostRead();
            Vector invMd(Mq->Height());
            for (int i=0; i<Mq->Height(); ++i)
            {
               invMd(i) = 1.0 / Md_host[i];
            }

            Vector BMBt_diag(B->Height());
            B->AssembleDiagonal_ADAt(invMd, BMBt_diag);

            Array<int> ess_tdof_list;  // empty

            invM = new OperatorJacobiSmoother(Md, ess_tdof_list);
            invS = new OperatorJacobiSmoother(BMBt_diag, ess_tdof_list);
         }
         else
         {
            // get diagonal
            if (Mq)
            {
               const SparseMatrix &Mqm(Mq->SpMat());
               Mqm.GetDiag(Md);
               invM = new DSmoother(Mqm);
            }
            else if (Mqnl)
            {
               const SparseMatrix &Mqm = static_cast<SparseMatrix&>(
                                            Mqnl->GetGradient(x.GetBlock(0)));
               Mqm.GetDiag(Md);
               invM = new DSmoother(Mqm);
            }
            else if (Mnl)
            {
               BlockOperator &bop = static_cast<BlockOperator&>(
                                       Mnl->GetGradient(x));

               const SparseMatrix &Mqm = static_cast<SparseMatrix&>(
                                            bop.GetBlock(0,0));

               Mqm.GetDiag(Md);
               invM = new DSmoother(Mqm);
            }

            Md.HostReadWrite();

            const SparseMatrix &Bm(B->SpMat());
            MinvBt = Transpose(Bm);

            for (int i = 0; i < Md.Size(); i++)
            {
               MinvBt->ScaleRow(i, 1./Md(i));
            }

            S = mfem::Mult(Bm, *MinvBt);

            if (Mt)
            {
               const SparseMatrix &Mtm(Mt->SpMat());
               SparseMatrix *Snew = Add(Mtm, *S);
               delete S;
               S = Snew;
            }
            else if (Mtnl)
            {
               const SparseMatrix &grad = static_cast<SparseMatrix&>(
                                             Mtnl->GetGradient(x.GetBlock(1)));
               SparseMatrix *Snew = Add(grad, *S);
               delete S;
               S = Snew;
            }

#ifndef MFEM_USE_SUITESPARSE
            invS = new GSSmoother(*S);
            prec_str = "GS";
#else
            invS = new UMFPackSolver(*S);
            prec_str = "UMFPack";
#endif
         }

         invM->iterative_mode = false;
         invS->iterative_mode = false;

         darcyPrec->SetDiagonalBlock(0, invM);
         darcyPrec->SetDiagonalBlock(1, invS);

         solver = new GMRESSolver();
         solver_str = "GMRES";
         solver->SetAbsTol(atol);
         solver->SetRelTol(rtol);
         solver->SetMaxIter(maxIter);
         solver->SetOperator(*op);
         solver->SetPreconditioner(*prec);
         solver->SetPrintLevel(btime?0:1);
         solver->iterative_mode = true;

         delete MinvBt;
      }

      chrono.Stop();
      std::cout << "Preconditioner took " << chrono.RealTime() << "s.\n";
   }

   // 11. Solve the linear system with GMRES.
   //     Check the norm of the unpreconditioned residual.

   chrono.Clear();
   chrono.Start();

   solver->Mult(RHS, X);
   darcy->RecoverFEMSolution(X, rhs, x);

   chrono.Stop();

   std::cout << solver_str;
   if (prec_str) { std::cout << "+" << prec_str; }
   if (lsolver_str) { std::cout << "+" << lsolver_str; }
   if (solver->GetConverged())
   {
      std::cout << " converged in " << solver->GetNumIterations()
                << " iterations with a residual norm of " << solver->GetFinalNorm()
                << ".\n";
   }
   else
   {
      std::cout << " did not converge in " << solver->GetNumIterations()
                << " iterations. Residual norm is " << solver->GetFinalNorm()
                << ".\n";
   }
   std::cout << "solver took " << chrono.RealTime() << "s.\n";

   dx_v -= x_v;
   dx_v *= idt;
}

}
