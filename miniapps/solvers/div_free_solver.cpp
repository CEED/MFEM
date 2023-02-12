// Copyright (c) 2010-2022, Lawrence Livermore National Security, LLC. Produced
// at the Lawrence Livermore National Laboratory. All Rights reserved. See files
// LICENSE and NOTICE for details. LLNL-CODE-806117.
//
// This file is part of the MFEM library. For more information and source code
// availability visit https://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the BSD-3 license. We welcome feedback and contributions, see file
// CONTRIBUTING.md for details.

#include "div_free_solver.hpp"

using namespace std;
using namespace mfem;
using namespace blocksolvers;

void SetOptions(IterativeSolver& solver, const IterSolveParameters& param)
{
   solver.SetPrintLevel(param.print_level);
   solver.SetMaxIter(param.max_iter);
   solver.SetAbsTol(param.abs_tol);
   solver.SetRelTol(param.rel_tol);
}

HypreParMatrix* TwoStepsRAP(const HypreParMatrix& Rt, const HypreParMatrix& A,
                            const HypreParMatrix& P)
{
   OperatorPtr R(Rt.Transpose());
   OperatorPtr RA(ParMult(R.As<HypreParMatrix>(), &A));
   return ParMult(RA.As<HypreParMatrix>(), &P, true);
}

void GetRowColumnsRef(const SparseMatrix& A, int row, Array<int>& cols)
{
   cols.MakeRef(const_cast<int*>(A.GetRowColumns(row)), A.RowSize(row));
}

SparseMatrix ElemToDof(const ParFiniteElementSpace& fes)
{
   int* I = new int[fes.GetNE()+1];
   copy_n(fes.GetElementToDofTable().GetI(), fes.GetNE()+1, I);
   Array<int> J(new int[I[fes.GetNE()]], I[fes.GetNE()]);
   copy_n(fes.GetElementToDofTable().GetJ(), J.Size(), J.begin());
   fes.AdjustVDofs(J);
   double* D = new double[J.Size()];
   fill_n(D, J.Size(), 1.0);
   return SparseMatrix(I, J, D, fes.GetNE(), fes.GetVSize());
}

DFSSpaces::DFSSpaces(int order, int num_refine, ParMesh *mesh,
                     const Array<int>& ess_attr, const DFSParameters& param)
   : hdiv_fec_(order, mesh->Dimension()), l2_fec_(order, mesh->Dimension()),
     l2_0_fec_(0, mesh->Dimension()), ess_bdr_attr_(ess_attr), level_(0)
{
   if (mesh->GetElement(0)->GetType() == Element::TETRAHEDRON && order)
   {
      mfem_error("DFSDataCollector: High order spaces on tetrahedra are not supported");
   }

   data_.param = param;

   if (mesh->Dimension() == 3)
   {
      hcurl_fec_.reset(new ND_FECollection(order+1, mesh->Dimension()));
   }
   else
   {
      hcurl_fec_.reset(new H1_FECollection(order+1, mesh->Dimension()));
   }

   all_bdr_attr_.SetSize(ess_attr.Size(), 1);
   hdiv_fes_.reset(new ParFiniteElementSpace(mesh, &hdiv_fec_));
   l2_fes_.reset(new ParFiniteElementSpace(mesh, &l2_fec_));
   coarse_hdiv_fes_.reset(new ParFiniteElementSpace(*hdiv_fes_));
   coarse_l2_fes_.reset(new ParFiniteElementSpace(*l2_fes_));
   l2_0_fes_.reset(new ParFiniteElementSpace(mesh, &l2_0_fec_));
   l2_0_fes_->SetUpdateOperatorType(Operator::MFEM_SPARSEMAT);
   el_l2dof_.reserve(num_refine+1);
   el_l2dof_.push_back(ElemToDof(*coarse_l2_fes_));

   data_.agg_hdivdof.resize(num_refine);
   data_.agg_l2dof.resize(num_refine);
   data_.P_hdiv.resize(num_refine, OperatorPtr(Operator::Hypre_ParCSR));
   data_.P_l2.resize(num_refine, OperatorPtr(Operator::Hypre_ParCSR));
   data_.Q_l2.resize(num_refine);
   hdiv_fes_->GetEssentialTrueDofs(ess_attr, data_.coarsest_ess_hdivdofs);
   data_.C.resize(num_refine+1);

   hcurl_fes_.reset(new ParFiniteElementSpace(mesh, hcurl_fec_.get()));
   coarse_hcurl_fes_.reset(new ParFiniteElementSpace(*hcurl_fes_));
   data_.P_hcurl.resize(num_refine, OperatorPtr(Operator::Hypre_ParCSR));
}

