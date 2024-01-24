#pragma once
#include "mfem.hpp"
#include <functional>


namespace mfem
{
/// @brief Inverse sigmoid function
double inv_sigmoid(const double x);

/// @brief Sigmoid function
double sigmoid(const double x);

/// @brief Derivative of sigmoid function
double der_sigmoid(const double x);

/// @brief SIMP function, ρ₀ + (ρ̄ - ρ₀)*x^k
double simp(const double x, const double rho_0=1e-06, const double k=3.0,
            const double rho_max=1.0);

/// @brief Derivative of SIMP function, k*(ρ̄ - ρ₀)*x^(k-1)
double der_simp(const double x, const double rho_0=1e-06,
                const double k=3.0, const double rho_max=1.0);
// Elasticity solver
class EllipticSolver
{
protected:
   BilinearForm &a; // LHS
   LinearForm &b; // RHS
   Array2D<int> ess_bdr; // Component-wise essential boundary marker
   Array<int> ess_tdof_list;
   bool symmetric;
#ifdef MFEM_USE_MPI
   bool parallel; // Flag for ParFiniteElementSpace
   MPI_Comm comm;
#endif
public:
   /// @brief Linear solver for elliptic problem with given Essential BC
   /// @param a Bilinear Form
   /// @param b Linear Form
   /// @param ess_bdr_list Essential boundary marker for boundary attributes
   EllipticSolver(BilinearForm &a, LinearForm &b, Array<int> &ess_bdr_);
   /// @brief Linear solver for elliptic problem with given component-wise essential BC
   /// ess_bdr[0,:] - All components, ess_bdr[i,:] - ith-direction
   /// @param a Bilinear Form
   /// @param b Linear Form
   /// @param ess_bdr_list Component-wise essential boundary marker for boundary attributes
   EllipticSolver(BilinearForm &a, LinearForm &b, Array2D<int> &ess_bdr);

   /// @brief Solve linear system and return FEM solution in x.
   /// @param x FEM solution
   /// @param A_assembled If true, skip assembly of LHS (bilinearform)
   /// @param b_Assembled If true, skip assembly of RHS (linearform)
   /// @return convergence flag
   bool Solve(GridFunction &x, bool A_assembled=false, bool b_Assembled=false);
   bool SolveTranspose(GridFunction &x, LinearForm *f, bool A_assembled=false,
                       bool b_Assembled=false);

   bool isParallel() { return parallel; }
   bool isSymmetric() { return symmetric; }
protected:
   /// @brief Get true dofs related to the boundaries in @ess_bdr
   /// @return True dof list
   void GetEssentialTrueDofs();
private:
};

class DensityFilter
{
public:
protected:
private:

public:
   DensityFilter() = default;
   virtual void Apply(const GridFunction &rho, GridFunction &frho) const = 0;
   virtual void Apply(Coefficient &rho, GridFunction &frho) const = 0;
protected:
private:
};

class HelmholtzFilter : public DensityFilter
{
public:
protected:
   FiniteElementSpace &fes;
   std::unique_ptr<BilinearForm> filter;
   Array<int> &ess_bdr;
   ConstantCoefficient eps2;
private:

public:
   HelmholtzFilter(FiniteElementSpace &fes, const double eps, Array<int> &ess_bdr);
   void Apply(const GridFunction &rho, GridFunction &frho) const override
   {
      GridFunctionCoefficient rho_cf(&rho);
      Apply(rho_cf, frho);
   }
   void Apply(Coefficient &rho, GridFunction &frho) const override;
protected:
private:
};

class DesignDensity
{
   // variables
public:
protected:
   std::unique_ptr<GridFunction> x_gf;
   std::unique_ptr<GridFunction> frho;
   std::unique_ptr<Coefficient> rho_cf;
   DensityFilter &filter;
   double target_volume_fraction;
   double target_volume;
   double current_volume;
   double domain_volume;
   double vol_tol;
private:

   // functions
public:
   DesignDensity(FiniteElementSpace &fes, DensityFilter &filter,
                 FiniteElementSpace &filter_fes, double vol_frac,
                 double volume_tolerance=1e-09);
   FiniteElementSpace *FESpace() {return x_gf->FESpace(); }
   FiniteElementSpace *FESpace_filter() {return frho->FESpace(); }
   double GetVolume() { return current_volume; }
   double GetDomainVolume() { return domain_volume; }
   GridFunction &GetGridFunction() { return *x_gf; }
   Coefficient &GetDensityCoefficient() { return *rho_cf; }
   GridFunction &GetFilteredDensity() { return *frho; }
   void UpdateFilteredDensity()
   {
      filter.Apply(*rho_cf, *frho);
   }
   DensityFilter &GetFilter() { return filter; }

   virtual void Project() = 0;
   virtual double StationarityError(GridFunction &grad) = 0;
   virtual double ComputeVolume() = 0;
   virtual std::unique_ptr<Coefficient> GetDensityDiffCoeff(
      GridFunction &other_gf) = 0;
protected:
private:
};

class DensityProjector
{
public:
   virtual Coefficient &GetPhysicalDensity(GridFunction &frho) = 0;
   virtual Coefficient &GetDerivative(GridFunction &frho) = 0;
};
class SIMPProjector : public DensityProjector
{
private:
   const double k, rho0;
   std::unique_ptr<MappedGridFunctionCoefficient> phys_density, dphys_dfrho;
public:
   SIMPProjector(const double k, const double rho0);
   Coefficient &GetPhysicalDensity(GridFunction &frho) override;
   Coefficient &GetDerivative(GridFunction &frho) override;
};

class ThresholdProjector : public DensityProjector
{
private:
   const double beta, eta;
   std::unique_ptr<MappedGridFunctionCoefficient> phys_density, dphys_dfrho;
public:
   ThresholdProjector(const double beta, const double eta);
   Coefficient &GetPhysicalDensity(GridFunction &frho) override;
   Coefficient &GetDerivative(GridFunction &frho) override;
};

class SigmoidDesignDensity : public DesignDensity
{
   // variables
public:
protected:
private:
   std::unique_ptr<GridFunction> zero_gf;

   // functions
public:
   SigmoidDesignDensity(FiniteElementSpace &fes, DensityFilter &filter,
                       FiniteElementSpace &fes_filter,
                       double vol_frac);
   void Project() override;
   double StationarityError(GridFunction &grad) override
   {
      return StationarityError(grad, false);
   };
   double StationarityError(GridFunction &grad, bool useL2norm);
   double StationarityErrorL2(GridFunction &grad);
   double ComputeBregmanDivergence(GridFunction *p, GridFunction *q,
                                   double log_tol=1e-13);
   double ComputeVolume() override
   {
      current_volume = zero_gf->ComputeL1Error(*rho_cf);
      return current_volume;
   }
   std::unique_ptr<Coefficient> GetDensityDiffCoeff(GridFunction &other_gf) override
   {
      return std::unique_ptr<Coefficient>(new MappedPairGridFunctionCoeffitient(
                                             x_gf.get(), &other_gf,
      [](double x, double y) {return sigmoid(x) - sigmoid(y);}));
   }
protected:
private:
};

class ExponentialDesignDensity : public DesignDensity
{
   // variables
public:
protected:
private:
   std::unique_ptr<GridFunction> zero_gf;

