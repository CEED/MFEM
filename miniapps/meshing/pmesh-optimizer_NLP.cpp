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
//
//    ---------------------------------------------------------------------
//    Mesh Optimizer NLP Miniapp: Optimize high-order meshes - Parallel Version
//    ---------------------------------------------------------------------
//
// This miniapp performs mesh optimization using the Target-Matrix Optimization
// Paradigm (TMOP) by P.Knupp et al., it used non-linear programming techniques
// to solve the proble,
//
// Compile with: make pmesh-optimizer_NLP


#include "mfem.hpp"
#include "../common/mfem-common.hpp"
#include <iostream>
#include <fstream>
#include "mesh-optimizer_using_NLP.hpp"
#include "MMA.hpp"

using namespace mfem;
using namespace std;

int ftype = 2;

double trueSolFunc(const Vector & x)
{
  if (ftype == 0)
  {
    double val = std::sin( M_PI *x[0] )*std::sin(2.0*M_PI*x[1]);
    return val;
  }
  else if (ftype == 1) // circular wave centered in domain
  {
    double k_w = 30.0;
    double k_t = 0.5;
    double T_ref = 1.0;

    double val = 0.5+0.5*std::tanh(k_w*((std::sin( M_PI *x[0] )*std::sin(M_PI *x[1]))-k_t*T_ref));
    return val;
  }
  else if (ftype == 2) // circular shock wave front centered at origin
  {
    double xc = -0.05,
           yc = -0.05,
           rc = 0.7,
           alpha = 50;
    double dx = (x[0]-xc),
           dy = x[1]-yc;
    double val = dx*dx + dy*dy;
    if (val > 0.0) { val = std::sqrt(val); }
    val -= rc;
    val *= alpha;
    return std::atan(val);
  }
  return 0.0;
  //--------------------------------------------------------------
    // double k_w = 30.0;
    // // double k_t = 0.5;
    // // double T_ref = 1.0;

    // double starval = 0.5+0.1*std::sin(5.0*std::atan2(x[0],x[1])) - std::sqrt(x[0]*x[0]+x[1]*x[1]);

    // double val = 0.5+0.5*std::tanh(k_w*starval);
    // return val;
};

double loadFunc(const Vector & x)
{
  if (ftype == 0)
  {
    double val = 5.0*M_PI*M_PI * std::sin( M_PI *x[0] )*std::sin(2.0*M_PI *x[1]);                 //Mathias
    return val;
  }
  else if (ftype == 1)
  {
    double k_w = 30.0;
    double k_t = 0.5;
    double T_ref = 1.0;

    double bt = k_w*M_PI*M_PI*std::sin( M_PI *x[0] )*std::sin(M_PI *x[1]);
    double bx = k_w*M_PI*std::cos( M_PI *x[0] )*std::sin(M_PI *x[1]);
    double by = k_w*M_PI*std::sin( M_PI *x[0] )*std::cos(M_PI *x[1]);
    double sh = std::tanh(k_w*((std::sin( M_PI *x[0] )*std::sin(M_PI *x[1]))-k_t*T_ref));


    double val = -1.0*( 0.5*( - 1.0*bt - (2.0*sh*(1 - sh*sh))*bx*bx + sh*sh*bt) +
                        0.5*( - 1.0*bt - (2.0*sh*(1 - sh*sh))*by*by + sh*sh*bt) );
    return val;
  }
  else if (ftype == 2)
  {
    double xc = -0.05,
           yc = -0.05,
           r = 0.7,
           alpha = 50;
    double dx = (x[0]-xc),
           dy = x[1]-yc;
    double val = dx*dx + dy*dy;
    if (val > 0.0) { val = std::sqrt(val); }
    double lambda = val;
    double numerator = alpha - std::pow(alpha, 3) * (std::pow(lambda, 2) - std::pow(r, 2));

    // Denominator components
    double term1 = std::pow(alpha, 2) * std::pow(xc, 2);
    double term2 = -2 * std::pow(alpha, 2) * r * lambda;
    double term3 = -2 * xc * alpha * alpha * x[0];
    double term4 = std::pow(alpha, 2) * std::pow(yc, 2);
    double term5 = -2 * yc * alpha * alpha * x[1];
    double term6 = std::pow(alpha, 2) * std::pow(r, 2);
    double term7 = std::pow(alpha, 2) * std::pow(x[0], 2);
    double term8 = std::pow(alpha, 2) * std::pow(x[1], 2);

    // Denominator
    double denominator = lambda * std::pow(
        term1 + term2 + term3 + term4 + term5 + term6 + term7 + term8 + 1, 2);

    // Compute f
    double f = numerator / denominator;
    return f;
  }
  return 0.0;
};

