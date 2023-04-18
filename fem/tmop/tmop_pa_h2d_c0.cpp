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
#include "../../general/jit/jit.hpp"
MFEM_JIT
#include "tmop_pa.hpp"

namespace mfem
{

MFEM_JIT
template<int T_D1D = 0, int T_Q1D = 0, int T_MAX = 4>
void TMOP_AssembleDiagonalPA_C0_2D(const int NE,
                                   const ConstDeviceMatrix &B,
                                   const DeviceTensor<5, const double> &H0,
                                   DeviceTensor<4> &D,
                                   const int d1d,
                                   const int q1d,
                                   const int max)
{
   const int Q1D = T_Q1D ? T_Q1D : q1d;

   mfem::forall_2D(NE, Q1D, Q1D, [=] MFEM_HOST_DEVICE (int e)
   {
      constexpr int DIM = 2;
      const int D1D = T_D1D ? T_D1D : d1d;
      const int Q1D = T_Q1D ? T_Q1D : q1d;
      constexpr int MD1 = T_D1D ? T_D1D : T_MAX;
      constexpr int MQ1 = T_Q1D ? T_Q1D : T_MAX;

      MFEM_SHARED double qd[MQ1*MD1];
      DeviceTensor<2,double> QD(qd, MQ1, MD1);

      for (int v = 0; v < DIM; v++)
      {
         MFEM_FOREACH_THREAD(qx,x,Q1D)
         {
            MFEM_FOREACH_THREAD(dy,y,D1D)
            {
               QD(qx,dy) = 0.0;
               MFEM_UNROLL(MQ1)
               for (int qy = 0; qy < Q1D; ++qy)
               {
                  const double bb = B(qy,dy) * B(qy,dy);
                  QD(qx,dy) += bb * H0(v,v,qx,qy,e);
               }
            }
         }
         MFEM_SYNC_THREAD;
         MFEM_FOREACH_THREAD(dy,y,D1D)
         {
            MFEM_FOREACH_THREAD(dx,x,D1D)
            {
               double d = 0.0;
               MFEM_UNROLL(MQ1)
               for (int qx = 0; qx < Q1D; ++qx)
               {
                  const double bb = B(qx,dx) * B(qx,dx);
                  d += bb * QD(qx,dy);
               }
               D(dx,dy,v,e) += d;
            }
         }
         MFEM_SYNC_THREAD;
      }
   });
}

void TMOP_Integrator::AssembleDiagonalPA_C0_2D(Vector &diagonal) const
{
   const int NE = PA.ne;
   constexpr int DIM = 2;
   const int D1D = PA.maps->ndof;
   const int Q1D = PA.maps->nqpt;

   const auto B = Reshape(PA.maps->B.Read(), Q1D, D1D);
   const auto H0 = Reshape(PA.H0.Read(), DIM, DIM, Q1D, Q1D, NE);
   auto D = Reshape(diagonal.ReadWrite(), D1D, D1D, DIM, NE);

   decltype(&TMOP_AssembleDiagonalPA_C0_2D<>) ker =
      TMOP_AssembleDiagonalPA_C0_2D;
#ifndef MFEM_USE_JIT
   const int d=D1D, q=Q1D;
   if (d==2 && q==2) { ker = TMOP_AssembleDiagonalPA_C0_2D<2,2>; }
   if (d==2 && q==3) { ker = TMOP_AssembleDiagonalPA_C0_2D<2,3>; }
   if (d==2 && q==4) { ker = TMOP_AssembleDiagonalPA_C0_2D<2,4>; }
   if (d==2 && q==5) { ker = TMOP_AssembleDiagonalPA_C0_2D<2,5>; }
   if (d==2 && q==6) { ker = TMOP_AssembleDiagonalPA_C0_2D<2,6>; }

   if (d==3 && q==3) { ker = TMOP_AssembleDiagonalPA_C0_2D<3,3>; }
   if (d==3 && q==4) { ker = TMOP_AssembleDiagonalPA_C0_2D<3,4>; }
   if (d==3 && q==5) { ker = TMOP_AssembleDiagonalPA_C0_2D<3,5>; }
   if (d==3 && q==6) { ker = TMOP_AssembleDiagonalPA_C0_2D<3,6>; }

   if (d==4 && q==4) { ker = TMOP_AssembleDiagonalPA_C0_2D<4,4>; }
   if (d==4 && q==5) { ker = TMOP_AssembleDiagonalPA_C0_2D<4,5>; }
   if (d==4 && q==6) { ker = TMOP_AssembleDiagonalPA_C0_2D<4,6>; }

   if (d==5 && q==5) { ker = TMOP_AssembleDiagonalPA_C0_2D<5,5>; }
   if (d==5 && q==6) { ker = TMOP_AssembleDiagonalPA_C0_2D<5,6>; }
#endif
   ker(NE,B,H0,D,D1D,Q1D,4);
}

} // namespace mfem
