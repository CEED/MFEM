#ifndef INITIAL_COEFFICIENT
#define INITIAL_COEFFICIENT

#include "mfem.hpp"
#include "exact.hpp"
#include <set>
using namespace mfem;
using namespace std;


class InitialCoefficient : public Coefficient
{
private:
  double **psizr;
  double *rbbbs;
  double *zbbbs;
  double psix;
  Vector cv;
  int nbbbs;
  int N_control;
  double r0;
  double r1;
  double z0;
  double z1;
  double dr;
  double dz;
  int nz;
  int nr;
  set<int> plasma_inds;
  bool mask_plasma = false;
  bool use_manufactured = false;
  ExactCoefficient exact_coeff;
public:
  InitialCoefficient(double **psizr_, double *rbbbs_, double *zbbbs_, double r0_, double r1_, double z0_, double z1_, int nz_, int nr_, int nbbbs_, double psix_) : psizr(psizr_), rbbbs(rbbbs_), zbbbs(zbbbs_), r0(r0_), r1(r1_), z0(z0_), z1(z1_), nz(nz_), nr(nr_), nbbbs(nbbbs_), psix(psix_) {
    dr = (r1 - r0) / ((nr - 1) * 1.0);
    dz = (z1 - z0) / ((nz - 1) * 1.0);
  }
  InitialCoefficient(ExactCoefficient exact_coeff_) : exact_coeff(exact_coeff_) {
    use_manufactured = true;
  }
  void SetPlasmaInds(set<int> & plasma_inds_) {
    plasma_inds = plasma_inds_;
    mask_plasma = true;
  };

  void compute_QP(int N_control, Mesh * mesh, FiniteElementSpace * fes);

  SparseMatrix* compute_K();
  Vector compute_g();
  
  virtual double Eval(ElementTransformation &T, const IntegrationPoint &ip);
  virtual ~InitialCoefficient() { }
};

InitialCoefficient read_data_file(const char *data_file);
InitialCoefficient from_manufactured_solution();


#endif
