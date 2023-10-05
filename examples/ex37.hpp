//                  MFEM Example 37 - Serial/Parallel Shared Code
//
//

#include "mfem.hpp"
#include <fstream>
#include <iostream>
#include <functional>

namespace mfem
{

/**
 * @brief Inverse sigmoid function
 *
 */
double inv_sigmoid(const double x)
{
   const double tol = 1e-12;
   const double tmp = std::min(std::max(tol,x),1.0-tol);
   return std::log(tmp/(1.0-tmp));
}

/**
 * @brief Sigmoid function
 *
 */
double sigmoid(const double x)
{
   if (x >= 0)
   {
      return 1.0/(1.0+std::exp(-x));
   }
   else
   {
      return std::exp(x)/(1.0+std::exp(x));
   }
}

/**
 * @brief Derivative of sigmoid function
 *
 */
double der_sigmoid(const double x)
{
   double tmp = sigmoid(x);
   return tmp*(1.0 - tmp);
}

double simp(const double x, const double rho_min=1e-06, const double exp=3.0,
            const double rho_max=1.0)
{
   return rho_min + std::pow(x, exp) * (rho_max - rho_min);
}

double der_simp(const double x, const double rho_min=1e-06,
                const double exp=3.0, const double rho_max=1.0)
{
   return (exp - 1.0) * std::pow(x, exp - 1) * (rho_max - rho_min);
}


/**
 * @brief Returns f(u(x)) where u is a scalar GridFunction and f:R → R
 *
 */
class MappedGridFunctionCoefficient : public GridFunctionCoefficient
{
protected:
   std::function<double(const double)> fun; // f:R → R
public:
   MappedGridFunctionCoefficient()
      :GridFunctionCoefficient(),
       fun([](const double x) {return x;}) {}
   MappedGridFunctionCoefficient(const GridFunction *gf,
                                 std::function<double(const double)> fun_,
                                 int comp=1)
      :GridFunctionCoefficient(gf, comp),
       fun(fun_) {}


   virtual double Eval(ElementTransformation &T,
                       const IntegrationPoint &ip)
   {
      return fun(GridFunctionCoefficient::Eval(T, ip));
   }
   void SetFunction(std::function<double(const double)> fun_) { fun = fun_; }
};


/**
 * @brief Returns f(u(x)) - f(v(x)) where u, v are scalar GridFunctions and f:R → R
 *
 */
class DiffMappedGridFunctionCoefficient : public GridFunctionCoefficient
{
protected:
   const GridFunction *OtherGridF;
   GridFunctionCoefficient OtherGridF_cf;
   std::function<double(const double)> fun; // f:R → R
public:
   DiffMappedGridFunctionCoefficient()
      :GridFunctionCoefficient(),
       OtherGridF(nullptr),
       OtherGridF_cf(),
       fun([](const double x) {return x;}) {}
   DiffMappedGridFunctionCoefficient(const GridFunction *gf,
                                     const GridFunction *other_gf,
                                     std::function<double(const double)> fun_,
                                     int comp=1)
      :GridFunctionCoefficient(gf, comp),
       OtherGridF(other_gf),
       OtherGridF_cf(OtherGridF),
       fun(fun_) {}

   virtual double Eval(ElementTransformation &T,
                       const IntegrationPoint &ip)
   {
      const double value1 = fun(GridFunctionCoefficient::Eval(T, ip));
      const double value2 = fun(OtherGridF_cf.Eval(T, ip));
      return value1 - value2;
   }
   void SetFunction(std::function<double(const double)> fun_) { fun = fun_; }
};


/**
 * @brief Strain energy density coefficient
 *
 */
class StrainEnergyDensityCoefficient : public Coefficient
{
protected:
   Coefficient * lambda=nullptr;
   Coefficient * mu=nullptr;
   GridFunction *u = nullptr; // displacement
   GridFunction *rho_filter = nullptr; // filter density
   DenseMatrix grad; // auxiliary matrix, used in Eval
   double exponent;
   double rho_min;

public:
   StrainEnergyDensityCoefficient(Coefficient *lambda_, Coefficient *mu_,
                                  GridFunction * u_, GridFunction * rho_filter_, double rho_min_=1e-6,
                                  double exponent_ = 3.0)
      : lambda(lambda_), mu(mu_),  u(u_), rho_filter(rho_filter_),
        exponent(exponent_), rho_min(rho_min_)
   {
      MFEM_ASSERT(rho_min_ >= 0.0, "rho_min must be >= 0");
      MFEM_ASSERT(rho_min_ < 1.0,  "rho_min must be > 1");
      MFEM_ASSERT(u, "displacement field is not set");
      MFEM_ASSERT(rho_filter, "density field is not set");
   }

   virtual double Eval(ElementTransformation &T, const IntegrationPoint &ip)
   {
      double L = lambda->Eval(T, ip);
      double M = mu->Eval(T, ip);
      u->GetVectorGradient(T, grad);
      double div_u = grad.Trace();
      double density = L*div_u*div_u;
      grad.Symmetrize();
      density += 2*M*grad.FNorm2();
      double val = rho_filter->GetValue(T,ip);

      return -exponent * pow(val, exponent-1.0) * (1-rho_min) * density;
   }
};

/**
 * @brief Volumetric force for linear elasticity
 *
 */
class VolumeForceCoefficient : public VectorCoefficient
{
private:
   double r;
   Vector center;
   Vector force;
public:
   VolumeForceCoefficient(double r_,Vector &  center_, Vector & force_) :
      VectorCoefficient(center_.Size()), r(r_), center(center_), force(force_) { }

