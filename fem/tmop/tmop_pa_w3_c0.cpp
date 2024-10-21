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

#include "../tmop.hpp"
#include "../../fem/kernels.hpp"
#include "../../general/forall.hpp"
#include "../../linalg/kernels.hpp"

namespace mfem
{

template <int T_D1D = 0, int T_Q1D = 0, int T_MAX = 4>
void TMOP_EnergyPA_C0_3D(const real_t lim_normal,
                         const DeviceTensor<4, const real_t> &LD,
                         const bool const_c0,
                         const DeviceTensor<4, const real_t> &C0,
                         const int NE,
                         const DeviceTensor<6, const real_t> &J,
                         const ConstDeviceCube &W,
                         const ConstDeviceMatrix &b,
                         const ConstDeviceMatrix &bld,
                         const DeviceTensor<5, const real_t> &X0,
                         const DeviceTensor<5, const real_t> &X1,
                         DeviceTensor<4> &E,
                         const bool exp_lim,
                         const int d1d,
                         const int q1d,
                         const int max)
{
   const int Q1D = T_Q1D ? T_Q1D : q1d;

   mfem::forall_3D(NE, Q1D, Q1D, Q1D, [=] MFEM_HOST_DEVICE(int e)
   {
      const int D1D = T_D1D ? T_D1D : d1d;
      const int Q1D = T_Q1D ? T_Q1D : q1d;
      constexpr int MQ1 = T_Q1D ? T_Q1D : T_MAX;
      constexpr int MD1 = T_D1D ? T_D1D : T_MAX;
      constexpr int MDQ = (MQ1 > MD1) ? MQ1 : MD1;

      MFEM_SHARED real_t B[MQ1 * MD1];
      MFEM_SHARED real_t sBLD[MQ1 * MD1];
      kernels::internal::LoadB<MD1, MQ1>(D1D, Q1D, bld, sBLD);
      ConstDeviceMatrix BLD(sBLD, D1D, Q1D);

      MFEM_SHARED real_t sm0[MDQ * MDQ * MDQ];
      MFEM_SHARED real_t sm1[MDQ * MDQ * MDQ];
      DeviceCube DDD(sm0, MD1, MD1, MD1);
      DeviceCube DDQ(sm1, MD1, MD1, MQ1);
      DeviceCube DQQ(sm0, MD1, MQ1, MQ1);
      DeviceCube QQQ(sm1, MQ1, MQ1, MQ1);

      MFEM_SHARED real_t DDD0[3][MD1 * MD1 * MD1];
      MFEM_SHARED real_t DDQ0[3][MD1 * MD1 * MQ1];
      MFEM_SHARED real_t DQQ0[3][MD1 * MQ1 * MQ1];
      MFEM_SHARED real_t QQQ0[3][MQ1 * MQ1 * MQ1];

      MFEM_SHARED real_t DDD1[3][MD1 * MD1 * MD1];
      MFEM_SHARED real_t DDQ1[3][MD1 * MD1 * MQ1];
      MFEM_SHARED real_t DQQ1[3][MD1 * MQ1 * MQ1];
      MFEM_SHARED real_t QQQ1[3][MQ1 * MQ1 * MQ1];

      kernels::internal::LoadX(e, D1D, LD, DDD);
      kernels::internal::LoadX<MD1>(e, D1D, X0, DDD0);
      kernels::internal::LoadX<MD1>(e, D1D, X1, DDD1);

      kernels::internal::LoadB<MD1, MQ1>(D1D, Q1D, b, B);

      kernels::internal::EvalX(D1D, Q1D, BLD, DDD, DDQ);
      kernels::internal::EvalY(D1D, Q1D, BLD, DDQ, DQQ);
      kernels::internal::EvalZ(D1D, Q1D, BLD, DQQ, QQQ);

      kernels::internal::EvalX<MD1, MQ1>(D1D, Q1D, B, DDD0, DDQ0);
      kernels::internal::EvalY<MD1, MQ1>(D1D, Q1D, B, DDQ0, DQQ0);
      kernels::internal::EvalZ<MD1, MQ1>(D1D, Q1D, B, DQQ0, QQQ0);

      kernels::internal::EvalX<MD1, MQ1>(D1D, Q1D, B, DDD1, DDQ1);
      kernels::internal::EvalY<MD1, MQ1>(D1D, Q1D, B, DDQ1, DQQ1);
      kernels::internal::EvalZ<MD1, MQ1>(D1D, Q1D, B, DQQ1, QQQ1);

      MFEM_FOREACH_THREAD(qz, z, Q1D)
      {
         MFEM_FOREACH_THREAD(qy, y, Q1D)
         {
            MFEM_FOREACH_THREAD(qx, x, Q1D)
            {
               real_t D, p0[3], p1[3];
               const real_t *Jtr = &J(0, 0, qx, qy, qz, e);
               const real_t detJtr = kernels::Det<3>(Jtr);
               const real_t weight = W(qx, qy, qz) * detJtr;
               const real_t coeff0 =
                  const_c0 ? C0(0, 0, 0, 0) : C0(qx, qy, qz, e);

               kernels::internal::PullEval(qx, qy, qz, QQQ, D);
               kernels::internal::PullEval<MQ1>(Q1D, qx, qy, qz, QQQ0, p0);
               kernels::internal::PullEval<MQ1>(Q1D, qx, qy, qz, QQQ1, p1);

               const real_t dist = D; // GetValues, default comp set to 0
               real_t id2 = 0.0;
               real_t dsq = 0.0;
               if (!exp_lim)
               {
                  id2 = 0.5 / (dist * dist);
                  dsq = kernels::DistanceSquared<3>(p1, p0) * id2;
                  E(qx, qy, qz, e) = weight * lim_normal * dsq * coeff0;
               }
               else
               {
                  id2 = 1.0 / (dist * dist);
                  dsq = kernels::DistanceSquared<3>(p1, p0) * id2;
                  E(qx, qy, qz, e) =
                     weight * lim_normal * exp(10.0 * (dsq - 1.0)) * coeff0;
               }
            }
         }
      }
   });
}

real_t TMOP_Integrator::GetLocalStateEnergyPA_C0_3D(const Vector &x) const
{
   constexpr int DIM = 3;
   const real_t ln = lim_normal;
   const bool const_c0 = PA.C0.Size() == 1;
   const int NE = PA.ne, d = PA.maps->ndof, q = PA.maps->nqpt;

   MFEM_VERIFY(PA.maps_lim->ndof == d, "");
   MFEM_VERIFY(PA.maps_lim->nqpt == q, "");

   const auto C0 = const_c0 ? Reshape(PA.C0.Read(), 1, 1, 1, 1)
                            : Reshape(PA.C0.Read(), q, q, q, NE);
   const auto LD = Reshape(PA.LD.Read(), d, d, d, NE);
   const auto J = Reshape(PA.Jtr.Read(), DIM, DIM, q, q, q, NE);
   const auto B = Reshape(PA.maps->B.Read(), q, d);
   const auto BLD = Reshape(PA.maps_lim->B.Read(), q, d);
   const auto W = Reshape(PA.ir->GetWeights().Read(), q, q, q);
   const auto X0 = Reshape(PA.X0.Read(), d, d, d, DIM, NE);
   const auto X1 = Reshape(x.Read(), d, d, d, DIM, NE);
   auto E = Reshape(PA.E.Write(), q, q, q, NE);

   auto el = dynamic_cast<TMOP_ExponentialLimiter *>(lim_func);
   const bool exp_lim = (el) ? true : false;

   decltype(&TMOP_EnergyPA_C0_3D<>) ker = TMOP_EnergyPA_C0_3D;

   if (d == 2 && q == 2) { ker = TMOP_EnergyPA_C0_3D<2, 2>; }
   if (d == 2 && q == 3) { ker = TMOP_EnergyPA_C0_3D<2, 3>; }
   if (d == 2 && q == 4) { ker = TMOP_EnergyPA_C0_3D<2, 4>; }
   if (d == 2 && q == 5) { ker = TMOP_EnergyPA_C0_3D<2, 5>; }
   if (d == 2 && q == 6) { ker = TMOP_EnergyPA_C0_3D<2, 6>; }

   if (d == 3 && q == 3) { ker = TMOP_EnergyPA_C0_3D<3, 3>; }
   if (d == 3 && q == 4) { ker = TMOP_EnergyPA_C0_3D<3, 4>; }
   if (d == 3 && q == 5) { ker = TMOP_EnergyPA_C0_3D<3, 5>; }
   if (d == 3 && q == 6) { ker = TMOP_EnergyPA_C0_3D<3, 6>; }

   if (d == 4 && q == 4) { ker = TMOP_EnergyPA_C0_3D<4, 4>; }
   if (d == 4 && q == 5) { ker = TMOP_EnergyPA_C0_3D<4, 5>; }
   if (d == 4 && q == 6) { ker = TMOP_EnergyPA_C0_3D<4, 6>; }

   if (d == 5 && q == 5) { ker = TMOP_EnergyPA_C0_3D<5, 5>; }
   if (d == 5 && q == 6) { ker = TMOP_EnergyPA_C0_3D<5, 6>; }

   ker(ln, LD, const_c0, C0, NE, J, W, B, BLD, X0, X1, E, exp_lim, d, q, 4);
   return PA.E * PA.O;
}

} // namespace mfem