SparseMatrix* AggToInteriorDof(const Array<int>& bdr_truedofs,
                               const SparseMatrix& agg_elem,
                               const SparseMatrix& elem_dof,
                               const HypreParMatrix& dof_truedof,
                               Array<HYPRE_BigInt>& agg_starts)
{
   OperatorPtr agg_dof(Mult(agg_elem, elem_dof));
   SparseMatrix& agg_dof_ref = *agg_dof.As<SparseMatrix>();
   OperatorPtr agg_tdof(dof_truedof.LeftDiagMult(agg_dof_ref, agg_starts));
   OperatorPtr agg_tdof_T(agg_tdof.As<HypreParMatrix>()->Transpose());
   SparseMatrix tdof_agg, is_shared;
   HYPRE_BigInt* trash;
   agg_tdof_T.As<HypreParMatrix>()->GetDiag(tdof_agg);
   agg_tdof_T.As<HypreParMatrix>()->GetOffd(is_shared, trash);

   int * I = new int [tdof_agg.NumRows()+1]();
   int * J = new int[tdof_agg.NumNonZeroElems()];

   Array<int> is_bdr;
   FiniteElementSpace::ListToMarker(bdr_truedofs, tdof_agg.NumRows(), is_bdr);

   int counter = 0;
   for (int i = 0; i < tdof_agg.NumRows(); ++i)
   {
      bool agg_bdr = is_bdr[i] || is_shared.RowSize(i) || tdof_agg.RowSize(i)>1;
      if (agg_bdr) { I[i+1] = I[i]; continue; }
      I[i+1] = I[i] + 1;
      J[counter++] = tdof_agg.GetRowColumns(i)[0];
   }

   double * D = new double[I[tdof_agg.NumRows()]];
   std::fill_n(D, I[tdof_agg.NumRows()], 1.0);

   SparseMatrix intdof_agg(I, J, D, tdof_agg.NumRows(), tdof_agg.NumCols());
   return Transpose(intdof_agg);
}

void DFSSpaces::MakeDofRelationTables(int level)
{
   Array<HYPRE_BigInt> agg_starts(Array<HYPRE_BigInt>(l2_0_fes_->GetDofOffsets(),
                                                      2));
   auto& elem_agg = (const SparseMatrix&)*l2_0_fes_->GetUpdateOperator();
   OperatorPtr agg_elem(Transpose(elem_agg));
   SparseMatrix& agg_el = *agg_elem.As<SparseMatrix>();

   el_l2dof_.push_back(ElemToDof(*l2_fes_));
   data_.agg_l2dof[level].Reset(Mult(agg_el, el_l2dof_[level+1]));

   Array<int> bdr_tdofs;
   hdiv_fes_->GetEssentialTrueDofs(all_bdr_attr_, bdr_tdofs);
   auto tmp = AggToInteriorDof(bdr_tdofs, agg_el, ElemToDof(*hdiv_fes_),
                               *hdiv_fes_->Dof_TrueDof_Matrix(), agg_starts);
   data_.agg_hdivdof[level].Reset(tmp);
}

void DFSSpaces::CollectDFSData()
{
   auto GetP = [this](OperatorPtr& P, unique_ptr<ParFiniteElementSpace>& cfes,
                      ParFiniteElementSpace& fes, bool remove_zero)
   {
      fes.Update();
      fes.GetTrueTransferOperator(*cfes, P);
      if (remove_zero)
      {
         P.As<HypreParMatrix>()->DropSmallEntries(1e-16);
      }
      (level_ < (int)data_.P_l2.size()-1) ? cfes->Update() : cfes.reset();
   };

   GetP(data_.P_hdiv[level_], coarse_hdiv_fes_, *hdiv_fes_, true);
   GetP(data_.P_l2[level_], coarse_l2_fes_, *l2_fes_, false);
   MakeDofRelationTables(level_);

   GetP(data_.P_hcurl[level_], coarse_hcurl_fes_, *hcurl_fes_, true);

   Vector trash1(hcurl_fes_->GetVSize()), trash2(hdiv_fes_->GetVSize());
   ParDiscreteLinearOperator curl(hcurl_fes_.get(), hdiv_fes_.get());
   curl.AddDomainInterpolator(new CurlInterpolator);
   curl.Assemble();
   curl.EliminateTrialDofs(ess_bdr_attr_, trash1, trash2);
   curl.Finalize();
   data_.C[level_+1].Reset(curl.ParallelAssemble());

   ++level_;

   if (level_ == (int)data_.P_l2.size()) { DataFinalize(); }
}

void DFSSpaces::DataFinalize()
{
   ParBilinearForm mass(l2_fes_.get());
   mass.AddDomainIntegrator(new MassIntegrator());
   mass.Assemble();
   mass.Finalize();
   OperatorPtr W(mass.LoseMat());

   SparseMatrix P_l2;
   for (int l = (int)data_.P_l2.size()-1; l >= 0; --l)
   {
      data_.P_l2[l].As<HypreParMatrix>()->GetDiag(P_l2);
      OperatorPtr PT_l2(Transpose(P_l2));
      auto PTW = Mult(*PT_l2.As<SparseMatrix>(), *W.As<SparseMatrix>());
      auto cW = Mult(*PTW, P_l2);
      auto cW_inv = new SymDirectSubBlockSolver(*cW, el_l2dof_[l]);
      data_.Q_l2[l].Reset(new ProductOperator(cW_inv, PTW, true, true));
      W.Reset(cW);
   }

   l2_0_fes_.reset();
}

void DarcySolver::EliminateEssentialBC(const Vector &ess_data,
                                       Vector &rhs) const
{
   BlockVector blk_ess_data(ess_data.GetData(), offsets_);
   BlockVector blk_rhs(rhs, offsets_);
   M_e_->Mult(-1.0, blk_ess_data.GetBlock(0), 1.0, blk_rhs.GetBlock(0));
   B_e_->Mult(-1.0, blk_ess_data.GetBlock(0), 1.0, blk_rhs.GetBlock(1));
   for (int dof : ess_tdof_list_) { rhs[dof] = ess_data[dof]; }
}