   // functions
public:
   ExponentialDesignDensity(FiniteElementSpace &fes, DensityFilter &filter,
                       FiniteElementSpace &fes_filter,
                       double vol_frac);
   void Project() override;
   double StationarityError(GridFunction &grad) override
   {
      return StationarityError(grad, false);
   };
   double StationarityError(GridFunction &grad, bool useL2norm);
   double StationarityErrorL2(GridFunction &grad);
   double ComputeBregmanDivergence(GridFunction *p, GridFunction *q,
                                   double log_tol=1e-13);
   double ComputeVolume() override
   {
      current_volume = zero_gf->ComputeL1Error(*rho_cf);
      return current_volume;
   }
   std::unique_ptr<Coefficient> GetDensityDiffCoeff(GridFunction &other_gf) override
   {
      return std::unique_ptr<Coefficient>(new MappedPairGridFunctionCoeffitient(
                                             x_gf.get(), &other_gf,
      [](double x, double y) {return std::exp(x) - std::exp(y);}));
   }
protected:
private:
};

class PrimalDesignDensity : public DesignDensity
{
   // variables
public:
protected:
private:
   ConstantCoefficient zero_cf;

   // functions
public:
   PrimalDesignDensity(FiniteElementSpace &fes, DensityFilter& filter,
                       FiniteElementSpace &fes_filter,
                       double vol_frac):
      DesignDensity(fes, filter, fes_filter, vol_frac), zero_cf(0.0)
   {
      rho_cf.reset(new GridFunctionCoefficient(x_gf.get()));
   }
   void Project() override;
   double StationarityError(GridFunction &grad) override;
   double ComputeVolume() override
   {
      current_volume = x_gf->ComputeL1Error(zero_cf);
      return current_volume;
   }
   std::unique_ptr<Coefficient> GetDensityDiffCoeff(GridFunction &other_gf) override
   {
      return std::unique_ptr<Coefficient>(new MappedPairGridFunctionCoeffitient(
                                             x_gf.get(), &other_gf,
      [](double x, double y) {return x - y;}));
   }
protected:
private:
};

class ParametrizedLinearEquation
{
public:
protected:
   std::unique_ptr<BilinearForm> a;
   std::unique_ptr<LinearForm> b;
   GridFunction &frho;
   DensityProjector &projector;
   bool AisStationary;
   bool BisStationary;
   Array2D<int> &ess_bdr;
private:


public:
   ParametrizedLinearEquation(FiniteElementSpace &fes,
                              GridFunction &filtered_density,
                              DensityProjector &projector, Array2D<int> &ess_bdr);
   void SetBilinearFormStationary(bool isStationary=true);
   void SetLinearFormStationary(bool isStationary=true);
   void Solve(GridFunction &x);

   /// @brief Solve a(λ, v)=F(v) assuming a is symmetric.
   /// @param
   /// @param u
   /// @return
   void DualSolve(GridFunction &x, LinearForm &new_b);
   virtual std::unique_ptr<Coefficient> GetdEdfrho(GridFunction &u,
                                                   GridFunction &lambda, GridFunction &frho) = 0;
   FiniteElementSpace *FESpace() { return a->FESpace(); }
   LinearForm &GetLinearForm() {return *b;}
protected:
   virtual void SolveSystem(GridFunction &x) = 0;
private:
};

class TopOptProblem
{
public:
protected:
   LinearForm &obj;
   ParametrizedLinearEquation &state_equation;
   DesignDensity &density;
   std::shared_ptr<GridFunction> gradF;
   std::shared_ptr<GridFunction> gradF_filter;
   std::shared_ptr<GridFunction> state, dual_solution;
   std::unique_ptr<BilinearForm> filter_to_density;
   std::unique_ptr<LinearForm> gradF_filter_form;
   std::unique_ptr<Coefficient> dEdfrho;
   const bool solve_dual;
   const bool apply_projection;
   double val;
private:
#ifdef MFEM_USE_MPI
   bool parallel;
   MPI_Comm comm;
#endif

public:

   /// @brief Create Topology optimization problem
   /// @param objective Objective linear functional, F(u)
   /// @param state_equation State equation, a(u,v) = b(v)
   /// @param density Density object, ρ
   /// @param solve_dual If true, kip dual solve, a(v,λ)=F(v) and assume λ=u
   /// @note It assume that the state equation is symmetric and objective
   TopOptProblem(LinearForm &objective,
                 ParametrizedLinearEquation &state_equation,
                 DesignDensity &density, bool solve_dual, bool apply_projection);

