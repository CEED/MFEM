#include "mfem.hpp"
#include "gs.hpp"
#include "boundary.hpp"
#include "amr.hpp"
#include "field.hpp"
#include "double_integrals.hpp"
#include <stdio.h>
#include <chrono>


using namespace std;
using namespace mfem;


void PrintMatlab(SparseMatrix *Mat, SparseMatrix *M1, SparseMatrix *M2);
void Print_(const Vector &y) {
  for (int i = 0; i < y.Size(); ++i) {
    printf("%d %.14e\n", i+1, y[i]);
  }
}

void test_grad(SysOperator *op, GridFunction x, FiniteElementSpace fespace) {

  LinearForm y1(&fespace);
  LinearForm y2(&fespace);
  double C1, C2, f1, f2;
  // LinearForm Cy(&fespace);
  LinearForm fy(&fespace);
  Vector * df1;
  Vector * df2;

  int size = y1.Size();

  Vector *currents = op->get_uv();
  GridFunction res_1(&fespace);
  GridFunction res_2(&fespace);
  GridFunction res_3(&fespace);
  GridFunction res_4(&fespace);
  GridFunction Cy(&fespace);
  GridFunction Ba(&fespace);
  SparseMatrix By;
  double plasma_current_1, plasma_current_2;
  double Ca;

  // *********************************
  // Test Ca and Ba
  double alpha = 1.0;
  double eps = 1e-4;

  alpha += eps;
  op->NonlinearEquationRes(x, currents, alpha);
  plasma_current_1 = op->get_plasma_current();
  res_1 = op->get_res();

  alpha -= eps;
  op->NonlinearEquationRes(x, currents, alpha);
  plasma_current_2 = op->get_plasma_current();
  Ca = op->get_Ca();

  printf("Ca: %e\n", Ca);
  
  Ba = op->get_Ba();
  res_2 = op->get_res();

  double Ca_FD = (plasma_current_1 - plasma_current_2) / eps;

  printf("\ndC/dalpha\n");
  printf("Ca: code=%e, FD=%e, Diff=%e\n", Ca, Ca_FD, Ca-Ca_FD);

  GridFunction Ba_FD(&fespace);
  add(1.0 / eps, res_1, -1.0 / eps, res_2, Ba_FD);
  printf("\ndB/dalpha\n");
  for (int i = 0; i < size; ++i) {
    if ((Ba_FD[i] != 0) || (Ba[i] != 0)) {
      printf("%d: code=%e, FD=%e, Diff=%e\n", i, Ba[i], Ba_FD[i], Ba[i]-Ba_FD[i]);
    }
  }

  // *********************************
  // Test Cy and By, ind_x
  int ind_x = op->get_ind_x();
  int ind_ma = op->get_ind_ma();

  int ind = ind_x;
  while (true) {
    x[ind] += eps;
    op->NonlinearEquationRes(x, currents, alpha);
    plasma_current_1 = op->get_plasma_current();
    res_3 = op->get_res();

    x[ind] -= eps;
    op->NonlinearEquationRes(x, currents, alpha);
    plasma_current_2 = op->get_plasma_current();
    Cy = op->get_Cy();
    By = op->get_By();
    res_4 = op->get_res();

    double Cy_FD = (plasma_current_1 - plasma_current_2) / eps;
  
    printf("\ndC/dy\n");
    printf("Cy: code=%e, FD=%e, Diff=%e\n", Cy[ind], Cy_FD, Cy[ind]-Cy_FD);

    printf("\ndB/dy\n");
    GridFunction By_FD(&fespace);
    add(1.0 / eps, res_3, -1.0 / eps, res_4, By_FD);

    int *I = By.GetI();
    int *J = By.GetJ();
    double *A = By.GetData();
    int height = By.Height();
    for (int i = 0; i < height; ++i) {
      for (int j = I[i]; j < I[i+1]; ++j) {
        if ((J[j] == ind)) {
          printf("%d %d: code=%e, FD=%e, Diff=%e\n", i, J[j], A[j], By_FD[i], A[j]-By_FD[i]);
        }
      }
    }

    if (ind == ind_x) {
      ind = ind_ma;
    } else {
      break;
    }

  }

  if (true) {
    return;
  }
  
  // *********************************
  // Test grad_obj and hess_obj
  double obj1, obj2, grad_obj_FD;

  op->set_i_option(2);
  
  GridFunction grad_obj(&fespace);
  GridFunction grad_obj_1(&fespace);
  GridFunction grad_obj_2(&fespace);
  GridFunction grad_obj_3(&fespace);
  GridFunction grad_obj_4(&fespace);
  grad_obj = op->compute_grad_obj(x);
  printf("\ndf/dy\n");
  for (int i = 0; i < size; ++i) {
    x[i] += eps;
    obj1 = op->compute_obj(x);
    x[i] -= eps;
    obj2 = op->compute_obj(x);

    grad_obj_FD = (obj1 - obj2) / eps;
    if ((grad_obj[i] != 0) || (grad_obj_FD != 0)) {
      printf("%d: code=%e, FD=%e, Diff=%e\n", i, grad_obj[i], grad_obj_FD, grad_obj[i]-grad_obj_FD);
    }
  }

  x[ind_x] += eps;
  grad_obj_1 = op->compute_grad_obj(x);

  x[ind_x] -= eps;
  grad_obj_2 = op->compute_grad_obj(x);

  x[ind_ma] += eps;
  grad_obj_3 = op->compute_grad_obj(x);

  x[ind_ma] -= eps;
  grad_obj_4 = op->compute_grad_obj(x);

  GridFunction K_FD_x(&fespace);
  GridFunction K_FD_ma(&fespace);
  add(1.0 / eps, grad_obj_1, -1.0 / eps, grad_obj_2, K_FD_x);
  add(1.0 / eps, grad_obj_3, -1.0 / eps, grad_obj_4, K_FD_ma);
  
  SparseMatrix * K = op->compute_hess_obj(x);

  // printf("-----\n");
  // K_FD_x.Print();
  printf("ind_x =%d\n", ind_x);
  printf("ind_ma=%d\n", ind_ma);
  
  printf("\nd2f/dy2\n");
  int *I_K = K->GetI();
  int *J_K = K->GetJ();
  double *A_K = K->GetData();
  int height_K = K->Height();
  double TOL = 1e-15;
  for (int i = 0; i < height_K; ++i) {
    for (int j = I_K[i]; j < I_K[i+1]; ++j) {
      if ((J_K[j] == ind_x) && ((abs(A_K[j]) > TOL) || (abs(K_FD_x[i]) > TOL))) {
        printf("%d %d: code=%e, FD=%e, Diff=%e\n", i, J_K[j], A_K[j], K_FD_x[i], A_K[j]-K_FD_x[i]);
      }
      if ((J_K[j] == ind_ma) && ((abs(A_K[j]) > TOL) || (abs(K_FD_ma[i]) > TOL))) {
        printf("%d %d: code=%e, FD=%e, Diff=%e\n", i, J_K[j], A_K[j], K_FD_ma[i], A_K[j]-K_FD_ma[i]);
      }
    }
  }
  
  
  
}



void PrintMatlab(SparseMatrix *Mat, SparseMatrix *M1, SparseMatrix *M2) {
  // Mat: diff
  // M1: true
  // M2: FD
  int *I = Mat->GetI();
  int *J = Mat->GetJ();
  double *A = Mat->GetData();
  int height = Mat->Height();

  double tol = 1e-5;
  
  int i, j;
  for (i = 0; i < height; ++i) {
    for (j = I[i]; j < I[i+1]; ++j) {
      if (abs(A[j]) > tol) {
        double m1 = 0.0;
        double m2 = 0.0;
        for (int k = M1->GetI()[i]; k < M1->GetI()[i+1]; ++k) {
          // printf("%d, %d, %d \n", k, M1->GetJ()[k], J[j]);
          if (M1->GetJ()[k] == J[j]) {
            m1 = M1->GetData()[k];
            break;
          }
        }
        for (int k = M2->GetI()[i]; k < M2->GetI()[i+1]; ++k) {
          if (M2->GetJ()[k] == J[j]) {
            m2 = M2->GetData()[k];
            break;
          }
        }
        
        printf("i=%d, j=%d, J=%10.3e, FD=%10.3e, diff=%10.3e ", i, J[j], m1, m2, A[j] / max(m1, m2));
        if (abs(A[j] / max(m1, m2)) > 1e-4) {
          printf("***");
        }
        printf("\n");
      }
    }
  }
}