   virtual void Eval(Vector &V, ElementTransformation &T,
                     const IntegrationPoint &ip)
   {
      Vector xx; xx.SetSize(T.GetDimension());
      T.Transform(ip,xx);
      for (int i=0; i<xx.Size(); i++)
      {
         xx[i]=xx[i]-center[i];
      }

      double cr=xx.Norml2();
      V.SetSize(T.GetDimension());
      if (cr <= r)
      {
         V = force;
      }
      else
      {
         V = 0.0;
      }
   }

   void Set(double r_,Vector & center_, Vector & force_)
   {
      r=r_;
      center = center_;
      force = force_;
   }
};

/**
 * @brief Class for solving Poisson's equation:
 *
 *       - ∇ ⋅(κ ∇ u) = f  in Ω
 *
 */
class DiffusionSolver
{
private:
   Mesh * mesh = nullptr;
   int order = 1;
   // diffusion coefficient
   Coefficient * diffcf = nullptr;
   // mass coefficient
   Coefficient * masscf = nullptr;
   Coefficient * rhscf = nullptr;
   Coefficient * essbdr_cf = nullptr;
   Coefficient * neumann_cf = nullptr;
   VectorCoefficient * gradient_cf = nullptr;

   // FEM solver
   int dim;
   FiniteElementCollection * fec = nullptr;
   FiniteElementSpace * fes = nullptr;
   Array<int> ess_bdr;
   Array<int> neumann_bdr;
   GridFunction * u = nullptr;
   LinearForm * b = nullptr;
   bool parallel;
#ifdef MFEM_USE_MPI
   ParMesh * pmesh = nullptr;
   ParFiniteElementSpace * pfes = nullptr;
#endif

public:
   DiffusionSolver() { }
   DiffusionSolver(Mesh * mesh_, int order_, Coefficient * diffcf_,
                   Coefficient * cf_);

   void SetMesh(Mesh * mesh_)
   {
      mesh = mesh_;
      parallel = false;
#ifdef MFEM_USE_MPI
      pmesh = dynamic_cast<ParMesh *>(mesh);
      if (pmesh) { parallel = true; }
#endif
   }
   void SetOrder(int order_) { order = order_ ; }
   void SetDiffusionCoefficient(Coefficient * diffcf_) { diffcf = diffcf_; }
   void SetMassCoefficient(Coefficient * masscf_) { masscf = masscf_; }
   void SetRHSCoefficient(Coefficient * rhscf_) { rhscf = rhscf_; }
   void SetEssentialBoundary(const Array<int> & ess_bdr_) { ess_bdr = ess_bdr_;};
   void SetNeumannBoundary(const Array<int> & neumann_bdr_) { neumann_bdr = neumann_bdr_;};
   void SetNeumannData(Coefficient * neumann_cf_) {neumann_cf = neumann_cf_;}
   void SetEssBdrData(Coefficient * essbdr_cf_) {essbdr_cf = essbdr_cf_;}
   void SetGradientData(VectorCoefficient * gradient_cf_) {gradient_cf = gradient_cf_;}

   void ResetFEM();
   void SetupFEM();

   void Solve();
   GridFunction * GetFEMSolution();
   LinearForm * GetLinearForm() {return b;}
#ifdef MFEM_USE_MPI
   ParGridFunction * GetParFEMSolution();
   ParLinearForm * GetParLinearForm()
   {
      if (parallel)
      {
         return dynamic_cast<ParLinearForm *>(b);
      }
      else
      {
         MFEM_ABORT("Wrong code path. Call GetLinearForm");
         return nullptr;
      }
   }
#endif

   ~DiffusionSolver();

};

/**
 * @brief Class for solving linear elasticity:
 *
 *        -∇ ⋅ σ(u) = f  in Ω  + BCs
 *
 *  where
 *
 *        σ(u) = λ ∇⋅u I + μ (∇ u + ∇uᵀ)
 *
 */
class LinearElasticitySolver
{
private:
   Mesh * mesh = nullptr;
   int order = 1;
   Coefficient * lambda_cf = nullptr;
   Coefficient * mu_cf = nullptr;
   VectorCoefficient * essbdr_cf = nullptr;
   VectorCoefficient * rhs_cf = nullptr;

   // FEM solver
   int dim;
   FiniteElementCollection * fec = nullptr;
   FiniteElementSpace * fes = nullptr;
   Array<int> ess_bdr;
   Array<int> neumann_bdr;
   GridFunction * u = nullptr;
   LinearForm * b = nullptr;
   bool parallel;
#ifdef MFEM_USE_MPI
   ParMesh * pmesh = nullptr;
   ParFiniteElementSpace * pfes = nullptr;
#endif

public:
   LinearElasticitySolver() { }
   LinearElasticitySolver(Mesh * mesh_, int order_,
                          Coefficient * lambda_cf_, Coefficient * mu_cf_);