BBTSolver::BBTSolver(const HypreParMatrix& B, IterSolveParameters param)
   : Solver(B.NumRows()), BBT_solver_(B.GetComm())
{
   OperatorPtr BT(B.Transpose());
   BBT_.Reset(ParMult(&B, BT.As<HypreParMatrix>()));
   BBT_.As<HypreParMatrix>()->CopyColStarts();

   BBT_prec_.Reset(new HypreBoomerAMG(*BBT_.As<HypreParMatrix>()));
   BBT_prec_.As<HypreBoomerAMG>()->SetPrintLevel(0);

   SetOptions(BBT_solver_, param);
   BBT_solver_.SetOperator(*BBT_);
   BBT_solver_.SetPreconditioner(*BBT_prec_.As<HypreBoomerAMG>());
}

LocalSolver::LocalSolver(const DenseMatrix& M, const DenseMatrix& B)
   : Solver(M.NumRows()+B.NumRows()), local_system_(height), offset_(M.NumRows())
{
   local_system_.CopyMN(M, 0, 0);
   local_system_.CopyMN(B, offset_, 0);
   local_system_.CopyMNt(B, 0, offset_);

   local_system_.SetRow(offset_, 0.0);
   local_system_.SetCol(offset_, 0.0);
   local_system_(offset_, offset_) = -1.0;
   local_solver_.SetOperator(local_system_);
}

void LocalSolver::Mult(const Vector &x, Vector &y) const
{
   const double x0 = x[offset_];
   const_cast<Vector&>(x)[offset_] = 0.0;

   y.SetSize(local_system_.NumRows());
   local_solver_.Mult(x, y);

   const_cast<Vector&>(x)[offset_] = x0;
}

SaddleSchwarzSmoother::SaddleSchwarzSmoother(const HypreParMatrix& M,
                                             const HypreParMatrix& B,
                                             const SparseMatrix& agg_hdivdof,
                                             const SparseMatrix& agg_l2dof,
                                             const HypreParMatrix& P_l2,
                                             const HypreParMatrix& Q_l2)
   : Solver(M.NumRows() + B.NumRows()), agg_hdivdof_(agg_hdivdof),
     agg_l2dof_(agg_l2dof), solvers_loc_(agg_l2dof.NumRows())
{
   coarse_l2_projector_.Reset(new ProductOperator(&P_l2, &Q_l2, false, false));

   offsets_loc_.SetSize(3, 0);
   offsets_.SetSize(3, 0);
   offsets_[1] = M.NumRows();
   offsets_[2] = M.NumRows() + B.NumRows();

   SparseMatrix M_diag, B_diag;
   M.GetDiag(M_diag);
   B.GetDiag(B_diag);

   DenseMatrix B_loc, M_loc;

   for (int agg = 0; agg < (int)solvers_loc_.size(); agg++)
   {
      GetRowColumnsRef(agg_hdivdof_, agg, hdivdofs_loc_);
      GetRowColumnsRef(agg_l2dof_, agg, l2dofs_loc_);
      M_loc.SetSize(hdivdofs_loc_.Size(), hdivdofs_loc_.Size());
      B_loc.SetSize(l2dofs_loc_.Size(), hdivdofs_loc_.Size());
      M_diag.GetSubMatrix(hdivdofs_loc_, hdivdofs_loc_, M_loc);
      B_diag.GetSubMatrix(l2dofs_loc_, hdivdofs_loc_, B_loc);
      solvers_loc_[agg].Reset(new LocalSolver(M_loc, B_loc));
   }
}

void SaddleSchwarzSmoother::Mult(const Vector & x, Vector & y) const
{
   y.SetSize(offsets_[2]);
   y = 0.0;

   BlockVector blk_y(y.GetData(), offsets_);
   BlockVector Pi_x(offsets_); // aggregate-wise average free projection of x
   static_cast<Vector&>(Pi_x) = x;

   // Right hand side: F_l = F - W_l P_l2[l] (W_{l+1})^{-1} P_l2[l]^T F
   // This ensures the existence of solutions to the local problems
   Vector coarse_l2_projection(Pi_x.BlockSize(1));
   coarse_l2_projector_->MultTranspose(Pi_x.GetBlock(1), coarse_l2_projection);

   Pi_x.GetBlock(1) -= coarse_l2_projection;

   for (int agg = 0; agg < (int)solvers_loc_.size(); agg++)
   {
      GetRowColumnsRef(agg_hdivdof_, agg, hdivdofs_loc_);
      GetRowColumnsRef(agg_l2dof_, agg, l2dofs_loc_);

      offsets_loc_[1] = hdivdofs_loc_.Size();
      offsets_loc_[2] = offsets_loc_[1]+l2dofs_loc_.Size();

      BlockVector rhs_loc(offsets_loc_), sol_loc(offsets_loc_);
      Pi_x.GetBlock(0).GetSubVector(hdivdofs_loc_, rhs_loc.GetBlock(0));
      Pi_x.GetBlock(1).GetSubVector(l2dofs_loc_, rhs_loc.GetBlock(1));

      solvers_loc_[agg]->Mult(rhs_loc, sol_loc);

      blk_y.GetBlock(0).AddElementVector(hdivdofs_loc_, sol_loc.GetBlock(0));
      blk_y.GetBlock(1).AddElementVector(l2dofs_loc_, sol_loc.GetBlock(1));
   }

   coarse_l2_projector_->Mult(blk_y.GetBlock(1), coarse_l2_projection);
   blk_y.GetBlock(1) -= coarse_l2_projection;
}

