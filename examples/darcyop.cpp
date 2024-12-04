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
#include <fstream>

namespace mfem
{

DarcyOperator::DarcyOperator(const Array<int> &ess_flux_tdofs_list_,
                             DarcyForm *darcy_, LinearForm *g_, LinearForm *f_, LinearForm *h_,
                             const Array<Coefficient*> &coeffs_, SolverType stype_, bool btime_u_,
                             bool btime_p_)
   : TimeDependentOperator(0, 0., IMPLICIT),
     ess_flux_tdofs_list(ess_flux_tdofs_list_), darcy(darcy_), g(g_), f(f_), h(h_),
     coeffs(coeffs_), solver_type(stype_), btime_u(btime_u_), btime_p(btime_p_)
{
   offsets = ConstructOffsets(*darcy);
   width = height = offsets.Last();

   if (darcy->GetHybridization())
   {
      trace_space = darcy->GetHybridization()->ConstraintFESpace();
   }

   if (btime_u || btime_p)
      idtcoeff = new FunctionCoefficient([&](const Vector &) { return idt; });

   if (btime_u)
   {
      BilinearForm *Mq = const_cast<BilinearForm*>(
                            (const_cast<const DarcyForm*>(darcy))->GetFluxMassForm());
      NonlinearForm *Mqnl = const_cast<NonlinearForm*>(
                               (const_cast<const DarcyForm*>(darcy))->GetFluxMassNonlinearForm());
      const int dim = darcy->FluxFESpace()->GetMesh()->Dimension();
      const bool dg = (darcy->FluxFESpace()->FEColl()->GetRangeType(
                          dim) == FiniteElement::SCALAR);
      if (Mq)
      {
         if (dg)
         {
            Mq->AddDomainIntegrator(new VectorMassIntegrator(*idtcoeff));
         }
         else
         {
            Mq->AddDomainIntegrator(new VectorFEMassIntegrator(*idtcoeff));
         }
      }
      if (Mqnl)
      {
         if (dg)
         {
            Mqnl->AddDomainIntegrator(new VectorMassIntegrator(*idtcoeff));
         }
         else
         {
            Mqnl->AddDomainIntegrator(new VectorFEMassIntegrator(*idtcoeff));
         }

         if (trace_space)
         {
            //hybridization must be reconstructed, since the non-linear
            //potential mass must be passed to it
            darcy->EnableHybridization(trace_space,
                                       new NormalTraceJumpIntegrator(),
                                       ess_flux_tdofs_list);
         }
      }
      Mq0 = new BilinearForm(darcy->FluxFESpace());
      if (dg)
      {
         Mq0->AddDomainIntegrator(new VectorMassIntegrator(*idtcoeff));
      }
      else
      {
         Mq0->AddDomainIntegrator(new VectorFEMassIntegrator(*idtcoeff));
      }
   }

   if (btime_p)
   {
      BilinearForm *Mt = const_cast<BilinearForm*>(
                            (const_cast<const DarcyForm*>(darcy))->GetPotentialMassForm());
      NonlinearForm *Mtnl = const_cast<NonlinearForm*>(
                               (const_cast<const DarcyForm*>(darcy))->GetPotentialMassNonlinearForm());
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

DarcyOperator::~DarcyOperator()
{
   delete prec;
   delete solver;
   delete monitor;
   delete Mt0;
   delete Mq0;
   delete idtcoeff;
}

Array<int> DarcyOperator::ConstructOffsets(const DarcyForm &darcy)
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

void DarcyOperator::ImplicitSolve(const real_t dt, const Vector &x_v,
                                  Vector &dx_v)
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
      if (Mq0)
      {
         Mq0->Update();
         Mq0->Assemble();
         //Mq0->Finalize();
      }
      if (Mt0)
      {
         Mt0->Update();
         Mt0->Assemble();
         //Mt0->Finalize();
      }
   }

   if (Mq0)
   {
      GridFunction u_h;
      u_h.MakeRef(darcy->FluxFESpace(), x.GetBlock(0), 0);
      Mq0->AddMult(u_h, *g, +1.);
   }

   if (Mt0)
   {
      GridFunction p_h;
      p_h.MakeRef(darcy->PotentialFESpace(), x.GetBlock(1), 0);
      Mt0->AddMult(p_h, *f, -1.);
   }
#if 0
   if (Mq0 && Mt0)
   {
      GridFunction u_h, p_h;
      u_h.MakeRef(darcy->FluxFESpace(), x.GetBlock(0), 0);
      p_h.MakeRef(darcy->PotentialFESpace(), x.GetBlock(1), 0);
      darcy->GetFluxDivForm()->AddMultTranspose(p_h, *g, -1.);
      darcy->GetFluxDivForm()->AddMult(u_h, *f, +1.);
   }
#endif
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

      // We do not want to initialize any new forms here, only obtain
      // the existing ones, so we const cast the DarcyForm
      const DarcyForm *cdarcy = const_cast<const DarcyForm*>(darcy);

      //const BilinearForm *Mq = cdarcy->GetFluxMassForm();
      const NonlinearForm *Mqnl = cdarcy->GetFluxMassNonlinearForm();
      const BlockNonlinearForm *Mnl = cdarcy->GetBlockNonlinearForm();
      //const MixedBilinearForm *B = cdarcy->GetFluxDivForm();
      //const BilinearForm *Mt = cdarcy->GetPotentialMassForm();
      const NonlinearForm *Mtnl = cdarcy->GetPotentialMassNonlinearForm();

      if (trace_space)
      {
         if (Mqnl || Mtnl || Mnl)
         {
            darcy->GetHybridization()->SetLocalNLSolver(
               DarcyHybridization::LSsolveType::Newton,
               maxIter, rtol * 1e-3, atol, -1);
            lsolver_str = "Newton";

            IterativeSolver *lin_solver = NULL;
            switch (solver_type)
            {
               case SolverType::Default:
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
               case SolverType::KINSol:
#ifdef MFEM_USE_SUNDIALS
                  lin_solver = new GMRESSolver();
                  lin_solver->SetAbsTol(atol);
                  lin_solver->SetRelTol(rtol * 1e-2);
                  lin_solver->SetMaxIter(maxIter);
                  lin_solver->SetPrintLevel(0);
                  prec = lin_solver;
                  prec_str = "GMRES";
                  solver = new KINSolver(KIN_PICARD);
                  static_cast<KINSolver*>(solver)->EnableAndersonAcc(10);
                  solver_str = "KINSol";
#else
                  MFEM_ABORT("Sundials not installed!");
#endif
                  break;
            }
         }
         else
         {
            prec = new GSSmoother(static_cast<SparseMatrix&>(*op));
            prec_str = "GS";
            solver = new GMRESSolver();
            solver_str = "GMRES";
            if (monitor_step >= 0)
            {
               monitor = new IterativeGLVis(this, monitor_step);
               solver->SetMonitor(*monitor);
            }
         }

         solver->SetAbsTol(atol);
         solver->SetRelTol(rtol);
         solver->SetMaxIter(maxIter);
         solver->SetOperator(*op);
         if (prec) { solver->SetPreconditioner(*prec); }
         solver->SetPrintLevel((btime_u || btime_p)?0:1);
      }
      else if (darcy->GetReduction())
      {
         SparseMatrix &R = *op.As<SparseMatrix>();
#ifndef MFEM_USE_SUITESPARSE
         prec = new GSSmoother(R);
         prec_str = "GS";
#else
         prec = new UMFPackSolver(R);
         prec_str = "UMFPack";
#endif

         solver = new GMRESSolver();
         solver_str = "GMRES";
         solver->SetAbsTol(atol);
         solver->SetRelTol(rtol);
         solver->SetMaxIter(maxIter);
         solver->SetOperator(*op);
         solver->SetPreconditioner(*prec);
         solver->SetPrintLevel((btime_u || btime_p)?0:1);
         solver->iterative_mode = true;
      }
      else
      {
         if ((Mqnl || Mtnl || Mnl) && solver_type != SolverType::Default)
         {
            IterativeSolver *lin_solver = NULL;
            switch (solver_type)
            {
               case SolverType::Default:
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
               case SolverType::KINSol:
#ifdef MFEM_USE_SUNDIALS
                  lin_solver = new GMRESSolver();
                  lin_solver->SetAbsTol(atol);
                  lin_solver->SetRelTol(rtol * 1e-2);
                  lin_solver->SetMaxIter(maxIter);
                  lin_solver->SetPrintLevel(0);
                  prec = lin_solver;
                  prec_str = "GMRES";
                  solver = new KINSolver(KIN_PICARD);
                  static_cast<KINSolver*>(solver)->EnableAndersonAcc(10);
                  solver_str = "KINSol";
#else
                  MFEM_ABORT("Sundials not installed!");
#endif
                  break;
            }

            if (prec)
            {
               if (ess_flux_tdofs_list.Size() > 0)
               {
                  MFEM_ABORT("Gradient is not implemented with essential DOFs!");
               }
               solver->SetOperator(*darcy);
            }
            else
            {
               solver->SetOperator(*op);
            }
         }
         else
         {
            if (Mqnl || Mtnl || Mnl)
            {
               std::cerr << "A linear solver is used for a non-linear problem!" << std::endl;
            }

            prec = new SchurPreconditioner(darcy);
            prec_str = static_cast<SchurPreconditioner*>(prec)->GetString();

            solver = new GMRESSolver();
            solver_str = "GMRES";
            solver->SetOperator(*op);
         }

         solver->SetAbsTol(atol);
         solver->SetRelTol(rtol);
         solver->SetMaxIter(maxIter);
         if (prec) { solver->SetPreconditioner(*prec); }
         solver->SetPrintLevel((btime_u || btime_p)?0:1);
         solver->iterative_mode = true;
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


DarcyOperator::SchurPreconditioner::SchurPreconditioner(const DarcyForm *darcy_,
                                                        bool nonlinear_)
   : Solver(darcy_->Height()), darcy(darcy_), nonlinear(nonlinear_)
{
   if (!nonlinear)
   {
      Vector x(Width());
      x = 0.;
      Construct(x);
   }

#ifndef MFEM_USE_SUITESPARSE
   prec_str = "GS";
#else
   prec_str = "UMFPack";
#endif
}

DarcyOperator::SchurPreconditioner::~SchurPreconditioner()
{
   delete darcyPrec;
   delete S;
}

void DarcyOperator::SchurPreconditioner::Construct(const Vector &x_v) const

{
   const Array<int> &block_offsets = darcy->GetOffsets();
   BlockVector x(x_v.GetData(), block_offsets);

   // Construct the operators for preconditioner
   //
   //                 P = [ diag(M)         0         ]
   //                     [  0       B diag(M)^-1 B^T ]
   //
   //     Here we use Symmetric Gauss-Seidel to approximate the inverse of the
   //     temperature Schur Complement

   const bool pa = (darcy->GetAssemblyLevel() != AssemblyLevel::LEGACY);

   const BilinearForm *Mq = darcy->GetFluxMassForm();
   const NonlinearForm *Mqnl = darcy->GetFluxMassNonlinearForm();
   const BlockNonlinearForm *Mnl = darcy->GetBlockNonlinearForm();
   const MixedBilinearForm *B = darcy->GetFluxDivForm();
   const BilinearForm *Mt = darcy->GetPotentialMassForm();
   const NonlinearForm *Mtnl = darcy->GetPotentialMassNonlinearForm();

   Vector Md(block_offsets[1] - block_offsets[0]);
   delete darcyPrec;
   darcyPrec = new BlockDiagonalPreconditioner(block_offsets);
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
      BlockOperator *bop = NULL;

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
         bop = static_cast<BlockOperator*>(&Mnl->GetGradient(x));

         const SparseMatrix &Mqm = static_cast<SparseMatrix&>(bop->GetBlock(0,0));

         Mqm.GetDiag(Md);
         invM = new DSmoother(Mqm);
      }

      Md.HostReadWrite();

      const SparseMatrix &Bm(B->SpMat());
      SparseMatrix *MinvBt = Transpose(Bm);

      for (int i = 0; i < Md.Size(); i++)
      {
         MinvBt->ScaleRow(i, 1./Md(i));
      }

      delete S;
      S = mfem::Mult(Bm, *MinvBt);
      delete MinvBt;

      if (Mt)
      {
         const SparseMatrix &Mtm(Mt->SpMat());
         SparseMatrix *Snew = Add(Mtm, *S);
         delete S;
         S = Snew;
      }
      else if (Mtnl)
      {
         const SparseMatrix &Mtm = static_cast<SparseMatrix&>(
                                      Mtnl->GetGradient(x.GetBlock(1)));
         SparseMatrix *Snew = Add(Mtm, *S);
         delete S;
         S = Snew;
      }
      if (Mnl)
      {
         const SparseMatrix &Mtm = static_cast<SparseMatrix&>(bop->GetBlock(1,1));
         if (Mtm.NumNonZeroElems() > 0)
         {
            SparseMatrix *Snew = Add(Mtm, *S);
            delete S;
            S = Snew;
         }
      }

#ifndef MFEM_USE_SUITESPARSE
      invS = new GSSmoother(*S);
#else
      invS = new UMFPackSolver(*S);
#endif
   }

   invM->iterative_mode = false;
   invS->iterative_mode = false;

   darcyPrec->SetDiagonalBlock(0, invM);
   darcyPrec->SetDiagonalBlock(1, invS);
}

DarcyOperator::IterativeGLVis::IterativeGLVis(DarcyOperator *p_, int step_)
   : p(p_), step(step_)
{
   const char vishost[] = "localhost";
   const int  visport   = 19916;
   q_sock.open(vishost, visport);
   q_sock.precision(8);
   t_sock.open(vishost, visport);
   t_sock.precision(8);
}

void DarcyOperator::IterativeGLVis::MonitorSolution(int it, real_t norm,
                                                    const Vector &X, bool final)
{
   if (step != 0 && it % step != 0 && !final) { return; }

   BlockVector x(p->darcy->GetOffsets()); x = 0.;
   BlockVector rhs(p->g->GetData(), p->darcy->GetOffsets());
   p->darcy->RecoverFEMSolution(X, rhs, x);

   GridFunction q_h(p->darcy->FluxFESpace(), x.GetBlock(0));
   GridFunction t_h(p->darcy->PotentialFESpace(), x.GetBlock(1));

   //heat flux

   std::stringstream ss;
   ss.str("");
   ss << "mesh_" << it << ".mesh";
   std::ofstream ofs(ss.str());
   q_h.FESpace()->GetMesh()->Print(ofs);
   ofs.close();

   q_sock << "solution\n" << *q_h.FESpace()->GetMesh() << q_h << std::endl;
   if (it == 0)
   {
      q_sock << "window_title 'Heat flux'" << std::endl;
      q_sock << "keys Rljvvvvvmmc" << std::endl;
   }

   ss.str("");
   ss << "qh_" << std::setfill('0') << std::setw(5) << it << ".gf";
   q_h.Save(ss.str().c_str());

   //temperature

   t_sock << "solution\n" << *t_h.FESpace()->GetMesh() << t_h << std::endl;
   if (it == 0)
   {
      t_sock << "window_title 'Temperature'" << std::endl;
      t_sock << "keys Rljmmc" << std::endl;
   }

   ss.str("");
   ss << "th_" << std::setfill('0') << std::setw(5) << it << ".gf";
   t_h.Save(ss.str().c_str());
}

}
