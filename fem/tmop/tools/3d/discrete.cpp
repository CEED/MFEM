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

#include "../../../tmop.hpp"
#include "../../../kernels.hpp"
#include "../../../../general/forall.hpp"

namespace mfem
{

template <int T_D1D = 0, int T_Q1D = 0, int T_MAX = 4>
void DatcSize(const int NE,
              const int ncomp,
              const int sizeidx,
              const real_t input_min_size,
              const real_t *nc_red,
              const ConstDeviceMatrix &W,
              const ConstDeviceMatrix &B,
              const DeviceTensor<5, const real_t> &X,
              DeviceTensor<6> &J,
              const int d1d = 0,
              const int q1d = 0,
              const int max = 4)
{
   MFEM_VERIFY(ncomp == 1, "");
   const int D1D = T_D1D ? T_D1D : d1d;
   const int Q1D = T_Q1D ? T_Q1D : q1d;
   MFEM_VERIFY(D1D <= Q1D, "");

   const real_t infinity = std::numeric_limits<real_t>::infinity();
   MFEM_VERIFY(sizeidx == 0, "");
   MFEM_VERIFY(MFEM_CUDA_BLOCKS == 256, "Wrong CUDA block size used!");

   mfem::forall_3D(NE, Q1D, Q1D, Q1D, [=] MFEM_HOST_DEVICE(int e)
   {
      constexpr int DIM = 3;
      const int D1D = T_D1D ? T_D1D : d1d;
      const int Q1D = T_Q1D ? T_Q1D : q1d;
      constexpr int MQ1 = T_Q1D ? T_Q1D : T_MAX;
      constexpr int MD1 = T_D1D ? T_D1D : T_MAX;
      constexpr int MDQ = (MQ1 > MD1) ? MQ1 : MD1;

      MFEM_SHARED real_t sB[MQ1 * MD1];
      MFEM_SHARED real_t sm0[MDQ * MDQ * MDQ];
      MFEM_SHARED real_t sm1[MDQ * MDQ * MDQ];

      kernels::internal::LoadB<MD1, MQ1>(D1D, Q1D, B, sB);

      ConstDeviceMatrix B(sB, D1D, Q1D);
      DeviceCube DDD(sm0, MD1, MD1, MD1);
      DeviceCube DDQ(sm1, MD1, MD1, MQ1);
      DeviceCube DQQ(sm0, MD1, MQ1, MQ1);
      DeviceCube QQQ(sm1, MQ1, MQ1, MQ1);

      kernels::internal::LoadX(e, D1D, sizeidx, X, DDD);

      real_t min;
      MFEM_SHARED real_t min_size[MFEM_CUDA_BLOCKS];
      DeviceTensor<3, real_t> M((real_t *)(min_size), D1D, D1D, D1D);
      const DeviceTensor<3, const real_t> D((real_t *)(DDD + sizeidx), D1D, D1D,
                                            D1D);
      MFEM_FOREACH_THREAD(t, x, MFEM_CUDA_BLOCKS) { min_size[t] = infinity; }
      MFEM_SYNC_THREAD;
      MFEM_FOREACH_THREAD(dz, z, D1D)
      {
         MFEM_FOREACH_THREAD(dy, y, D1D)
         {
            MFEM_FOREACH_THREAD(dx, x, D1D) { M(dx, dy, dz) = D(dx, dy, dz); }
         }
      }
      MFEM_SYNC_THREAD;
      for (int wrk = MFEM_CUDA_BLOCKS >> 1; wrk > 0; wrk >>= 1)
      {
         MFEM_FOREACH_THREAD(t, x, MFEM_CUDA_BLOCKS)
         {
            if (t < wrk && MFEM_THREAD_ID(y) == 0 && MFEM_THREAD_ID(z) == 0)
            {
               min_size[t] = fmin(min_size[t], min_size[t + wrk]);
            }
         }
         MFEM_SYNC_THREAD;
      }
      min = min_size[0];
      if (input_min_size > 0.) { min = input_min_size; }
      kernels::internal::EvalX(D1D, Q1D, B, DDD, DDQ);
      kernels::internal::EvalY(D1D, Q1D, B, DDQ, DQQ);
      kernels::internal::EvalZ(D1D, Q1D, B, DQQ, QQQ);
      MFEM_FOREACH_THREAD(qx, x, Q1D)
      {
         MFEM_FOREACH_THREAD(qy, y, Q1D)
         {
            MFEM_FOREACH_THREAD(qz, z, Q1D)
            {
               real_t T;
               kernels::internal::PullEval(qx, qy, qz, QQQ, T);
               const real_t shape_par_vals = T;
               const real_t size = fmax(shape_par_vals, min) / nc_red[e];
               const real_t alpha = std::pow(size, 1.0 / DIM);
               for (int i = 0; i < DIM; i++)
               {
                  for (int j = 0; j < DIM; j++)
                  {
                     J(i, j, qx, qy, qz, e) = alpha * W(i, j);
                  }
               }
            }
         }
      }
   });
}

// PA.Jtr Size = (dim, dim, PA.ne*PA.nq);
void DiscreteAdaptTC::ComputeAllElementTargets(const FiniteElementSpace &pa_fes,
                                               const IntegrationRule &ir,
                                               const Vector &xe,
                                               DenseTensor &Jtr) const
{
   MFEM_VERIFY(target_type == IDEAL_SHAPE_GIVEN_SIZE ||
                  target_type == GIVEN_SHAPE_AND_SIZE,
               "");

   MFEM_VERIFY(tspec_fesv, "No target specifications have been set.");
   const FiniteElementSpace *fes = tspec_fesv;

   // Cases that are not implemented below
   if (skewidx != -1 || aspectratioidx != -1 || orientationidx != -1 ||
       fes->GetMesh()->Dimension() != 3 || sizeidx == -1)
   {
      return ComputeAllElementTargets_Fallback(pa_fes, ir, xe, Jtr);
   }

   const Mesh *mesh = fes->GetMesh();
   const int NE = mesh->GetNE();
   // Quick return for empty processors:
   if (NE == 0) { return; }
   const int dim = mesh->Dimension();
   MFEM_VERIFY(mesh->GetNumGeometries(dim) <= 1,
               "mixed meshes are not supported");
   MFEM_VERIFY(!fes->IsVariableOrder(), "variable orders are not supported");
   const FiniteElement &fe = *fes->GetFE(0);
   const DenseMatrix &w = Geometries.GetGeomToPerfGeomJac(fe.GetGeomType());
   const DofToQuad::Mode mode = DofToQuad::TENSOR;
   const DofToQuad &maps = fe.GetDofToQuad(ir, mode);
   const int d = maps.ndof, q = maps.nqpt;
   const real_t input_min_size = lim_min_size;

   Vector nc_size_red(NE, Device::GetDeviceMemoryType());
   nc_size_red.HostWrite();
   NCMesh *ncmesh = tspec_fesv->GetMesh()->ncmesh;
   for (int e = 0; e < NE; e++)
   {
      nc_size_red(e) = (ncmesh) ? ncmesh->GetElementSizeReduction(e) : 1.0;
   }
   const real_t *nc_red = nc_size_red.Read();

   Vector tspec_e;
   const ElementDofOrdering ordering = ElementDofOrdering::LEXICOGRAPHIC;
   const Operator *R = fes->GetElementRestriction(ordering);
   MFEM_VERIFY(R && R->Height() == NE * ncomp * d * d * d,
               "Restriction error!");
   tspec_e.SetSize(R->Height(), Device::GetDeviceMemoryType());
   tspec_e.UseDevice(true);
   tspec.UseDevice(true);
   R->Mult(tspec, tspec_e);

   constexpr int DIM = 3;
   const auto B = Reshape(maps.B.Read(), q, d);
   const auto W = Reshape(w.Read(), DIM, DIM);
   const auto X = Reshape(tspec_e.Read(), d, d, d, ncomp, NE);
   auto J = Reshape(Jtr.Write(), DIM, DIM, q, q, q, NE);

   decltype(&DatcSize<>) ker = DatcSize;

   if (d == 2 && q == 2) { ker = DatcSize<2, 2>; }
   if (d == 2 && q == 3) { ker = DatcSize<2, 3>; }
   if (d == 2 && q == 4) { ker = DatcSize<2, 4>; }
   if (d == 2 && q == 5) { ker = DatcSize<2, 5>; }
   if (d == 2 && q == 6) { ker = DatcSize<2, 6>; }

   if (d == 3 && q == 3) { ker = DatcSize<3, 3>; }
   if (d == 3 && q == 4) { ker = DatcSize<4, 4>; }
   if (d == 3 && q == 5) { ker = DatcSize<5, 5>; }
   if (d == 3 && q == 6) { ker = DatcSize<6, 6>; }

   if (d == 4 && q == 4) { ker = DatcSize<4, 4>; }
   if (d == 4 && q == 5) { ker = DatcSize<4, 5>; }
   if (d == 4 && q == 6) { ker = DatcSize<4, 6>; }

   if (d == 5 && q == 5) { ker = DatcSize<5, 5>; }
   if (d == 5 && q == 6) { ker = DatcSize<5, 6>; }

   ker(NE, ncomp, sizeidx, input_min_size, nc_red, W, B, X, J, d, q, 4);
}

} // namespace mfem