BDPMinres::BDPMinres(HypreParMatrix& M, HypreParMatrix& B,
                     IterSolveParameters param)
   : DarcySolver(M.NumRows(), B.NumRows()), op_(offsets_), prec_(offsets_),
     BT_(B.Transpose()), solver_(M.GetComm())
{
   op_.SetBlock(0,0, &M);
   op_.SetBlock(0,1, BT_.As<HypreParMatrix>());
   op_.SetBlock(1,0, &B);

   Vector Md;
   M.GetDiag(Md);
   BT_.As<HypreParMatrix>()->InvScaleRows(Md);
   S_.Reset(ParMult(&B, BT_.As<HypreParMatrix>()));
   BT_.As<HypreParMatrix>()->ScaleRows(Md);

   prec_.SetDiagonalBlock(0, new HypreDiagScale(M));
   prec_.SetDiagonalBlock(1, new HypreBoomerAMG(*S_.As<HypreParMatrix>()));
   static_cast<HypreBoomerAMG&>(prec_.GetDiagonalBlock(1)).SetPrintLevel(0);
   prec_.owns_blocks = true;

   SetOptions(solver_, param);
   solver_.SetOperator(op_);
   solver_.SetPreconditioner(prec_);
}

void BDPMinres::Mult(const Vector & x, Vector & y) const
{
   Vector x_e(x);
   if (rhs_needs_elimination_) { EliminateEssentialBC(y, x_e);}
   solver_.Mult(x_e, y);
   for (int dof : ess_zero_dofs_) { y[dof] = 0.0; }
}

DivFreeSolver::DivFreeSolver(const HypreParMatrix &M, const HypreParMatrix& B,
                             const DFSData& data)
   : DarcySolver(M.NumRows(), B.NumRows()), data_(data), param_(data.param),
     BT_(B.Transpose()), BBT_solver_(B, param_.BBT_solve_param),
     ops_offsets_(data.P_l2.size()+1), ops_(ops_offsets_.size()),
     blk_Ps_(ops_.Size()-1), smoothers_(ops_.Size())
{
   ops_offsets_.back().MakeRef(DarcySolver::offsets_);
   ops_.Last() = new BlockOperator(ops_offsets_.back());
   ops_.Last()->SetBlock(0, 0, const_cast<HypreParMatrix*>(&M));
   ops_.Last()->SetBlock(1, 0, const_cast<HypreParMatrix*>(&B));
   ops_.Last()->SetBlock(0, 1, BT_.Ptr());

   for (int l = data.P_l2.size(); l >= 0; --l)
   {
      auto& M_f = static_cast<HypreParMatrix&>(ops_[l]->GetBlock(0, 0));
      auto& B_f = static_cast<HypreParMatrix&>(ops_[l]->GetBlock(1, 0));

      if (l == 0)
      {
         SparseMatrix M_f_diag, B_f_diag;
         M_f.GetDiag(M_f_diag);
         B_f.GetDiag(B_f_diag);
         for (int dof : data.coarsest_ess_hdivdofs)
         {
            M_f_diag.EliminateRowCol(dof);
            B_f_diag.EliminateCol(dof);
         }

         const IterSolveParameters& param = param_.coarse_solve_param;
         auto coarse_solver = new BDPMinres(M_f, B_f, param);
         if (ops_.Size() > 1)
         {
            coarse_solver->SetEssZeroDofs(data.coarsest_ess_hdivdofs);
         }
         smoothers_[l] = coarse_solver;
         continue;
      }

      HypreParMatrix& P_hdiv_l = *data.P_hdiv[l-1].As<HypreParMatrix>();
      HypreParMatrix& P_l2_l = *data.P_l2[l-1].As<HypreParMatrix>();
      SparseMatrix& agg_hdivdof_l = *data.agg_hdivdof[l-1].As<SparseMatrix>();
      SparseMatrix& agg_l2dof_l = *data.agg_l2dof[l-1].As<SparseMatrix>();
      HypreParMatrix& Q_l2_l = *data.Q_l2[l-1].As<HypreParMatrix>();
      HypreParMatrix* C_l = data.C[l].As<HypreParMatrix>();

      auto S0 = new SaddleSchwarzSmoother(M_f, B_f, agg_hdivdof_l,
                                          agg_l2dof_l, P_l2_l, Q_l2_l);
      if (param_.coupled_solve)
      {
         auto S1 = new BlockDiagonalPreconditioner(ops_offsets_[l]);
         S1->SetDiagonalBlock(0, new AuxSpaceSmoother(M_f, C_l));
         S1->owns_blocks = true;
         smoothers_[l] = new ProductSolver(ops_[l], S0, S1, false, true, true);
      }
      else
      {
         smoothers_[l] = S0;
      }

      HypreParMatrix* M_c = TwoStepsRAP(P_hdiv_l, M_f, P_hdiv_l);
      HypreParMatrix* B_c = TwoStepsRAP(P_l2_l, B_f, P_hdiv_l);

      ops_offsets_[l-1].SetSize(3, 0);
      ops_offsets_[l-1][1] = M_c->NumRows();
      ops_offsets_[l-1][2] = M_c->NumRows() + B_c->NumRows();

      blk_Ps_[l-1] = new BlockOperator(ops_offsets_[l], ops_offsets_[l-1]);
      blk_Ps_[l-1]->SetBlock(0, 0, &P_hdiv_l);
      blk_Ps_[l-1]->SetBlock(1, 1, &P_l2_l);

      ops_[l-1] = new BlockOperator(ops_offsets_[l-1]);
      ops_[l-1]->SetBlock(0, 0, M_c);
      ops_[l-1]->SetBlock(1, 0, B_c);
      ops_[l-1]->SetBlock(0, 1, B_c->Transpose());
      ops_[l-1]->owns_blocks = true;
   }

   Array<bool> own_ops(ops_.Size());
   Array<bool> own_smoothers(smoothers_.Size());
   Array<bool> own_Ps(blk_Ps_.Size());
   own_ops = true;
   own_smoothers = true;
   own_Ps = true;

   if (data_.P_l2.size() == 0) { return; }

   if (param_.coupled_solve)
   {
      solver_.Reset(new GMRESSolver(B.GetComm()));
      solver_.As<GMRESSolver>()->SetOperator(*(ops_.Last()));
      prec_.Reset(new Multigrid(ops_, smoothers_, blk_Ps_,
                                own_ops, own_smoothers, own_Ps));
   }
   else
   {
      Array<HypreParMatrix*> ops(data_.P_hcurl.size()+1);
      Array<Solver*> smoothers(ops.Size());
      Array<HypreParMatrix*> Ps(data_.P_hcurl.size());
      own_Ps = false;

      HypreParMatrix& C_finest = *data.C.back().As<HypreParMatrix>();
      ops.Last() = TwoStepsRAP(C_finest, M, C_finest);
      ops.Last()->EliminateZeroRows();
      ops.Last()->DropSmallEntries(1e-14);

      solver_.Reset(new CGSolver(B.GetComm()));
      solver_.As<CGSolver>()->SetOperator(*ops.Last());
      smoothers.Last() = new HypreSmoother(*ops.Last());
      static_cast<HypreSmoother*>(smoothers.Last())->SetOperatorSymmetry(true);

      for (int l = Ps.Size()-1; l >= 0; --l)
      {
         Ps[l] = data_.P_hcurl[l].As<HypreParMatrix>();
         ops[l] = TwoStepsRAP(*Ps[l], *ops[l+1], *Ps[l]);
         ops[l]->DropSmallEntries(1e-14);
         smoothers[l] = new HypreSmoother(*ops[l]);
         static_cast<HypreSmoother*>(smoothers[l])->SetOperatorSymmetry(true);
      }

      prec_.Reset(new Multigrid(ops, smoothers, Ps, own_ops, own_smoothers, own_Ps));
   }

   solver_.As<IterativeSolver>()->SetPreconditioner(*prec_.As<Solver>());
   SetOptions(*solver_.As<IterativeSolver>(), param_);
}

