#include "mfem.hpp"
#include "logger.hpp"
#include "funs.hpp"
#include "linear_solver.hpp"
#include "topopt.hpp"

using namespace mfem;

enum PenaltyType
{
   NONE=0,
   ENTROPY=1, // -varphi(rho)
   QUADRATIC=2, // rho(1-rho)
};
enum ObjectiveType
{
   L2PROJ=0,
   COMPLIANCE=1,
};

class ErrorCoefficient : public Coefficient
{
   GridFunctionCoefficient gf_cf;
   std::function<real_t(const Vector &x)> fun;
   mutable Vector x;

public:
   ErrorCoefficient(GridFunction &gf,
                    std::function<real_t(const Vector &x)> fun):
      gf_cf(&gf), fun(fun)
   {
      x.SetSize(gf.FESpace()->GetMesh()->SpaceDimension());
   }

   real_t Eval(ElementTransformation &T,
               const IntegrationPoint &ip) override
   {
      T.Transform(ip, x);
      return sigmoid(gf_cf.Eval(T, ip)) - fun(x);
   };
};

HypreParMatrix *reassemble(ParBilinearForm &op)
{
   SparseMatrix *S = op.LoseMat();
   if (S) { delete S; }
   op.Assemble();
   op.Finalize();
   return op.ParallelAssemble();
}

void reassemble(ParLinearForm &b, BlockVector &b_v, BlockVector &b_tv,
                const int block_id)
{
   b.Assemble();
   b.SyncAliasMemory(b_v);
   b.ParallelAssemble(b_tv.GetBlock(block_id));
   b_tv.GetBlock(block_id).SyncAliasMemory(b_tv);
}

void MPISequential(std::function<void(int)> f)
{
   for (int i=0; i<Mpi::WorldSize(); i++)
   {
      if (i == Mpi::WorldRank())
      {
         f(i);
      }
      MPI_Barrier(MPI_COMM_WORLD);
   }
}


HypreParMatrix * LinearFormToSparseMatrix(ParLinearForm &lf)
{
   ParFiniteElementSpace *pfes = lf.ParFESpace();
   // std::unique_ptr<int> i;
   // std::unique_ptr<HYPRE_BigInt> j;
   // std::unique_ptr<real_t> *d;
   Array<int> i; Array<HYPRE_BigInt> j; Vector d;
   Array<HYPRE_BigInt> cols;
   int local_siz = pfes->TrueVSize();
   HYPRE_BigInt global_siz = pfes->GlobalTrueVSize();
   int current;
   i.SetSize(local_siz+1); std::iota(i.begin(), i.end(), 0);
   j.SetSize(local_siz);
   if (HYPRE_AssumedPartitionCheck())
   {
      std::fill(j.begin(), j.end(), 0);
      cols.SetSize(2);
      cols[0] = Mpi::Root() ? 0 : 1;
      cols[1] = 1;
   }
   else
   {
      std::fill(j.begin(), j.end(), 0);
      MFEM_ABORT("Not yet implemented");
      // NOTE: Not sure how to construct since one column is shared with other processors.
      // Doesn't make sense to define increasing cols.
      cols.SetSize(Mpi::WorldSize() + 1);
      cols = 0;
      cols[Mpi::WorldRank()+1] = Mpi::Root() ? 0 :1;
      MPI_Allreduce(MPI_IN_PLACE, cols.GetData(), Mpi::WorldSize() + 1, MPI_INT,
                    MPI_SUM, pfes->GetComm());
      cols.PartialSum();
   }
   d.SetSize(local_siz);

   lf.Assemble(); lf.ParallelAssemble(d);

   // return nullptr;
   return new HypreParMatrix(pfes->GetComm(),
                             local_siz, global_siz, 1,
                             i.GetData(), j.GetData(), d.GetData(),
                             pfes->GetTrueDofOffsets(), cols.GetData());
}

