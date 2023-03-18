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
#include "tmop_pa.hpp"
#include "../../general/forall.hpp"
MFEM_JIT
#include "../../linalg/kernels.hpp"

namespace mfem
{

/* // Original i-j assembly (old invariants code).
   for (int e = 0; e < NE; e++)
   {
      for (int q = 0; q < nqp; q++)
      {
         el.CalcDShape(ip, DSh);
         Mult(DSh, Jrt, DS);
         for (int i = 0; i < dof; i++)
         {
            for (int j = 0; j < dof; j++)
            {
               for (int r = 0; r < dim; r++)
               {
                  for (int c = 0; c < dim; c++)
                  {
                     for (int rr = 0; rr < dim; rr++)
                     {
                        for (int cc = 0; cc < dim; cc++)
                        {
                           const double H = h(r, c, rr, cc);
                           A(e, i + r*dof, j + rr*dof) +=
                                 weight_q * DS(i, c) * DS(j, cc) * H;
                        }
                     }
                  }
               }
            }
         }
      }
   }*/

MFEM_JIT
template<int T_D1D = 0, int T_Q1D = 0, int T_MAX = 4>
void TMOP_AssembleDiagonalPA_2D(const int NE,
                                const ConstDeviceMatrix &B,
                                const ConstDeviceMatrix &G,
                                const DeviceTensor<5, const double> &J,
                                const DeviceTensor<7, const double> &H,
                                DeviceTensor<4> &D,
                                const int d1d = 0,
                                const int q1d = 0,
                                const int max = 0)
{
   const int Q1D = T_Q1D ? T_Q1D : q1d;

   MFEM_FORALL_2D(e, NE, Q1D, Q1D, 1,
   {
      constexpr int DIM = 2;
      const int D1D = T_D1D ? T_D1D : d1d;
      const int Q1D = T_Q1D ? T_Q1D : q1d;
      constexpr int MD1 = T_D1D ? T_D1D : T_MAX;
      constexpr int MQ1 = T_Q1D ? T_Q1D : T_MAX;

      MFEM_SHARED double qd[DIM*DIM*MQ1*MD1];
      DeviceTensor<4,double> QD(qd, DIM, DIM, MQ1, MD1);

      for (int v = 0; v < DIM; v++)
      {
         MFEM_FOREACH_THREAD(qx,x,Q1D)
         {
            MFEM_FOREACH_THREAD(dy,y,D1D)
            {
               QD(0,0,qx,dy) = 0.0;
               QD(0,1,qx,dy) = 0.0;
               QD(1,0,qx,dy) = 0.0;
               QD(1,1,qx,dy) = 0.0;

               MFEM_UNROLL(MQ1)
               for (int qy = 0; qy < Q1D; ++qy)
               {
                  const double *Jtr = &J(0,0,qx,qy,e);

                  // Jrt = Jtr^{-1}
                  double jrt_data[4];
                  ConstDeviceMatrix Jrt(jrt_data,2,2);
                  kernels::CalcInverse<2>(Jtr, jrt_data);

                  const double gg = G(qy,dy) * G(qy,dy);
                  const double gb = G(qy,dy) * B(qy,dy);
                  const double bb = B(qy,dy) * B(qy,dy);
                  const double bgb[4] = { bb, gb, gb, gg };
                  ConstDeviceMatrix BG(bgb,2,2);

                  for (int i = 0; i < DIM; i++)
                  {
                     for (int j = 0; j < DIM; j++)
                     {
                        const double Jij = Jrt(i,i) * Jrt(j,j);
                        const double alpha = Jij * BG(i,j);
                        QD(i,j,qx,dy) += alpha * H(v,i,v,j,qx,qy,e);
                     }
                  }
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
                  const double gg = G(qx,dx) * G(qx,dx);
                  const double gb = G(qx,dx) * B(qx,dx);
                  const double bb = B(qx,dx) * B(qx,dx);
                  d += gg * QD(0,0,qx,dy);
                  d += gb * QD(0,1,qx,dy);
                  d += gb * QD(1,0,qx,dy);
                  d += bb * QD(1,1,qx,dy);
               }
               D(dx,dy,v,e) += d;
            }
         }
         MFEM_SYNC_THREAD;
      }
   });
}

void TMOP_Integrator::AssembleDiagonalPA_2D(Vector &diagonal) const
{
   const int NE = PA.ne;
   constexpr int DIM = 2;
   const int D1D = PA.maps->ndof;
   const int Q1D = PA.maps->nqpt;

   const DenseTensor &j = PA.Jtr;
   const Array<double> &b = PA.maps->B;
   const Array<double> &g = PA.maps->G;
   const Vector &h = PA.H;

   const auto B = Reshape(b.Read(), Q1D, D1D);
   const auto G = Reshape(g.Read(), Q1D, D1D);
   const auto J = Reshape(j.Read(), DIM, DIM, Q1D, Q1D, NE);
   const auto H = Reshape(h.Read(), DIM, DIM, DIM, DIM, Q1D, Q1D, NE);
   auto D = Reshape(diagonal.ReadWrite(), D1D, D1D, DIM, NE);

   decltype(&TMOP_AssembleDiagonalPA_2D<>) ker = TMOP_AssembleDiagonalPA_2D;
#ifndef MFEM_USE_JIT
   const int d=D1D, q=Q1D;
   if (d==2 && q==2) { ker = TMOP_AssembleDiagonalPA_2D<2,2>; }
   if (d==2 && q==3) { ker = TMOP_AssembleDiagonalPA_2D<2,3>; }
   if (d==2 && q==4) { ker = TMOP_AssembleDiagonalPA_2D<2,4>; }
   if (d==2 && q==5) { ker = TMOP_AssembleDiagonalPA_2D<2,5>; }
   if (d==2 && q==6) { ker = TMOP_AssembleDiagonalPA_2D<2,6>; }

   if (d==3 && q==3) { ker = TMOP_AssembleDiagonalPA_2D<3,3>; }
   if (d==3 && q==4) { ker = TMOP_AssembleDiagonalPA_2D<3,4>; }
   if (d==3 && q==5) { ker = TMOP_AssembleDiagonalPA_2D<3,5>; }
   if (d==3 && q==6) { ker = TMOP_AssembleDiagonalPA_2D<3,6>; }

   if (d==4 && q==4) { ker = TMOP_AssembleDiagonalPA_2D<4,4>; }
   if (d==4 && q==5) { ker = TMOP_AssembleDiagonalPA_2D<4,5>; }
   if (d==4 && q==6) { ker = TMOP_AssembleDiagonalPA_2D<4,6>; }

   if (d==5 && q==5) { ker = TMOP_AssembleDiagonalPA_2D<5,5>; }
   if (d==5 && q==6) { ker = TMOP_AssembleDiagonalPA_2D<5,6>; }
#endif
   ker(NE,B,G,J,H,D,D1D,Q1D,4);
}

} // namespace mfem