DivFreeSolver::~DivFreeSolver()
{
   if (param_.coupled_solve) { return; }
   for (int i = 0; i < ops_.Size(); ++i)
   {
      delete ops_[i];
      delete smoothers_[i];
      if (i == ops_.Size() - 1) { break; }
      delete blk_Ps_[i];
   }
}

void DivFreeSolver::SolveParticular(const Vector& rhs, Vector& sol) const
{
   std::vector<Vector> rhss(smoothers_.Size());
   std::vector<Vector> sols(smoothers_.Size());

   rhss.back().SetDataAndSize(const_cast<Vector&>(rhs), rhs.Size());
   sols.back().SetDataAndSize(sol, sol.Size());

   for (int l = blk_Ps_.Size()-1; l >= 0; --l)
   {
      rhss[l].SetSize(blk_Ps_[l]->NumCols());
      sols[l].SetSize(blk_Ps_[l]->NumCols());

      sols[l] = 0.0;
      rhss[l] = 0.0;

      blk_Ps_[l]->MultTranspose(rhss[l+1], rhss[l]);
   }

   for (int l = 0; l < smoothers_.Size(); ++l)
   {
      smoothers_[l]->Mult(rhss[l], sols[l]);
   }

   for (int l = 0; l < blk_Ps_.Size(); ++l)
   {
      Vector P_sol(blk_Ps_[l]->NumRows());
      blk_Ps_[l]->Mult(sols[l], P_sol);
      sols[l+1] += P_sol;
   }
}

void DivFreeSolver::SolveDivFree(const Vector &rhs, Vector& sol) const
{
   Vector rhs_divfree(data_.C.back()->NumCols());
   data_.C.back()->MultTranspose(rhs, rhs_divfree);

   Vector potential_divfree(rhs_divfree.Size());
   potential_divfree = 0.0;
   solver_->Mult(rhs_divfree, potential_divfree);

   data_.C.back()->Mult(potential_divfree, sol);
}

void DivFreeSolver::SolvePotential(const Vector& rhs, Vector& sol) const
{
   Vector rhs_p(BT_->NumCols());
   BT_->MultTranspose(rhs, rhs_p);
   BBT_solver_.Mult(rhs_p, sol);
}