   void SetMesh(Mesh * mesh_)
   {
      mesh = mesh_;
      parallel = false;
#ifdef MFEM_USE_MPI
      pmesh = dynamic_cast<ParMesh *>(mesh);
      if (pmesh) { parallel = true; }
#endif
   }
   void SetOrder(int order_) { order = order_ ; }
   void SetLameCoefficients(Coefficient * lambda_cf_, Coefficient * mu_cf_) { lambda_cf = lambda_cf_; mu_cf = mu_cf_;  }
   void SetRHSCoefficient(VectorCoefficient * rhs_cf_) { rhs_cf = rhs_cf_; }
   void SetEssentialBoundary(const Array<int> & ess_bdr_) { ess_bdr = ess_bdr_;};
   void SetNeumannBoundary(const Array<int> & neumann_bdr_) { neumann_bdr = neumann_bdr_;};
   void SetEssBdrData(VectorCoefficient * essbdr_cf_) {essbdr_cf = essbdr_cf_;}

   void ResetFEM();
   void SetupFEM();

   void Solve();
   GridFunction * GetFEMSolution();
   LinearForm * GetLinearForm() {return b;}
#ifdef MFEM_USE_MPI
   ParGridFunction * GetParFEMSolution();
   ParLinearForm * GetParLinearForm()
   {
      if (parallel)
      {
         return dynamic_cast<ParLinearForm *>(b);
      }
      else
      {
         MFEM_ABORT("Wrong code path. Call GetLinearForm");
         return nullptr;
      }
   }
#endif