   double Eval();
   double GetValue() {return val;}
   void UpdateGradient();
   GridFunction &GetGradient() { return *gradF; }
   GridFunction &GetGridFunction() { return density.GetGridFunction(); }
   Coefficient &GetDensity() { return density.GetDensityCoefficient(); }
   // ρ - ρ_other where ρ_other is the provided density.
   // Assume ρ is constructed by the same mapping.
   // @note If you need different mapping between two grid functions,
   //       use GetDensity().
   std::unique_ptr<Coefficient> GetDensityDiffCoeff(GridFunction &other_gf)
   {
      return density.GetDensityDiffCoeff(other_gf);
   }
   GridFunction &GetState() {return *state;}
protected:
private:
};


/// @brief Strain energy density coefficient
class StrainEnergyDensityCoefficient : public Coefficient
{
protected:
   Coefficient &lambda;
   Coefficient &mu;
   GridFunction &u1; // displacement
   GridFunction &u2; // dual-displacement
   Coefficient &dphys_dfrho;
   DenseMatrix grad1, grad2; // auxiliary matrix, used in Eval

public:
   StrainEnergyDensityCoefficient(Coefficient &lambda, Coefficient &mu,
                                  GridFunction &u, DensityProjector &projector, GridFunction &frho)
      : lambda(lambda), mu(mu),  u1(u),  u2(u),
        dphys_dfrho(projector.GetDerivative(frho))
   { }
   StrainEnergyDensityCoefficient(Coefficient &lambda, Coefficient &mu,
                                  GridFunction &u1, GridFunction &u2, DensityProjector &projector,
                                  GridFunction &frho)
      : lambda(lambda), mu(mu),  u1(u1),  u2(u2),
        dphys_dfrho(projector.GetDerivative(frho))
   { }

   double Eval(ElementTransformation &T, const IntegrationPoint &ip) override;
};

class ParametrizedElasticityEquation : public ParametrizedLinearEquation
{
public:
protected:
   Coefficient &lambda;
   Coefficient &mu;
   GridFunction &filtered_density;
   ProductCoefficient phys_lambda;
   ProductCoefficient phys_mu;
   VectorCoefficient &f;
private:

public:
   ParametrizedElasticityEquation(FiniteElementSpace &fes,
                                  GridFunction &filtered_density,
                                  DensityProjector &projector,
                                  Coefficient &lambda, Coefficient &mu,
                                  VectorCoefficient &f, Array2D<int> &ess_bdr);
   std::unique_ptr<Coefficient> GetdEdfrho(GridFunction &u,
                                           GridFunction &dual_solution, GridFunction &frho) override
   { return std::unique_ptr<Coefficient>(new StrainEnergyDensityCoefficient(lambda, mu, u, dual_solution, projector, frho)); }
protected:
   void SolveSystem(GridFunction &x) override
   {
      EllipticSolver solver(*a, *b, ess_bdr);
      bool converged = solver.Solve(x, AisStationary, BisStationary);
      if (!converged)
      {
#ifdef MFEM_USE_MPI
         if (!Mpi::IsInitialized() || Mpi::Root())
         {
            out << "ParametrizedElasticityEquation::SolveSystem Failed to Converge." <<
                std::endl;
         }
#else
         out << "ParametrizedElasticityEquation::SolveSystem Failed to Converge." <<
             std::endl;
#endif
      }
   }
private:
};


/// @brief Strain energy density coefficient
class ThermalEnergyDensityCoefficient : public Coefficient
{
protected:
   Coefficient &kappa;
   GridFunction &u1; // displacement
   GridFunction &u2; // displacement
   Coefficient &dphys_dfrho;
   Vector grad1, grad2; // auxiliary matrix, used in Eval

public:
   ThermalEnergyDensityCoefficient(Coefficient &kappa,
                                   GridFunction &u, DensityProjector &projector, GridFunction &frho)
      : kappa(kappa),  u1(u),  u2(u),
        dphys_dfrho(projector.GetDerivative(frho))
   { }
   ThermalEnergyDensityCoefficient(Coefficient &kappa,
                                   GridFunction &u1, GridFunction &u2, DensityProjector &projector,
                                   GridFunction &frho)
      : kappa(kappa),  u1(u1),  u2(u2),
        dphys_dfrho(projector.GetDerivative(frho))
   { }

