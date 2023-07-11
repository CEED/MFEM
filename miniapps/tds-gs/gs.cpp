#include "mfem.hpp"
#include "gs.hpp"
#include "boundary.hpp"
#include "field.hpp"
#include "double_integrals.hpp"
#include <stdio.h>

using namespace std;
using namespace mfem;


void PrintMatlab(SparseMatrix *Mat, SparseMatrix *M1, SparseMatrix *M2);
void Print_(const Vector &y) {
  for (int i = 0; i < y.Size(); ++i) {
    printf("%d %.14e\n", i+1, y[i]);
  }
}
SparseMatrix* test_grad(SysOperator *op, Vector & x, FiniteElementSpace *fespace) {

  LinearForm y1(fespace);
  LinearForm y2(fespace);
  double C1, C2, f1, f2;
  LinearForm Cy(fespace);
  LinearForm fy(fespace);
  Vector * df1;
  Vector * df2;

  int size = x.Size();

  double eps = 1e-4;
  
  // Test By and Cy
  SparseMatrix *Mat = new SparseMatrix(size, size);
  SparseMatrix *Hess = new SparseMatrix(size, size);
  // derivative wrt y
  for (int i = 0; i < size; ++i) {
    x[i] += eps;
    op->Mult(x, y1);
    C1 = op->get_Plasma_Current();
    f1 = op->compute_obj(x);
    df1 = op->compute_grad_obj(x);
    
    x[i] -= eps;
    op->Mult(x, y2);
    C2 = op->get_Plasma_Current();
    f2 = op->compute_obj(x);
    df2 = op->compute_grad_obj(x);
    
    add(1.0 / eps, y1, -1.0 / eps, y2, y2);
    add(1.0 / eps, *df1, -1.0 / eps, *df2, *df2);

    
    for (int j = 0; j < size; ++j) {
      if (y2[j] != 0) {
        Mat->Add(j, i, y2[j]);
      }
      if ((*df2)[j] != 0.0) {
        Hess->Add(j, i, (*df2)[j]);
      }
    }
    Cy[i] = (C1 - C2) / eps;
    fy[i] = (f1 - f2) / eps;
  }
  Mat->Finalize();
  Hess->Finalize();

  SparseMatrix *By = dynamic_cast<SparseMatrix *>(&op->GetGradient(x));
  Vector Cy_ = op->get_Plasma_Vec();
  Vector* g = op->compute_grad_obj(x);
  Vector fy_test(size);
  SparseMatrix* d2f = op->compute_hess_obj(x);
  fy_test = *g;

  printf("\ndf/dy\n");
  for (int i = 0; i < size; ++i) {
    if ((fy[i] != 0) || (fy_test[i] != 0)) {
      printf("%d: A=%e, B=%e, Diff=%e\n", i, fy[i], fy_test[i], fy[i]-fy_test[i]);
    }
  }
  SparseMatrix *Diff_ = Add(1.0, *Hess, -1.0, *d2f);
  printf("\nd2f/dy2\n");
  PrintMatlab(Diff_, d2f, Hess);

  SparseMatrix *Diff = Add(1.0, *Mat, -1.0, *By);
  printf("\ndB/dy\n");
  PrintMatlab(Diff, By, Mat);

  printf("\ndC/dy\n");
  for (int i = 0; i < size; ++i) {
    if ((Cy[i] != 0) || (Cy[i] != 0)) {
      printf("%d: A=%e, B=%e, Diff=%e\n", i, Cy[i], Cy_[i], Cy[i]-Cy_[i]);
    }
  }

  // Test Ca and Ba
  double *alpha_bar = op->get_alpha_bar();
  
  op->Mult(x, y1);
  C1 = op->get_Plasma_Current();

  *alpha_bar += eps;
  op->Mult(x, y2);
  C2 = op->get_Plasma_Current();
  *alpha_bar -= eps;

  SparseMatrix *By2 = dynamic_cast<SparseMatrix *>(&op->GetGradient(x));
  double Ca = op->get_Alpha_Term();
  
  double Ca_test = (C2 - C1) / eps;
  printf("\ndC/dalpha\n");
  printf("Ca: A=%e, B=%e, Diff=%e\n", Ca, Ca_test, Ca-Ca_test);

  printf("\ndB/dalpha\n");
  Vector Ba = op->get_B_alpha();
  add(1.0 / eps, y2, -1.0 / eps, y1, y2);
  for (int i = 0; i < size; ++i) {
    if ((y2[i] != 0) || (Ba[i] != 0)) {
      printf("%d: A=%e, B=%e, Diff=%e\n", i, y2[i], Ba[i], y2[i]-Ba[i]);
    }
  }
  

  // Mat->PrintMatlab();
  // Cy.Print();
  
  return Mat;
  
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



void DefineRHS(PlasmaModelBase & model, double & rho_gamma,
               Mesh & mesh, map<int, double> & coil_current_values,
               ExactCoefficient & exact_coefficient,
               ExactForcingCoefficient & exact_forcing_coeff, LinearForm & coil_term,
               SparseMatrix * F) {
  /*
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
  Vector coil_current(attribs.Max());
  coil_current = 0.0;
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
      coil_current(attrib-1) = coil_current_values[attrib];

      //
      Vector pw_vector(attribs.Max());
      pw_vector = 0.0;
      pw_vector(attrib-1) = 1.0;
      PWConstCoefficient pw_coeff(pw_vector);
      LinearForm lf(fespace);
      lf.AddDomainIntegrator(new DomainLFIntegrator(pw_coeff));
      lf.Assemble();
      double area = lf(ones);
      // cout << area << endl;
      for (int j = 0; j < ndof; ++j) {
        if (lf[j] != 0) {
          F->Set(j, current_counter, lf[j] / area);
        }
      }
      ++current_counter;
    }
  }
  F->Finalize();
  // F->PrintMatlab();

  exact_forcing_coeff.set_coil_current(&coil_current);

  // note: current contribution comes from F above, this is no longer necessary
  // // add contribution from currents into rhs
  // PWConstCoefficient coil_current_pw(coil_current);
  // if (false) {
  //   coil_term.AddDomainIntegrator(new DomainLFIntegrator(coil_current_pw));
  // }

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

  // GridFunction u_ex_(fespace);
  // const char *data_file = "separated_file.data";
  // InitialCoefficient init_coeff = read_data_file(data_file, true);
  // u_ex_.ProjectCoefficient(init_coeff);
  // coil_term -= u_ex_;

  // GridFunction out(fespace);
  // out = coil_term;
  
}

void DefineLHS(PlasmaModelBase & model, double rho_gamma, BilinearForm & diff_operator) {
   // Set up the bilinear form diff_operator corresponding to the diffusion integrator


   DiffusionIntegratorCoefficient diff_op_coeff(&model);
   if (true) {
     diff_operator.AddDomainIntegrator(new DiffusionIntegrator(diff_op_coeff));
   }

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


void Solve(FiniteElementSpace & fespace, SysOperator & op, PlasmaModelBase *model, GridFunction & x, int & kdim,
           int & max_newton_iter, int & max_krylov_iter,
           double & newton_tol, double & krylov_tol, double & ur_coeff,
           vector<Vector> *alpha_coeffs, vector<Array<int>> *J_inds,
           double & Ip, int N_control, int do_control) {
  cout << "size: " << alpha_coeffs->size() << endl;

  GridFunction dx(&fespace);
  GridFunction res(&fespace);
  LinearForm out_vec(&fespace);
  dx = 0.0;

  bool add_alpha = true;
  bool reduce = true;

  GridFunction psi_r(&fespace);
  GridFunction psi_z(&fespace);
    
  FieldCoefficient BrCoeff(&x, &psi_r, &psi_z, model, fespace, 0);
  FieldCoefficient BpCoeff(&x, &psi_r, &psi_z, model, fespace, 1);
  FieldCoefficient BzCoeff(&x, &psi_r, &psi_z, model, fespace, 2);
  GridFunction Br_field(&fespace);
  GridFunction Bp_field(&fespace);
  GridFunction Bz_field(&fespace);

  // Save data in the VisIt format
  VisItDataCollection visit_dc("gs", fespace.GetMesh());
  visit_dc.RegisterField("psi", &x);
  visit_dc.RegisterField("Br", &Br_field);
  visit_dc.RegisterField("Bp", &Bp_field);
  visit_dc.RegisterField("Bz", &Bz_field);
  
  if (do_control) {
    // solve the optimization problem of determining currents to fit desired plasma shape
    // + K  dx +      + By^T dp = - opt_res
    //         + H du - F ^T dp = - reg_res
    // + By dx - F du +         = - eq_res
    //
    // 0-beta problem
    // + K   dx +      + By^T dp +       + Cy dl = b1 = - opt_res
    //          + H du - F ^T dp +       +       = b2 = - reg_res
    // + By  dx - F du +         + Ba da +       = b3 = - eq_res
    // +        +      + Ba^T dp +       + Ca dl = b4
    // + CyT dx +      +         + Ca da +       = b5

    // relevant matrices
    // SparseMatrix * K = op.GetK();
    SparseMatrix * F = op.GetF();
    SparseMatrix * H = op.GetH();

    // solution at previous iteration
    Vector * uv = op.get_uv();
    printf("currents: [");
    for (int i = 0; i < uv->Size(); ++i) {
      printf("%.3e ", (*uv)[i]);
    }
    printf("]\n");
    
    // Vector * g = op.get_g();
    GridFunction pv(&fespace);
    pv = 0.0;
    double lv = 0.0;

    GridFunction uv_prev(&fespace);
    // uv_prev.Load("exact.gf");
    
    // placeholder for rhs
    GridFunction eq_res(&fespace);
    Vector reg_res(uv->Size());
    GridFunction opt_res(&fespace);

    GridFunction b1(&fespace);
    Vector b2(uv->Size());
    GridFunction b3(&fespace);
    b1 = 0.0;
    b2 = 0.0;
    b3 = 0.0;

    GridFunction b1p(&fespace);
    Vector b2p(uv->Size());
    GridFunction b3p(&fespace);
    b1p = 0.0;
    b2p = 0.0;
    b3p = 0.0;

    double alpha_res;
    double *alpha_bar = op.get_alpha_bar();
    cout << "alpha " << *alpha_bar << endl;

    // define block structure of matrix
    int n_off = 6;
    if (reduce) {
      n_off = 5;
    }
    Array<int> row_offsets(n_off);
    Array<int> col_offsets(n_off);
    if (reduce) {
      row_offsets[0] = 0;
      row_offsets[1] = pv.Size();
      row_offsets[2] = row_offsets[1] + pv.Size();
      row_offsets[3] = row_offsets[2] + 1;
      row_offsets[4] = row_offsets[3] + 1;
    } else {
      row_offsets[0] = 0;
      row_offsets[1] = pv.Size();
      row_offsets[2] = row_offsets[1] + uv->Size();
      row_offsets[3] = row_offsets[2] + pv.Size();
      row_offsets[4] = row_offsets[3] + 1;
      row_offsets[5] = row_offsets[4] + 1;
    }
    // newton iterations
    double error_old;
    double error;
    LinearForm solver_error(&fespace);
    for (int i = 0; i < max_newton_iter; ++i) {
      // print out objective function and regularization

      // -b3 = eq_res = B(y^n) - F u^n
      op.Mult(x, eq_res);

      if (i == 0) {
        eq_res.Save("initial_eq_res.gf");
      }

      SparseMatrix *By = dynamic_cast<SparseMatrix *>(&op.GetGradient(x));
      double C = op.get_Plasma_Current();
      cout << "plasma current " << C << endl;
      double Ca = op.get_Alpha_Term();
      Vector Cy = op.get_Plasma_Vec();
      Vector Ba = op.get_B_alpha();
      double psi_x = op.get_psi_x();
      double psi_ma = op.get_psi_ma();
      double* x_x = op.get_x_x();
      double* x_ma = op.get_x_ma();

      printf("psi_x:  %e, location: (%e, %e)\n", psi_x, x_x[0], x_x[1]);
      printf("psi_ma: %e, location: (%e, %e)\n", psi_ma, x_ma[0], x_ma[1]);

      BrCoeff.set_psi_vals(psi_x, psi_ma);
      BpCoeff.set_psi_vals(psi_x, psi_ma);
      BzCoeff.set_psi_vals(psi_x, psi_ma);

      SparseMatrix * K = op.compute_hess_obj(x);
      Vector * g = op.compute_grad_obj(x);

      // -b1 = opt_res = K y^n + B_y^T p^n + C_y l^n - g
      K->Mult(x, opt_res);
      By->AddMultTranspose(pv, opt_res);
      add(opt_res, -1.0, *g, opt_res);
      // add(opt_res, -psi_x, *g, opt_res);
      if (add_alpha) {
        add(opt_res, lv, Cy, opt_res);
      }

      // -b2 = reg_res = H u^n - F^T p^n
      H->Mult(*uv, reg_res);
      F->AddMultTranspose(pv, reg_res, -1.0);

      if (add_alpha) {
        cout << "alpha: " << *alpha_bar << endl;
      }

      // b4 = B_a^T p^n + C_a l^n
      double b4 = Ba * pv + Ca * lv;
      b4 *= -1.0;

      // b5 = C - Ip
      // if (i < 5) {
      //   Ip = 1e5;
      // } else if (i < 10){
      //   Ip = 1e6;
      // } else {
      //   Ip = 1e7;
      // }
      double b5= C - Ip;
      b5 *= -1.0;

      // get max errors for residuals
      error = GetMaxError(eq_res);
      double max_opt_res = GetMaxError(opt_res);
      double max_reg_res = GetMaxError(reg_res);

      // objective + regularization
      // x^T K x - g^T x + C + uv^T H uv
      double true_obj = K->InnerProduct(x, x) - ((*g) * x) + N_control * (6.864813e-02) * (6.864813e-02);
      double regularization = (H->InnerProduct(*uv, *uv));

      double test_obj = op.compute_obj(x);
      // true_obj = op.compute_obj(x) / N_control;
      true_obj = op.compute_obj(x);
      printf("objective test: %e, %e, %e\n", true_obj, test_obj, true_obj - test_obj);
      
      if (i == 0) {
        printf("i: %3d, nonl_res: %.3e, ratio %9s, res: [%.3e, %.3e, %.3e, %.3e], loss: %.3e, obj: %.3e, reg: %.3e\n",
               i, error, "", max_opt_res, max_reg_res, abs(b4), abs(b5), true_obj+regularization, true_obj, regularization);
      } else {
        printf("i: %3d, nonl_res: %.3e, ratio %.3e, res: [%.3e, %.3e, %.3e, %.3e], loss: %.3e, obj: %.3e, reg: %.3e\n",
               i, error, error_old / error, max_opt_res, max_reg_res, abs(b4), abs(b5), true_obj+regularization,
               true_obj, regularization);
      }
      error_old = error;
      printf("\n");

      if (error < newton_tol) {
        break;
      }

      // prepare block matrix
      SparseMatrix *FT = Transpose(*F);
      SparseMatrix *mF = Add(-1.0, *F, 0.0, *F);
      SparseMatrix *mFT = Transpose(*mF);
      SparseMatrix *ByT = Transpose(*By);

      SparseMatrix *invH = new SparseMatrix(uv->Size(), uv->Size());
      for (int j = 0; j < uv->Size(); ++j) {
        invH->Set(j, j, 1.0 / (*H)(j, j));
      }
      invH->Finalize();

      SparseMatrix *invDiagByT = new SparseMatrix(pv.Size(), pv.Size());
      for (int j = 0; j < pv.Size(); ++j) {
        invDiagByT->Set(j, j, 1.0 / (*ByT)(j, j));
      }
      invDiagByT->Finalize();

      SparseMatrix *invHFT = Mult(*invH, *FT);
      SparseMatrix *mFinvHFT = Mult(*mF, *invHFT);
      SparseMatrix *FinvH = Mult(*F, *invH);

      // SparseMatrix *M1 = Mult(*mFinvHFT, *invDiagByT);
      SparseMatrix *M1 = Mult(*mFinvHFT, *ByT);
      SparseMatrix *M2 = Mult(*M1, *K);
      SparseMatrix *S = Add(1.0, *By, -1.0, *M2);

      SparseMatrix *Ba_;
      SparseMatrix *Cy_;
      SparseMatrix *Ca_;
      SparseMatrix *inv_Ca_;
      Ba_ = new SparseMatrix(pv.Size(), 1);
      Cy_ = new SparseMatrix(pv.Size(), 1);
      Ca_ = new SparseMatrix(1, 1);
      inv_Ca_ = new SparseMatrix(1, 1);
      Ca_->Add(0, 0, Ca);
      inv_Ca_->Add(0, 0, 1.0 / Ca);
      for (int j = 0; j < pv.Size(); ++j) {
        if (Ba[j] != 0) {
          Ba_->Add(j, 0, Ba[j]);
        }
        if (Cy[j] != 0) {
          Cy_->Add(j, 0, Cy[j]);
        }
      }
      Ca_->Finalize();
      inv_Ca_->Finalize();
      Ba_->Finalize();
      Cy_->Finalize();
      SparseMatrix *CyT_ = Transpose(*Cy_);
      SparseMatrix *BaT_ = Transpose(*Ba_);
      
      // SparseMatrix *BaCyT;
      // SparseMatrix *CyBaT;
      // BaCyT = new SparseMatrix(pv.Size(), pv.Size());
      // CyBaT = new SparseMatrix(pv.Size(), pv.Size());
      // for (int j = 0; j < pv.Size(); ++j) {
      //   for (int k = 0; k < pv.Size(); ++k) {
      //     if ((Ba[j] * Cy[k]) != 0) {
      //       BaCyT->Add(j, k, Ba[j] * Cy[k]);
      //     }
      //     if (Cy[j] * Ba[k] != 0) {
      //       CyBaT->Add(j, k, Cy[j] * Ba[k]);
      //     }
      //   }
      // }
      // BaCyT->Finalize();
      // CyBaT->Finalize();
      // SparseMatrix *TopLeft  = Add(-1.0 / Ca, *CyBaT, 1.0, *ByT);
      // SparseMatrix *BotRight = Add(-1.0 / Ca, *BaCyT, 1.0, *By);
      
      // the system
      //        + (ByT - 1 / Ca Cy BaT) dp + K dx = b1p = b1 - 1 / Ca Cy b4
      // + H du - F ^T dp                         = b2p = b2
      // - F du + (By  - 1 / Ca Ba CyT) dx        = b3p = b3 - 1 / Ca Ba b5
      //
      // is equivalent to
      //                              B^T dp* + (K - 1 / Ca Cy CyT) dx* = b1*
      //                                                            du* = Hinv b2*
      // (- 1 / Ca Ba Ba^T - F Hinv F^T ) dp* +                   B dx* = b3*
      //
      // where
      // b1* = b1p
      // b2* = b2p
      // b3* = b3p + F Hinv b2p
      // 
      // dx = dx*
      // dp = dp*
      // du = du* - Hinv FT dp*

      // get b1, b2, b3
      b1 = opt_res; b1 *= -1.0;
      b2 = reg_res; b2 *= -1.0;
      b3 = eq_res;  b3 *= -1.0;

      // get b1p, b2p, b3p
      // add(b1, -b4 / Ca, Cy, b1p);
      // b2p = b2;
      // add(b3, -b5 / Ca, Ba, b3p);

      BlockOperator Mat(row_offsets);
      if (reduce) {
        Mat.SetBlock(0, 0, By);
        Mat.SetBlock(0, 1, mFinvHFT);
        if (add_alpha) {
          Mat.SetBlock(0, 2, Ba_);
        }
      
        Mat.SetBlock(1, 0, K);
        Mat.SetBlock(1, 1, ByT);
        if (add_alpha) {
          Mat.SetBlock(1, 3, Cy_);
        }

        if (add_alpha) {
          Mat.SetBlock(2, 0, CyT_);
          Mat.SetBlock(2, 2, Ca_);

          Mat.SetBlock(3, 1, BaT_);
          Mat.SetBlock(3, 3, Ca_);
        } else {
          Mat.SetBlock(2, 2, Ca_);
          Mat.SetBlock(3, 3, Ca_);
        }
      } else {
        Mat.SetBlock(0, 0, By);
        Mat.SetBlock(0, 1, mF);
        Mat.SetBlock(0, 3, Ba_);
      
        Mat.SetBlock(1, 1, H);
        Mat.SetBlock(1, 2, mFT);
      
        Mat.SetBlock(2, 0, K);
        Mat.SetBlock(2, 2, ByT);
        Mat.SetBlock(2, 4, Cy_);
      
        Mat.SetBlock(3, 0, CyT_);
        Mat.SetBlock(3, 3, Ca_);

        Mat.SetBlock(4, 2, BaT_);
        Mat.SetBlock(4, 4, Ca_);
      }
      
      // preconditioner
      BlockDiagonalPreconditioner Prec(row_offsets);

      Solver *inv_ByT, *inv_By;
      int its = 60;
      inv_ByT = new GSSmoother(*ByT, 0, its);
      inv_By = new GSSmoother(*By, 0, its);

      if (reduce) {
        Prec.SetDiagonalBlock(0, inv_By);
        Prec.SetDiagonalBlock(1, inv_ByT);
        Prec.SetDiagonalBlock(2, inv_Ca_);
        Prec.SetDiagonalBlock(3, inv_Ca_);
      } else {
        Prec.SetDiagonalBlock(0, inv_By);
        Prec.SetDiagonalBlock(1, invH);
        Prec.SetDiagonalBlock(2, inv_ByT);
        Prec.SetDiagonalBlock(3, inv_Ca_);
        Prec.SetDiagonalBlock(4, inv_Ca_);
      }
      // create block rhs vector
      BlockVector Vec(row_offsets);

      if (reduce) {
        Vec.GetBlock(0) = b3;
        FinvH->AddMult(b2, Vec.GetBlock(0));
        Vec.GetBlock(1) = b1;
        Vec.GetBlock(2) = b5;
        Vec.GetBlock(3) = b4;
      } else {
        Vec.GetBlock(0) = b3;
        Vec.GetBlock(1) = b2;
        Vec.GetBlock(2) = b1;
        Vec.GetBlock(3) = b5;
        Vec.GetBlock(4) = b4;
      }
      // add(1.0, b1p, 0.0, b1p, Vec.GetBlock(0));
      // add(1.0, b3p, 0.0, b3p, Vec.GetBlock(1));
      // FinvH->AddMult(b2p, Vec.GetBlock(1));

      BlockVector dx(row_offsets);
      dx = 0.0;

      // solver
      GMRESSolver solver;
      // solver.SetAbsTol(1e-16);
      // solver.SetRelTol(1e-9);
      solver.SetAbsTol(1e-16);
      solver.SetRelTol(krylov_tol);
      solver.SetMaxIter(max_krylov_iter);
      solver.SetOperator(Mat);
      solver.SetPreconditioner(Prec);
      solver.SetKDim(kdim);
      solver.SetPrintLevel(0);

      solver.Mult(Vec, dx);
      if (solver.GetConverged())
        {
          std::cout << "GMRES converged in " << solver.GetNumIterations()
                    << " iterations with a residual norm of "
                    << solver.GetFinalNorm() << ".\n";
        }
      else
        {
          std::cout << "GMRES did not converge in " << solver.GetNumIterations()
                    << " iterations. Residual norm is " << solver.GetFinalNorm()
                    << ".\n";
        }

      if (reduce) {
        x += dx.GetBlock(0);
        pv += dx.GetBlock(1);
        if (add_alpha) {
          *alpha_bar += dx.GetBlock(2)[0];
          lv += dx.GetBlock(3)[0];
        }

        invHFT->AddMult(dx.GetBlock(1), *uv);
        invH->AddMult(b2, *uv);
      } else {
        x += dx.GetBlock(0);
        *uv += dx.GetBlock(1);
        pv += dx.GetBlock(2);
        *alpha_bar += dx.GetBlock(3)[0];
        lv += dx.GetBlock(4)[0];
      }

      printf("currents: [");
      for (int i = 0; i < uv->Size(); ++i) {
        printf("%.3e ", (*uv)[i]);
      }
      printf("]\n");
      
      x.Save("xtmp.gf");
      char name[60];
      sprintf(name, "xtmp%d.gf", i);
      x.Save(name);
      Br_field.Save("Br.gf");
      Bp_field.Save("Bp.gf");
      Bz_field.Save("Bz.gf");

      // compute magnetic field
      x.GetDerivative(1, 0, psi_r);
      x.GetDerivative(1, 1, psi_z);
      Br_field.ProjectCoefficient(BrCoeff);
      Bp_field.ProjectCoefficient(BpCoeff);
      Bz_field.ProjectCoefficient(BzCoeff);

      visit_dc.Save();
      

    }
    // op.Mult(x, out_vec);
    // error = GetMaxError(out_vec);
    // printf("\n\n********************************\n");
    // printf("final max residual: %.3e, ratio %.3e\n", error, error_old / error);
    // printf("********************************\n\n");



     
  } else {
    // for given currents, solve the GS equations

    double error_old;
    double error;
    LinearForm solver_error(&fespace);
    for (int i = 0; i < max_newton_iter; ++i) {

      op.Mult(x, out_vec);
      error = GetMaxError(out_vec);
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
      SparseMatrix *Mat = dynamic_cast<SparseMatrix *>(&op.GetGradient(x));

      // cout << "By" << "i" << i << endl;
      // Mat->PrintMatlab();

      GSSmoother M(*Mat);
      // printf("iter: %d, tol: %e, kdim: %d\n", max_krylov_iter, krylov_tol, kdim);
      int gmres_iter = max_krylov_iter;
      double gmres_tol = krylov_tol;
      int gmres_kdim = kdim;
      GMRES(*Mat, dx, out_vec, M, gmres_iter, gmres_kdim, gmres_tol, 0.0, 0);
      printf("gmres iters: %d, gmres err: %e\n", gmres_iter, gmres_tol);

      // add(dx, ur_coeff - 1.0, dx, dx);
      x -= dx;

      x.Save("xtmp.gf");
      dx.Save("dx.gf");
      GridFunction err(&fespace);
      err = out_vec;
      err.Save("res.gf");

      // cout << "x" << "i" << i << endl;
      // x.Print();
      // x.Print();
      // if (true) {
      //   return;
      // }

    }
    op.Mult(x, out_vec);
    error = GetMaxError(out_vec);
    printf("\n\n********************************\n");
    printf("final max residual: %.3e, ratio %.3e\n", error, error_old / error);
    printf("********************************\n\n");
  }
  
}


double gs(const char * mesh_file, const char * data_file, int order, int d_refine,
          int model_choice,
          double & alpha, double & beta, double & gamma, double & mu, double & Ip,
          double & r0, double & rho_gamma, int max_krylov_iter, int max_newton_iter,
          double & krylov_tol, double & newton_tol,
          double & c1, double & c2, double & c3, double & c4, double & c5, double & c6, double & c7,
          double & c8, double & c9, double & c10, double & c11,
          double & ur_coeff,
          int do_control, int N_control, double & weight_solenoids, double & weight_coils,
          bool do_manufactured_solution, bool do_initial) {

   map<int, double> coil_current_values;
   // center solenoids
   coil_current_values[832] = c1 / 2.71245;
   coil_current_values[833] = c2 / 2.7126;
   coil_current_values[834] = c3 / 5.4249;
   coil_current_values[835] = c4 / 2.7126;
   coil_current_values[836] = c5 / 2.71245;
   // poloidal flux coils
   coil_current_values[837] = c6 / 2.0 / 1.5;
   coil_current_values[838] = c7 / 2.0 / 1.5;
   coil_current_values[839] = c8 / 2.0 / 1.5;
   coil_current_values[840] = c9 / 2.0 / 1.5;
   coil_current_values[841] = c10 / 2.0 / 1.5;
   coil_current_values[842] = c11 / 2.0 / 1.5;

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

   // Read the mesh from the given mesh file, and refine "d_refine" times uniformly.
   Mesh mesh(mesh_file);
   for (int i = 0; i < d_refine; ++i) {
     mesh.UniformRefinement();
   }
   mesh.Save("mesh.mesh");
   if (do_initial) {
     mesh.Save("initial.mesh");
   }
   
   // save options in model
   // alpha: multiplier in \bar{S}_{ff'} term
   // beta: multiplier for S_{p'} term
   // gamma: multiplier for S_{ff'} term
   const char *data_file_ = "fpol_pres_ffprim_pprime.data";
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
      Define RHS
      -------------------------------------------------------------------------------------------
      -------------------------------------------------------------------------------------------
      -------------------------------------------------------------------------------------------
    */
   // Set up the contribution from the coils
  LinearForm coil_term(&fespace);
  int ndof = fespace.GetNDofs();
  SparseMatrix * F;
  F = new SparseMatrix(ndof, num_currents);

  DefineRHS(model, rho_gamma, mesh, coil_current_values, exact_coefficient, exact_forcing_coeff, coil_term, F);
  cout << F->ActualWidth() << endl;
  cout << uv_currents.Size() << endl;
  // if (true) {
  //   return 0.0;
  // }

  SparseMatrix * H;
  H = new SparseMatrix(num_currents, num_currents);
  for (int i = 0; i < num_currents; ++i) {
    if (i < 5) {
      H->Set(i, i, weight_coils);
    } else {
      H->Set(i, i, weight_solenoids);
    }
  }
  H->Finalize();

   /* 
      -------------------------------------------------------------------------------------------
      -------------------------------------------------------------------------------------------
      -------------------------------------------------------------------------------------------
      Define LHS
      -------------------------------------------------------------------------------------------
      -------------------------------------------------------------------------------------------
      -------------------------------------------------------------------------------------------
    */
   BilinearForm diff_operator(&fespace);
   DefineLHS(model, rho_gamma, diff_operator);

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
   GridFunction x(&fespace);
   GridFunction u(&fespace);
   SparseMatrix * K;
   Vector g;
   vector<Vector> *alpha_coeffs;
   vector<Array<int>> *J_inds;
   if (do_manufactured_solution) {
     u.ProjectCoefficient(exact_coefficient);
     u.Save("exact.gf");
   } else {
     InitialCoefficient init_coeff = read_data_file(data_file);

     if (do_control) {
       init_coeff.compute_QP(N_control, &mesh, &fespace);
       K = init_coeff.compute_K();
       g = init_coeff.compute_g();
       alpha_coeffs = init_coeff.get_alpha();
       cout << "size: " << alpha_coeffs->size() << endl;
     
       J_inds = init_coeff.get_J();
     }
     // K->PrintMatlab();
     // g.Print();

     u.ProjectCoefficient(init_coeff);

     if (!do_initial) {
       ifstream ifs("interpolated.gf");
       GridFunction lgf(&mesh, ifs);
       u = lgf;
     }

     u.Save("initial.gf");

     // Mesh initial_mesh("initial.mesh");
     // ifstream ifs("initial_guess.gf");

     // GridFunctionCoefficient init_coeff_gf(&lgf);
     // u.ProjectCoefficient(init_coeff_gf);
    
     // H1_FECollection initial_fec(order, initial_mesh.Dimension());
     // FiniteElementSpace initial_fespace(&initial_mesh, &initial_fec);

     // GridTransfer gt(&initial_fespace, &fespace);
     // Operator transfer_op = gt.ForwardOperator();
     
     // BilinearForm a(&fespace);
     // MixedBilinearForm a_mixed(&initial_fespace, &fespace);

     // ConstantCoefficient one(1.0);
     // a.AddDomainIntegrator(new MassIntegrator(one));
     // a_mixed.AddDomainIntegrator(new MixedScalarMassIntegrator(one));

     // a.Assemble();
     // a.Finalize();
     // a_mixed.Assemble();
     // a_mixed.Finalize();

     // GridFunction rhs(&fespace);
     // a_mixed.Mult(lgf, rhs);

     // CGSolver cg;
     // cg.SetRelTol(1e-12);
     // cg.SetMaxIter(1000);
     // cg.SetPrintLevel(1);
     // cg.SetOperator(a);

     // Array<int> ess_tdof_list; // empty
     // OperatorJacobiSmoother Jacobi(a, ess_tdof_list);
     // cg.SetPreconditioner(Jacobi);

   }

   cout << "size: " << alpha_coeffs->size() << endl;
   x = u;
   // now we have an initial guess: x
   // x.Save("initial_guess.gf");
   bool include_plasma = true;
   if (do_initial) {
     include_plasma = false;
   }
   SysOperator op(&diff_operator, &coil_term, &model, &fespace, &mesh, attr_lim, &u, F, &uv_currents, H, K, &g, alpha_coeffs, J_inds, &alpha, include_plasma);

   // test_grad(&op, x, &fespace);
   // if (true) {
   //   return 0.0;
   // }
   
   Solve(fespace, op, &model, x, kdim, max_newton_iter, max_krylov_iter, newton_tol, krylov_tol, ur_coeff,
         alpha_coeffs, J_inds, Ip, N_control, do_control);
   if (do_initial) {
     x.Save("initial_guess.gf");
     printf("Saved solution to initial_guess.gf\n");
     printf("Saved mesh to mesh.mesh\n");
     printf("glvis -m mesh.mesh -g initial_guess.gf\n");
   } else {
     x.Save("final.gf");
     printf("Saved solution to final.gf\n");
     printf("Saved mesh to mesh.mesh\n");
     printf("glvis -m mesh.mesh -g final.gf\n");
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
     diff.Save("error.gf");
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