class HellingerDerivativeMatrixCoefficient : public MatrixCoefficient
{
   // attributes
private:
   VectorGridFunctionCoefficient latent_gf;
   Coefficient *r_min;
   bool own_rmin;
   Vector latent_val;
protected:
public:
   // methods
private:
protected:
public:
   HellingerDerivativeMatrixCoefficient(const GridFunction * latent_gf,
                                        const real_t r_min)
      :MatrixCoefficient(latent_gf->VectorDim()), latent_gf(latent_gf),
       r_min(new ConstantCoefficient(r_min)), own_rmin(true),
       latent_val(latent_gf->VectorDim()) { }
   HellingerDerivativeMatrixCoefficient(const GridFunction * latent_gf,
                                        Coefficient &r_min)
      :MatrixCoefficient(latent_gf->VectorDim()), latent_gf(latent_gf),
       r_min(&r_min),
       own_rmin(false), latent_val(latent_gf->VectorDim()) { }
   ~HellingerDerivativeMatrixCoefficient() {if (own_rmin) {delete r_min;}}
   void SetLengthScale(const real_t new_rmin)
   {
      if (own_rmin)
      {
         auto *r_min_const = dynamic_cast<ConstantCoefficient*>(r_min);
         if (r_min_const)
         {
            r_min_const->constant = new_rmin;
            return;
         }
         else
         {
            delete r_min;
         }
      }
      r_min = new ConstantCoefficient(new_rmin);
      own_rmin = true;
   }
   void SetLengthScale(Coefficient &new_rmin)
   {
      if (own_rmin) {delete r_min;}
      r_min = &new_rmin;
      own_rmin = false;
   }

   void Eval(DenseMatrix &K, ElementTransformation &T,
             const IntegrationPoint &ip) override
   {
      latent_gf.Eval(latent_val, T, ip);
      const real_t rmin = r_min->Eval(T, ip);
      const real_t rmin2 = std::pow(rmin, 2.0);
      const real_t norm2 = latent_val*latent_val;
      K.Diag(1.0 / std::sqrt(norm2+rmin2), latent_val.Size());
      AddMult_a_VVt(-std::pow(norm2 + rmin2, -1.5), latent_val, K);
      K *= 1.0 / rmin;
   }
};

class HellingerLatent2PrimalCoefficient : public VectorCoefficient
{
   // attributes
private:
   VectorGridFunctionCoefficient latent_gf;
   Coefficient *r_min;
   bool own_rmin;
   std::unique_ptr<HellingerDerivativeMatrixCoefficient> der;
protected:
public:
   // methods
private:
protected:
public:
   HellingerLatent2PrimalCoefficient(const GridFunction *latent_gf,
                                     const real_t r_min)
      :VectorCoefficient(latent_gf->VectorDim()), latent_gf(latent_gf),
       r_min(new ConstantCoefficient(r_min)), own_rmin(true) {}
   HellingerLatent2PrimalCoefficient(const GridFunction *latent_gf,
                                     Coefficient &r_min)
      :VectorCoefficient(latent_gf->VectorDim()), latent_gf(latent_gf),
       r_min(&r_min), own_rmin(false) {}
   ~HellingerLatent2PrimalCoefficient()
   {
      if (own_rmin) {delete r_min;}
   }
   void SetLengthScale(const real_t new_rmin)
   {
      if (own_rmin)
      {
         auto *r_min_const = dynamic_cast<ConstantCoefficient*>(r_min);
         if (r_min_const)
         {
            r_min_const->constant = new_rmin;
            return;
         }
         else
         {
            delete r_min;
         }
      }
      r_min = new ConstantCoefficient(new_rmin);
      own_rmin = true;
      if (der)
      {
         der->SetLengthScale(*r_min);
      }
   }
   void SetLengthScale(Coefficient &new_rmin)
   {
      if (own_rmin) {delete r_min;}
      r_min = &new_rmin;
      own_rmin = false;
      if (der)
      {
         der->SetLengthScale(*r_min);
      }
   }

   void Eval(Vector &V, ElementTransformation &T,
             const IntegrationPoint &ip) override
   {
      V.SetSize(vdim);
      latent_gf.Eval(V, T, ip);
      const real_t rmin = r_min->Eval(T, ip);
      const real_t norm2 = V*V;
      V *= 1.0 / (rmin*std::sqrt((norm2+rmin*rmin)));
   }

   HellingerDerivativeMatrixCoefficient &GetDerivative()
   {
      if (!der)
      {
         der.reset(new HellingerDerivativeMatrixCoefficient(
                      latent_gf.GetGridFunction(), *r_min));
      }
      return *der;
   }
};

class FermiDiracDerivativeVectorCoefficient : public Coefficient
{
   // attributes
private:
   GridFunctionCoefficient latent_gf;
   Coefficient *minval;
   Coefficient *maxval;
   bool own_minmax;
protected:
public:
   // methods
private:
protected:
public:
   FermiDiracDerivativeVectorCoefficient(const GridFunction *latent_gf,
                                         const real_t minval=0,
                                         const real_t maxval=1)
      :Coefficient(), latent_gf(latent_gf),
       minval(new ConstantCoefficient(minval)),
       maxval(new ConstantCoefficient(maxval)), own_minmax(true) {}
   FermiDiracDerivativeVectorCoefficient(const GridFunction *latent_gf,
                                         Coefficient &minval,
                                         Coefficient &maxval)
      :Coefficient(), latent_gf(latent_gf),
       minval(&minval), maxval(&maxval), own_minmax(false) {}
   ~FermiDiracDerivativeVectorCoefficient() { if (own_minmax) {delete minval; delete maxval;} }

