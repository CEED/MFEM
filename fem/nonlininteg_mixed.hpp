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

#ifndef MFEM_NONLININTEG_MIXED
#define MFEM_NONLININTEG_MIXED

#include "../config/config.hpp"
#include "nonlininteg.hpp"
#include "hyperbolic.hpp"

namespace mfem
{

class MixedFluxFunction : public FluxFunction
{
public:
   MixedFluxFunction(const int num_equations, const int dim)
      : FluxFunction(num_equations, dim) { }

   virtual ~MixedFluxFunction() { }

   virtual real_t ComputeDualFlux(const Vector &state, const DenseMatrix &flux,
                                  ElementTransformation &Tr,
                                  DenseMatrix &dualFlux) const = 0;

   virtual void ComputeDualFluxJacobian(const Vector &, const DenseMatrix &flux,
                                        ElementTransformation &Tr,
                                        DenseTensor &J) const
   { MFEM_ABORT("Not Implemented."); }
};

class LinearDiffusionFlux : public MixedFluxFunction
{
   Coefficient *coeff;

public:
   LinearDiffusionFlux(int dim, Coefficient *coeff)
      : MixedFluxFunction(1, dim), coeff(coeff) { }

   real_t ComputeDualFlux(const Vector &, const DenseMatrix &flux,
                          ElementTransformation &Tr,
                          DenseMatrix &dualFlux) const override
   {
      const real_t ikappa = coeff->Eval(Tr, Tr.GetIntPoint());
      dualFlux.Set(ikappa, flux);
      return ikappa;
   }

   real_t ComputeFlux(const Vector &,
                      ElementTransformation &,
                      DenseMatrix &flux) const override
   {
      flux = 0.;
      return 0.;
   }

   void ComputeDualFluxJacobian(const Vector &, const DenseMatrix &flux,
                                ElementTransformation &Tr,
                                DenseTensor &J) const override
   {
      const real_t ikappa = coeff->Eval(Tr, Tr.GetIntPoint());
      for (int d = 0; d < dim; d++)
      {
         J(d)(0,0) = ikappa;
      }
   }
};

class MixedConductionNLFIntegrator : public BlockNonlinearFormIntegrator
{
   const MixedFluxFunction &fluxFunction;

   DenseMatrix vshape_u;
   Vector shape_u, shape_p;
   const IntegrationRule *IntRule;

public:
   MixedConductionNLFIntegrator(
      const MixedFluxFunction &fluxFunction,
      const IntegrationRule *ir = NULL)
      : fluxFunction(fluxFunction), IntRule(ir) { }

   void AssembleElementVector(const Array<const FiniteElement*> &el,
                              ElementTransformation &Tr,
                              const Array<const Vector*> &elfun,
                              const Array<Vector*> &elvect) override;

   void AssembleElementGrad(const Array<const FiniteElement*> &el,
                            ElementTransformation &Tr,
                            const Array<const Vector *> &elfun,
                            const Array2D<DenseMatrix *> &elmats);
};

}

#endif