int main (int argc, char *argv[])
{
   // 0. Initialize MPI and HYPRE.
   Mpi::Init(argc, argv);
   int myid = Mpi::WorldRank();
   Hypre::Init();

  // const char *petscrc_file = "";
  // MFEMInitializePetsc(NULL,NULL,petscrc_file,NULL);

  bool perturbMesh = false;
  double epsilon =  0.006;
  int ser_refinement_ = 1;
  int mesh_node_ordering = 0;
  int max_it = 100;
  double max_ch=0.001; //max design change
  double weight_1 = 1e7;
  double weight_2 = 1e-1;
  srand(9898975);

  ParMesh *PMesh = nullptr;

  {
    // Create mesh
    double Lx = 1.0;    double Ly = 1.0;
    int NX = 40;         int NY = 40;
    Mesh des_mesh = Mesh::MakeCartesian2D(NX, NY, Element::QUADRILATERAL, true, Lx, Ly);

    if(perturbMesh)
    {
      int tNumVertices  = des_mesh.GetNV();
      for (int i = 0; i < tNumVertices; ++i) {
        double * Coords = des_mesh.GetVertex(i);
        if (Coords[ 0 ] != 0.0 && Coords[ 0 ] != 1.0 && Coords[ 1 ] != 0.0 && Coords[ 1 ] != 1.0) {
          Coords[ 0 ] = Coords[ 0 ] + ((rand() / double(RAND_MAX)* 2.0 - 1.0)* epsilon);
          Coords[ 1 ] = Coords[ 1 ] + ((rand() / double(RAND_MAX)* 2.0 - 1.0)* epsilon);
        }
      }
    }

    // Refine mesh in serial
    for (int lev = 0; lev < ser_refinement_; lev++) {
      des_mesh.UniformRefinement();
    }
    // Create Parallel Mesh
    PMesh = new ParMesh(MPI_COMM_WORLD, des_mesh);
  }

  int spatialDimension = PMesh->SpaceDimension();

  // -----------------------
  // Remaining mesh settings
  // -----------------------

  // Nodes are only active for higher order meshes, and share locations with
  // the vertices, plus all the higher- order control points within  the
  // element and along the edges and on the faces.
  if (nullptr == PMesh->GetNodes()) {  PMesh->SetCurvature(1, false, -1, 0); }

  int mesh_poly_deg = PMesh->GetNodes()->FESpace()->GetElementOrder(0);

  // Create finite Element Spaces for analysis mesh
  if ( spatialDimension != 2 ) {
    ::mfem_error("... This example only supports 2D meshes");
  }

  // 4. Define a finite element space on the mesh. Here we use vector finite
  //    elements which are tensor products of quadratic finite elements. The
  //    number of components in the vector finite element space is specified by
  //    the last parameter of the FiniteElementSpace constructor.
  FiniteElementCollection *fec;
  if (mesh_poly_deg <= 0)
  {
    fec = new QuadraticPosFECollection;
    mesh_poly_deg = 2;
  }
  else { fec = new H1_FECollection(mesh_poly_deg, spatialDimension); }
  ParFiniteElementSpace *pfespace = new ParFiniteElementSpace(PMesh, fec, spatialDimension,
                                                               mesh_node_ordering);
  ParFiniteElementSpace pfespace_gf(PMesh, fec);
  ParGridFunction x_gf(&pfespace_gf);

  // 5. Make the mesh curved based on the above finite element space. This
  //    means that we define the mesh elements through a fespace-based
  //    transformation of the reference element.
  PMesh->SetNodalFESpace(pfespace);

  // 7. Get the mesh nodes (vertices and other degrees of freedom in the finite
  //    element space) as a finite element grid function in fespace. Note that
  //    changing x automatically changes the shapes of the mesh elements.
  ParGridFunction x(pfespace);
  PMesh->SetNodalGridFunction(&x);

  // set esing variable bounds
  Vector objgrad(pfespace->GetTrueVSize()); objgrad=0.0;
  Vector volgrad(pfespace->GetTrueVSize()); volgrad=1.0;
  Vector xxmax(pfespace->GetTrueVSize());   xxmax=  0.001;
  Vector xxmin(pfespace->GetTrueVSize());   xxmin= -0.001;

  int numOptVars = pfespace->GetTrueVSize();

  ParGridFunction gridfuncOptVar(pfespace);   gridfuncOptVar = 0.0;
  ParGridFunction gridfuncLSBoundIndicator(pfespace);
  gridfuncLSBoundIndicator = 0.0;

  // Identify coordinate dofs perpendicular to BE
  for (int i = 0; i < PMesh->GetNBE(); i++) {
    Element * tEle = PMesh->GetBdrElement(i);
    int attribute = tEle->GetAttribute();

    int NumVert = tEle->GetNVertices();
    ::Array<int> tVerts;
    tEle->GetVertices( tVerts );

    for ( int Ii= 0; Ii < NumVert; Ii++) {
      ::Array<int> tVDofs(spatialDimension);

      pfespace->GetVertexVDofs(tVerts[Ii],tVDofs);

      for ( int Ij= 0; Ij < spatialDimension; Ij++) {
        if ((attribute == 1 || attribute == 3) && Ij == 1) {
          gridfuncLSBoundIndicator[ tVDofs[Ij] ] = 1.0;
        }
        if ( (attribute == 2 || attribute == 4) && Ij == 0) {
          gridfuncLSBoundIndicator[ tVDofs[Ij] ] = 1.0;
        }
      }
    }
  }

  gridfuncOptVar.SetTrueVector();
  gridfuncLSBoundIndicator.SetTrueVector();

  Vector & trueOptvar = gridfuncOptVar.GetTrueVector();

  std::vector<std::pair<int, double>> essentialBC(4);
  essentialBC[0] = {1, 0};
  essentialBC[1] = {2, 0};
  essentialBC[2] = {3, 0};
  essentialBC[3] = {4, 0};

  MMA* mma = nullptr;
  {
#ifdef MFEM_USE_PETSC
    double a=0.0;
    double c=1000.0;
    double d=0.0;
    mma=new MMA(MPI_COMM_WORLD,1, objgrad,&a,&c,&d);
#else
    mma=new MMA(MPI_COMM_WORLD, trueOptvar.Size(), 0, trueOptvar);
#endif
  }

  Diffusion_Solver solver(PMesh, essentialBC, 1);
  QuantityOfInterest QoIEvaluator(PMesh, 1);
  NodeAwareTMOPQuality MeshQualityEvaluator(PMesh, 1);

  Coefficient *QCoef = new FunctionCoefficient(loadFunc);
  solver.SetManufacturedSolution(QCoef);
  Coefficient *trueSolution = new FunctionCoefficient(trueSolFunc);
  QoIEvaluator.setTrueSolCoeff(  trueSolution );
  x_gf.ProjectCoefficient(*trueSolution);

  ParaViewDataCollection paraview_dc("MeshOptimizer", PMesh);
  paraview_dc.SetLevelsOfDetail(1);
  paraview_dc.SetDataFormat(VTKFormat::BINARY);
  paraview_dc.SetHighOrderOutput(true);

  for(int i=1;i<max_it;i++)
  {
    solver.SetDesign( gridfuncOptVar );
    solver.FSolve();

    ParGridFunction & discretSol = solver.GetSolution();

    QoIEvaluator.SetDesign( gridfuncOptVar );
    MeshQualityEvaluator.SetDesign( gridfuncOptVar );

    QoIEvaluator.SetDiscreteSol( discretSol );

    double ObjVal = QoIEvaluator.EvalQoI();
    double meshQualityVal = MeshQualityEvaluator.EvalQoI();

    double val = weight_1 * ObjVal+ weight_2 * meshQualityVal;

    QoIEvaluator.EvalQoIGrad();
    MeshQualityEvaluator.EvalQoIGrad();

    ParLinearForm * dQdu = QoIEvaluator.GetDQDu();
    ParLinearForm * dQdxExpl = QoIEvaluator.GetDQDx();
    ParLinearForm * dMeshQdxExpl = MeshQualityEvaluator.GetDQDx();

    solver.ASolve( *dQdu );

    ParLinearForm * dQdxImpl = solver.GetImplicitDqDx();

    ParLinearForm dQdx(pfespace); dQdx = 0.0;
    dQdx.Add(weight_1, *dQdxExpl);
    dQdx.Add(weight_1, *dQdxImpl);
    dQdx.Add(weight_2, *dMeshQdxExpl);

    objgrad = dQdx;

    // impose desing variable bounds - set xxmin and xxmax
    xxmin=gridfuncOptVar; xxmin-=max_ch;
    xxmax=gridfuncOptVar; xxmax+=max_ch;
    for(int li=0;li<xxmin.Size();li++){
      if( gridfuncLSBoundIndicator[li] ==1.0)
      {
        xxmin[li] = -1e-8;
        xxmax[li] =  1e-8;
      }
    }

    x_gf.ProjectCoefficient(*trueSolution);
    ParGridFunction objGradGF(pfespace); objGradGF = objgrad;
    paraview_dc.SetCycle(i);
    paraview_dc.SetTime(i*1.0);
    paraview_dc.RegisterField("ObjGrad",&objGradGF);
    paraview_dc.RegisterField("Solution",&x_gf);
    paraview_dc.Save();

    double  conDummy = -0.1;

    std:cout<<"Iter: "<<i<<" obj: "<<val<<" with: "<<ObjVal<<" | "<<meshQualityVal<<std::endl;


#ifdef MFEM_USE_PETSC
    mma->Update(trueOptvar,objgrad,&conDummy,&volgrad,xxmin,xxmax);
#else
    mma->Update(i, objgrad, &conDummy, volgrad.GetData(), xxmin,xxmax, trueOptvar.GetData());
#endif

    gridfuncOptVar.SetFromTrueVector();

    // std::string tDesingName = "DesingVarVec";
    // desingVarVec.Save( tDesingName.c_str() );

    // std::string tFieldName = "FieldVec";
    // tPreassureGF.Save( tFieldName.c_str() );
  }

  return 0;
}