   double Eval(ElementTransformation &T, const IntegrationPoint &ip) override;
};

class ParametrizedDiffusionEquation : public ParametrizedLinearEquation
{
public:
protected:
   Coefficient &kappa;
   GridFunction &filtered_density;
   ProductCoefficient phys_kappa;
   Coefficient &f;
private:

public:
   ParametrizedDiffusionEquation(FiniteElementSpace &fes,
                                 GridFunction &filtered_density,
                                 DensityProjector &projector,
                                 Coefficient &kappa,
                                 Coefficient &f, Array2D<int> &ess_bdr);
   std::unique_ptr<Coefficient> GetdEdfrho(GridFunction &u,
                                           GridFunction &dual_solution, GridFunction &frho) override
   { return std::unique_ptr<Coefficient>(new ThermalEnergyDensityCoefficient(kappa, u, dual_solution, projector, frho)); }
protected:
   void SolveSystem(GridFunction &x) override
   {
      EllipticSolver solver(*a, *b, ess_bdr);
      bool converged = solver.Solve(x, AisStationary, BisStationary);
      if (!converged)
      {
#ifdef MFEM_USE_MPI
         if (!Mpi::IsInitialized() || Mpi::Root())
         {
            out << "ParametrizedDiffusionEquation::SolveSystem Failed to Converge." <<
                std::endl;
         }
#else
         out << "ParametrizedDiffusionEquation::SolveSystem Failed to Converge." <<
             std::endl;
#endif
      }
   }
private:
};



/// @brief Find step size α satisfies F(ρ(α)) ≤ F(ρ_0) - c_1 (∇F, ρ(α) - ρ_0) where ρ(α) = P(ρ_0 - α d)
///        where P is a projection, d is the negative search direction.
///
///        We assume, 1) problem is already evaluated at the current point, ρ_0.
///                   2) projection is, if any, performed by problem.Eval()
///        The resulting density, ρ(α) and the function value will be updated in problem.
/// @param problem Topology optimization problem
/// @param x0 Current density gridfunction
/// @param direction Negative search direction
/// @param diff_densityForm Linear from L(v) = (x - x0, v)
/// @param c1 Weights on the directional derivative,
/// @param step_size Step size. Use reference to monitor updated step size.
/// @param max_it Maximum number of updates.
/// @param shrink_factor < 1. Step size will be updated to α <- α * shrink_factor
/// @return The number of re-evaluation during Armijo condition check.
int Step_Armijo(TopOptProblem &problem, const GridFunction &x0, const GridFunction &direction,
                LinearForm &diff_densityForm, const double c1,
                double &step_size, const int max_it=20, const double shrink_factor=0.5);

/// @brief Volumetric force for linear elasticity
class VolumeForceCoefficient : public VectorCoefficient
{
private:
   double r2;
   Vector &center;
   Vector &force;
public:
   VolumeForceCoefficient(double r_,Vector &  center_, Vector & force_);

   void Eval(Vector &V, ElementTransformation &T,
             const IntegrationPoint &ip) override;

   void Set(double r_,Vector & center_, Vector & force_);
   void UpdateSize();
};

/// @brief Volumetric force for linear elasticity
class LineVolumeForceCoefficient : public VectorCoefficient
{
private:
   double r2;
   Vector &center;
   Vector &force;
   int direction_dim;
public:
   LineVolumeForceCoefficient(double r_, Vector &center_, Vector & force_,
                              int direction_dim);

   void Eval(Vector &V, ElementTransformation &T,
             const IntegrationPoint &ip) override;

   void Set(double r_,Vector & center_, Vector & force_);
   void UpdateSize();
};
}