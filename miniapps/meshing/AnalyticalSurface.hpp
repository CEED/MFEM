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

#ifndef MFEM_ANALYTICAL_SURFACE
#define MFEM_ANALYTICAL_SURFACE

#include "mfem.hpp"
#include "Square.hpp"

namespace mfem
{

class AnalyticalSurface
{
public:
   int geometryType;
   AnalyticalGeometricShape *geometry;
   ParFiniteElementSpace &pfes_mesh;
   ParGridFunction distance_gf;
   const ParGridFunction &coord;
   const ParMesh &pmesh;

   AnalyticalSurface(int geometryType, ParFiniteElementSpace &pfes_mesh,
                     const ParGridFunction &coord, const ParMesh &pmesh);

   // Go from (x,y) -> t on the whole mesh.
   void ConvertPhysicalCoordinatesToParametric(ParGridFunction &coord);

   // Go from t -> (x,y) on the whole mesh.
   void ConvertParametricCoordinatesToPhysical(ParGridFunction &coord);

   // Derivative d(x_ai) / dt.
   // Fills just one entry of the Pmat_scale.
   void SetScaleMatrix(const Array<int> & vdofs, int i, int a, DenseMatrix & Pmat_scale);

   // First derivatives as a 4th order tensor.
   void SetScaleMatrixFourthOrder(const Vector &elfun, const Array<int> & vdofs, DenseMatrix & Pmat_scale);

   // Second derivatives.
   void SetHessianScaleMatrix(const Vector &elfun, const Array<int> & vdofs, int i, int idim, int j, int jdim, DenseMatrix &Pmat_hessian);

   // Go from t -> (x,y) on an element.
   void convertToPhysical(const Array<int> &vdofs, const Vector &elfun, Vector &convertedX);

   ~AnalyticalSurface();
};



}
#endif