   ~LinearElasticitySolver();

};

// -----------------------------------------------------------------------
// --------------------      Poisson solver     --------------------------
// -----------------------------------------------------------------------

DiffusionSolver::DiffusionSolver(Mesh * mesh_, int order_,
                                 Coefficient * diffcf_, Coefficient * rhscf_)
   : mesh(mesh_), order(order_), diffcf(diffcf_), rhscf(rhscf_)
{

#ifdef MFEM_USE_MPI
   pmesh = dynamic_cast<ParMesh *>(mesh);
   if (pmesh) { parallel = true; }
#endif

   SetupFEM();
}

void DiffusionSolver::SetupFEM()
{
   dim = mesh->Dimension();
   fec = new H1_FECollection(order, dim);

#ifdef MFEM_USE_MPI
   if (parallel)
   {
      pfes = new ParFiniteElementSpace(pmesh, fec);
      u = new ParGridFunction(pfes);
      b = new ParLinearForm(pfes);
   }
   else
   {
      fes = new FiniteElementSpace(mesh, fec);
      u = new GridFunction(fes);
      b = new LinearForm(fes);
   }
#else
   fes = new FiniteElementSpace(mesh, fec);
   u = new GridFunction(fes);
   b = new LinearForm(fes);
#endif
   *u=0.0;

   if (!ess_bdr.Size())
   {
      if (mesh->bdr_attributes.Size())
      {
         ess_bdr.SetSize(mesh->bdr_attributes.Max());
         ess_bdr = 1;
      }
   }
}

void DiffusionSolver::Solve()
{
   OperatorPtr A;
   Vector B, X;
   Array<int> ess_tdof_list;
#ifdef MFEM_USE_MPI
   if (parallel)
   {
      pfes->GetEssentialTrueDofs(ess_bdr,ess_tdof_list);
   }
   else
   {
      fes->GetEssentialTrueDofs(ess_bdr,ess_tdof_list);
   }
#else
   fes->GetEssentialTrueDofs(ess_bdr,ess_tdof_list);
#endif
   *u=0.0;
   if (b)
   {
      delete b;
#ifdef MFEM_USE_MPI
      if (parallel)
      {
         b = new ParLinearForm(pfes);
      }
      else
      {
         b = new LinearForm(fes);
      }
#else
      b = new LinearForm(fes);
#endif
   }
   if (rhscf)
   {
      b->AddDomainIntegrator(new DomainLFIntegrator(*rhscf));
   }
   if (neumann_cf)
   {
      MFEM_VERIFY(neumann_bdr.Size(), "neumann_bdr attributes not provided");
      b->AddBoundaryIntegrator(new BoundaryLFIntegrator(*neumann_cf),neumann_bdr);
   }
   else if (gradient_cf)
   {
      MFEM_VERIFY(neumann_bdr.Size(), "neumann_bdr attributes not provided");
      b->AddBoundaryIntegrator(new BoundaryNormalLFIntegrator(*gradient_cf),
                               neumann_bdr);
   }

   b->Assemble();

   BilinearForm * a = nullptr;

#ifdef MFEM_USE_MPI
   if (parallel)
   {
      a = new ParBilinearForm(pfes);
   }
   else
   {
      a = new BilinearForm(fes);
   }
#else
   a = new BilinearForm(fes);
#endif
   a->AddDomainIntegrator(new DiffusionIntegrator(*diffcf));
   if (masscf)
   {
      a->AddDomainIntegrator(new MassIntegrator(*masscf));
   }
   a->Assemble();
   if (essbdr_cf)
   {
      u->ProjectBdrCoefficient(*essbdr_cf,ess_bdr);
   }
   a->FormLinearSystem(ess_tdof_list, *u, *b, A, X, B);

   CGSolver * cg = nullptr;
   Solver * M = nullptr;
#ifdef MFEM_USE_MPI
   if (parallel)
   {
      M = new HypreBoomerAMG;
      dynamic_cast<HypreBoomerAMG*>(M)->SetPrintLevel(0);
      cg = new CGSolver(pmesh->GetComm());
   }
   else
   {
      M = new GSSmoother((SparseMatrix&)(*A));
      cg = new CGSolver;
   }
#else
   M = new GSSmoother((SparseMatrix&)(*A));
   cg = new CGSolver;
#endif
   cg->SetRelTol(1e-12);
   cg->SetMaxIter(10000);
   cg->SetPrintLevel(0);
   cg->SetPreconditioner(*M);
   cg->SetOperator(*A);
   cg->Mult(B, X);
   delete M;
   delete cg;
   a->RecoverFEMSolution(X, *b, *u);
   delete a;
}

GridFunction * DiffusionSolver::GetFEMSolution()
{
   return u;
}

#ifdef MFEM_USE_MPI
ParGridFunction * DiffusionSolver::GetParFEMSolution()
{
   if (parallel)
   {
      return dynamic_cast<ParGridFunction*>(u);
   }
   else
   {
      MFEM_ABORT("Wrong code path. Call GetFEMSolution");
      return nullptr;
   }
}
#endif

DiffusionSolver::~DiffusionSolver()
{
   delete u; u = nullptr;
   delete fes; fes = nullptr;
#ifdef MFEM_USE_MPI
   delete pfes; pfes=nullptr;
#endif
   delete fec; fec = nullptr;
   delete b;
}



// -----------------------------------------------------------------------
// ------------------      Elasticity solver     -------------------------
// -----------------------------------------------------------------------

LinearElasticitySolver::LinearElasticitySolver(Mesh * mesh_, int order_,
                                               Coefficient * lambda_cf_, Coefficient * mu_cf_)
   : mesh(mesh_), order(order_), lambda_cf(lambda_cf_), mu_cf(mu_cf_)
{
#ifdef MFEM_USE_MPI
   pmesh = dynamic_cast<ParMesh *>(mesh);
   if (pmesh) { parallel = true; }
#endif
   SetupFEM();
}

void LinearElasticitySolver::SetupFEM()
{
   dim = mesh->Dimension();
   fec = new H1_FECollection(order, dim,BasisType::Positive);

#ifdef MFEM_USE_MPI
   if (parallel)
   {
      pfes = new ParFiniteElementSpace(pmesh, fec, dim);
      u = new ParGridFunction(pfes);
      b = new ParLinearForm(pfes);
   }
   else
   {
      fes = new FiniteElementSpace(mesh, fec,dim);
      u = new GridFunction(fes);
      b = new LinearForm(fes);
   }
#else
   fes = new FiniteElementSpace(mesh, fec, dim);
   u = new GridFunction(fes);
   b = new LinearForm(fes);
#endif
   *u=0.0;

   if (!ess_bdr.Size())
   {
      if (mesh->bdr_attributes.Size())
      {
         ess_bdr.SetSize(mesh->bdr_attributes.Max());
         ess_bdr = 1;
      }
   }
}

void LinearElasticitySolver::Solve()
{
   GridFunction * x = nullptr;
   OperatorPtr A;
   Vector B, X;
   Array<int> ess_tdof_list;

#ifdef MFEM_USE_MPI
   if (parallel)
   {
      x = new ParGridFunction(pfes);
      pfes->GetEssentialTrueDofs(ess_bdr,ess_tdof_list);
   }
   else
   {
      x = new GridFunction(fes);
      fes->GetEssentialTrueDofs(ess_bdr,ess_tdof_list);
   }
#else
   x = new GridFunction(fes);
   fes->GetEssentialTrueDofs(ess_bdr,ess_tdof_list);
#endif
   *u=0.0;
   if (b)
   {
      delete b;
#ifdef MFEM_USE_MPI
      if (parallel)
      {
         b = new ParLinearForm(pfes);
      }
      else
      {
         b = new LinearForm(fes);
      }
#else
      b = new LinearForm(fes);
#endif
   }
   if (rhs_cf)
   {
      b->AddDomainIntegrator(new VectorDomainLFIntegrator(*rhs_cf));
   }

   b->Assemble();

   *x = 0.0;

   BilinearForm * a = nullptr;

#ifdef MFEM_USE_MPI
   if (parallel)
   {
      a = new ParBilinearForm(pfes);
   }
   else
   {
      a = new BilinearForm(fes);
   }
#else
   a = new BilinearForm(fes);
#endif
   a->AddDomainIntegrator(new ElasticityIntegrator(*lambda_cf, *mu_cf));
   a->Assemble();
   if (essbdr_cf)
   {
      u->ProjectBdrCoefficient(*essbdr_cf,ess_bdr);
   }
   a->FormLinearSystem(ess_tdof_list, *x, *b, A, X, B);

   CGSolver * cg = nullptr;
   Solver * M = nullptr;
#ifdef MFEM_USE_MPI
   if (parallel)
   {
      M = new HypreBoomerAMG;
      dynamic_cast<HypreBoomerAMG*>(M)->SetPrintLevel(0);
      cg = new CGSolver(pmesh->GetComm());
   }
   else
   {
      M = new GSSmoother((SparseMatrix&)(*A));
      cg = new CGSolver;
   }
#else
   M = new GSSmoother((SparseMatrix&)(*A));
   cg = new CGSolver;
#endif
   cg->SetRelTol(1e-10);
   cg->SetMaxIter(10000);
   cg->SetPrintLevel(0);
   cg->SetPreconditioner(*M);
   cg->SetOperator(*A);
   cg->Mult(B, X);
   delete M;
   delete cg;
   a->RecoverFEMSolution(X, *b, *x);
   *u+=*x;
   delete a;
   delete x;
}

GridFunction * LinearElasticitySolver::GetFEMSolution()
{
   return u;
}

#ifdef MFEM_USE_MPI
ParGridFunction * LinearElasticitySolver::GetParFEMSolution()
{
   if (parallel)
   {
      return dynamic_cast<ParGridFunction*>(u);
   }
   else
   {
      MFEM_ABORT("Wrong code path. Call GetFEMSolution");
      return nullptr;
   }
}
#endif

LinearElasticitySolver::~LinearElasticitySolver()
{
   delete u; u = nullptr;
   delete fes; fes = nullptr;
#ifdef MFEM_USE_MPI
   delete pfes; pfes=nullptr;
#endif
   delete fec; fec = nullptr;
   delete b;
}

class ExBiSecLVPGTopOpt
{
public:
   ExBiSecLVPGTopOpt(GridFunction *u, GridFunction *psi, GridFunction *rho_filter,
                     VectorCoefficient &v_force, const double lambda, const double mu,
                     const double target_volume, const double eps,
                     const double rho_min, const double exponent,
                     Array<int> &ess_bdr, Array<int> &ess_bdr_filter, const double c1,
                     const double c2): u(u), psi(psi),
      rho_filter(rho_filter),
      lambda_cf(lambda), mu_cf(mu),
      state_fes(u->FESpace()), control_fes(psi->FESpace()),
      filter_fes(rho_filter->FESpace()), eps_squared(eps*eps), one(1.0), zero(0.0),
      v_force(v_force), target_volume(target_volume),
      lambda_SIMP_cf(lambda_cf, simp_cf), mu_SIMP_cf(mu_cf, simp_cf),
      negDerSimpElasticityEnergy(&lambda_cf, &mu_cf, u, rho_filter, rho_min),
      c1(c1), c2(c2), alpha0(1.0), grad_evaluated(false)
   {
      simp_fun = [rho_min, exponent](const double x) {return simp(x, rho_min, exponent, 1.0); };
      simp_cf.SetFunction(simp_fun);
      simp_cf.SetGridFunction(rho_filter);

      current_compliance = infinity();
      current_volume = infinity();
      isParallel = false;
      rho = new MappedGridFunctionCoefficient(psi, sigmoid);
#ifdef MFEM_USE_MPI
      if (dynamic_cast<ParGridFunction*>(u))
      {
         par_state_fes = dynamic_cast<ParGridFunction*>(u)->ParFESpace();
         par_control_fes = dynamic_cast<ParGridFunction*>(psi)->ParFESpace();
         par_filter_fes = dynamic_cast<ParGridFunction*>(rho_filter)->ParFESpace();
         MFEM_VERIFY(par_state_fes && par_control_fes &&
                     par_filter_fes, "The state variable is in parallel, but not others.");
         isParallel = true;
      }
#endif
      w_filter = newGridFunction(filter_fes);
      grad = newGridFunction(control_fes);

      elasticitySolver = new LinearElasticitySolver();
      elasticitySolver->SetMesh(state_fes->GetMesh());
      elasticitySolver->SetOrder(state_fes->FEColl()->GetOrder());
      elasticitySolver->SetLameCoefficients(&lambda_SIMP_cf,&mu_SIMP_cf);
      elasticitySolver->SetupFEM();
      elasticitySolver->SetRHSCoefficient(&v_force);
      elasticitySolver->SetEssentialBoundary(ess_bdr);

      // 7. Set-up the filter solver.
      filterSolver = new DiffusionSolver();
      filterSolver->SetMesh(state_fes->GetMesh());
      filterSolver->SetOrder(state_fes->FEColl()->GetOrder());
      filterSolver->SetDiffusionCoefficient(&eps_squared);
      filterSolver->SetMassCoefficient(&one);
      filterSolver->SetupFEM();
      filterSolver->SetEssentialBoundary(ess_bdr_filter);

      invmass = newBilinearForm(control_fes);
      invmass->AddDomainIntegrator(new InverseIntegrator(new MassIntegrator(one)));
      invmass->Assemble();

      // 8. Define the Lagrange multiplier and gradient functions
      w_rhs = newLinearForm(control_fes);
      w_cf = new GridFunctionCoefficient(w_filter);
      w_rhs->AddDomainIntegrator(new DomainLFIntegrator(*w_cf));

   }