void PrintMatlab(FILE *fp, SparseMatrix *Mat) {
  int *I = Mat->GetI();
  int *J = Mat->GetJ();
  double *A = Mat->GetData();
  int height = Mat->Height();

  int i, j;
  for (i = 0; i < height; ++i) {
    for (j = I[i]; j < I[i+1]; ++j) {
      fprintf(fp, "%d %d %.3e\n", i, J[j], A[j]);
    }
  }
}




void DefineRHS(PlasmaModelBase & model, double & rho_gamma,
               Mesh & mesh, 
               ExactCoefficient & exact_coefficient,
               ExactForcingCoefficient & exact_forcing_coeff, LinearForm & coil_term,
               SparseMatrix * F) {
  /*
    8/24: This function creates the F matrix

    Inputs:
    model: PlasmaModel containing constants used in plasma
    attribs: unique element attributes used by the mesh
    coil_current_values: current values for each mesh attribute
    

    Outputs:
    coil_term: Linear Form of RHS

   */
  FiniteElementSpace * fespace = coil_term.FESpace();
  int ndof = fespace->GetNDofs();
  
  int current_counter = 0;
  GridFunction ones(fespace);
  ones = 1.0;

  // these are the unique element attributes used by the mesh
  Array<int> attribs(mesh.attributes);
  // 832 is the long coil
  for (int i = 0; i < attribs.Size(); ++i) {
    int attrib = attribs[i];
    switch(attrib) {
    case attr_ext:
      // exterior domain
      break;
    case attr_lim:
      // limiter domain
      break;
    case 1100:
      break;
    default:
      double mu = model.get_mu();
      //
      Vector pw_vector(attribs.Max());
      pw_vector = 0.0;
      pw_vector(attrib-1) = 1.0;
      PWConstCoefficient pw_coeff(pw_vector);
      LinearForm lf(fespace);
      lf.AddDomainIntegrator(new DomainLFIntegrator(pw_coeff));
      cout << attrib <<" in "<< attribs.Max()<<endl;
      cout << "number of ele="<<fespace -> GetNE()<<endl;
      cout << "number of ele in mesh="<<mesh.GetNE()<<endl;
      cout << "problem one line below" << endl;
      lf.Assemble();
      cout << "problem one line above" << endl;
      double area = lf(ones);
      for (int j = 0; j < ndof; ++j) {
        if (lf[j] != 0) {
          F->Set(j, current_counter, lf[j] / area);
        }
      }
      
      ++current_counter;
    }
  }
  F->Finalize();

  // manufactured solution forcing
  // has no effect when manufactured solution is turned off
  if (true) {
    coil_term.AddDomainIntegrator(new DomainLFIntegrator(exact_forcing_coeff));
  }

  coil_term.Assemble();

  // boundary terms
  if (true) {
    BilinearForm b(fespace);
    double mu = model.get_mu();
    
    auto N_lambda = [&rho_gamma, &mu](const Vector &x) -> double
    {
      return N_coefficient(x, rho_gamma, mu);
    };
    FunctionCoefficient first_boundary_coeff(N_lambda);
    b.AddBoundaryIntegrator(new MassIntegrator(first_boundary_coeff));
    auto M_lambda = [&mu](const Vector &x, const Vector &y) -> double
    {
      return M_coefficient(x, y, mu);
    };
    DoubleBoundaryBFIntegrator i(M_lambda);
    b.Assemble();
    AssembleDoubleBoundaryIntegrator(b, i, attr_ff_bdr);
    b.Finalize(); // is this needed?

    GridFunction u_ex(fespace);
    u_ex.ProjectCoefficient(exact_coefficient);

    // coil_term += b @ u_ex
    // if not a manufactured solution, we set u_ex = 0, and this term has no effect
    b.AddMult(u_ex, coil_term);

  }

  
}

void DefineLHS(PlasmaModelBase & model, double rho_gamma, BilinearForm & diff_operator) {
   // Set up the bilinear form diff_operator corresponding to the diffusion integrator


  DiffusionIntegratorCoefficient diff_op_coeff(&model);
  diff_operator.AddDomainIntegrator(new DiffusionIntegrator(diff_op_coeff));

   Vector pw_vector_(2000);
   pw_vector_ = 0.0;
   pw_vector_(1100-1) = 1.0;
   PWConstCoefficient pw_coeff(pw_vector_);
   // for debugging: solve I u = g
   if (true) {
     ConstantCoefficient one(1.0);
     diff_operator.AddDomainIntegrator(new MassIntegrator(pw_coeff));
   }
   
   // boundary integral
   double mu = model.get_mu();
   if (true) {
     auto N_lambda = [&rho_gamma, &mu](const Vector &x) -> double
     {
       return N_coefficient(x, rho_gamma, mu);
     };

     FunctionCoefficient first_boundary_coeff(N_lambda);
     diff_operator.AddBoundaryIntegrator(new MassIntegrator(first_boundary_coeff));

     
     // BoundaryCoefficient first_boundary_coeff(rho_gamma, &model, 1);
     // diff_operator.AddBoundaryIntegrator(new MassIntegrator(first_boundary_coeff));
     // https://en.cppreference.com/w/cpp/experimental/special_functions
   }

   // assemble diff_operator
   diff_operator.Assemble();

   if (true) {
     auto M_lambda = [&mu](const Vector &x, const Vector &y) -> double
     {
       return M_coefficient(x, y, mu);
     };
     DoubleBoundaryBFIntegrator i(M_lambda);
     AssembleDoubleBoundaryIntegrator(diff_operator, i, attr_ff_bdr);
     diff_operator.Finalize(); // is this needed?
   }
   
}


HypreParMatrix * convert_to_hypre(SparseMatrix *P) {

  // Define the partition
  HYPRE_BigInt col_starts[2], row_starts[2];
  row_starts[0] = 0;
  row_starts[1] = P->Height();
  col_starts[0] = 0;
  col_starts[1] = P->Height();

  return new HypreParMatrix(MPI_COMM_WORLD, P->Height(), (HYPRE_BigInt) P->Height(), (HYPRE_BigInt) P->Width(),
                            P->GetI(), P->GetJ(), P->GetData(),
                            row_starts, col_starts); 

}


