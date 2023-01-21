// Copyright (c) 2017, Lawrence Livermore National Security, LLC. Produced at
// the Lawrence Livermore National Laboratory. LLNL-CODE-734707. All Rights
// reserved. See files LICENSE and NOTICE for details.
//
// This file is part of CEED, a collection of benchmarks, miniapps, software
// libraries and APIs for efficient high-order finite element and spectral
// element discretizations for exascale applications. For more information and
// source code availability see http://github.com/ceed.
//
// The CEED research is supported by the Exascale Computing Project 17-SC-20-SC,
// a collaborative effort of two U.S. Department of Energy organizations (Office
// of Science and the National Nuclear Security Administration) responsible for
// the planning and preparation of a capable exascale ecosystem, including
// software, applications, hardware, advanced system engineering and early
// testbed platforms, in support of the nation's exascale computing imperative.

#ifndef MFEM_WEIGHTED_NITSCHE_SOLVER
#define MFEM_WEIGHTED_NITSCHE_SOLVER

#include "mfem.hpp"
#include "AnalyticalGeometricShape.hpp"
#include "marking.hpp"

using namespace std;
using namespace mfem;

/// BilinearFormIntegrator for the high-order extension of shifted boundary
/// method.
/// A(u, w) = -<2*mu*epsilon(u) n, w>
///           -<(p*I) n, w>
///           -<u, sigma(w,q) n> // transpose of the above two terms
///           +<alpha h^{-1} u , w >
namespace mfem
{

  class WeightedStressBoundaryForceIntegrator : public BilinearFormIntegrator
  {
  private:
    ParGridFunction *alpha;
    Coefficient *mu;
    Coefficient *kappa;
    ShiftedFaceMarker *analyticalSurface;
    
  public:
    WeightedStressBoundaryForceIntegrator(ParGridFunction &alphaF, Coefficient &mu_, Coefficient &kappa_, ShiftedFaceMarker *analyticalSurface)  : alpha(&alphaF), mu(&mu_), kappa(&kappa_), analyticalSurface(analyticalSurface) {}
    virtual void AssembleFaceMatrix(const FiniteElement &fe,
				    const FiniteElement &fe2,
				    FaceElementTransformations &Tr,
				    DenseMatrix &elmat);
  };

  class WeightedStressBoundaryForceTransposeIntegrator : public BilinearFormIntegrator
  {
  private:
    ParGridFunction *alpha;
    Coefficient *mu;
    Coefficient *kappa;
    ShiftedFaceMarker *analyticalSurface;

  public:
    WeightedStressBoundaryForceTransposeIntegrator(ParGridFunction &alphaF, Coefficient &mu_, Coefficient &kappa_, ShiftedFaceMarker *analyticalSurface)  : alpha(&alphaF), mu(&mu_), kappa(&kappa_), analyticalSurface(analyticalSurface) {}
    virtual void AssembleFaceMatrix(const FiniteElement &fe,
				    const FiniteElement &fe2,
				    FaceElementTransformations &Tr,
				    DenseMatrix &elmat);
  };

  // Performs full assembly for the normal velocity mass matrix operator.
  class WeightedNormalDisplacementPenaltyIntegrator : public BilinearFormIntegrator
  {
  private:
    ParGridFunction *alpha;
    double penaltyParameter;
    Coefficient *kappa;
    ShiftedFaceMarker *analyticalSurface;

  public:
    WeightedNormalDisplacementPenaltyIntegrator(ParGridFunction &alphaF, double penParameter, Coefficient &kappa_, ShiftedFaceMarker *analyticalSurface) : alpha(&alphaF), penaltyParameter(penParameter), kappa(&kappa_), analyticalSurface(analyticalSurface) { }
    virtual void AssembleFaceMatrix(const FiniteElement &fe,
				    const FiniteElement &fe2,
				    FaceElementTransformations &Tr,
				    DenseMatrix &elmat);
  };

  // Performs full assembly for the normal velocity mass matrix operator.
  class WeightedTangentialDisplacementPenaltyIntegrator : public BilinearFormIntegrator
  {
  private:
    ParGridFunction *alpha;
    double penaltyParameter;
    Coefficient *mu;
    ShiftedFaceMarker *analyticalSurface;
    
