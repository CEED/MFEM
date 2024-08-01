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

#ifndef MFEM_LPQ_COMMON_HPP
#define MFEM_LPQ_COMMON_HPP

#include "mfem.hpp"
#include "miniapps/common/mfem-common.hpp"
#include <fstream>
#include <iostream>

using namespace std;
using namespace mfem;

namespace lpq_common
{

extern int NDIGITS;
extern int MG_MAX_ITER;
extern real_t MG_REL_TOL;

extern int dim;
extern int space_dim;
extern real_t freq;
extern real_t kappa;

// Enumerator for the different solvers to implement
enum SolverType
{
   sli,
   cg,
   num_solvers,  // last
};

// Enumerator for the different integrators to implement
enum IntegratorType
{
   mass,
   diffusion,
   elasticity,
   maxwell,
   num_integrators,  // last
};

/// @brief Custom monitor that prints a csv-formatted file
class DataMonitor : public IterativeSolverMonitor
{
private:
   ofstream os;
   int precision;
public:
   DataMonitor(string file_name, int ndigits);
   void MonitorResidual(int it, real_t norm, const Vector &x, bool final);
   void MonitorSolution(int it, real_t norm, const Vector &x, bool final);
};

/// @brief Custom general geometric multigrid method.
///
/// Intermediate levels use L(p,q)-Jacobi preconditioner.
/// Coarsest level uses a used-selected solver with an
/// L(p,q)-Jacobi preconditioner.
class LpqGeometricMultigrid : public GeometricMultigrid
{
public:
   LpqGeometricMultigrid(ParFiniteElementSpaceHierarchy& fes_hierarchy,
                         Array<int>& ess_bdr,
                         IntegratorType it,
                         SolverType st,
                         real_t p_order,
                         real_t q_order);

   ~LpqGeometricMultigrid() { delete coarse_pc; }

private:
   IntegratorType integrator_type;
   SolverType solver_type;
   real_t p_order;
   real_t q_order;
   Solver* coarse_solver;
   OperatorLpqJacobiSmoother* coarse_pc;
   ConstantCoefficient one;

   void ConstructCoarseOperatorAndSolver(ParFiniteElementSpace& coarse_fespace);

   void ConstructOperatorAndSmoother(ParFiniteElementSpace& fespace, int level);

   void ConstructBilinearForm(ParFiniteElementSpace& fespace,
                              bool partial_assembly = true);

};

real_t diffusion_solution(const Vector &x);
real_t diffusion_source(const Vector &x);

void elasticity_solution(const Vector &x, Vector &u);
void elasticity_source(const Vector &x, Vector &f);

void maxwell_solution(const Vector &x, Vector &u);
void maxwell_source(const Vector &x, Vector &f);

} // namespace lpq_jacobi
#endif // MFEM_LPQ_COMMON_HPP