void Solve(FiniteElementSpace & fespace, PlasmaModelBase *model, GridFunction & x, int & kdim,
           int & max_newton_iter, int & max_krylov_iter,
           double & newton_tol, double & krylov_tol, 
           double & Ip, int N_control, int do_control,
           int add_alpha, int obj_option, double & obj_weight,
           double & rho_gamma,
           Mesh * mesh,
           ExactForcingCoefficient * exact_forcing_coeff,
           ExactCoefficient * exact_coefficient,
           InitialCoefficient * init_coeff,
           bool include_plasma,
           double & weight_coils,
           double & weight_solenoids,
           Vector * uv,
           double & alpha,
           int & PC_option, int & max_levels, int & max_dofs, double & light_tol,
           double & alpha_in, double & gamma_in,
           int amg_cycle_type, int amg_num_sweeps_a, int amg_num_sweeps_b, int amg_max_iter,
           double amr_frac_in, double amr_frac_out) {


  // initialize MPI and Hypre so we can use AMG
  Mpi::Init();
  Hypre::Init();

  // initialize containers for magnetic field
  GridFunction psi_r(&fespace);
  GridFunction psi_z(&fespace);
  FieldCoefficient BrCoeff(&x, &psi_r, &psi_z, model, fespace, 0);
  FieldCoefficient BpCoeff(&x, &psi_r, &psi_z, model, fespace, 1);
  FieldCoefficient BzCoeff(&x, &psi_r, &psi_z, model, fespace, 2);
  GridFunction Br_field(&fespace);
  GridFunction Bp_field(&fespace);
  GridFunction Bz_field(&fespace);
  GridFunction f(&fespace);

  // Save data in the VisIt format
  char outname[60];
  sprintf(outname, "out/gs_model%d_pc%d_cyc%d_it%d", model->get_model_choice(), PC_option, amg_cycle_type, amg_max_iter);  
  VisItDataCollection visit_dc(outname, fespace.GetMesh());
  visit_dc.RegisterField("psi", &x);
  visit_dc.RegisterField("Br", &Br_field);
  visit_dc.RegisterField("Bp", &Bp_field);
  visit_dc.RegisterField("Bz", &Bz_field);

  if (do_control) {
    // solve the optimization problem of determining currents to fit desired plasma shape
    /*
      pv: Lagrange multiplier
      lv: Lagrange multiplier
      
     */

    FILE *fp;
    char filename[60];
    sprintf(filename, "out_iter/iters_model%d_pc%d_cyc%d_it%d.txt", model->get_model_choice(), PC_option, amg_cycle_type, amg_max_iter);
    fp = fopen(filename, "w");

    // print initial currents
    printf("currents: [");
    for (int i = 0; i < uv->Size(); ++i) {
      printf("%.3e ", (*uv)[i]);
    }
    printf("]\n");

    // initial condition for lagrange multipliers
    GridFunction pv(&fespace);
    pv = 0.0;
    double lv = 0.0;

    // initialize residual, rhs
    GridFunction eq_res(&fespace);
    Vector reg_res(uv->Size());
    GridFunction opt_res(&fespace);
    GridFunction b1(&fespace);
    Vector b2(uv->Size());
    GridFunction b3(&fespace);
    b1 = 0.0;
    b2 = 0.0;
    b3 = 0.0;

    // define error estimator for AMR
    ErrorEstimator *estimator{nullptr};
    ConstantCoefficient one(1.0);
    DiffusionIntegratorCoefficient diff_op_coeff(model);
    DiffusionIntegrator *integ = new DiffusionIntegrator(diff_op_coeff);
    estimator = new LSZienkiewiczZhuEstimator(*integ, x);
    // estimator = new LSZienkiewiczZhuEstimator(*integ, f);
    RegionalThresholdRefiner refiner(*estimator);

    // track time
    auto t_init = std::chrono::high_resolution_clock::now();

    // *** AMR LOOP *** //
    for (int it_amr = 0; ; ++it_amr) {
      int total_gmres = 0;
      int cdofs = fespace.GetTrueVSize();
      printf("AMR iteration %d\n", it_amr);
      printf("Number of unknowns: %d\n", cdofs);

      // save mesh
      char name_mesh[60];
      sprintf(name_mesh, "gf/mesh_amr%d_model%d_pc%d_cyc%d_it%d.mesh", it_amr, model->get_model_choice(), PC_option, amg_cycle_type, amg_max_iter);
      mesh->Save(name_mesh);
      mesh->Save("meshes/mesh_refine.mesh");

      // *** prepare operators for solver *** //
      // RHS forcing of equation
      LinearForm coil_term(&fespace);
      // coefficient matrix for currents
      SparseMatrix *F;
      F = new SparseMatrix(fespace.GetNDofs(), num_currents);
      // elliptic operator
      BilinearForm diff_operator(&fespace);
      // Hessian matrix for objective function
      SparseMatrix * K_;
      // linear term in objective function
      Vector g_;
      // weights and locations of objective function coefficients
      vector<Vector> *alpha_coeffs;
      vector<Array<int>> *J_inds;
      alpha_coeffs = new vector<Vector>;
      J_inds = new vector<Array<int>>;
      // regularization matrix
      SparseMatrix * H;
      H = new SparseMatrix(num_currents, num_currents);

      // computations
      DefineRHS(*model, rho_gamma, *mesh, *exact_coefficient, *exact_forcing_coeff, coil_term, F);
      DefineLHS(*model, rho_gamma, diff_operator);
      init_coeff->compute_QP(N_control, mesh, &fespace);
      K_ = init_coeff->compute_K();
      g_ = init_coeff->compute_g();
      J_inds = init_coeff->get_J();
      alpha_coeffs = init_coeff->get_alpha();

      for (int i = 0; i < num_currents; ++i) {
        if (i < 5) {
          H->Set(i, i, weight_coils);
        } else {
          H->Set(i, i, weight_solenoids);
        }
      }
      H->Finalize();

      // define system operator
      SysOperator op(&diff_operator, &coil_term, model, &fespace, mesh, attr_lim, &x, F, uv, H, K_, &g_, alpha_coeffs, J_inds, &alpha, include_plasma);
      op.set_i_option(obj_option);
      op.set_obj_weight(obj_weight);  

      // set size of blocks in equation
      Array<int> row_offsets(3);
      row_offsets[0] = 0;
      row_offsets[1] = pv.Size();
      row_offsets[2] = row_offsets[1] + pv.Size();

      // inexact newton settings
      double eta_last = 0.0;
      double sg_threshold = 0.1;
      double lin_rtol_max = krylov_tol;
      double eta = krylov_tol;

      // *** NEWTON LOOP *** //
      double error_old;
      double error;
      for (int i = 0; i <= max_newton_iter; ++i) {

        // compute matrices and vectors in problem
        op.NonlinearEquationRes(x, uv, alpha);

        // get operators
        SparseMatrix By = op.get_By();
        double C = op.get_plasma_current();
        double Ca = op.get_Ca();
        Vector Cy = op.get_Cy();
        Vector Ba = op.get_Ba();
        SparseMatrix * AMat = op.compute_hess_obj(x);
        Vector g = op.compute_grad_obj(x);

        // plasma current
        printf("plasma_current = %e\n", C / op.get_mu());
        printf("alpha = %f\n", alpha);
        fprintf(fp, "plasma_current = %e\n", C / op.get_mu());
        fprintf(fp, "alpha = %f\n", alpha);

        // get psi x and magnetic axis points and locations
        double psi_x = op.get_psi_x();
        double psi_ma = op.get_psi_ma();
        double* x_x = op.get_x_x();
        double* x_ma = op.get_x_ma();
        BrCoeff.set_psi_vals(psi_x, psi_ma);
        BpCoeff.set_psi_vals(psi_x, psi_ma);
        BzCoeff.set_psi_vals(psi_x, psi_ma);
        printf("psi_x = %e; r_x = %e; z_x = %e\n", psi_x, x_x[0], x_x[1]);
        printf("psi_ma = %e; r_ma = %e; z_ma = %e\n", psi_ma, x_ma[0], x_ma[1]);
        fprintf(fp, "psi_x = %e; r_x = %e; z_x = %e\n", psi_x, x_x[0], x_x[1]);
        fprintf(fp, "psi_ma = %e; r_ma = %e; z_ma = %e\n", psi_ma, x_ma[0], x_ma[1]);

        // *** compute rhs vectors *** //
        // -b3 = eq_res = B(y^n) - F u^n
        eq_res = op.get_res();
        b3 = eq_res;  b3 *= -1.0;
        char name_eq_res[60];
        sprintf(name_eq_res, "gf/eq_res_amr%d_i%d.gf", it_amr, i);
        eq_res.Save(name_eq_res);

        // -b1 = Gy + By^T p + Cy lambda - g
        opt_res = g;
        By.AddMultTranspose(pv, opt_res);
        add(opt_res, lv, Cy, opt_res);
        b1 = opt_res; b1 *= -1.0;

        // -b2 = reg_res = H u^n - F^T p^n
        H->Mult(*uv, reg_res);
        F->AddMultTranspose(pv, reg_res, -1.0);
        b2 = reg_res; b2 *= -1.0;

        // -b4 = B_a^T p^n + C_a l^n
        double b4 = Ba * pv + Ca * lv;
        b4 *= -1.0;

        // -b5 = C - Ip * mu
        double b5= C - Ip * op.get_mu();
        b5 *= -1.0;

        // get max errors for residuals
        error = GetMaxError(eq_res);
        double max_opt_res = op.get_mu() * GetMaxError(opt_res);
        double max_reg_res = GetMaxError(reg_res) / op.get_mu();

        // objective + regularization
        // x^T K x - g^T x + C + uv^T H uv
        double true_obj = AMat->InnerProduct(x, x);
        true_obj *= 0.5;
        double test_obj = op.compute_obj(x);
        printf("objective test: yKy=%e, formula=%e, diff=%e\n", true_obj, test_obj, true_obj - test_obj);
        double regularization = (H->InnerProduct(*uv, *uv));

        // print progress
        if (i == 0) {
          printf("i: %3d, nonl_res: %.3e, ratio %9s, res: [%.3e, %.3e, %.3e, %.3e], loss: %.3e, obj: %.3e, reg: %.3e\n",
                 i, error, "", max_opt_res, max_reg_res, abs(b4), abs(b5), test_obj+regularization, test_obj, regularization);
          fprintf(fp, "i: %3d, nonl_res: %.3e, ratio %9s, res: [%.3e, %.3e, %.3e, %.3e], loss: %.3e, obj: %.3e, reg: %.3e\n",
                 i, error, "", max_opt_res, max_reg_res, abs(b4), abs(b5), test_obj+regularization, test_obj, regularization);
        } else {
          printf("i: %3d, nonl_res: %.3e, ratio %.3e, res: [%.3e, %.3e, %.3e, %.3e], loss: %.3e, obj: %.3e, reg: %.3e\n",
                 i, error, error_old / error, max_opt_res, max_reg_res, abs(b4), abs(b5), test_obj+regularization,
                 test_obj, regularization);
          fprintf(fp, "i: %3d, nonl_res: %.3e, ratio %.3e, res: [%.3e, %.3e, %.3e, %.3e], loss: %.3e, obj: %.3e, reg: %.3e\n",
                 i, error, error_old / error, max_opt_res, max_reg_res, abs(b4), abs(b5), test_obj+regularization,
                 test_obj, regularization);
        }

        // inexact newton, adjust relative tolerance
        if (i > 0) {
          eta = gamma_in * pow(error / error_old, alpha_in);
          double sg_eta = gamma_in * pow(eta_last, alpha_in);
          if (sg_eta > sg_threshold) {
            eta = max(eta, sg_eta);
          }
          eta = min(eta, lin_rtol_max);
          eta_last = eta;
        }
        printf("inexact newton rtol: %.2e\n", eta);
        
        error_old = error;
        printf("\n");
        if (error < newton_tol) {
          break;
        }
        if (i == max_newton_iter) {
          break;
        }


        // *** compute CMat *** ///
        SparseMatrix *invH;
        invH = new SparseMatrix(uv->Size(), uv->Size());
        for (int j = 0; j < uv->Size(); ++j) {
          invH->Set(j, j, 1.0 / (*H)(j, j));
        }
        invH->Finalize();
        SparseMatrix *FT = Transpose(*F);
        SparseMatrix *mF = Add(-1.0, *F, 0.0, *F);
        SparseMatrix *mFT = Transpose(*mF);
        SparseMatrix *invHFT = Mult(*invH, *FT);
        SparseMatrix *mFinvHFT = Mult(*mF, *invHFT);
        SparseMatrix *FinvH = Mult(*F, *invH);
        SparseMatrix *mMuFinvHFT = Add(op.get_mu(), *mFinvHFT, 0.0, *mFinvHFT);
        SparseMatrix *MuFinvH = Add(op.get_mu(), *FinvH, 0.0, *FinvH);
        double scale = 1.0 / sqrt(mMuFinvHFT->MaxNorm());
        // double scale = 1.0;
        SparseMatrix *CMat = Add(scale * scale, *mMuFinvHFT, 0.0, *mMuFinvHFT);

        // *** compute BMat and BTMat *** //
        SparseMatrix *CyBa;
        CyBa = new SparseMatrix(pv.Size(), pv.Size());
        for (int j = 0; j < pv.Size(); ++j) {
          for (int k = 0; k < pv.Size(); ++k) {
            if (Ba(j) * Cy(k) != 0.0) {
              CyBa->Set(j, k, Cy(j) * Ba(k) / Ca);
            }
          }
        }
        CyBa->Finalize();
        SparseMatrix *ByT = Transpose(By);
        SparseMatrix *ScaleByT = Add(scale, *ByT, 0.0, *CyBa);
        SparseMatrix *ScaleBy = Transpose(*ScaleByT);
        SparseMatrix *BMat = Add(scale, *ByT, -scale, *CyBa);
        SparseMatrix *BTMat = Transpose(*BMat);

        // define block system
        // (either symmetric version or not)
        BlockOperator BlockSystem(row_offsets);
        BlockVector rhs(row_offsets);
        int ind_x, ind_p;
        if (PC_option >= 0) {
          // non-symmetric system
          ind_x = 1;
          ind_p = 0;
        } else {
          // symmetric system
          ind_x = 0;
          ind_p = 1;
        }

        /*
          Form block system
          dx1 = [dy; dp]
          dx2 = [da; dl]

          c1 = [b1; b3 + F H^{-1} b_2]
          c2 = [b4; b5]
        */
        BlockSystem.SetBlock(0, ind_x, AMat);
        BlockSystem.SetBlock(0, ind_p, BMat);
        BlockSystem.SetBlock(1, ind_x, BTMat);
        BlockSystem.SetBlock(1, ind_p, CMat);

        FILE *fp_spy;
        char filename_spy[60];
        sprintf(filename_spy, "spys/spy_model%d_amr%d.txt", model->get_model_choice(), it_amr);
        fp_spy = fopen(filename_spy, "w");
        fprintf(fp_spy, "AMat\n");
        PrintMatlab(fp_spy, AMat);
        fprintf(fp_spy, "\nBMat\n");
        PrintMatlab(fp_spy, BMat);
        fprintf(fp_spy, "\nCMat\n");
        PrintMatlab(fp_spy, CMat);

        // Define rhs
        // rhs = [b1 - Cy b4 / Ca; b3 + mu F H^{-1} b_2 - Ba b5 / Ca]
        rhs = 0;
        add(1.0, b1, -b4 / Ca, Cy, rhs.GetBlock(0));
        MuFinvH->Mult(b2, rhs.GetBlock(1));
        rhs.GetBlock(1) += b3;
        add(1.0, rhs.GetBlock(1), - b5 / Ca, Ba, rhs.GetBlock(1));
        rhs.GetBlock(1) *= scale;

        // solver
        FGMRESSolver solver;
        // GMRESSolver solver;
        solver.SetAbsTol(1e-12);
        // solver.SetRelTol(krylov_tol);
        solver.SetRelTol(eta);
        solver.SetMaxIter(max_krylov_iter);
        solver.SetOperator(BlockSystem);
        solver.SetKDim(kdim);
        solver.SetPrintLevel(-1);

        // rhs guess
        BlockVector dx(row_offsets);
        dx = 0.0;
        double dalpha, dlv;
        
        if (PC_option == 0) {
          /*
            non symmetric system, block AMG
          */

          Solver *inv_SC, *inv_BT, *inv_B;

          // HypreParMatrix * B_Hypre = convert_to_hypre(BMat);
          // HypreParMatrix * BT_Hypre = convert_to_hypre(BTMat);
          HypreParMatrix * B_Hypre = convert_to_hypre(ScaleByT);
          HypreParMatrix * BT_Hypre = convert_to_hypre(ScaleBy);

          HypreBoomerAMG *B_AMG = new HypreBoomerAMG(*B_Hypre);
          HypreBoomerAMG *BT_AMG = new HypreBoomerAMG(*BT_Hypre);

          B_AMG->SetPrintLevel(0);
          B_AMG->SetCycleType(amg_cycle_type);
          B_AMG->SetCycleNumSweeps(amg_num_sweeps_a, amg_num_sweeps_b);
          B_AMG->SetMaxIter(amg_max_iter);

          BT_AMG->SetPrintLevel(0);
          BT_AMG->SetCycleType(amg_cycle_type);
          BT_AMG->SetCycleNumSweeps(amg_num_sweeps_a, amg_num_sweeps_b);
          BT_AMG->SetMaxIter(amg_max_iter);

          inv_B = B_AMG;
          inv_BT = BT_AMG;

          BlockDiagonalPreconditioner BlockPrec(row_offsets);
          BlockPrec.SetDiagonalBlock(0, inv_B);
          BlockPrec.SetDiagonalBlock(1, inv_BT);
          solver.SetPreconditioner(BlockPrec);

          solver.Mult(rhs, dx);
          printf("BlockOperator.BlockSystem iterations:            %d\n", solver.GetNumIterations());
          fprintf(fp, "amr=%d newton=%d iters=%d\n", it_amr, i, solver.GetNumIterations());

        } else if (PC_option == 1) {
          /*
            non symmetric system, block AMG, schur complement
           */

          // form approximation to schur complement
          double denom = Ca;
          for (int j = 0; j < pv.Size(); ++j) {
            denom -= Ba(j) * Cy(j) / By(j, j);
          }
          SparseMatrix *Mapp;
          Mapp = new SparseMatrix(pv.Size(), pv.Size());
          for (int j = 0; j < pv.Size(); ++j) {
            for (int k = 0; k < pv.Size(); ++k) {
              if (j == k) {
                Mapp->Set(j, k, 1.0 / By(j, j) - Ba(j) * Cy(k) / (denom * By(j, j) * By(k, k)));
              } else {
                if (Ba(j) * Cy(k) != 0.0) {
                  Mapp->Set(j, k, - Ba(j) * Cy(k) / (denom * By(j, j) * By(k, k)));
                }
              }
            }
          }
          Mapp->Finalize();
          // B - A Mapp C
          SparseMatrix *MC = Mult(*Mapp, *CMat);
          SparseMatrix *AMC = Mult(*AMat, *MC);
          SparseMatrix *SC = Add(1.0, *BMat, -1.0, *AMC);

          Solver *inv_SC, *inv_BT, *inv_B;

          HypreParMatrix * SC_Hypre = convert_to_hypre(SC);
          HypreParMatrix * BT_Hypre = convert_to_hypre(BTMat);

          HypreBoomerAMG *SC_AMG = new HypreBoomerAMG(*SC_Hypre);
          HypreBoomerAMG *BT_AMG = new HypreBoomerAMG(*BT_Hypre);

          SC_AMG->SetPrintLevel(0);
          SC_AMG->SetCycleType(amg_cycle_type);
          SC_AMG->SetCycleNumSweeps(amg_num_sweeps_a, amg_num_sweeps_b);
          SC_AMG->SetMaxIter(amg_max_iter);

          BT_AMG->SetPrintLevel(0);
          BT_AMG->SetCycleType(amg_cycle_type);
          BT_AMG->SetCycleNumSweeps(amg_num_sweeps_a, amg_num_sweeps_b);
          BT_AMG->SetMaxIter(amg_max_iter);

          inv_SC = SC_AMG;
          inv_BT = BT_AMG;

          BlockDiagonalPreconditioner BlockPrec(row_offsets);
          BlockPrec.SetDiagonalBlock(0, inv_SC);
          BlockPrec.SetDiagonalBlock(1, inv_BT);
          solver.SetPreconditioner(BlockPrec);
          
          solver.Mult(rhs, dx);
          printf("BlockOperator.BlockSystem iterations:            %d\n", solver.GetNumIterations());
          fprintf(fp, "amr=%d newton=%d iters=%d\n", it_amr, i, solver.GetNumIterations());
          
        } else if (PC_option == 2){
          /*
            Non Symmetric System, AMG on full block system
          */
          Solver *inv_BlockMatrix;
          HypreParMatrix * AMat_Hypre = convert_to_hypre(AMat);
          HypreParMatrix * BMat_Hypre = convert_to_hypre(BMat);
          HypreParMatrix * BTMat_Hypre = convert_to_hypre(BTMat);
          HypreParMatrix * CMat_Hypre = convert_to_hypre(CMat);

          Array2D<HypreParMatrix *> Block(2, 2);
          Block(0, 0) = BMat_Hypre;
          Block(0, 1) = AMat_Hypre;
          Block(1, 0) = CMat_Hypre;
          Block(1, 1) = BTMat_Hypre;

          HypreParMatrix * BlockMatrix_Hypre = HypreParMatrixFromBlocks(Block);
          HypreBoomerAMG *BlockMatrix_AMG = new HypreBoomerAMG(*BlockMatrix_Hypre);

          BlockMatrix_AMG->SetPrintLevel(0);
          BlockMatrix_AMG->SetCycleType(amg_cycle_type);
          BlockMatrix_AMG->SetCycleNumSweeps(amg_num_sweeps_a, amg_num_sweeps_b);
          BlockMatrix_AMG->SetMaxIter(amg_max_iter);

          inv_BlockMatrix = BlockMatrix_AMG;
          solver.SetPreconditioner(*inv_BlockMatrix);

          solver.Mult(rhs, dx);
          
        } else if (PC_option == 3){
          /*
            Non Symmetric System, AMG on partial full block system
          */
          Solver *inv_BlockMatrix;
          HypreParMatrix * AMat_Hypre = convert_to_hypre(AMat);
          HypreParMatrix * BMat_Hypre = convert_to_hypre(BMat);
          HypreParMatrix * BTMat_Hypre = convert_to_hypre(BTMat);

          Array2D<HypreParMatrix *> Block(2, 2);
          Block(0, 0) = BMat_Hypre;
          Block(0, 1) = AMat_Hypre;
          Block(1, 1) = BTMat_Hypre;

          HypreParMatrix * BlockMatrix_Hypre = HypreParMatrixFromBlocks(Block);
          HypreBoomerAMG *BlockMatrix_AMG = new HypreBoomerAMG(*BlockMatrix_Hypre);

          BlockMatrix_AMG->SetPrintLevel(0);
          BlockMatrix_AMG->SetCycleType(amg_cycle_type);
          BlockMatrix_AMG->SetCycleNumSweeps(amg_num_sweeps_a, amg_num_sweeps_b);
          BlockMatrix_AMG->SetMaxIter(amg_max_iter);

          inv_BlockMatrix = BlockMatrix_AMG;
          solver.SetPreconditioner(*inv_BlockMatrix);

          solver.Mult(rhs, dx);
          fprintf(fp, "amr=%d newton=%d iters=%d\n", it_amr, i, solver.GetNumIterations());
          
        } else if (PC_option == 4){
          /*
            Non Symmetric System, stepped approach
          */

          Solver *inv_SC, *inv_BT, *inv_B;

          HypreParMatrix * B_Hypre = convert_to_hypre(BMat);
          HypreParMatrix * BT_Hypre = convert_to_hypre(BTMat);

          HypreBoomerAMG *B_AMG = new HypreBoomerAMG(*B_Hypre);
          HypreBoomerAMG *BT_AMG = new HypreBoomerAMG(*BT_Hypre);

          B_AMG->SetPrintLevel(0);
          B_AMG->SetCycleType(amg_cycle_type);
          B_AMG->SetCycleNumSweeps(amg_num_sweeps_a, amg_num_sweeps_b);
          B_AMG->SetMaxIter(amg_max_iter);

          BT_AMG->SetPrintLevel(0);
          BT_AMG->SetCycleType(amg_cycle_type);
          BT_AMG->SetCycleNumSweeps(amg_num_sweeps_a, amg_num_sweeps_b);
          BT_AMG->SetMaxIter(amg_max_iter);

          inv_B = B_AMG;
          inv_BT = BT_AMG;

          // B - A (BT)^{-1} C
          SchurComplement SC(BTMat, CMat, AMat, BMat, inv_BT, light_tol);
          SchurComplementInverse SCinv(&SC, inv_B, light_tol);

          GMRESSolver bsolver;
          bsolver.SetAbsTol(1e-16);
          bsolver.SetRelTol(light_tol);
          bsolver.SetMaxIter(max_krylov_iter);
          bsolver.SetOperator(*BTMat);
          bsolver.SetKDim(kdim);
          bsolver.SetPrintLevel(-1);
          bsolver.SetPreconditioner(*inv_BT);
          
          BlockDiagonalPreconditioner BlockPrec(row_offsets);
          BlockPrec.SetDiagonalBlock(0, &SCinv);
          BlockPrec.SetDiagonalBlock(1, &bsolver);
          solver.SetPreconditioner(BlockPrec);

          solver.Mult(rhs, dx);
          printf("BlockOperator.BlockSystem iterations:            %d\n", solver.GetNumIterations());
          printf("SchurComplement.SC average iterations:           %.2f\n", SC.GetAvgIterations());
          printf("SchurComplementInverse.SCinv average iterations: %.2f\n", SCinv.GetAvgIterations());
          printf("bsolver average iterations:                      %.2f\n", ((double) bsolver.GetNumIterations()));
          fprintf(fp, "amr=%d newton=%d iters=%d amgTot=%d\n", it_amr, i, solver.GetNumIterations(), solver.GetNumIterations() * (SC.GetAvgIterations() * SCinv.GetAvgIterations() + ((double) bsolver.GetNumIterations())));
          
        } else if (PC_option == 5) {

          // upper triangular AMG
          Solver *inv_SC, *inv_BT, *inv_B;

          // HypreParMatrix * B_Hypre = convert_to_hypre(BMat);
          // HypreParMatrix * BT_Hypre = convert_to_hypre(BTMat);
          HypreParMatrix * B_Hypre = convert_to_hypre(ScaleByT);
          HypreParMatrix * BT_Hypre = convert_to_hypre(ScaleBy);

          HypreBoomerAMG *B_AMG = new HypreBoomerAMG(*B_Hypre);
          HypreBoomerAMG *BT_AMG = new HypreBoomerAMG(*BT_Hypre);

          B_AMG->SetPrintLevel(0);
          B_AMG->SetCycleType(amg_cycle_type);
          B_AMG->SetCycleNumSweeps(amg_num_sweeps_a, amg_num_sweeps_b);
          B_AMG->SetMaxIter(amg_max_iter);

          BT_AMG->SetPrintLevel(0);
          BT_AMG->SetCycleType(amg_cycle_type);
          BT_AMG->SetCycleNumSweeps(amg_num_sweeps_a, amg_num_sweeps_b);
          BT_AMG->SetMaxIter(amg_max_iter);

          inv_B = B_AMG;
          inv_BT = BT_AMG;
          
          SchurPC SCPC(AMat, CMat, inv_B, inv_BT, &Ba, &Cy, Ca, 1);
          solver.SetPreconditioner(SCPC);
          solver.Mult(rhs, dx);
          printf("BlockOperator.BlockSystem iterations:            %d\n", solver.GetNumIterations());
          fprintf(fp, "amr=%d newton=%d iters=%d\n", it_amr, i, solver.GetNumIterations());

        } else if (PC_option == 6) {
          // lower triangular AMG

          Solver *inv_SC, *inv_BT, *inv_B;

          // HypreParMatrix * B_Hypre = convert_to_hypre(BMat);
          // HypreParMatrix * BT_Hypre = convert_to_hypre(BTMat);
          HypreParMatrix * B_Hypre = convert_to_hypre(ScaleByT);
          HypreParMatrix * BT_Hypre = convert_to_hypre(ScaleBy);

          HypreBoomerAMG *B_AMG = new HypreBoomerAMG(*B_Hypre);
          HypreBoomerAMG *BT_AMG = new HypreBoomerAMG(*BT_Hypre);

          B_AMG->SetPrintLevel(0);
          B_AMG->SetCycleType(amg_cycle_type);
          B_AMG->SetCycleNumSweeps(amg_num_sweeps_a, amg_num_sweeps_b);
          B_AMG->SetMaxIter(amg_max_iter);

          BT_AMG->SetPrintLevel(0);
          BT_AMG->SetCycleType(amg_cycle_type);
          BT_AMG->SetCycleNumSweeps(amg_num_sweeps_a, amg_num_sweeps_b);
          BT_AMG->SetMaxIter(amg_max_iter);

          inv_B = B_AMG;
          inv_BT = BT_AMG;
          
          SchurPC SCPC(AMat, CMat, inv_B, inv_BT, &Ba, &Cy, Ca, 2);
          solver.SetPreconditioner(SCPC);
          solver.Mult(rhs, dx);
          printf("BlockOperator.BlockSystem iterations:            %d\n", solver.GetNumIterations());
          fprintf(fp, "amr=%d newton=%d iters=%d\n", it_amr, i, solver.GetNumIterations());

        } else if (PC_option == 7) {
          // block diagonal woodbury

          Solver *inv_SC, *inv_BT, *inv_B;

          HypreParMatrix * B_Hypre = convert_to_hypre(ScaleByT);
          HypreParMatrix * BT_Hypre = convert_to_hypre(ScaleBy);

          HypreBoomerAMG *B_AMG = new HypreBoomerAMG(*B_Hypre);
          HypreBoomerAMG *BT_AMG = new HypreBoomerAMG(*BT_Hypre);
          
          B_AMG->SetPrintLevel(0);
          B_AMG->SetCycleType(amg_cycle_type);
          B_AMG->SetCycleNumSweeps(amg_num_sweeps_a, amg_num_sweeps_b);
          B_AMG->SetMaxIter(amg_max_iter);

          BT_AMG->SetPrintLevel(0);
          BT_AMG->SetCycleType(amg_cycle_type);
          BT_AMG->SetCycleNumSweeps(amg_num_sweeps_a, amg_num_sweeps_b);
          BT_AMG->SetMaxIter(amg_max_iter);

          inv_B = B_AMG;
          inv_BT = BT_AMG;
          
          SchurPC SCPC(AMat, CMat, inv_B, inv_BT, &Ba, &Cy, Ca, 3);
          solver.SetPreconditioner(SCPC);
          solver.Mult(rhs, dx);
          printf("BlockOperator.BlockSystem iterations:            %d\n", solver.GetNumIterations());
          fprintf(fp, "amr=%d newton=%d iters=%d\n", it_amr, i, solver.GetNumIterations());

        } else if (PC_option == -1) {
          /*
            Symmetric System, Schur Complement
          */

          // *** compute SC_op = C - B^T invApaI B *** //
          double alpha_ = .001;
          Vector diag(pv.Size());
          AMat->GetDiag(diag);
          SparseMatrix *ApaI_app;
          ApaI_app = new SparseMatrix(pv.Size(), pv.Size());
          SparseMatrix *invApaI;
          invApaI = new SparseMatrix(pv.Size(), pv.Size());
          SparseMatrix *aI;
          aI = new SparseMatrix(pv.Size(), pv.Size());
          for (int j = 0; j < pv.Size(); ++j) {
            invApaI->Set(j, j, 1.0 / (alpha_ + diag(j)));
            ApaI_app->Set(j, j, (alpha_ + diag(j)));
            aI->Set(j, j, alpha_);
          }
          aI->Finalize();
          invApaI->Finalize();
          SparseMatrix *ApaI = Add(1.0, *AMat, 1.0, *aI);
          SparseMatrix *op1 = Mult(*invApaI, *BMat);
          SparseMatrix *op2 = Mult(*BTMat, *op1);
          SparseMatrix *SC_op = Add(1.0, *CMat, -1.0, *op2);

          Solver *inv_ApaI, *inv_SC;

          //https://hypre.readthedocs.io/en/latest/api-sol-parcsr.html
          // set up AMG for Schur complement
          HypreParMatrix * SC_Hypre = convert_to_hypre(SC_op);
          HypreBoomerAMG *SC_AMG = new HypreBoomerAMG(*SC_Hypre);
          SC_AMG->SetPrintLevel(0);
          SC_AMG->SetCycleType(1);
          SC_AMG->SetCycleNumSweeps(1, 1);
          SC_AMG->SetMaxIter(10);
          inv_SC = SC_AMG;

          // Gauss Seidel for A + alpha I
          GSSmoother ojs(*ApaI, 0, 2);
          inv_ApaI = &ojs;

          // Operators for SC and inverse of SC
          SchurComplement SC(ApaI, BMat, BTMat, CMat, inv_ApaI, light_tol);
          SchurComplementInverse SCinv(&SC, inv_SC, light_tol);

          // solver for A + alpha I
          CGSolver ApaIinv;
          ApaIinv.SetAbsTol(1e-16);
          ApaIinv.SetRelTol(krylov_tol);
          ApaIinv.SetMaxIter(max_krylov_iter);
          ApaIinv.SetOperator(*ApaI);
          ApaIinv.SetPreconditioner(ojs);
          ApaIinv.SetPrintLevel(0);
        
          // define preconditioner
          BlockDiagonalPreconditioner BlockPrec(row_offsets);
          BlockPrec.SetDiagonalBlock(0, &ApaIinv);
          BlockPrec.SetDiagonalBlock(1, &SCinv);
          solver.SetPreconditioner(BlockPrec);

          solver.Mult(rhs, dx);
          printf("ApaIinv average iterations:                        %d\n", ApaIinv.GetNumIterations());
          printf("SchurComplement.SC average iterations:           %.2f\n", SC.GetAvgIterations());
          printf("SchurComplementInverse.SCinv average iterations: %.2f\n", SCinv.GetAvgIterations());
          printf("BlockOperator.BlockSystem iterations:            %d\n", solver.GetNumIterations());
        } else {
          // TODO!!!
        }

        if (solver.GetConverged()) {
          printf("GMRES converged in %d iterations with a residual norm of %e\n", solver.GetNumIterations(), solver.GetFinalNorm());
        }
        else
          {
            printf("GMRES did not converge in %d iterations. Residual norm is %e\n", solver.GetNumIterations(), solver.GetFinalNorm());
          }
        total_gmres += solver.GetNumIterations();

        if (solver.GetNumIterations() == -1) {
          printf("failure...\n");
          return;
        }
        dx.GetBlock(ind_p) *= scale;

        // get solution
        x += dx.GetBlock(ind_x);
        pv += dx.GetBlock(ind_p);

        invHFT->AddMult(dx.GetBlock(ind_p), *uv);
        invH->AddMult(b2, *uv);

        dalpha = (b5 - (Cy * dx.GetBlock(ind_x))) / Ca;
        dlv = (b4 - (Ba * dx.GetBlock(ind_p))) / Ca;
        alpha += dalpha;
        lv += dlv;
        

        // *** calculate residuals after solve *** //
        // first block row
        Vector res1(pv.Size());
        res1 = 0.0;
        AMat->AddMult(dx.GetBlock(ind_x), res1);
        ByT->AddMult(dx.GetBlock(ind_p), res1);
        add(res1, dlv, Cy, res1);
        add(res1, -1.0, b1, res1);
        printf("res1: %.2e\n", GetMaxError(res1));

        // second block row
        Vector res2(pv.Size());
        res2 = 0.0;
        mMuFinvHFT->AddMult(dx.GetBlock(ind_p), res2);
        MuFinvH->Mult(b2, res2);
        res2 *= -1.0;
        By.AddMult(dx.GetBlock(ind_x), res2);
        mMuFinvHFT->AddMult(dx.GetBlock(ind_p), res2);
        add(res2, dalpha, Ba, res2);
        add(res2, -1.0, b3, res2);
        printf("res2: %.2e\n", GetMaxError(res2));

        // print currents
        printf("currents: [");
        for (int i = 0; i < uv->Size(); ++i) {
          printf("%.3e ", (*uv)[i]);
        }
        printf("]\n");

        // save grid function and magnetic field
        char name_[60];
        sprintf(name_, "gf/xtmp_amr%d.gf", it_amr);
        x.Save(name_);
        char name[60];
        sprintf(name, "gf/xtmp_amr%d_i%d.gf", it_amr, i);
        x.Save(name);
        Br_field.Save("gf/Br.gf");
        Bp_field.Save("gf/Bp.gf");
        Bz_field.Save("gf/Bz.gf");
        x.GetDerivative(1, 0, psi_r);
        x.GetDerivative(1, 1, psi_z);
        Br_field.ProjectCoefficient(BrCoeff);
        Bp_field.ProjectCoefficient(BpCoeff);
        Bz_field.ProjectCoefficient(BzCoeff);

        visit_dc.Save();

        //if (true) {
        //return;
        //}
      }

      if (it_amr >= max_levels) {
        printf("max number of refinement levels\n");
        break;
      }
      if (cdofs > max_dofs)
        {
          cout << "Reached the maximum number of dofs. Stop." << endl;
          break;
        }

      f = op.get_f();
      refiner.ApplyRef(*mesh, 1000, amr_frac_in, amr_frac_out);
      if (refiner.Stop()) {
        cout << "Stopping criterion satisfied. Stop." << endl;
        break;
      } else {
        printf("Refining mesh, i=%d\n", it_amr+1);
      }

      // update variables due to refinement
      fespace.Update();
      x.Update();
      psi_r.Update();
      psi_z.Update();
      Br_field.Update();
      Bp_field.Update();
      Bz_field.Update();
      pv.Update();
      eq_res.Update();
      opt_res.Update();
      b1.Update();
      b3.Update();
      
      printf("******* fespace.GetTrueVSize(): %d\n", fespace.GetTrueVSize());


    }

    auto t_end = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double, std::milli> ms_double = t_end - t_init;
    printf("time elapsed: %f seconds\n", ms_double.count() / 1000.0);

     
  } else {
    // for given currents, solve the GS equations

    GridFunction dx(&fespace);
    dx = 0.0;

    // *** prepare operators for solver *** //
    // RHS forcing of equation
    LinearForm coil_term(&fespace);
    // coefficient matrix for currents
    SparseMatrix *F;
    F = new SparseMatrix(fespace.GetNDofs(), num_currents);
    // elliptic operator
    BilinearForm diff_operator(&fespace);
    // Hessian matrix for objective function
    SparseMatrix * K_;
    // linear term in objective function
    Vector g_;
    // weights and locations of objective function coefficients
    vector<Vector> *alpha_coeffs;
    vector<Array<int>> *J_inds;
    alpha_coeffs = new vector<Vector>;
    J_inds = new vector<Array<int>>;
    // regularization matrix
    SparseMatrix * H;
    H = new SparseMatrix(num_currents, num_currents);

    // computations
    DefineRHS(*model, rho_gamma, *mesh, *exact_coefficient, *exact_forcing_coeff, coil_term, F);
    DefineLHS(*model, rho_gamma, diff_operator);
    init_coeff->compute_QP(N_control, mesh, &fespace);
    K_ = init_coeff->compute_K();
    g_ = init_coeff->compute_g();
    J_inds = init_coeff->get_J();
    alpha_coeffs = init_coeff->get_alpha();

    for (int i = 0; i < num_currents; ++i) {
      if (i < 5) {
        H->Set(i, i, weight_coils);
      } else {
        H->Set(i, i, weight_solenoids);
      }
    }
    H->Finalize();
    
    SysOperator op(&diff_operator, &coil_term, model, &fespace, mesh, attr_lim, &x, F, uv, H, K_, &g_, alpha_coeffs, J_inds, &alpha, include_plasma);
    op.set_i_option(obj_option);
    op.set_obj_weight(obj_weight);

    GridFunction eq_res(&fespace);
    GridFunction b3(&fespace);
    b3 = 0.0;
    
    LinearForm out_vec(&fespace);
    double error_old;
    double error;
    for (int i = 0; i < max_newton_iter; ++i) {

      op.NonlinearEquationRes(x, uv, alpha);

      eq_res = op.get_res();
      b3 = eq_res;  // b3 *= -1.0;
      // F->AddMult(*uv, eq_res, -op.get_mu());

      error = GetMaxError(eq_res);

      // op.Mult(x, out_vec);
      // error = GetMaxError(out_vec);
      // cout << "eq_res" << "i" << i << endl;
      // out_vec.Print();

      printf("\n");
      if (i == 0) {
        printf("i: %3d, max residual: %.3e\n", i, error);
      } else {
        printf("i: %3d, max residual: %.3e, ratio %.3e\n", i, error, error_old / error);
      }
      error_old = error;

      if (error < newton_tol) {
        break;
      }

      dx = 0.0;
      SparseMatrix By = op.get_By();
      // SparseMatrix *Mat = dynamic_cast<SparseMatrix *>(&op.GetGradient(x));

      // cout << "By" << "i" << i << endl;
      // Mat->PrintMatlab();

      Solver *inv_B;
      HypreParMatrix * B_Hypre = convert_to_hypre(&By);
      HypreBoomerAMG *B_AMG = new HypreBoomerAMG(*B_Hypre);
      B_AMG->SetPrintLevel(0);
      B_AMG->SetCycleType(1);
      B_AMG->SetCycleNumSweeps(1, 1);
      B_AMG->SetMaxIter(1);
      inv_B = B_AMG;
      
      // GSSmoother M(By);
      // printf("iter: %d, tol: %e, kdim: %d\n", max_krylov_iter, krylov_tol, kdim);
      int gmres_iter = max_krylov_iter;
      double gmres_tol = krylov_tol;
      int gmres_kdim = kdim;
      GMRES(By, dx, b3, *inv_B, gmres_iter, gmres_kdim, gmres_tol, 0.0, 0);
      printf("gmres iters: %d, gmres err: %e\n", gmres_iter, gmres_tol);

      // add(dx, ur_coeff - 1.0, dx, dx);
      x -= dx;

      x.Save("gf/xtmp.gf");
      GridFunction err(&fespace);
      err = out_vec;
      err.Save("gf/res.gf");

      visit_dc.Save();


    }
    op.Mult(x, out_vec);
    error = GetMaxError(out_vec);
    printf("\n\n********************************\n");
    printf("final max residual: %.3e, ratio %.3e\n", error, error_old / error);
    printf("********************************\n\n");
  }

  
  
}