   double Eval()
   {
      bool proj_succeeded = proj_bisec();
      if (!proj_succeeded)
      {
         current_compliance = infinity();
         return current_compliance;
      }
      // Step 1 - Filter solve
      // Solve (ϵ^2 ∇ ρ̃, ∇ v ) + (ρ̃,v) = (ρ,v)
      filterSolver->SetRHSCoefficient(rho);
      filterSolver->Solve();
      *rho_filter = *filterSolver->GetFEMSolution();

      // Step 2 - State solve
      // Solve (λ r(ρ̃) ∇⋅u, ∇⋅v) + (2 μ r(ρ̃) ε(u), ε(v)) = (f,v)
      elasticitySolver->Solve();
      *u = *elasticitySolver->GetFEMSolution();
#ifdef MFEM_USE_MPI
      if (isParallel)
      {
         ParGridFunction *par_u = dynamic_cast<ParGridFunction*>(u);
         current_compliance = (*elasticitySolver->GetParLinearForm())(*par_u);
      }
      else
      {
         current_compliance = (*elasticitySolver->GetLinearForm())(*u);
      }
#elif
      current_compliance = (*elasticitySolver->GetLinearForm())(*u);
#endif
      grad_evaluated = false;
      return current_compliance;
   }

   void Gradient()
   {
      if (grad_evaluated) { return; }
      if (current_compliance == infinity()) { Eval(); }
      grad_evaluated = true;
      // Step 3 - Adjoint filter solve
      // Solve (ϵ² ∇ w̃, ∇ v) + (w̃ ,v) = (-r'(ρ̃) ( λ |∇⋅u|² + 2 μ |ε(u)|²),v)
      filterSolver->SetRHSCoefficient(&negDerSimpElasticityEnergy);
      filterSolver->Solve();
      *w_filter = *filterSolver->GetFEMSolution();

      // Step 4 - Compute gradient
      // Solve G = M⁻¹w̃
      w_rhs->Assemble();
      invmass->Mult(*w_rhs, *grad);

   }
   GridFunction *GetGradient()
   {
      if (!grad_evaluated)
      {
         Gradient();
      }
      return grad;
   }


