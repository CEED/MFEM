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

#include "../tmop.hpp"
#include "../linearform.hpp"
#include "../../general/jit/jit.hpp"
MFEM_JIT
#include "tmop_pa.hpp"

namespace mfem
{

MFEM_JIT
template<int T_D1D = 0, int T_Q1D = 0, int T_MAX = 4>
void TMOP_EnergyPA_C0_3D(const double lim_normal,
                         const DeviceTensor<4, const double> &LD,
                         const bool const_c0,
                         const DeviceTensor<4, const double> &C0,
                         const int NE,
                         const DeviceTensor<6, const double> &J,
                         const ConstDeviceCube &W,
                         const ConstDeviceMatrix &b,
                         const ConstDeviceMatrix &bld,
                         const DeviceTensor<5, const double> &X0,
                         const DeviceTensor<5, const double> &X1,
                         DeviceTensor<4> &E,
                         const bool exp_lim,
                         const int d1d,
                         const int q1d,
                         const int max)
{
   const int Q1D = T_Q1D ? T_Q1D : q1d;

   MFEM_FORALL_3D(e, NE, Q1D, Q1D, Q1D,
   {
      const int D1D = T_D1D ? T_D1D : d1d;
      const int Q1D = T_Q1D ? T_Q1D : q1d;
      constexpr int MQ1 = T_Q1D ? T_Q1D : T_MAX;
      constexpr int MD1 = T_D1D ? T_D1D : T_MAX;
      constexpr int MDQ = (MQ1 > MD1) ? MQ1 : MD1;

      MFEM_SHARED double B[MQ1*MD1];
      MFEM_SHARED double sBLD[MQ1*MD1];
      kernels::internal::LoadB<MD1,MQ1>(D1D,Q1D,bld,sBLD);
      ConstDeviceMatrix BLD(sBLD, D1D, Q1D);

      MFEM_SHARED double sm0[MDQ*MDQ*MDQ];
      MFEM_SHARED double sm1[MDQ*MDQ*MDQ];
      DeviceCube DDD(sm0, MD1,MD1,MD1);
      DeviceCube DDQ(sm1, MD1,MD1,MQ1);
      DeviceCube DQQ(sm0, MD1,MQ1,MQ1);
      DeviceCube QQQ(sm1, MQ1,MQ1,MQ1);

      MFEM_SHARED double DDD0[3][MD1*MD1*MD1];
      MFEM_SHARED double DDQ0[3][MD1*MD1*MQ1];
      MFEM_SHARED double DQQ0[3][MD1*MQ1*MQ1];
      MFEM_SHARED double QQQ0[3][MQ1*MQ1*MQ1];

      MFEM_SHARED double DDD1[3][MD1*MD1*MD1];
      MFEM_SHARED double DDQ1[3][MD1*MD1*MQ1];
      MFEM_SHARED double DQQ1[3][MD1*MQ1*MQ1];
      MFEM_SHARED double QQQ1[3][MQ1*MQ1*MQ1];

      kernels::internal::LoadX(e,D1D,LD,DDD);
      kernels::internal::LoadX<MD1>(e,D1D,X0,DDD0);
      kernels::internal::LoadX<MD1>(e,D1D,X1,DDD1);

      kernels::internal::LoadB<MD1,MQ1>(D1D,Q1D,b,B);

      kernels::internal::EvalX(D1D,Q1D,BLD,DDD,DDQ);
      kernels::internal::EvalY(D1D,Q1D,BLD,DDQ,DQQ);
      kernels::internal::EvalZ(D1D,Q1D,BLD,DQQ,QQQ);

      kernels::internal::EvalX<MD1,MQ1>(D1D,Q1D,B,DDD0,DDQ0);
      kernels::internal::EvalY<MD1,MQ1>(D1D,Q1D,B,DDQ0,DQQ0);
      kernels::internal::EvalZ<MD1,MQ1>(D1D,Q1D,B,DQQ0,QQQ0);

      kernels::internal::EvalX<MD1,MQ1>(D1D,Q1D,B,DDD1,DDQ1);
      kernels::internal::EvalY<MD1,MQ1>(D1D,Q1D,B,DDQ1,DQQ1);
      kernels::internal::EvalZ<MD1,MQ1>(D1D,Q1D,B,DQQ1,QQQ1);

      MFEM_FOREACH_THREAD(qz,z,Q1D)
      {
         MFEM_FOREACH_THREAD(qy,y,Q1D)
         {
            MFEM_FOREACH_THREAD(qx,x,Q1D)
            {
               double D, p0[3], p1[3];
               const double *Jtr = &J(0,0,qx,qy,qz,e);
               const double detJtr = kernels::Det<3>(Jtr);
               const double weight = W(qx,qy,qz) * detJtr;
               const double coeff0 = const_c0 ? C0(0,0,0,0) : C0(qx,qy,qz,e);

               kernels::internal::PullEval(qx,qy,qz,QQQ,D);
               kernels::internal::PullEval<MQ1>(Q1D,qx,qy,qz,QQQ0,p0);
               kernels::internal::PullEval<MQ1>(Q1D,qx,qy,qz,QQQ1,p1);

               const double dist = D; // GetValues, default comp set to 0
               double id2 = 0.0;
               double dsq = 0.0;
               if (!exp_lim)
               {
                  id2 = 0.5 / (dist*dist);
                  dsq = kernels::DistanceSquared<3>(p1,p0) * id2;
                  E(qx,qy,qz,e) = weight * lim_normal * dsq * coeff0;
               }
               else
               {
                  id2 = 1.0 / (dist*dist);
                  dsq = kernels::DistanceSquared<3>(p1,p0) * id2;
                  E(qx,qy,qz,e) = weight * lim_normal * exp(10.0*(dsq-1.0)) * coeff0;
               }
            }
         }
      }
   });
}

double TMOP_Integrator::GetLocalStateEnergyPA_C0_3D(const Vector &x) const
{
   const int NE = PA.ne;
   constexpr int DIM = 3;
   const int D1D = PA.maps->ndof;
   const int Q1D = PA.maps->nqpt;

   const double ln = lim_normal;
   MFEM_VERIFY(PA.maps_lim->ndof == D1D, "");
   MFEM_VERIFY(PA.maps_lim->nqpt == Q1D, "");

   const bool const_c0 = PA.C0.Size() == 1;
   const auto C0 = const_c0 ?
                   Reshape(PA.C0.Read(), 1, 1, 1, 1) :
                   Reshape(PA.C0.Read(), Q1D, Q1D, Q1D, NE);
   const auto LD = Reshape(PA.LD.Read(), D1D, D1D, D1D, NE);
   const auto J = Reshape(PA.Jtr.Read(), DIM, DIM, Q1D, Q1D, Q1D, NE);
   const auto B = Reshape(PA.maps->B.Read(), Q1D, D1D);
   const auto BLD = Reshape(PA.maps_lim->B.Read(), Q1D, D1D);
   const auto W = Reshape(PA.ir->GetWeights().Read(), Q1D, Q1D, Q1D);
   const auto X0 = Reshape(PA.X0.Read(), D1D, D1D, D1D, DIM, NE);
   const auto X1 = Reshape(x.Read(), D1D, D1D, D1D, DIM, NE);
   auto E = Reshape(PA.E.Write(), Q1D, Q1D, Q1D, NE);

   auto el = dynamic_cast<TMOP_ExponentialLimiter *>(lim_func);
   const bool exp_lim = (el) ? true : false;


   decltype(&TMOP_EnergyPA_C0_3D<>) ker = TMOP_EnergyPA_C0_3D;
#ifndef MFEM_USE_JIT
   const int d=D1D, q=Q1D;
   if (d==2 && q==2) { ker = TMOP_EnergyPA_C0_3D<2,2>; }
   if (d==2 && q==3) { ker = TMOP_EnergyPA_C0_3D<2,3>; }
   if (d==2 && q==4) { ker = TMOP_EnergyPA_C0_3D<2,4>; }
   if (d==2 && q==5) { ker = TMOP_EnergyPA_C0_3D<2,5>; }
   if (d==2 && q==6) { ker = TMOP_EnergyPA_C0_3D<2,6>; }

   if (d==3 && q==3) { ker = TMOP_EnergyPA_C0_3D<3,3>; }
   if (d==3 && q==4) { ker = TMOP_EnergyPA_C0_3D<3,4>; }
   if (d==3 && q==5) { ker = TMOP_EnergyPA_C0_3D<3,5>; }
   if (d==3 && q==6) { ker = TMOP_EnergyPA_C0_3D<3,6>; }

   if (d==4 && q==4) { ker = TMOP_EnergyPA_C0_3D<4,4>; }
   if (d==4 && q==5) { ker = TMOP_EnergyPA_C0_3D<4,5>; }
   if (d==4 && q==6) { ker = TMOP_EnergyPA_C0_3D<4,6>; }

   if (d==5 && q==5) { ker = TMOP_EnergyPA_C0_3D<5,5>; }
   if (d==5 && q==6) { ker = TMOP_EnergyPA_C0_3D<5,6>; }
#endif
   ker(ln,LD,const_c0,C0,NE,J,W,B,BLD,X0,X1,E,exp_lim,D1D,Q1D,4);
   return PA.E * PA.O;
}

} // namespace mfem