double gs(const char * mesh_file, const char * initial_gf, const char * data_file, int order, int d_refine,
          int model_choice,
          double & alpha, double & beta, double & gamma, double & mu, double & Ip,
          double & r0, double & rho_gamma, int max_krylov_iter, int max_newton_iter,
          double & krylov_tol, double & newton_tol,
          double & c1, double & c2, double & c3, double & c4, double & c5, double & c6, double & c7,
          double & c8, double & c9, double & c10, double & c11,
          double & ur_coeff,
          int do_control, int N_control, double & weight_solenoids, double & weight_coils,
          double & weight_obj, int obj_option, bool optimize_alpha,
          bool do_manufactured_solution, bool do_initial,
          int & PC_option, int & max_levels, int & max_dofs, double & light_tol,
          double & alpha_in, double & gamma_in,
          int amg_cycle_type, int amg_num_sweeps_a, int amg_num_sweeps_b, int amg_max_iter,
          double amr_frac_in, double amr_frac_out) {

   Vector uv_currents(num_currents);
   uv_currents[0] = c1;
   uv_currents[1] = c2;
   uv_currents[2] = c3;
   uv_currents[3] = c4;
   uv_currents[4] = c5;
   uv_currents[5] = c6;
   uv_currents[6] = c7;
   uv_currents[7] = c8;
   uv_currents[8] = c9;
   uv_currents[9] = c10;
   uv_currents[10] = c11;

   // exact solution
   double r0_ = 1.0;
   double z0_ = 0.0;
   double L_ = 0.35;

   // solver options
   int kdim = 10000;

   /* 
      -------------------------------------------------------------------------------------------
      -------------------------------------------------------------------------------------------
      -------------------------------------------------------------------------------------------
      Process Inputs
      -------------------------------------------------------------------------------------------
      -------------------------------------------------------------------------------------------
      -------------------------------------------------------------------------------------------
   */   

   Mesh mesh(mesh_file);
   
   // save options in model
   // alpha: multiplier in \bar{S}_{ff'} term
   // beta: multiplier for S_{p'} term
   // gamma: multiplier for S_{ff'} term
   const char *data_file_ = "data/fpol_pres_ffprim_pprime.data";
   PlasmaModelFile model(mu, data_file_, alpha, beta, gamma, model_choice);

   // Define a finite element space on the mesh. Here we use H1 continuous
   // high-order Lagrange finite elements of the given order.
   H1_FECollection fec(order, mesh.Dimension());
   FiniteElementSpace fespace(&mesh, &fec);
   cout << "Number of unknowns: " << fespace.GetTrueVSize() << endl;

   double k_ = M_PI/(2.0*L_);
   ExactForcingCoefficient exact_forcing_coeff(r0_, z0_, k_, model, do_manufactured_solution);
   ExactCoefficient exact_coefficient(r0_, z0_, k_, do_manufactured_solution);

   /* 
      -------------------------------------------------------------------------------------------
      -------------------------------------------------------------------------------------------
      -------------------------------------------------------------------------------------------
      Solve
      -------------------------------------------------------------------------------------------
      -------------------------------------------------------------------------------------------
      -------------------------------------------------------------------------------------------
    */

   if (do_initial) {
     do_control = false;
   }
   
   // Define the solution x as a finite element grid function in fespace. Set
   // the initial guess to zero, which also sets the boundary conditions.
   GridFunction u(&fespace);
   
   InitialCoefficient init_coeff = read_data_file(data_file);
   if (do_manufactured_solution) {
     u.ProjectCoefficient(exact_coefficient);
     u.Save("gf/exact.gf");
   } else {
     if (!do_initial) {
       // ifstream ifs("initial/interpolated.gf");
       ifstream ifs(initial_gf);
       GridFunction lgf(&mesh, ifs);
       lgf.SetSpace(&fespace);
       u = lgf;
     }

     u.Save("gf/initial.gf");

   }


   // Read the mesh from the given mesh file, and refine "d_refine" times uniformly.
   for (int i = 0; i < d_refine; ++i) {
     mesh.UniformRefinement();
   }
   mesh.Save("meshes/mesh.mesh");
   if (do_initial) {
     mesh.Save("meshes/initial.mesh");
   }
   // fespace.Update();
   // u.Update();

   GridFunction x(&fespace);
   x = u;

   bool include_plasma = true;
   if (do_initial) {
     include_plasma = false;
   }

   // TODO: remove optimize_alpha
   Solve(fespace, &model, x, kdim, max_newton_iter, max_krylov_iter, newton_tol, krylov_tol, 
         Ip, N_control, do_control,
         optimize_alpha, obj_option, weight_obj,
         rho_gamma,
         &mesh,
         &exact_forcing_coeff,
         &exact_coefficient,
         &init_coeff,
         include_plasma,
         weight_coils,
         weight_solenoids,
         &uv_currents,
         alpha,
         PC_option, max_levels, max_dofs, light_tol,
         alpha_in, gamma_in,
         amg_cycle_type, amg_num_sweeps_a, amg_num_sweeps_b, amg_max_iter,
         amr_frac_in, amr_frac_out);
   
   if (do_initial) {
     char name_gf_out[60];
     char name_mesh_out[60];
     sprintf(name_gf_out, "initial/initial_guess_g%d.gf", d_refine);
     sprintf(name_mesh_out, "initial/initial_mesh_g%d.mesh", d_refine);

     x.Save(name_gf_out);
     mesh.Save(name_mesh_out);
     printf("Saved solution to %s\n", name_gf_out);
     printf("Saved mesh to %s\n", name_mesh_out);
     printf("glvis -m %s -g %s\n", name_mesh_out, name_gf_out);
   } else {
     char name_gf_out[60];
     sprintf(name_gf_out, "gf/final_model%d_pc%d_cyc%d_it%d.gf", model.get_model_choice(), PC_option, amg_cycle_type, amg_max_iter);
     // x.Save("gf/final.gf");
     x.Save(name_gf_out);
     // printf("Saved solution to final.gf\n");
     // printf("Saved mesh to mesh.mesh\n");
     printf("glvis -m meshes/mesh_refine.mesh -g %s\n", name_gf_out);

   }
   /* 
      -------------------------------------------------------------------------------------------
      -------------------------------------------------------------------------------------------
      -------------------------------------------------------------------------------------------
      Error
      -------------------------------------------------------------------------------------------
      -------------------------------------------------------------------------------------------
      -------------------------------------------------------------------------------------------
    */

   if (do_manufactured_solution) {
     GridFunction diff(&fespace);
     add(x, -1.0, u, diff);
     double num_error = GetMaxError(diff);
     diff.Save("gf/error.gf");
     double L2_error = x.ComputeL2Error(exact_coefficient);
     printf("\n\n********************************\n");
     printf("numerical error: %.3e\n", num_error);
     printf("L2 error: %.3e\n", L2_error);
     printf("********************************\n\n");

     return L2_error;
   } else {
     return 0.0;
   }
  
}