   void SetAlpha0(const double alpha)
   {
      alpha0 = alpha;
   }

   double Step()
   {
      double alpha(alpha0), L(0.0), U(infinity());
      GridFunction *psi_k = newGridFunction(control_fes);
      GridFunction *direction = newGridFunction(control_fes);
      LinearForm *directionalDer = newLinearForm(control_fes);
      *psi_k = *psi;
      Eval();
      *direction = *GetGradient();
      direction->Neg();

      // Measure the downhill slope using Naïve L2 inner product
      //
      // GridFunctionCoefficient direction_cf(direction);
      // directionalDer->AddDomainIntegrator(new DomainLFIntegrator(direction_cf));

      // Measure the downhill slope using weighted inner product,
      // <d, grad>_w = (d, sigmoid'(ψ_k) grad)
      //
      MappedGridFunctionCoefficient der_sigmoid_psi(psi_k, der_sigmoid);
      GridFunctionCoefficient direction_cf(direction);
      ProductCoefficient der_sigmoid_diff_psi(direction_cf, der_sigmoid_psi);
      directionalDer->AddDomainIntegrator(new DomainLFIntegrator(
                                             der_sigmoid_diff_psi));

      // Measure the downhill slope using change of rho for given direction
      // ρ = sigmoid(sigmoid⁻¹(ρ_k) + α d) = sigmoid(ψ)
      // -> α d̃ = dρ/dd = α sigmoid'(ψ) d
      // <d̃, \tilde(grad)> = <sigmoid'(ψ_k) d, sigmoid'(ψ) grad>
      //
      // MappedGridFunctionCoefficient der_sigmoid_psi(psi, der_sigmoid);
      // MappedGridFunctionCoefficient der_sigmoid_psi_k(psi_k, der_sigmoid);
      // GridFunctionCoefficient direction_cf(direction);
      // auto double_der_sigmoid = ProductCoefficient(der_sigmoid_psi, der_sigmoid_psi_k);
      // auto directional = ProductCoefficient(double_der_sigmoid, direction_cf);
      // directionalDer->AddDomainIntegrator(new DomainLFIntegrator(directional));

      directionalDer->Assemble();

      const double d = (*directionalDer)(*grad);

      const double compliance = current_compliance;
      out << "(" << compliance << ", " << d << ")" << std::endl;
      int k = 0;
      int maxit = 3;
      for (; k<maxit; k++)
      {
         // update and evaluate F
         *psi = *psi_k;
         psi->Add(alpha, *direction);
         Eval();
         if (current_compliance == infinity())
         {
            out << "Projection failed. alpha:" << alpha << " -> ";
            U = alpha;
            alpha = (L + U) / 2.0;
            out << alpha << std::endl;
         }
         else
         {
            out << "Sufficient decrease condition: ";
            out << "(" << current_compliance << ", " << compliance + c1*d*alpha << "), ";
            if (current_compliance > compliance + c1*d*alpha)
               // sufficient decreament condition failed
            {
               // We moved too far so that the descent direction is no more valid
               // -> Backtracking (left half of the search interval)
               out << "Failed. alpha:" << alpha << " -> ";
               // Decrease upper bound
               U = alpha;
               alpha = (L + U) / 2.0;
               out << alpha << std::endl;
            }
            else
            {
               out << "Success." << std::endl;

               const double current_d = (*directionalDer)(*GetGradient());

               out << "Direction update condition: ";
               out << "(" << current_d << ", " << c2*d << "), ";
               if (current_d < c2*d) // Current direction is still decent
               {
                  // Increase lower bound and try go further
                  out << "Failed. alpha:" << alpha << " -> ";
                  L = alpha;
                  alpha = U == infinity() ? 2.0*L : (L + U) / 2.0;
                  out << alpha << std::endl;
               }
               else // objective decreased enough, and direction is sufficiently changed
               {
                  out << "Success." << std::endl;
                  out << "Final alpha: " << alpha << std::endl;
                  break;
               }
            }
         }
      }
      out << "The number of eval: " << k << std::endl;
      delete psi_k;
      delete direction;
      delete directionalDer;
      return alpha;
   }