void DivFreeSolver::Mult(const Vector & x, Vector & y) const
{
   MFEM_VERIFY(x.Size() == offsets_[2], "MLDivFreeSolver: x size is invalid");
   MFEM_VERIFY(y.Size() == offsets_[2], "MLDivFreeSolver: y size is invalid");

   Vector x_e(x);
   if (rhs_needs_elimination_) { EliminateEssentialBC(y, x_e);}

   if (ops_.Size() == 1) { smoothers_[0]->Mult(x_e, y); return; }

   BlockVector blk_y(y, offsets_);

   BlockVector resid(offsets_);
   ops_.Last()->Mult(y, resid);
   add(1.0, x_e, -1.0, resid, resid);

   BlockVector correction(offsets_);
   correction = 0.0;

   if (param_.coupled_solve)
   {
      solver_->Mult(resid, correction);
      y += correction;
   }
   else
   {
      StopWatch ch;
      ch.Start();

      SolveParticular(resid, correction);
      blk_y += correction;

      if (param_.verbose)
      {
         cout << "Particular solution found in " << ch.RealTime() << "s.\n";
      }

      ch.Clear();
      ch.Start();

      ops_.Last()->Mult(y, resid);
      add(1.0, x_e, -1.0, resid, resid);

      SolveDivFree(resid.GetBlock(0), correction.GetBlock(0));
      blk_y.GetBlock(0) += correction.GetBlock(0);

      if (param_.verbose)
      {
         cout << "Divergence free solution found in " << ch.RealTime() << "s.\n";
      }

      ch.Clear();
      ch.Start();

      auto M = dynamic_cast<HypreParMatrix&>(ops_.Last()->GetBlock(0, 0));
      M.Mult(-1.0, correction.GetBlock(0), 1.0, resid.GetBlock(0));
      SolvePotential(resid.GetBlock(0), correction.GetBlock(1));
      blk_y.GetBlock(1) += correction.GetBlock(1);

      if (param_.verbose)
      {
         cout << "Scalar potential found in " << ch.RealTime() << "s.\n";
      }
   }
}

int DivFreeSolver::GetNumIterations() const
{
   if (ops_.Size() == 1)
   {
      return static_cast<BDPMinres*>(smoothers_[0])->GetNumIterations();
   }
   return solver_.As<IterativeSolver>()->GetNumIterations();
}

