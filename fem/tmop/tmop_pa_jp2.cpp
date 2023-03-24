// Copyright (c) 2010-2023, Lawrence Livermore National Security, LLC. Produced
// at the Lawrence Livermore National Laboratory. All Rights reserved. See files
// LICENSE and NOTICE for details. LLNL-CODE-806117.
//
// This file is part of the MFEM library. For more information and source code
// availability visit https://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the BSD-3 license. We welcome feedback and contributions, see file
// CONTRIBUTING.md for details.

#include "../tmop.hpp"
#include "../tmop_tools.hpp"
#include "../../general/jit/jit.hpp"
MFEM_JIT
#include "tmop_pa.hpp"

namespace mfem
{

MFEM_JIT
template<int T_D1D = 0, int T_Q1D = 0, int T_MAX = 4>
void TMOP_MinDetJpr_2D(const int NE,
                       const ConstDeviceMatrix &B,
                       const ConstDeviceMatrix &G,
                       const DeviceTensor<4, const double> &X,
                       DeviceTensor<3> &E,
                       const int d1d,
                       const int q1d,
                       const int max)
{
   constexpr int NBZ = 1;
   const int Q1D = T_Q1D ? T_Q1D : q1d;

   MFEM_FORALL_2D(e, NE, Q1D, Q1D, NBZ,
   {
      const int D1D = T_D1D ? T_D1D : d1d;
      const int Q1D = T_Q1D ? T_Q1D : q1d;
      constexpr int NBZ = 1;
      constexpr int MQ1 = T_Q1D ? T_Q1D : T_MAX;
      constexpr int MD1 = T_D1D ? T_D1D : T_MAX;

      MFEM_SHARED double BG[2][MQ1*MD1];
      MFEM_SHARED double XY[2][NBZ][MD1*MD1];
      MFEM_SHARED double DQ[4][NBZ][MD1*MQ1];
      MFEM_SHARED double QQ[4][NBZ][MQ1*MQ1];

      kernels::internal::LoadX<MD1,NBZ>(e,D1D,X,XY);
      kernels::internal::LoadBG<MD1,MQ1>(D1D,Q1D,B,G,BG);

      kernels::internal::GradX<MD1,MQ1,NBZ>(D1D,Q1D,BG,XY,DQ);
      kernels::internal::GradY<MD1,MQ1,NBZ>(D1D,Q1D,BG,DQ,QQ);

      MFEM_FOREACH_THREAD(qy,y,Q1D)
      {
         MFEM_FOREACH_THREAD(qx,x,Q1D)
         {
            double J[4];
            kernels::internal::PullGrad<MQ1,NBZ>(Q1D,qx,qy,QQ,J);
            E(qx,qy,e) = kernels::Det<2>(J);
         }
      }
   });
}

double TMOPNewtonSolver::MinDetJpr_2D(const FiniteElementSpace *fes,
                                      const Vector &x) const
{
   const ElementDofOrdering ordering = ElementDofOrdering::LEXICOGRAPHIC;
   const Operator *R = fes->GetElementRestriction(ordering);
   Vector xe(R->Height(), Device::GetDeviceMemoryType());
   xe.UseDevice(true);
   R->Mult(x, xe);

   const DofToQuad &maps = fes->GetFE(0)->GetDofToQuad(ir, DofToQuad::TENSOR);
   const int NE = fes->GetMesh()->GetNE();
   const int NQ = ir.GetNPoints();
   const int D1D = maps.ndof;
   const int Q1D = maps.nqpt;

   constexpr int DIM = 2;
   const auto B = Reshape(maps.B.Read(), Q1D, D1D);
   const auto G = Reshape(maps.G.Read(), Q1D, D1D);
   const auto XE = Reshape(xe.Read(), D1D, D1D, DIM, NE);

   Vector e(NE*NQ);
   e.UseDevice(true);
   auto E = Reshape(e.Write(), Q1D, Q1D, NE);

   decltype(&TMOP_MinDetJpr_2D<>) ker = TMOP_MinDetJpr_2D;
#ifndef MFEM_USE_JIT
   const int d=D1D, q=Q1D;
   if (d==2 && q==2) { ker = TMOP_MinDetJpr_2D<2,2>; }
   if (d==2 && q==3) { ker = TMOP_MinDetJpr_2D<2,3>; }
   if (d==2 && q==4) { ker = TMOP_MinDetJpr_2D<2,4>; }
   if (d==2 && q==5) { ker = TMOP_MinDetJpr_2D<2,5>; }
   if (d==2 && q==6) { ker = TMOP_MinDetJpr_2D<2,6>; }

   if (d==3 && q==3) { ker = TMOP_MinDetJpr_2D<3,3>; }
   if (d==3 && q==4) { ker = TMOP_MinDetJpr_2D<3,4>; }
   if (d==3 && q==5) { ker = TMOP_MinDetJpr_2D<3,5>; }
   if (d==3 && q==6) { ker = TMOP_MinDetJpr_2D<3,6>; }

   if (d==4 && q==4) { ker = TMOP_MinDetJpr_2D<4,4>; }
   if (d==4 && q==5) { ker = TMOP_MinDetJpr_2D<4,5>; }
   if (d==4 && q==6) { ker = TMOP_MinDetJpr_2D<4,6>; }

   if (d==5 && q==5) { ker = TMOP_MinDetJpr_2D<5,5>; }
   if (d==5 && q==6) { ker = TMOP_MinDetJpr_2D<5,6>; }
#endif
   ker(NE,B,G,XE,E,D1D,Q1D,4);
   return e.Min();
}

} // namespace mfem