   double GetCompliance()
   {
      return current_compliance == infinity() ? Eval() : current_compliance;
   }

   double GetVolume()
   {
      return current_volume;
   }

   GridFunction GetSIMPrho()
   {
      GridFunction r_gf(filter_fes);
      r_gf.ProjectCoefficient(simp_cf);
      return r_gf;
   }
#ifdef MFEM_USE_MPI
   ParGridFunction GetParSIMPrho()
   {
      ParGridFunction r_gf(par_filter_fes);
      r_gf.ProjectCoefficient(simp_cf);
      return r_gf;
   }
#endif

   ~ExBiSecLVPGTopOpt()
   {
      delete rho;
      delete w_cf;
      delete elasticitySolver;
      delete filterSolver;
      delete invmass;
      delete w_rhs;
   }


   /**
    * @brief Bregman projection of ρ = sigmoid(ψ) onto the subspace
    *        ∫_Ω ρ dx = θ vol(Ω) as follows:
    *
    *        1. Compute the root of the R → R function
    *            f(c) = ∫_Ω sigmoid(ψ + c) dx - θ vol(Ω)
    *        2. Set ψ ← ψ + c.
    *
    * @param psi a GridFunction to be updated
    * @param target_volume θ vol(Ω)
    * @param tol Newton iteration tolerance
    * @param max_its Newton maximum iteration number
    * @return double Final volume, ∫_Ω sigmoid(ψ)
    */
   bool proj(double tol=1e-12, int max_its=10)
   {
      MappedGridFunctionCoefficient sigmoid_psi(psi, sigmoid);
      MappedGridFunctionCoefficient der_sigmoid_psi(psi, der_sigmoid);


      LinearForm *int_sigmoid_psi = newLinearForm(control_fes);
      int_sigmoid_psi->AddDomainIntegrator(new DomainLFIntegrator(sigmoid_psi));
      LinearForm *int_der_sigmoid_psi = newLinearForm(control_fes);
      int_der_sigmoid_psi->AddDomainIntegrator(new DomainLFIntegrator(
                                                  der_sigmoid_psi));
      bool done = false;
      for (int k=0; k<max_its; k++) // Newton iteration
      {
         int_sigmoid_psi->Assemble(); // Recompute f(c) with updated ψ
         double f = int_sigmoid_psi->Sum();

         int_der_sigmoid_psi->Assemble(); // Recompute df(c) with updated ψ
         double df = int_der_sigmoid_psi->Sum();

#ifdef MFEM_USE_MPI
         if (isParallel)
         {
            MPI_Allreduce(MPI_IN_PLACE, &f, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
            MPI_Allreduce(MPI_IN_PLACE, &df, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
         }
#endif
         f -= target_volume;

         const double dc = -f/df;
         *psi += dc;
         if (abs(dc) < tol) { done = true; break; }
      }
      if (!done)
      {
         mfem_warning("Projection reached maximum iteration without converging. Result may not be accurate.");
      }
      int_sigmoid_psi->Assemble();
      current_volume = int_sigmoid_psi->Sum();
#ifdef MFEM_USE_MPI
      if (isParallel)
      {
         MPI_Allreduce(MPI_IN_PLACE, &current_volume, 1, MPI_DOUBLE, MPI_SUM,
                       MPI_COMM_WORLD);
      }
#endif

      return done;
   }


   /**
    * @brief Bregman projection of ρ = sigmoid(ψ) onto the subspace
    *        ∫_Ω ρ dx = θ vol(Ω) as follows:
    *
    *        1. Compute the root of the R → R function
    *            f(c) = ∫_Ω sigmoid(ψ + c) dx - θ vol(Ω)
    *        2. Set ψ ← ψ + c.
    *
    * @param psi a GridFunction to be updated
    * @param target_volume θ vol(Ω)
    * @param tol Newton iteration tolerance
    * @param max_its Newton maximum iteration number
    * @return double Final volume, ∫_Ω sigmoid(ψ)
    */
   bool proj_bisec(double tol=1e-12, int max_its=10)
   {
      double c = 0;
      MappedGridFunctionCoefficient rho(psi, [&c](const double x) {return sigmoid(x + c);});
      MappedGridFunctionCoefficient drho(psi, [&c](const double x) {return der_sigmoid(x + c);});
      LinearForm *V = newLinearForm(control_fes);
      LinearForm *dV = newLinearForm(control_fes);
      V->AddDomainIntegrator(new DomainLFIntegrator(rho));
      dV->AddDomainIntegrator(new DomainLFIntegrator(drho));
      V->Assemble();
      dV->Assemble();
      double Vc = V->Sum();
      double dVc = dV->Sum();
      if (fabs(Vc - target_volume) < tol)
      {
         current_volume = Vc;
         return true;
      }
      double dc = -(Vc - target_volume) / dVc;
      c += dc;
      int k;
      // Find an interval (c, c+dc) that contains c⋆.
      for (k=0; k < max_its; k++)
      {
         double Vc_old = Vc;
         V->Assemble();
         Vc = V->Sum();
         if ((Vc_old - target_volume)*(Vc - target_volume) < 0)
         {
            break;
         }
         c += dc;
      }
      if (k == max_its)
      {
         return false;
      }
      dc = fabs(dc);
      while (fabs(dc) > 1e-08)
      {
         dc /= 2.0;
         c = Vc > target_volume ? c - dc : c + dc;
         V->Assemble();
         Vc = V->Sum();
      }
      *psi += c;
      c = 0;
      V->Assemble();
      current_volume = V->Sum();
      delete V;
      delete dV;
      return true;
   }



protected:
   GridFunction *u, *psi, *rho_filter, *w_filter, *grad;
   MappedGridFunctionCoefficient *rho;
   GridFunctionCoefficient *w_cf;
   ConstantCoefficient lambda_cf, mu_cf;
   FiniteElementSpace *state_fes, *control_fes, *filter_fes;
#ifdef MFEM_USE_MPI
   ParFiniteElementSpace *par_state_fes, *par_control_fes, *par_filter_fes;
#endif
   ConstantCoefficient eps_squared, one, zero;
   VectorCoefficient &v_force;
   LinearElasticitySolver *elasticitySolver;
   DiffusionSolver *filterSolver;
   BilinearForm *invmass;
   LinearForm *w_rhs;
   std::function<double(const double)> simp_fun;
   MappedGridFunctionCoefficient simp_cf;
   ProductCoefficient lambda_SIMP_cf, mu_SIMP_cf;
   StrainEnergyDensityCoefficient negDerSimpElasticityEnergy;
   const double c1;
   const double c2;
   double alpha0;
   const double target_volume;
private:
   bool isParallel;
   bool grad_evaluated;
   double current_compliance, current_volume;
   GridFunction *newGridFunction(FiniteElementSpace *fes)
   {
      GridFunction *x;
      if (isParallel)
      {
#ifdef MFEM_USE_MPI
         ParFiniteElementSpace *pfes = dynamic_cast<ParFiniteElementSpace*>(fes);
         x = new ParGridFunction(pfes);
#endif
      }
      {
         x = new GridFunction(fes);
      }
      return x;
   }
   LinearForm *newLinearForm(FiniteElementSpace *fes)
   {
      LinearForm *b;
      if (isParallel)
      {
#ifdef MFEM_USE_MPI
         ParFiniteElementSpace *pfes = dynamic_cast<ParFiniteElementSpace*>(fes);
         b = new ParLinearForm(pfes);
#endif
      }
      {
         b = new LinearForm(fes);
      }
      return b;
   }
   BilinearForm *newBilinearForm(FiniteElementSpace *fes)
   {
      BilinearForm *a;
      if (isParallel)
      {
#ifdef MFEM_USE_MPI
         ParFiniteElementSpace *pfes = dynamic_cast<ParFiniteElementSpace*>(fes);
         a = new ParBilinearForm(pfes);
#endif
      }
      {
         a = new BilinearForm(fes);
      }
      return a;
   }
};

class AndersonAccelerator
{
public:
   AndersonAccelerator(const int s, const int m):
      m(m), k(0),
      Rk(s, m), Fk(s, m), N(m),
      svd(N, 'A', 'A'), alpha(m), one(m),
      DUt1(m), svd_tol_inv(1e04)
   {
      Fk = 0.0;
      Rk = 0.0;
      one = 1.0;
   }
   virtual void Step(const Vector &x, Vector &y)
   {
      Fk.GetColumnReference(k % m, fk);
      Rk.GetColumnReference(k % m, rk);
      fk = y;
      rk = y;
      rk -= x;
      // Solve Normal Equation, (RᵀR)α  = 1
      // Using SVD
      if (k >= m)
      {
         MultAtB(Rk, Rk, N);
         N.Inverse()->Mult(one, alpha);
      }
      else
      {
         if (k == 0)
         {
            return;
         }
         DenseMatrix subRk;
         alpha = 0.0;
         Vector smallone(one, 0, k), smallalpha(alpha, 0, k);

         Rk.GetSubMatrix(0, x.Size(), 0, k, subRk);
         MultAtB(subRk, subRk, N);
         N.Inverse()->Mult(smallone, smallalpha);
      }
      // svd.Eval(N);
      // DenseMatrix &U = svd.LeftSingularvectors();
      // DenseMatrix &Vt = svd.RightSingularvectors();
      // Vector &D = svd.Singularvalues();
      // D.Reciprocal();
      // for (auto &d : D) { d = d * (d < D[0]*svd_tol_inv); }
      // U.MultTranspose(one, DUt1);
      // DUt1 *= D;
      // Vt.MultTranspose(DUt1, alpha);
      alpha *= 1.0 / alpha.Sum();
      Fk.Mult(alpha, y);
      k++;
   };

   void SetSVDtol(const double new_svd_tol)
   {
      svd_tol_inv = 1.0 / new_svd_tol;
   }

protected:
   int m; // truncation
   int k; // iteration count
   mutable DenseMatrix Rk, Fk, N;
   mutable DenseMatrixSVD svd;
   mutable Vector rk, fk, alpha, DUt1;
   Vector one;
   double svd_tol_inv;

private:
};
} // end of namespace mfem
