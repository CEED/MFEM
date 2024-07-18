#ifndef CURLCURLINTEGRATOR
#define CURLCURLINTEGRATOR

#include "mfem.hpp"
using namespace mfem;
using namespace std;

//a special coefficient to compute (grad B)^T - (div B)I
class BmatCoeff : public MatrixCoefficient
{
private:
    GridFunction *B;
public:
    BmatCoeff(GridFunction *B_, int dim_=3) : MatrixCoefficient(dim_), B(B_) {
        MFEM_ASSERT(B->VectorDim() == 3 && dim_ == 3, "invalid BmatCoeff dim");
    };
    virtual void Eval(DenseMatrix &K, ElementTransformation &T, const IntegrationPoint &ip);
    virtual ~BmatCoeff() {};
};

class SpecialVectorCurlCurlIntegrator: public BilinearFormIntegrator
{
private:
    VectorFunctionCoefficient *BC;
    BmatCoeff *BmatC;
    Vector shape, divshape, Bvec, tmp, BdotGrad;
    DenseMatrix dshape, dshapedxt, gshape, recmat;
    DenseMatrix Bmat, partrecmat, partrecmat2;

public:
    SpecialVectorCurlCurlIntegrator(VectorFunctionCoefficient &BCoeff, BmatCoeff &bmatcoeff)
    { BC = &BCoeff; BmatC=&bmatcoeff;}
    virtual void AssembleElementMatrix(const FiniteElement &el,
                                       ElementTransformation &Trans,
                                       DenseMatrix &elmat);
};

class VectorGradDivIntegrator: public BilinearFormIntegrator
{
private:
    VectorFunctionCoefficient *BC;
    Vector shape, divshape, Bvec;
    DenseMatrix dshape, dshapedxt, gshape;

public:
    VectorGradDivIntegrator(VectorFunctionCoefficient &BCoeff)
    { BC = &BCoeff;}
    virtual void AssembleElementMatrix(const FiniteElement &el,
                                       ElementTransformation &Trans,
                                       DenseMatrix &elmat);
};

class SpecialVectorDiffusionIntegrator: public BilinearFormIntegrator
{
private:
    VectorFunctionCoefficient *BC;
    BmatCoeff *BmatC;
    Vector shape, divshape, Bvec, BdotGrad;
    DenseMatrix dshape, dshapedxt, gshape, recmat, recmatT;
    DenseMatrix Bmat, partrecmat, partrecmat2;

public:
    SpecialVectorDiffusionIntegrator(VectorFunctionCoefficient &BCoeff)
    { BC = &BCoeff;}
    virtual void AssembleElementMatrix(const FiniteElement &el,
                                       ElementTransformation &Trans,
                                       DenseMatrix &elmat);
};
#endif