BlockHybridizationSolver::BlockHybridizationSolver(const shared_ptr<ParBilinearForm> &a,
                                                   const shared_ptr<ParMixedBilinearForm> &b,
                                                   const IterSolveParameters &param,
                                                   const Array<int> &ess_bdr_attr)
   : DarcySolver(a->ParFESpace()->GetTrueVSize(), b->TestParFESpace()->GetTrueVSize()),
     trial_space(*a->ParFESpace()), test_space(*b->TestParFESpace()), elimination_(false),
     solver_(a->ParFESpace()->GetComm())
{
    ParMesh &pmesh(*trial_space.GetParMesh());
    const int ne = pmesh.GetNE();

    hat_offsets.SetSize(ne + 1);
    hat_offsets[0] = 0;
    for (int i = 0; i < ne; ++i)
    {
        hat_offsets[i + 1] = trial_space.GetFE(i)->GetDof();
    }
    hat_offsets.PartialSum();

    data_offsets.SetSize(ne + 1);
    data_offsets[0] = 0;

    ipiv_offsets.SetSize(ne + 1);
    ipiv_offsets[0] = 0;

    test_offsets.SetSize(ne + 1);
    test_offsets[0] = 0;

    for (int i = 0; i < ne; ++i)
    {
        const int trial_size = trial_space.GetFE(i)->GetDof();
        test_offsets[i + 1] = test_space.GetFE(i)->GetDof();
        const int matrix_size = trial_size + test_offsets[i + 1];

        data_offsets[i + 1] = data_offsets[i] + matrix_size*matrix_size;
        ipiv_offsets[i + 1] = ipiv_offsets[i] + matrix_size;
    }
    test_offsets.PartialSum();

    data = new double[data_offsets.Last()]();
    ipiv = new int[ipiv_offsets.Last()];

    Array<int> ess_dof_marker;
    for (int attr : ess_bdr_attr)
    {
        if (attr)
        {
            elimination_ = true;
            trial_space.GetEssentialVDofs(ess_bdr_attr, ess_dof_marker);
            break;
        }
    }

    const int order = trial_space.FEColl()->GetOrder()-1;
    DG_Interface_FECollection fec(order, pmesh.Dimension());
    c_fes = new ParFiniteElementSpace(&pmesh, &fec);
    ParFiniteElementSpace &c_space(*c_fes);

    Ct = new SparseMatrix(hat_offsets.Last(), c_space.GetNDofs());
    Array<int> dofs, c_dofs;
    const double eps = 1e-12;
    DenseMatrix elmat;
    FaceElementTransformations *FTr;
    NormalTraceJumpIntegrator c_int;
    const int num_faces = pmesh.GetNumFaces();

    for (int i = 0; i < num_faces; ++i)
    {
        FTr = pmesh.GetInteriorFaceTransformations(i);
        if (!FTr) 
        {
            continue;
        }

        int o1 = hat_offsets[FTr->Elem1No];
        int s1 = hat_offsets[FTr->Elem1No + 1] - o1;
        int o2 = hat_offsets[FTr->Elem2No];
        int s2 = hat_offsets[FTr->Elem2No + 1] - o2;

        dofs.SetSize(s1 + s2);
        for (int j = 0; j < s1; ++j)
        {
            dofs[j] = o1 + j;
        }
        for (int j = 0; j < s2; ++j)
        {
            dofs[s1 + j] = o2 + j;
        }
        c_space.GetFaceDofs(i, c_dofs);
        c_int.AssembleFaceMatrix(*c_space.GetFaceElement(i),
                                 *trial_space.GetFE(FTr->Elem1No),
                                 *trial_space.GetFE(FTr->Elem2No),
                                 *FTr,
                                 elmat);
        elmat.Threshold(eps * elmat.MaxMaxNorm());
        Ct->AddSubMatrix(dofs, c_dofs, elmat);
    }

    const int num_shared_faces = pmesh.GetNSharedFaces();
    for (int i = 0; i < num_shared_faces; ++i)
    {
        const int face_no = pmesh.GetSharedFace(i);
        FTr = pmesh.GetFaceElementTransformations(face_no);
        c_space.GetFaceDofs(face_no, c_dofs);
        const FiniteElement *face_fe(c_space.GetFaceElement(face_no));
        const FiniteElement *fe(trial_space.GetFE(FTr->Elem1No));

        int o1 = hat_offsets[FTr->Elem1No];
        int s1 = hat_offsets[FTr->Elem1No + 1] - o1;

        dofs.SetSize(s1);
        for (int j = 0; j < s1; ++j)
        {
            dofs[j] = o1 + j;
        }
        c_int.AssembleFaceMatrix(*face_fe, *fe, *fe, *FTr, elmat);
        elmat.Threshold(eps * elmat.MaxMaxNorm());
        Ct->AddSubMatrix(dofs, c_dofs, elmat);
    }
    Ct->Finalize();

    SparseMatrix H(Ct->Width());
    DenseMatrix Ct_local, Minv_Ct_local, H_local;

    Array<int> c_dof_marker(Ct->Width());
    c_dof_marker = -1;
    int c_mark_start = 0;

    for (int i = 0; i < ne; ++i)
    {
        trial_space.GetElementDofs(i, dofs);
        const int trial_size = dofs.Size();

        DenseMatrix A(trial_size);
        a->ComputeElementMatrix(i, A);
        A.Threshold(eps * A.MaxMaxNorm());

        const int test_size = test_offsets[i + 1] - test_offsets[i];
        DenseMatrix B(test_size, trial_size);
        b->ComputeElementMatrix(i, B);
        B.Neg();
        B.Threshold(eps * B.MaxMaxNorm());

        const int matrix_size = trial_size + test_size;
        DenseMatrix M(data + data_offsets[i], matrix_size, matrix_size);

        M.CopyMN(A, 0, 0);
        M.CopyMN(B, trial_size, 0);
        M.CopyMNt(B, 0, trial_size);

        if (elimination_)
        {
            FiniteElementSpace::AdjustVDofs(dofs);
            for (int j = 0; j < trial_size; ++j)
            {
                if (ess_dof_marker[dofs[j]])
                {
                    for (int k = 0; k < matrix_size; ++k)
                    {
                        if (k == j)
                        {
                            M(k, k) = 1.0;
                        }
                        else
                        {
                            M(k, j) = 0.0;
                            M(j, k) = 0.0;
                        }
                    }
                }
            }
        }

        c_dofs.SetSize(0);
        dofs.SetSize(trial_size);
        const int hat_offset = hat_offsets[i];

        for (int j = 0; j < trial_size; ++j)
        {
            const int row = hat_offset + j;
            const int ncols = Ct->RowSize(row);
            const int *cols = Ct->GetRowColumns(row);
            for (int l = 0; l < ncols; ++l)
            {
                const int c_dof = cols[l];
                if (c_dof_marker[c_dof] < c_mark_start)
                {
                    c_dof_marker[c_dof] = c_mark_start + c_dofs.Size();
                    c_dofs.Append(c_dof);
                }
            }
            dofs[j] = row;
        }
        
        Ct_local.SetSize(M.Height(), c_dofs.Size()); // Ct_local = [C 0]^T
        Ct_local = 0.0;
        for (int j = 0; j < trial_size; ++j)
        {
            const int row = dofs[j];
            const int ncols = Ct->RowSize(row);
            const int *cols = Ct->GetRowColumns(row);
            const double *vals = Ct->GetRowEntries(row);
            for (int l = 0; l < ncols; ++l)
            {
                const int loc = c_dof_marker[cols[l]] - c_mark_start;
                Ct_local(j, loc) = vals[l];
            }
        }

        LUFactors Minv(data + data_offsets[i], ipiv + ipiv_offsets[i]);
        Minv.Factor(matrix_size);
        Minv_Ct_local = Ct_local;
        Minv.Solve(Ct_local.Height(), Ct_local.Width(), Minv_Ct_local.Data());

        H_local.SetSize(Ct_local.Width());

        MultAtB(Ct_local, Minv_Ct_local, H_local);
        H.AddSubMatrix(c_dofs, c_dofs, H_local);

        c_mark_start += c_dofs.Size();
        MFEM_VERIFY(c_mark_start >= 0, "overflow");
    }
    H.Finalize(1, true);  // skip_zeros = 1 (default), fix_empty_rows = true

    pH.SetType(Operator::Hypre_ParCSR);
    OperatorPtr pP(pH.Type()), dH(pH.Type());
    pP.ConvertFrom(c_space.Dof_TrueDof_Matrix());
    dH.MakeSquareBlockDiag(c_space.GetComm(), c_space.GlobalVSize(),
                           c_space.GetDofOffsets(), &H);
    pH.MakePtAP(dH, pP);

    M = new HypreBoomerAMG(*pH.As<HypreParMatrix>());
    M->SetPrintLevel(0);

    SetOptions(solver_, param);
    solver_.SetPreconditioner(*M);
    solver_.SetOperator(*pH);
}

