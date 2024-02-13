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

#include "../gslib.hpp"
#include "findpts_3.hpp"
#include "../../general/forall.hpp"
#include "../../linalg/kernels.hpp"
#include "../../linalg/dinvariants.hpp"

#ifdef MFEM_USE_GSLIB

namespace mfem
{

#define CODE_INTERNAL 0
#define CODE_BORDER 1
#define CODE_NOT_FOUND 2
#define dlong int
#define dfloat double

static MFEM_HOST_DEVICE void lagrange_eval(dfloat *p0, dfloat x,
                                           dlong i, dlong p_Nq,
                                           dfloat *z, dfloat *lagrangeCoeff)
{
   dfloat p_i = (1 << (p_Nq - 1));
   for (dlong j = 0; j < p_Nq; ++j)
   {
      dfloat d_j = x - z[j];
      p_i *= j == i ? 1 : d_j;
   }
   p0[i] = lagrangeCoeff[i] * p_i;
}

static void InterpolateLocal3D_Kernel(const dfloat *const gf_in,
                                      dlong *const el,
                                      dfloat *const r,
                                      dfloat *const int_out,
                                      const int npt,
                                      const int ncomp,
                                      const int nel,
                                      const int dof1Dsol,
                                      const int gf_offset,
                                      dfloat *gll1D,
                                      double *lagcoeff,
                                      dfloat *infok)
{
   const int p_Nq = dof1Dsol;
   const int Nfields = ncomp;
   const int fieldOffset = gf_offset;
   const int p_Np = p_Nq*p_Nq*p_Nq;
   const int pMax = 12;
   mfem::forall_2D(npt, dof1Dsol, 1, [=] MFEM_HOST_DEVICE (int i)
   {
      MFEM_SHARED dfloat wtr[pMax];
      MFEM_SHARED dfloat wts[pMax];
      MFEM_SHARED dfloat wtt[pMax];
      MFEM_SHARED dfloat sums[pMax];

      // Evaluate basis functions at the reference space coordinates
      MFEM_FOREACH_THREAD(j,x,p_Nq)
      {
         lagrange_eval(wtr, r[3 * i + 0], j, p_Nq, gll1D, lagcoeff);
         lagrange_eval(wts, r[3 * i + 1], j, p_Nq, gll1D, lagcoeff);
         lagrange_eval(wtt, r[3 * i + 2], j, p_Nq, gll1D, lagcoeff);
      }
      MFEM_SYNC_THREAD;

      for (int fld = 0; fld < Nfields; ++fld)
      {

         const dlong elemOffset = el[i] * p_Np + fld * fieldOffset;

         MFEM_FOREACH_THREAD(j,x,p_Nq)
         {
            dfloat sum_j = 0;
            for (dlong k = 0; k < p_Nq; ++k)
            {
               dfloat sum_k = 0;
               for (dlong l = 0; l < p_Nq; ++l)
               {
                  sum_k += gf_in[elemOffset + j + k * p_Nq + l * p_Nq * p_Nq] * wtt[l];
               }
               sum_j += wts[k] * sum_k;
            }
            sums[j] = wtr[j] * sum_j;
         }
         MFEM_SYNC_THREAD;

         MFEM_FOREACH_THREAD(j,x,p_Nq)
         {
            if (j == 0)
            {
               double sumv = 0.0;
               for (dlong jj = 0; jj < p_Nq; ++jj)
               {
                  sumv += sums[jj];
               }
               int_out[i + fld * npt] = sumv;
            }
         }
         MFEM_SYNC_THREAD;
      }
   });
}

void FindPointsGSLIB::InterpolateLocal3(const Vector &field_in,
                                        Array<int> &gsl_elem_dev_l,
                                        Vector &gsl_ref_l,
                                        Vector &field_out,
                                        int npt, int ncomp,
                                        int nel, int dof1Dsol)
{
   const int gf_offset = field_in.Size()/ncomp;
   if (dim == 3)
   {
      InterpolateLocal3D_Kernel(field_in.Read(),
                                gsl_elem_dev_l.ReadWrite(),
                                gsl_ref_l.ReadWrite(),
                                field_out.Write(),
                                npt, ncomp, nel, dof1Dsol, gf_offset,
                                DEV.gll1dsol.ReadWrite(),
                                DEV.lagcoeffsol.ReadWrite(),
                                DEV.info.ReadWrite());
   }
   else
   {
      MFEM_ABORT("Device implementation only for 3D yet.");
   }
}


#undef CODE_INTERNAL
#undef CODE_BORDER
#undef CODE_NOT_FOUND
#undef dlong
#undef dfloat

} // namespace mfem

#endif //ifdef MFEM_USE_GSLIB