   void SetGridFunction(GridFunction *new_gf) { latent_gf = new_gf; }

   real_t Eval(ElementTransformation &T, const IntegrationPoint &ip) override
   {
      const real_t m = minval->Eval(T, T.GetIntPoint());
      const real_t M = maxval->Eval(T, T.GetIntPoint());
      const real_t val = sigmoid(latent_gf.Eval(T, ip));
      return (M - m)*(val*(1.0-val));
   }
};


class FermiDiracLatent2PrimalCoefficient : public Coefficient
{
   // attributes
private:
   GridFunctionCoefficient latent_gf;
   Coefficient *minval;
   Coefficient *maxval;
   bool own_minmax;
   std::unique_ptr<FermiDiracDerivativeVectorCoefficient> der;
protected:
public:
   // methods
private:
protected:
public:
   FermiDiracLatent2PrimalCoefficient(const GridFunction *gf,
                                      const real_t minval=0,
                                      const real_t maxval=1)
      :Coefficient(), latent_gf(gf),
       minval(new ConstantCoefficient(minval)),
       maxval(new ConstantCoefficient(maxval)), own_minmax(true) {}
   FermiDiracLatent2PrimalCoefficient(const GridFunction *gf, Coefficient &minval,
                                      Coefficient &maxval)
      :Coefficient(), latent_gf(gf),
       minval(&minval), maxval(&maxval), own_minmax(false) {}
   ~FermiDiracLatent2PrimalCoefficient()
   {
      if (own_minmax) {delete minval; delete maxval;}
   }
   void SetGridFunction(GridFunction *new_gf) { latent_gf = new_gf; }

   real_t Eval(ElementTransformation &T, const IntegrationPoint &ip) override
   {
      const real_t m = minval->Eval(T, T.GetIntPoint());
      const real_t M = maxval->Eval(T, T.GetIntPoint());
      const real_t val = sigmoid(latent_gf.Eval(T, ip));
      return (M - m)*val + m;
   }

   FermiDiracDerivativeVectorCoefficient &GetDerivative()
   {
      if (!der)
      {
         der.reset(new FermiDiracDerivativeVectorCoefficient(
                      latent_gf.GetGridFunction(), *minval, *maxval));
      }
      return *der;
   }
};


// class VolumeConstraintOperator : public Operator
// {
//    // attributes
// private:
//    Coefficient &rho_cf;
//    const real_t vol_frac;
//    std::unique_ptr<ParLinearForm> b;
// protected:
// public:
//    // methods
// private:
// protected:
// public:
//    VolumeConstraintOperator(const int n, Coefficient &rho_cf,
//                             ParFiniteElementSpace &pfes, const real_t vol_frac)
//       :Operator(1,n), rho_cf(rho_cf), vol_frac(vol_frac)
//    {
//       b.reset(new ParLinearForm(&pfes));
//       b->AddDomainIntegrator(new DomainLFIntegrator(rho_cf));
//    }
//    void Mult(const Vector &x, Vector &y) const override
//    {
//       b->Assemble();
//       y.SetSize(1);
//       real_t vol = b->Sum();
//       MPI_Allreduce(MPI_IN_PLACE, &vol, 1, MFEM_MPI_REAL_T, MPI_SUM,
//                     b->ParFESpace()->GetComm());
//       y = vol - vol_frac;
//    }
// };