BlockHybridizationSolver::~BlockHybridizationSolver()
{
    delete M;
    delete [] ipiv;
    delete [] data;
    delete Ct;
    delete c_fes;
}

void BlockHybridizationSolver::Mult(const Vector &x, Vector &y) const
{
    const SparseMatrix &R = *trial_space.GetRestrictionMatrix();
    Vector x_e(x);
    if (elimination_) { EliminateEssentialBC(y, x_e);}
    Vector x0(R.Width());
    BlockVector block_x(x_e.GetData(), offsets_);
    R.MultTranspose(block_x.GetBlock(0), x0);

    ParMesh &pmesh(*trial_space.GetParMesh());
    const int ne = pmesh.GetNE();

    Array<int> block_offsets(3);
    block_offsets[0] = 0;
    block_offsets[1] = hat_offsets.Last();
    block_offsets[2] = offsets_[2] - offsets_[1];
    block_offsets.PartialSum();

    BlockVector rhs(block_offsets);
    rhs.SetVector(block_x.GetBlock(1), block_offsets[1]);

    Array<bool> dof_marker(x0.Size());
    dof_marker = false;

    Array<int> dofs;
    Vector Minv_sub_vec, g_i;
    for (int i = 0; i < ne; ++i)
    {
        trial_space.GetElementDofs(i, dofs);
        const int trial_size = dofs.Size();
        g_i.MakeRef(rhs, hat_offsets[i], trial_size);
        x0.GetSubVector(dofs, g_i);  // reverses the sign if dof < 0

        trial_space.AdjustVDofs(dofs);
        for (int j = 0; j < trial_size; ++j)
        {
            int dof = dofs[j];
            if (dof_marker[dof])
            {
                g_i(j) = 0.0;
            }
            else
            {
                dof_marker[dof] = true;
            }
        }

        const int hat_offset = hat_offsets[i];
        const int test_offset = test_offsets[i];
        dofs.SetSize(trial_size + test_size);
        for (int j = 0; j < trial_size; ++j)
        {
            dofs[j] = hat_offset + j;
        }
        const int test_size = test_offsets[i + 1] - test_offset;
        for (int j = 0; j < test_size; ++j)
        {
            dofs[trial_size + j] = block_offsets[1] + test_offset + j;
        }
        rhs.GetSubVector(dofs, Minv_sub_vec);
        LUFactors Minv(data + data_offsets[i], ipiv + ipiv_offsets[i]);
        Minv.Solve(Minv_sub_vec.Size(), 1, Minv_sub_vec.GetData());
        rhs.SetSubVector(dofs, Minv_sub_vec); // Set is okay because each dof
                                              // belongs to only one element.
    }

    Vector rhs_r(Ct->Width());
    rhs_r = 0.0;
    Ct->MultTranspose(rhs.GetBlock(0), rhs_r);

    Vector rhs_true(pH.Ptr()->Height());
    const Operator &P(*c_fes->GetProlongationMatrix());
    P.MultTranspose(rhs_r, rhs_true);

    Vector lambda_true(rhs_true.Size());
    lambda_true = 0.0;

    solver_.Mult(rhs_true, lambda_true);

    P.Mult(lambda_true, rhs_r);
    BlockVector Ct_lambda(block_offsets);
    Ct_lambda = 0.0;  // This is necessary.
    Ct->Mult(rhs_r, Ct_lambda.GetBlock(0));

    for (int i = 0; i < ne; ++i)
    {
        const int hat_offset = hat_offsets[i];
        const int trial_size = hat_offsets[i + 1] - hat_offset;
        const int test_offset = test_offsets[i];
        dofs.SetSize(trial_size + test_size);
        for (int j = 0; j < trial_size; ++j)
        {
            dofs[j] = hat_offset + j;
        }
        const int test_size = test_offsets[i + 1] - test_offset;
        for (int j = 0; j < test_size; ++j)
        {
            dofs[trial_size + j] = block_offsets[1] + test_offset + j;
        }

        LUFactors Minv(data + data_offsets[i], ipiv + ipiv_offsets[i]);
        Ct_lambda.GetSubVector(dofs, Minv_sub_vec);
        Minv.Solve(Minv_sub_vec.Size(), 1, Minv_sub_vec.GetData());
        Minv_sub_vec.Neg();
        rhs.AddElementVector(dofs, Minv_sub_vec);
    }

    dofs.SetSize(0);
    Vector sub_vec;
    for (int i = 0; i < ne; ++i)
    {
        trial_space.GetElementDofs(i, dofs);
        sub_vec.MakeRef(rhs.GetBlock(0), hat_offsets[i], dofs.Size());
        x0.SetSubVector(dofs, sub_vec);
    }
    BlockVector block_y(y, offsets_);
    R.Mult(x0, block_y.GetBlock(0));
    y.SetVector(rhs.GetBlock(1), offsets_[1]);
}
