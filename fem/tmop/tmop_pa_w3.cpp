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

#include "tmop_pa.hpp"

namespace mfem
{

template<int T_D1D = 0, int T_Q1D = 0, int T_MAX = 4>
void TMOP_EnergyPA_3D(const double metric_normal,
                      const double *w,
                      const int mid,
                      const int NE,
                      const DeviceTensor<6, const double> &J,
                      const ConstDeviceCube &W,
                      const ConstDeviceMatrix &B,
                      const ConstDeviceMatrix &G,
                      const DeviceTensor<5, const double> &X,
                      DeviceTensor<4> &E,
                      const int d1d,
                      const int q1d,
                      const int max)
{
   using Args = kernels::InvariantsEvaluator3D::Buffers;
   MFEM_VERIFY(mid == 302 || mid == 303 || mid == 315 || mid == 318 ||
               mid == 321 || mid == 332 || mid == 338,
               "3D metric not yet implemented!");

   const int Q1D = T_Q1D ? T_Q1D : q1d;

   mfem::forall_3D(NE, Q1D, Q1D, Q1D, [=] MFEM_HOST_DEVICE (int e)
   {
      const int D1D = T_D1D ? T_D1D : d1d;
      const int Q1D = T_Q1D ? T_Q1D : q1d;
      constexpr int MQ1 = T_Q1D ? T_Q1D : T_MAX;
      constexpr int MD1 = T_D1D ? T_D1D : T_MAX;

      MFEM_SHARED double BG[2][MQ1*MD1];
      MFEM_SHARED double DDD[3][MD1*MD1*MD1];
      MFEM_SHARED double DDQ[6][MD1*MD1*MQ1];
      MFEM_SHARED double DQQ[9][MD1*MQ1*MQ1];
      MFEM_SHARED double QQQ[9][MQ1*MQ1*MQ1];

      kernels::internal::LoadX<MD1>(e,D1D,X,DDD);
      kernels::internal::LoadBG<MD1,MQ1>(D1D,Q1D,B,G,BG);

      kernels::internal::GradX<MD1,MQ1>(D1D,Q1D,BG,DDD,DDQ);
      kernels::internal::GradY<MD1,MQ1>(D1D,Q1D,BG,DDQ,DQQ);
      kernels::internal::GradZ<MD1,MQ1>(D1D,Q1D,BG,DQQ,QQQ);

      MFEM_FOREACH_THREAD(qz,z,Q1D)
      {
         MFEM_FOREACH_THREAD(qy,y,Q1D)
         {
            MFEM_FOREACH_THREAD(qx,x,Q1D)
            {
               const double *Jtr = &J(0,0,qx,qy,qz,e);
               const double detJtr = kernels::Det<3>(Jtr);
               const double weight = metric_normal * W(qx,qy,qz) * detJtr;

               // Jrt = Jtr^{-1}
               double Jrt[9];
               kernels::CalcInverse<3>(Jtr, Jrt);

               // Jpr = X^t.DSh
               double Jpr[9];
               kernels::internal::PullGrad<MQ1>(Q1D,qx,qy,qz, QQQ, Jpr);

               // Jpt = X^t.DS = (X^t.DSh).Jrt = Jpr.Jrt
               double Jpt[9];
               kernels::Mult(3,3,3, Jpr, Jrt, Jpt);

               // metric->EvalW(Jpt);
               double B[9];
               kernels::InvariantsEvaluator3D ie(Args().J(Jpt).B(B));

               auto EvalW_302 = [&]() // I1b * I2b / 9 - 1
               {
                  return ie.Get_I1b()*ie.Get_I2b()/9. - 1.;
               };

               auto EvalW_303 = [&]() // mu_303 = I1b/3 - 1
               {
                  return ie.Get_I1b()/3. - 1.;
               };

               auto EvalW_315 = [&]() // (I3b - 1)^2
               {
                  const double a = ie.Get_I3b() - 1.0;
                  return a*a;
               };

               auto EvalW_318 = [&]() //  0.5 * (I3 + 1/I3) - 1.
               {
                  const double I3 = ie.Get_I3();
                  return 0.5*(I3 + 1.0/I3) - 1.0;
               };

               auto EvalW_321 = [&]() // I1 + I2/I3 - 6
               {
                  return ie.Get_I1() + ie.Get_I2()/ie.Get_I3() - 6.0;
               };

               auto EvalW_332 = [&]()
               {
                  return w[0] * EvalW_302() + w[1] * EvalW_315();
               };

               auto EvalW_338 = [&]()
               {
                  return w[0] * EvalW_302() + w[1] * EvalW_318();
               };

               const double EvalW =
                  mid == 302 ? EvalW_302() :
                  mid == 303 ? EvalW_303() :
                  mid == 315 ? EvalW_315() :
                  mid == 318 ? EvalW_318() :
                  mid == 321 ? EvalW_321() :
                  mid == 332 ? EvalW_332() :
                  mid == 338 ? EvalW_338() : 0.0;

               E(qx,qy,qz,e) = weight * EvalW;
            }
         }
      }
   });
}

double TMOP_Integrator::GetLocalStateEnergyPA_3D(const Vector &x) const
{
   constexpr int DIM = 3;
   const double mn = metric_normal;
   const int NE = PA.ne, M = metric->Id();
   const int d = PA.maps->ndof, q = PA.maps->nqpt;

   Array<double> mp;
   if (auto m = dynamic_cast<TMOP_Combo_QualityMetric *>(metric))
   {
      m->GetWeights(mp);
   }
   const double *w = mp.Read();

   const auto J = Reshape(PA.Jtr.Read(), DIM, DIM, q, q, q, NE);
   const auto B = Reshape(PA.maps->B.Read(), q, d);
   const auto G = Reshape(PA.maps->G.Read(), q, d);
   const auto W = Reshape(PA.ir->GetWeights().Read(), q, q, q);
   const auto X = Reshape(x.Read(), d, d, d, DIM, NE);
   auto E = Reshape(PA.E.Write(), q, q, q, NE);

   decltype(&TMOP_EnergyPA_3D<>) ker = TMOP_EnergyPA_3D;

   if (d==2 && q==2) { ker = TMOP_EnergyPA_3D<2,2>; }
   if (d==2 && q==3) { ker = TMOP_EnergyPA_3D<2,3>; }
   if (d==2 && q==4) { ker = TMOP_EnergyPA_3D<2,4>; }
   if (d==2 && q==5) { ker = TMOP_EnergyPA_3D<2,5>; }
   if (d==2 && q==6) { ker = TMOP_EnergyPA_3D<2,6>; }

   if (d==3 && q==3) { ker = TMOP_EnergyPA_3D<3,3>; }
   if (d==3 && q==4) { ker = TMOP_EnergyPA_3D<3,4>; }
   if (d==3 && q==5) { ker = TMOP_EnergyPA_3D<3,5>; }
   if (d==3 && q==6) { ker = TMOP_EnergyPA_3D<3,6>; }

   if (d==4 && q==4) { ker = TMOP_EnergyPA_3D<4,4>; }
   if (d==4 && q==5) { ker = TMOP_EnergyPA_3D<4,5>; }
   if (d==4 && q==6) { ker = TMOP_EnergyPA_3D<4,6>; }

   if (d==5 && q==5) { ker = TMOP_EnergyPA_3D<5,5>; }
   if (d==5 && q==6) { ker = TMOP_EnergyPA_3D<5,6>; }

   ker(mn,w,M,NE,J,W,B,G,X,E,d,q,4);
   return PA.E * PA.O;
}

} // namespace mfem
