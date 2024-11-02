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

#include "../pa.hpp"
#include "h2s.hpp"

namespace mfem
{
void TMOP_Integrator::AssembleGradPA_2D(const Vector &x) const
{
   const int mid = metric->Id();

   TMOPSetup2D ker(this, x);

   if (mid == 1) { return TMOPKernel<1>(ker); }
   if (mid == 2) { return TMOPKernel<2>(ker); }
   if (mid == 7) { return TMOPKernel<7>(ker); }
   if (mid == 56) { return TMOPKernel<56>(ker); }
   if (mid == 77) { return TMOPKernel<77>(ker); }
   if (mid == 80) { return TMOPKernel<80>(ker); }
   if (mid == 94) { return TMOPKernel<94>(ker); }

   MFEM_ABORT("Unsupported TMOP metric " << mid);
}

} // namespace mfem
