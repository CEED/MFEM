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

#include "../../pa.hpp"
#include "../../../tmop.hpp"
#include "../../../../general/forall.hpp"

namespace mfem
{

template <int T_D1D = 0, int T_Q1D = 0, int T_MAX = 4>
void TMOP_AssembleDiagonalPA_C0_2D(const int NE,
                                   const ConstDeviceMatrix &B,
                                   const DeviceTensor<5, const real_t> &H0,
                                   DeviceTensor<4> &D,
                                   const int d1d,
                                   const int q1d,
                                   const int max)
{
   const int Q1D = T_Q1D ? T_Q1D : q1d;
   mfem::forall_2D(NE, Q1D, Q1D, [=] MFEM_HOST_DEVICE(int e)
   {
      constexpr int DIM = 2;
      const int D1D = T_D1D ? T_D1D : d1d;
      const int Q1D = T_Q1D ? T_Q1D : q1d;
      constexpr int MD1 = T_D1D ? T_D1D : DofQuadLimits::MAX_D1D;
      constexpr int MQ1 = T_Q1D ? T_Q1D : DofQuadLimits::MAX_Q1D;

      MFEM_SHARED real_t qd[MQ1 * MD1];
      DeviceTensor<2, real_t> QD(qd, MQ1, MD1);

      for (int v = 0; v < DIM; v++)
      {
         MFEM_FOREACH_THREAD(qx, x, Q1D)
         {
            MFEM_FOREACH_THREAD(dy, y, D1D)
            {
               QD(qx, dy) = 0.0;
               MFEM_UNROLL(MQ1)
               for (int qy = 0; qy < Q1D; ++qy)
               {
                  const real_t bb = B(qy, dy) * B(qy, dy);
                  QD(qx, dy) += bb * H0(v, v, qx, qy, e);
               }
            }
         }
         MFEM_SYNC_THREAD;
         MFEM_FOREACH_THREAD(dy, y, D1D)
         {
            MFEM_FOREACH_THREAD(dx, x, D1D)
            {
               real_t d = 0.0;
               MFEM_UNROLL(MQ1)
               for (int qx = 0; qx < Q1D; ++qx)
               {
                  const real_t bb = B(qx, dx) * B(qx, dx);
                  d += bb * QD(qx, dy);
               }
               D(dx, dy, v, e) += d;
            }
         }
         MFEM_SYNC_THREAD;
      }
   });
}

TMOP_REGISTER_KERNELS(TMOPAssembleDiagonalCoefKernels,
                      TMOP_AssembleDiagonalPA_C0_2D);

void TMOP_Integrator::AssembleDiagonalPA_C0_2D(Vector &diagonal) const
{
   constexpr int DIM = 2;
   const int NE = PA.ne, d = PA.maps->ndof, q = PA.maps->nqpt;

   const auto B = Reshape(PA.maps->B.Read(), q, d);
   const auto H0 = Reshape(PA.H0.Read(), DIM, DIM, q, q, NE);
   auto D = Reshape(diagonal.ReadWrite(), d, d, DIM, NE);

   const static auto specialized_kernels = []
   { return tmop::KernelSpecializations<TMOPAssembleDiagonalCoefKernels>(); }();

   TMOPAssembleDiagonalCoefKernels::Run(d, q, NE, B, H0, D, d, q, 4);
}

} // namespace mfem