int main(int argc, char *argv[])
{
   // 1. Initialize MPI and HYPRE.
   Mpi::Init(argc, argv);
   Hypre::Init();

   int ref_levels = 7;
   int order = 1;
   int dim = 2;
   real_t r_min = 0.05;
   real_t rho_min = 1e-06;
   real_t vol_frac = 0.5;
   real_t exponent = 3.0;

   int max_md_it = 300;
   int max_newton_it = 30;
   real_t tol_md = 1e-04;
   real_t tol_newt = 1e-08;

   real_t entropy_reg = 0.0;
   real_t h1_reg = 0.0;
   real_t entropy_penalty = 0.0;
   PenaltyType penalty_type = PenaltyType::NONE;
   ObjectiveType obj_type = ObjectiveType::COMPLIANCE;

   bool use_paraview = true;

   OptionsParser args(argc, argv);
   // FE-related options
   args.AddOption(&ref_levels, "-r", "--refine",
                  "The number of uniform mesh refinement");
   args.AddOption(&order, "-o", "--order",
                  "Finite element order (polynomial degree).");
   // problem dependent options
   args.AddOption(&r_min, "-l", "--length-scale",
                  "r_min for length scale");
   args.AddOption(&rho_min, "-rho0", "--rho-min",
                  "minimum density");
   args.AddOption(&vol_frac, "-theta", "--vol-frac",
                  "volume fraction");
   args.AddOption(&h1_reg, "-h1", "--h1-reg",
                  "H1 regularization constant");
   args.AddOption(&entropy_reg, "-ent", "--entropy-reg",
                  "Entropy regularization constant");

   args.AddOption(&max_md_it, "-mi", "--max-it",
                  "Maximum mirror descent iteration");
   args.AddOption(&max_md_it, "-mi-newt", "--max-it-newton",
                  "Maximum Newton iteration");

   // visualization related options
   args.AddOption(&use_paraview, "-pv", "--paraview", "-no-pv",
                  "--no-paraview",
                  "Enable or disable paraview export.");
   args.Parse();
   if (!args.Good())
   {
      if (Mpi::Root())
      {
         args.PrintUsage(out);
      }
      return 1;
   }

   Array<int> densityBC; // 0: flat; 1: solid; -1: void

   std::unique_ptr<ParMesh> pmesh;
   {
      int ref_serial = 0;
      while (std::pow(2.0, ref_serial*dim) < Mpi::WorldSize()) { ref_serial++; }
      ref_serial = std::min(ref_serial, ref_levels);
      int ref_parallel = ref_levels - ref_serial;
      Mesh mesh = Mesh::MakeCartesian2D(
                     std::pow(2, ref_serial)*3, std::pow(2, ref_serial),
                     Element::QUADRILATERAL, true, 3.0, 1.0);
      pmesh.reset(new ParMesh(MPI_COMM_WORLD, mesh));
      mesh.Clear();
      for (int i=0; i<ref_parallel; i++) {pmesh->UniformRefinement();}
   }
   // FunctionCoefficient rho_targ();
   densityBC.SetSize(pmesh->bdr_attributes.Max());
   densityBC=0;
   densityBC[0] = -1; // bottom
   densityBC[2] = -1; // top

   Array<int> essNeumannBC(densityBC);
   for (int &bdr : essNeumannBC) { bdr = (bdr == 0); }
   Array<int> voidBC(densityBC);
   for (int &bdr : voidBC) { bdr = (bdr == -1); }
   Array<int> materialBC(densityBC);
   for (int &bdr : materialBC) { bdr = (bdr == 1); }
   Array2D<int> essDispDiriBC(dim+1, densityBC.Size());
   int num_bdr_attr = 4;
   essDispDiriBC.SetSize(3, num_bdr_attr);
   essDispDiriBC = 0;
   essDispDiriBC(0,3) = 1;


   RT_FECollection RT_fec(order, dim);
   DG_FECollection DG_fec(order, dim);
   H1_FECollection CG_fec(order+1, dim);

   ParFiniteElementSpace RT_fes(pmesh.get(), &RT_fec);
   ParFiniteElementSpace DG_fes(pmesh.get(), &DG_fec);
   ParFiniteElementSpace CG_fes_vec(pmesh.get(), &CG_fec, dim);

   HYPRE_BigInt global_NE = pmesh->GetGlobalNE();
   HYPRE_BigInt global_RT_dof = RT_fes.GlobalTrueVSize();
   HYPRE_BigInt global_DG_dof = DG_fes.GlobalTrueVSize();
   HYPRE_BigInt global_CG_dof = CG_fes_vec.GlobalTrueVSize();
   if (Mpi::Root())
   {
      out << "Num Elements: " << global_NE << std::endl;
      out << "CG space dof: " << global_CG_dof << std::endl << std::endl;
      out << "RT space dof: " << global_RT_dof << std::endl;
      out << "DG space dof: " << global_DG_dof << std::endl;
      out << "   Total dof: " << global_RT_dof + global_DG_dof * 2 + 1 << std::endl;
   }

   Array<int> ess_neumann_bc_tdofs;
   RT_fes.GetEssentialTrueDofs(essNeumannBC, ess_neumann_bc_tdofs);
   ess_neumann_bc_tdofs.Sort();

   Array<int> offsets(5);
   offsets[0] = 0;
   offsets[1] = RT_fes.GetVSize();
   offsets[2] = DG_fes.GetVSize();
   offsets[3] = DG_fes.GetVSize();
   offsets[4] = 1;
   offsets.PartialSum();
   BlockVector x(offsets), b(offsets);
   BlockVector x_old(offsets);

   Array<int> true_offsets(5);
   true_offsets[0] = 0;
   true_offsets[1] = RT_fes.GetTrueVSize();
   true_offsets[2] = DG_fes.GetTrueVSize();
   true_offsets[3] = DG_fes.GetTrueVSize();
   true_offsets[4] = Mpi::Root() ? 1 : 0;
   true_offsets.PartialSum();
   BlockVector x_tv(true_offsets), b_tv(true_offsets), dummy(true_offsets);
   x_tv.GetBlock(0) = 0.0;                   // Constant solution -> Psi=0
   x_tv.GetBlock(1) = vol_frac;              // Initial design
   x_tv.GetBlock(2) = invsigmoid(vol_frac);  // initial latent
   b_tv = 0.0;

   Vector b_tv_reduced(b_tv.Size() - ess_neumann_bc_tdofs.Size());
   Vector x_tv_reduced(x_tv.Size() - ess_neumann_bc_tdofs.Size());

   ParGridFunction Psi, rho, psi, u(&CG_fes_vec);
   Psi.MakeRef(&RT_fes, x.GetBlock(0), 0);
   rho.MakeRef(&DG_fes, x.GetBlock(1), 0);
   psi.MakeRef(&DG_fes, x.GetBlock(2), 0);
   Psi.Distribute(&(x_tv.GetBlock(0)));
   rho.Distribute(&(x_tv.GetBlock(1)));
   psi.Distribute(&(x_tv.GetBlock(2)));
   u = 0.0;
   ParGridFunction Psi_k(&RT_fes), Psi_old(&RT_fes);
   ParGridFunction rho_k(&DG_fes), rho_old(&DG_fes);
   ParGridFunction psi_k(&DG_fes), psi_old(&DG_fes);
   ParGridFunction zero_gf(&DG_fes); zero_gf = 0.0;
   const real_t E = 1.0;
   const real_t nu = 0.3;
   const real_t lambda = E*nu/((1+nu)*(1-2*nu));
   const real_t mu = E/(2*(1+nu));
   ConstantCoefficient lambda_cf(lambda), mu_cf(mu);

   MappedGFCoefficient simp_cf(
      psi, [exponent, rho_min](const real_t x)
   {
      return simp(sigmoid(x), exponent, rho_min);
   });
   MappedGFCoefficient der_simp_cf(
      psi, [exponent, rho_min](const real_t x)
   {
      return der_simp(sigmoid(x), exponent, rho_min);
   });
   MappedGFCoefficient penalty_grad_cf;
   penalty_grad_cf.SetGridFunction(&psi);
   switch (penalty_type)
   {
      case NONE:
      {
         break;
      }
      case ENTROPY:
      {
         penalty_grad_cf.SetFunction([entropy_penalty](const real_t x)
         {
            return -entropy_penalty*x;
         });
         break;
      }
      case QUADRATIC:
      {
         penalty_grad_cf.SetFunction([entropy_penalty](const real_t x)
         {
            const real_t rho_val = sigmoid(x);
            return entropy_penalty*(1-2*rho_val);
         });
         break;
      }
      default:
         MFEM_ABORT("Undefined penalty type");
   }

   ProductCoefficient lambda_simp_cf(lambda_cf, simp_cf);
   ProductCoefficient mu_simp_cf(mu_cf, simp_cf);
   std::unique_ptr<Coefficient> grad_cf;
   switch (obj_type)
   {
      case L2PROJ:
      {
         grad_cf.reset(new ErrorCoefficient(
                          psi, [](const Vector &x)
         {
            real_t val = 0.0;
            val = val + (std::fabs(x[0] - 0.5) < 0.25 && std::fabs(x[1]-0.5) < 0.25);
            real_t r2_1 = std::pow(x[0]-1.5, 2.0)+std::pow(x[1]-0.5,2.0);
            if (r2_1 < std::pow(0.25,2.0))
            {
               val += std::sqrt(r2_1)/0.25*0.5 + 0.5;
            }
            real_t r2_2 = std::pow(x[0]-2.5, 2.0)+std::pow(x[1]-0.5,2.0);
            val += r2_2 < std::pow(0.25,2.0);
            return val;
         }));
         vol_frac = std::pow(0.5,2.0);                 // rectangle
         vol_frac += M_PI*(5.0/6.0)*std::pow(0.25, 2); // cylinder - cone
         vol_frac += M_PI*std::pow(0.25, 2);           // cylinder
         vol_frac /= 3.0;                              // divide by the domain size.
         x_tv.GetBlock(0) = 0.0;                   // Constant solution -> Psi=0
         x_tv.GetBlock(1) = vol_frac;              // Initial design
         x_tv.GetBlock(2) = invsigmoid(vol_frac);  // initial latent

         break;
      }
      case COMPLIANCE:
      {
         grad_cf.reset(new StrainEnergyDensityCoefficient(lambda_cf, mu_cf, der_simp_cf,
                                                          u));
         break;
      }
      default:
         MFEM_ABORT("Undefined objective type");
   }
   //

   ConstantCoefficient alpha_cf(1.0);
   // ProductCoefficient neg_alpha_cf(-1.0, alpha_cf);
   ConstantCoefficient one_cf(1.0);
   ConstantCoefficient neg_one_cf(-1.0);
   ConstantCoefficient zero_cf(0.0);
   Vector zero_vec_d(dim); zero_vec_d=0.0;
   VectorConstantCoefficient zero_vec_cf(zero_vec_d);
   ProductCoefficient alpha_grad_cf(alpha_cf, *grad_cf);
   ProductCoefficient alpha_penalty_grad_cf(alpha_cf, penalty_grad_cf);

   VectorGridFunctionCoefficient Psi_cf(&Psi);
   VectorGridFunctionCoefficient Psi_k_cf(&Psi_k);
   VectorGridFunctionCoefficient Psi_old_cf(&Psi_old);
   HellingerLatent2PrimalCoefficient mapped_grad_rho_cf(&Psi, r_min);
   HellingerLatent2PrimalCoefficient mapped_grad_rho_k_cf(&Psi_k, r_min);
   HellingerDerivativeMatrixCoefficient &DSigma_cf =
      mapped_grad_rho_cf.GetDerivative();
   MatrixVectorProductCoefficient DSigma_Psi_cf(DSigma_cf, Psi_cf);
   DivergenceGridFunctionCoefficient divPsi_k_cf(&Psi_k);
   ScalarVectorProductCoefficient neg_mapped_grad_rho_cf(-1.0, mapped_grad_rho_cf);

   GridFunctionCoefficient psi_cf(&psi);
   GridFunctionCoefficient psi_k_cf(&psi_k);
   GridFunctionCoefficient psi_old_cf(&psi_old);
   FermiDiracLatent2PrimalCoefficient mapped_rho_cf(&psi, 0.0, 1.0);
   FermiDiracLatent2PrimalCoefficient mapped_rho_k_cf(&psi_k, 0.0, 1.0);
   FermiDiracDerivativeVectorCoefficient &dsigma_cf =
      mapped_rho_cf.GetDerivative();
   ProductCoefficient dsigma_psi_cf(dsigma_cf, psi_cf);
   ProductCoefficient neg_mapped_rho_cf(-1.0, mapped_rho_cf);
   ProductCoefficient neg_psi_k_cf(-1.0, psi_k_cf);
   ParGridFunction mapped_rho_gf(rho);
   ParGridFunction rmin_gf(&DG_fes);
   rmin_gf = r_min;

   GridFunctionCoefficient rho_cf(&rho);
   GridFunctionCoefficient rho_old_cf(&rho_old);


   // Global block operator, matrices are not owned.
   Array2D<HypreParMatrix*> blockOp(4,4);
   blockOp = nullptr;
   std::unique_ptr<HypreParMatrix> glbMat;

   // <Sigma'(Psi), Xi>
   std::unique_ptr<HypreParMatrix> DSigmaM;
   ParBilinearForm DSigmaOp(&RT_fes);
   DSigmaOp.AddDomainIntegrator(new VectorFEMassIntegrator(DSigma_cf));

   // <sigma'(psi), xi>
   std::unique_ptr<HypreParMatrix> dsigmaM;
   ParBilinearForm dsigmaOp(&DG_fes);
   dsigmaOp.AddDomainIntegrator(new MassIntegrator(dsigma_cf));

   ConstantCoefficient h1_reg_cf(h1_reg);
   if (h1_reg)
   {
      dsigmaOp.AddDomainIntegrator(new DiffusionIntegrator(h1_reg_cf));
      dsigmaOp.AddInteriorFaceIntegrator(new DGDiffusionIntegrator(h1_reg_cf, -1.0,
                                                                   (order+1)*(order+1)));
   }

   // <div Psi, q>
   ParMixedBilinearForm divOp(&RT_fes, &DG_fes);
   divOp.AddDomainIntegrator(new VectorFEDivergenceIntegrator());
   divOp.Assemble();
   divOp.Finalize();
   std::unique_ptr<HypreParMatrix> D(divOp.ParallelAssemble());
   std::unique_ptr<HypreParMatrix> negG(D->Transpose());
   blockOp(1, 0) = D.get();
   blockOp(0, 1) = negG.get();

   // -<rho, xi>
   ParBilinearForm neg_MassOp(&DG_fes);
   neg_MassOp.AddDomainIntegrator(new MassIntegrator(neg_one_cf));
   neg_MassOp.Assemble();
   neg_MassOp.Finalize();
   std::unique_ptr<HypreParMatrix> negM(neg_MassOp.ParallelAssemble());
   blockOp(2, 1) = negM.get();
   blockOp(1, 2) = negM.get();

   // <rho, 1>
   ParLinearForm volform(&DG_fes);
   volform.AddDomainIntegrator(new DomainLFIntegrator(one_cf));
   std::unique_ptr<HypreParMatrix> M1(LinearFormToSparseMatrix(volform));
   std::unique_ptr<HypreParMatrix> M1T(M1->Transpose());
   M1T->Mult(x_tv.GetBlock(1), b_tv.GetBlock(3)); // compute current volume!
   blockOp(1,3) = M1.get();
   blockOp(3,1) = M1T.get();

   // Elasticity
   ElasticityProblem elasticity(CG_fes_vec, essDispDiriBC, lambda_simp_cf,
                                mu_simp_cf,
                                false);
   std::unique_ptr<VectorFunctionCoefficient> load(new VectorFunctionCoefficient(
                                                      2, [](const Vector &x, Vector &f)
   {
      f = 0.0;
      if (std::pow(x[0]-2.9, 2.0) + std::pow(x[1] - 0.5, 2.0) < 0.05*0.05)
      {
         f[1] = -1.0;
      }
   }));
   elasticity.GetParLinearForm()->AddDomainIntegrator(
      new VectorDomainLFIntegrator(*load));
   elasticity.SetAStationary(false);
   elasticity.SetBStationary(false);

   // Right hand side
   ParLinearForm first_order_optimality(&DG_fes, b.GetBlock(1).GetData());
   first_order_optimality.AddDomainIntegrator(
      new DomainLFIntegrator(divPsi_k_cf));
   first_order_optimality.AddDomainIntegrator(
      new DomainLFIntegrator(neg_psi_k_cf));
   first_order_optimality.AddDomainIntegrator(
      new DomainLFIntegrator(alpha_grad_cf));
   if (entropy_penalty && penalty_type != NONE)
   {
      first_order_optimality.AddDomainIntegrator(
         new DomainLFIntegrator(alpha_penalty_grad_cf));
   }

   ParLinearForm GradNewtResidual(&RT_fes, b.GetBlock(0).GetData());
   GradNewtResidual.AddDomainIntegrator(new VectorFEDomainLFIntegrator(
                                           neg_mapped_grad_rho_cf));
   GradNewtResidual.AddDomainIntegrator(new VectorFEDomainLFIntegrator(
                                           DSigma_Psi_cf));
   GradNewtResidual.AddBoundaryIntegrator(new VectorFEBoundaryFluxLFIntegrator(
                                             one_cf), materialBC);
   // voidBC -> not necessary because it is 0!

   ParLinearForm RhoNewtResidual(&DG_fes, b.GetBlock(2).GetData());
   RhoNewtResidual.AddDomainIntegrator(new DomainLFIntegrator(neg_mapped_rho_cf));
   RhoNewtResidual.AddDomainIntegrator(new DomainLFIntegrator(dsigma_psi_cf));


   // Visualization
   real_t alpha = 1.0;
   int it_md(1), it_newt(0);
   real_t res_md,res_newt, res_l2_linsolver;
   real_t curr_vol;
   real_t obj(0);
   real_t max_psi(0), max_Psi(0);
   TableLogger logger;
   logger.SaveWhenPrint("grad_proj_md");
   logger.Append("it_md", it_md);
   logger.Append("alpha", alpha);
   logger.Append("obj", obj);
   logger.Append("res_md", res_md);
   logger.Append("it_newt", it_newt);
   logger.Append("res_newt", res_newt);
   logger.Append("volume", curr_vol);
   logger.Append("max_psi", max_psi);
   logger.Append("max_Psi", max_Psi);
   logger.Append("solver_res", res_l2_linsolver);
   std::unique_ptr<ParaViewDataCollection> paraview_dc;
   if (use_paraview)
   {
      paraview_dc.reset(new mfem::ParaViewDataCollection("grad_proj_md",
                                                         pmesh.get()));
      if (paraview_dc->Error()) { use_paraview=false; }
      else
      {
         paraview_dc->SetPrefixPath("ParaView");
         paraview_dc->SetLevelsOfDetail(order + 3);
         paraview_dc->SetDataFormat(VTKFormat::BINARY);
         paraview_dc->SetHighOrderOutput(true);
         paraview_dc->RegisterField("density", &rho);
         paraview_dc->RegisterField("psi", &psi);
         paraview_dc->RegisterField("Psi", &Psi);
         paraview_dc->RegisterField("mapped_density", &mapped_rho_gf);
         paraview_dc->RegisterField("rmin", &rmin_gf);
         paraview_dc->RegisterField("u", &u);
         paraview_dc->SetTime(0);
         paraview_dc->SetCycle(0);
         paraview_dc->Save();
      }
   }


   for (; it_md<max_md_it; it_md++)
   {
      // TODO: Custom update rule for alpha
      alpha = std::pow((real_t)it_md, 1.0); // update alpha
      // alpha = 2; // update alpha


      alpha_cf.constant = alpha;

      // Store the previous
      Psi_k = Psi;
      rho_k = rho;
      psi_k = psi;

      if (obj_type == COMPLIANCE)
      {
         elasticity.Solve(u);
      }

      // // Update RHS of first-order optimality condition
      reassemble(first_order_optimality, b, b_tv, 1);
      if (entropy_reg)
      {
         b_tv.GetBlock(1) *= 1.0 / (1.0 + entropy_reg); // entropy regularization
      }

      it_newt = 0;
      for (; it_newt < max_newton_it; it_newt++)
      {
         // Previous newton iteration
         Psi_old = Psi;
         psi_old = psi;
         rho_old = rho;

         // Update nonlinear operators
         DSigmaM.reset(reassemble(DSigmaOp));
         dsigmaM.reset(reassemble(dsigmaOp));
         blockOp(0,0) = DSigmaM.get();
         blockOp(2,2) = dsigmaM.get();

         // Update Newton residual
         reassemble(GradNewtResidual, b, b_tv, 0);
         reassemble(RhoNewtResidual, b, b_tv, 2);
         b_tv.SetSubVector(ess_neumann_bc_tdofs, 0.0);

         // Global matrix
         glbMat.reset(HypreParMatrixFromBlocks(blockOp));
         // Boundary condition, ess_bdr for RT space, grad rho dot n = 0
         glbMat->EliminateBC(ess_neumann_bc_tdofs, Operator::DIAG_ONE);

         // MUMPS solver
         MUMPSSolver mumps(MPI_COMM_WORLD);
         mumps.SetPrintLevel(0);
         mumps.SetMatrixSymType(MUMPSSolver::MatType::SYMMETRIC_INDEFINITE);
         mumps.SetOperator(*glbMat);
         mumps.iterative_mode=true;
         mumps.Mult(b_tv, x_tv);
         // Compute residual
         glbMat->Mult(x_tv, dummy);
         dummy -= b_tv;
         res_l2_linsolver = std::sqrt(InnerProduct(MPI_COMM_WORLD, dummy, dummy));

         // Update current volume
         curr_vol = InnerProduct(MPI_COMM_WORLD, rho, volform);

         // Update gridfunctions from true vector
         Psi.Distribute(&(x_tv.GetBlock(0)));
         rho.Distribute(&(x_tv.GetBlock(1)));
         psi.Distribute(&(x_tv.GetBlock(2)));

         // Newton residual
         res_newt = psi_old.ComputeL1Error(psi_cf)
                    + rho_old.ComputeL2Error(rho_cf)
                    + Psi_old.ComputeL1Error(Psi_cf);
         if (res_newt < tol_newt) { break; }
      }

      // Objective, L2 diff
      switch (obj_type)
      {
         case L2PROJ:
         {
            obj = std::pow(zero_gf.ComputeL2Error(*grad_cf), 2.0)/2.0;
            break;
         }
         case COMPLIANCE:
         {
            obj = elasticity.GetParLinearForm()->operator()(u);
            break;
         }
      }

      // Residual and useful info
      max_Psi = Psi.ComputeMaxError(zero_vec_cf);
      max_psi = psi.ComputeMaxError(zero_cf);
      res_md = psi_k.ComputeL1Error(psi_cf)/alpha + rho_k.ComputeL2Error(
                  rho_cf) + Psi_k.ComputeL1Error(Psi_cf)/alpha;

      // Visualization
      logger.Print();
      mapped_rho_gf.ProjectCoefficient(mapped_rho_cf);
      paraview_dc->SetTime(it_md);
      paraview_dc->SetCycle(it_md);
      paraview_dc->Save();

      if (it_md >= 1 && res_md < tol_md) { break; }
   }
   return 0;
}