  public:
    WeightedTangentialDisplacementPenaltyIntegrator(ParGridFunction &alphaF, double penParameter, Coefficient &mu_, ShiftedFaceMarker *analyticalSurface) : alpha(&alphaF), penaltyParameter(penParameter), mu(&mu_), analyticalSurface(analyticalSurface) { }
    virtual void AssembleFaceMatrix(const FiniteElement &fe,
				    const FiniteElement &fe2,
				    FaceElementTransformations &Tr,
				    DenseMatrix &elmat);
  };
  
  class WeightedStressNitscheBCForceIntegrator : public LinearFormIntegrator
  {
  private:
    ParGridFunction *alpha;
    Coefficient *mu;
    Coefficient *kappa;
    VectorCoefficient *uD;
    ShiftedFaceMarker *analyticalSurface;
    
  public:
    WeightedStressNitscheBCForceIntegrator(ParGridFunction &alphaF, Coefficient &mu_, Coefficient &kappa_, VectorCoefficient &uD_, ShiftedFaceMarker *analyticalSurface)  : alpha(&alphaF), mu(&mu_), kappa(&kappa_), uD(&uD_), analyticalSurface(analyticalSurface) {}
    virtual void AssembleRHSElementVect(const FiniteElement &el,
					FaceElementTransformations &Tr,
					Vector &elvect);
    virtual void AssembleRHSElementVect(const FiniteElement &el,
					ElementTransformation &Tr,
					Vector &elvect);
  };
  
  // Performs full assembly for the normal velocity mass matrix operator.
  class WeightedNormalDisplacementBCPenaltyIntegrator : public LinearFormIntegrator
  {
  private:
    ParGridFunction *alpha;
    double penaltyParameter;
    Coefficient *kappa;
    VectorCoefficient *uD;
    ShiftedFaceMarker *analyticalSurface;
    
  public:
    WeightedNormalDisplacementBCPenaltyIntegrator(ParGridFunction &alphaF, double penParameter, Coefficient &kappa_, VectorCoefficient &uD_, ShiftedFaceMarker *analyticalSurface) : alpha(&alphaF), penaltyParameter(penParameter), kappa(&kappa_), uD(&uD_), analyticalSurface(analyticalSurface) { }
    virtual void AssembleRHSElementVect(const FiniteElement &el,
					FaceElementTransformations &Tr,
					Vector &elvect);
    virtual void AssembleRHSElementVect(const FiniteElement &el,
					ElementTransformation &Tr,
					Vector &elvect);
  };

  // Performs full assembly for the normal velocity mass matrix operator.
  class WeightedTangentialDisplacementBCPenaltyIntegrator : public LinearFormIntegrator
  {
  private:
    ParGridFunction *alpha;
    double penaltyParameter;
    Coefficient *mu;
    VectorCoefficient *uD;
    ShiftedFaceMarker *analyticalSurface;
    
  public:
    WeightedTangentialDisplacementBCPenaltyIntegrator(ParGridFunction &alphaF, double penParameter, Coefficient &mu_, VectorCoefficient &uD_, ShiftedFaceMarker *analyticalSurface) : alpha(&alphaF), penaltyParameter(penParameter), mu(&mu_), uD(&uD_), analyticalSurface(analyticalSurface) { }
    virtual void AssembleRHSElementVect(const FiniteElement &el,
					FaceElementTransformations &Tr,
					Vector &elvect);
    virtual void AssembleRHSElementVect(const FiniteElement &el,
					ElementTransformation &Tr,
					Vector &elvect);
  };
  
  // Performs full assembly for the normal velocity mass matrix operator.
  class WeightedTractionBCIntegrator : public LinearFormIntegrator
  {
  private:
    ParGridFunction *alpha;
    VectorCoefficient *tN;
    ShiftedFaceMarker *analyticalSurface;
    
  public:
    WeightedTractionBCIntegrator(ParGridFunction &alphaF, VectorCoefficient &tN_, ShiftedFaceMarker *analyticalSurface) : alpha(&alphaF), tN(&tN_), analyticalSurface(analyticalSurface) { }
    virtual void AssembleRHSElementVect(const FiniteElement &el,
					FaceElementTransformations &Tr,
					Vector &elvect);
    virtual void AssembleRHSElementVect(const FiniteElement &el,
					ElementTransformation &Tr,
					Vector &elvect);
  };

}

#endif // NITSCHE_SOLVER
