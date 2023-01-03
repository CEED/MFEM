#include "cf_elast_solver.hpp"

using namespace mfem;

CFElasticitySolver::CFElasticitySolver(mfem::ParMesh* mesh_, int vorder)
{
    pmesh=mesh_;
    int dim=pmesh->Dimension();
    vfec=new H1_FECollection(vorder,dim);
    vfes=new mfem::ParFiniteElementSpace(pmesh,vfec,dim, Ordering::byVDIM);

    fdisp.SetSpace(vfes); fdisp=0.0;
    adisp.SetSpace(vfes); adisp=0.0;

    sol.SetSize(vfes->GetTrueVSize()); sol=0.0;
    rhs.SetSize(vfes->GetTrueVSize()); rhs=0.0;
    adj.SetSize(vfes->GetTrueVSize()); adj=0.0;

    nf=nullptr;
    SetNewtonSolver();
    SetLinearSolver();

    prec=nullptr;
    ls=nullptr;
    ns=nullptr;

    lvforce=nullptr;
    volforce=nullptr;

    level_set_function=nullptr;
    el_markers=nullptr;
}


CFElasticitySolver::~CFElasticitySolver()
{
    delete ns;
    delete prec;
    delete ls;
    delete nf;

    delete vfes;
    delete vfec;

    delete lvforce;

    for(unsigned int i=0;i<materials.size();i++)
    {
        delete materials[i];
    }

    for(auto it=surf_loads.begin();it!=surf_loads.end();it++){
        delete it->second;
    }
}

void CFElasticitySolver::SetNewtonSolver(double rtol, double atol,int miter, int prt_level)
{
    rel_tol=rtol;
    abs_tol=atol;
    max_iter=miter;
    print_level=prt_level;
}

void CFElasticitySolver::SetLinearSolver(double rtol, double atol, int miter)
{
    linear_rtol=rtol;
    linear_atol=atol;
    linear_iter=miter;
}

void CFElasticitySolver::AddDispBC(int id, int dir, double val)
{
    if(dir==0){
        bcx[id]=mfem::ConstantCoefficient(val);
        AddDispBC(id,dir,bcx[id]);
    }
    if(dir==1){
        bcy[id]=mfem::ConstantCoefficient(val);
        AddDispBC(id,dir,bcy[id]);

    }
    if(dir==2){
        bcz[id]=mfem::ConstantCoefficient(val);
        AddDispBC(id,dir,bcz[id]);
    }
    if(dir==4){
        bcx[id]=mfem::ConstantCoefficient(val);
        bcy[id]=mfem::ConstantCoefficient(val);
        bcz[id]=mfem::ConstantCoefficient(val);
        AddDispBC(id,0,bcx[id]);
        AddDispBC(id,1,bcy[id]);
        AddDispBC(id,2,bcz[id]);
    }
}

void CFElasticitySolver::DelDispBC()
{
    bccx.clear();
    bccy.clear();
    bccz.clear();

    bcx.clear();
    bcy.clear();
    bcz.clear();

    ess_tdofv.DeleteAll();
}

void CFElasticitySolver::AddDispBC(int id, int dir, Coefficient &val)
{
    if(dir==0){ bccx[id]=&val; }
    if(dir==1){ bccy[id]=&val; }
    if(dir==2){ bccz[id]=&val; }
    if(dir==4){ bccx[id]=&val; bccy[id]=&val; bccz[id]=&val;}
    if(pmesh->Dimension()==2)
    {
        bccz.clear();
    }
}

void CFElasticitySolver::SetVolForce(double fx, double fy, double fz)
{
    delete lvforce;
    int dim=pmesh->Dimension();
    mfem::Vector ff(dim); ff(0)=fx; ff(1)=fy;
    if(dim==3){ff(2)=fz;}
    lvforce=new mfem::VectorConstantCoefficient(ff);
    volforce=lvforce;

}

void CFElasticitySolver::SetVolForce(mfem::VectorCoefficient& fv)
{
    volforce=&fv;
}

void CFElasticitySolver::FSolve()
{
    // Set the BC
    ess_tdofv.DeleteAll();
    if(nf!=nullptr){delete nf; nf=nullptr;}
    Array<int> ess_tdofx;
    Array<int> ess_tdofy;
    Array<int> ess_tdofz;

    int dim=pmesh->Dimension();
    {
        for(auto it=bccx.begin();it!=bccx.end();it++)
        {
            mfem::Array<int> ess_bdr(pmesh->bdr_attributes.Max());
            ess_bdr=0;
            ess_bdr[it->first -1]=1;
            mfem::Array<int> ess_tdof_list;
            vfes->GetEssentialTrueDofs(ess_bdr,ess_tdof_list,0);
            ess_tdofx.Append(ess_tdof_list);

            mfem::VectorArrayCoefficient pcoeff(dim);
            pcoeff.Set(0, it->second, false);
            fdisp.ProjectBdrCoefficient(pcoeff, ess_bdr);
        }

        //copy tdofsx from velocity grid function
        {
            fdisp.GetTrueDofs(rhs); // use the rhs vector as a tmp vector
            for(int ii=0;ii<ess_tdofx.Size();ii++)
            {
                sol[ess_tdofx[ii]]=rhs[ess_tdofx[ii]];
            }
        }
        ess_tdofv.Append(ess_tdofx);

        for(auto it=bccy.begin();it!=bccy.end();it++)
        {
            mfem::Array<int> ess_bdr(pmesh->bdr_attributes.Max());
            ess_bdr=0;
            ess_bdr[it->first -1]=1;
            mfem::Array<int> ess_tdof_list;
            vfes->GetEssentialTrueDofs(ess_bdr,ess_tdof_list,1);
            ess_tdofy.Append(ess_tdof_list);

            mfem::VectorArrayCoefficient pcoeff(dim);
            pcoeff.Set(1, it->second, false);
            fdisp.ProjectBdrCoefficient(pcoeff, ess_bdr);
        }
        //copy tdofsy from velocity grid function
        {
            fdisp.GetTrueDofs(rhs); // use the rhs vector as a tmp vector
            for(int ii=0;ii<ess_tdofy.Size();ii++)
            {
                sol[ess_tdofy[ii]]=rhs[ess_tdofy[ii]];
            }
        }
        ess_tdofv.Append(ess_tdofy);

        if(dim==3){
            for(auto it=bccz.begin();it!=bccz.end();it++)
            {
                mfem::Array<int> ess_bdr(pmesh->bdr_attributes.Max());
                ess_bdr=0;
                ess_bdr[it->first -1]=1;
                mfem::Array<int> ess_tdof_list;
                vfes->GetEssentialTrueDofs(ess_bdr,ess_tdof_list,2);
                ess_tdofz.Append(ess_tdof_list);

                mfem::VectorArrayCoefficient pcoeff(dim);
                pcoeff.Set(2, it->second, false);
                fdisp.ProjectBdrCoefficient(pcoeff, ess_bdr);
            }

            //copy tdofsz from velocity grid function
            {
                fdisp.GetTrueDofs(rhs); // use the rhs vector as a tmp vector
                for(int ii=0;ii<ess_tdofz.Size();ii++)
                {
                    sol[ess_tdofz[ii]]=rhs[ess_tdofz[ii]];
                }
            }
            ess_tdofv.Append(ess_tdofz);
        }
    }

    //allocate the nf
    if(nf==nullptr)
    {
        nf=new mfem::ParNonlinearForm(vfes);
        //add the integrators
        for(unsigned int i=0;i<materials.size();i++)
        {
            nf->AddDomainIntegrator(new NLElasticityIntegrator(materials[i]) );
        }

        if(volforce!=nullptr){
            nf->AddDomainIntegrator(new NLVolForceIntegrator(volforce));
        }

        //mfem::Array<int> bdre(pmesh->bdr_attributes.Max());

        for(auto it=surf_loads.begin();it!=surf_loads.end();it++){
            nf->AddBdrFaceIntegrator(new NLSurfLoadIntegrator(it->first,it->second));
        }

        for(auto it=load_coeff.begin();it!=load_coeff.end();it++){
            nf->AddBdrFaceIntegrator(new NLSurfLoadIntegrator(it->first,it->second));
        }
    }


    nf->SetEssentialTrueDofs(ess_tdofv);

    //allocate the solvers
    if(ns==nullptr)
    {
        ns=new mfem::NewtonSolver(pmesh->GetComm());
        ls=new mfem::CGSolver(pmesh->GetComm());
        prec=new mfem::HypreBoomerAMG();
        prec->SetSystemsOptions(pmesh->Dimension());
        prec->SetElasticityOptions(vfes);
    }

    //set the parameters
    ns->SetSolver(*ls);
    ns->SetOperator(*nf);
    ns->SetPrintLevel(print_level);
    ns->SetRelTol(rel_tol);
    ns->SetAbsTol(abs_tol);
    ns->SetMaxIter(max_iter);

    ls->SetPrintLevel(print_level);
    ls->SetAbsTol(linear_atol);
    ls->SetRelTol(linear_rtol);
    ls->SetMaxIter(linear_iter);
    ls->SetPreconditioner(*prec);

    prec->SetPrintLevel(print_level);

    //solve the problem
    Vector b;
    ns->Mult(b,sol);
}

void LinIsoElasticityCoefficient::EvalRes(double EE,double nnu,  double* gradu, double* res)
{
    // generated with maple
    double t1,t2,t3,t5,t10,t12,t19,t22,t25,t27,t32,t33,t38,t41,t46,t51;
    t1 = 1.0+nnu;
    t2 = 1/t1/2.0;
    t3 = t2*EE;
    t5 = EE*nnu;
    t10 = 1/(1.0-2.0*nnu)/t1;
    t12 = t10*t5+2.0*t3;
    t19 = (gradu[0]+gradu[4]+gradu[8])*t10*t5/2.0;
    t22 = gradu[4]*t10*t5/2.0;
    t25 = gradu[8]*t10*t5/2.0;
    t27 = gradu[1]+gradu[3];
    t32 = t27*t3/2.0+t2*t27*EE/2.0;
    t33 = gradu[2]+gradu[6];
    t38 = t33*t3/2.0+t2*t33*EE/2.0;
    t41 = gradu[0]*t10*t5/2.0;
    t46 = gradu[5]+gradu[7];
    t51 = t46*t3/2.0+t2*t46*EE/2.0;
    res[0] = gradu[0]*t12/2.0+gradu[0]*t3+t19+t22+t25;
    res[1] = t32;
    res[2] = t38;
    res[3] = t32;
    res[4] = t41+gradu[4]*t12/2.0+gradu[4]*t3+t19+t25;
    res[5] = t51;
    res[6] = t38;
    res[7] = t51;
    res[8] = t41+t22+gradu[8]*t12/2.0+gradu[8]*t3+t19;
}

void LinIsoElasticityCoefficient::Eval(double EE,double nnu, double* CC)
{
    double t1 = 1.0+nnu;
    double t3 = EE/(2.0*t1);
    double t11 = nnu*EE/(t1*(1.0-2.0*nnu));
    double t12 = 2.0*t3+t11;
    CC[0] = t12;
    CC[1] = 0.0;
    CC[2] = 0.0;
    CC[3] = 0.0;
    CC[4] = t11;
    CC[5] = 0.0;
    CC[6] = 0.0;
    CC[7] = 0.0;
    CC[8] = t11;
    CC[9] = 0.0;
    CC[10] = t3;
    CC[11] = 0.0;
    CC[12] = t3;
    CC[13] = 0.0;
    CC[14] = 0.0;
    CC[15] = 0.0;
    CC[16] = 0.0;
    CC[17] = 0.0;
    CC[18] = 0.0;
    CC[19] = 0.0;
    CC[20] = t3;
    CC[21] = 0.0;
    CC[22] = 0.0;
    CC[23] = 0.0;
    CC[24] = t3;
    CC[25] = 0.0;
    CC[26] = 0.0;
    CC[27] = 0.0;
    CC[28] = t3;
    CC[29] = 0.0;
    CC[30] = t3;
    CC[31] = 0.0;
    CC[32] = 0.0;
    CC[33] = 0.0;
    CC[34] = 0.0;
    CC[35] = 0.0;
    CC[36] = t11;
    CC[37] = 0.0;
    CC[38] = 0.0;
    CC[39] = 0.0;
    CC[40] = t12;
    CC[41] = 0.0;
    CC[42] = 0.0;
    CC[43] = 0.0;
    CC[44] = t11;
    CC[45] = 0.0;
    CC[46] = 0.0;
    CC[47] = 0.0;
    CC[48] = 0.0;
    CC[49] = 0.0;
    CC[50] = t3;
    CC[51] = 0.0;
    CC[52] = t3;
    CC[53] = 0.0;
    CC[54] = 0.0;
    CC[55] = 0.0;
    CC[56] = t3;
    CC[57] = 0.0;
    CC[58] = 0.0;
    CC[59] = 0.0;
    CC[60] = t3;
    CC[61] = 0.0;
    CC[62] = 0.0;
    CC[63] = 0.0;
    CC[64] = 0.0;
    CC[65] = 0.0;
    CC[66] = 0.0;
    CC[67] = 0.0;
    CC[68] = t3;
    CC[69] = 0.0;
    CC[70] = t3;
    CC[71] = 0.0;
    CC[72] = t11;
    CC[73] = 0.0;
    CC[74] = 0.0;
    CC[75] = 0.0;
    CC[76] = t11;
    CC[77] = 0.0;
    CC[78] = 0.0;
    CC[79] = 0.0;
    CC[80] = t12;
}

void LinIsoElasticityCoefficient::EvalStress(DenseMatrix &ss, ElementTransformation &T, const IntegrationPoint &ip)
{
    MFEM_ASSERT(ss.Size()==3,"The size of the stress tensor should be set to 3.");
    double EE=E->Eval(T,ip);
    double nnu=nu->Eval(T,ip);
    //evaluate the strain
    EvalStrain(tmpm,T,ip);
    //evaluate the stress

    double mu=EE/(2.0*(1.0+nnu));
    double ll=nnu*EE/((1.0+nnu)*(1.0-2.0*nnu));

    for(int i=0;i<9;i++)
    {
        ss.GetData()[i]=2.0*mu*tmpm.GetData()[i];
    }

    ss(0,0)=ss(0,0)+ll*(tmpm(0,0)+tmpm(1,1)+tmpm(2,2));
    ss(1,1)=ss(1,1)+ll*(tmpm(0,0)+tmpm(1,1)+tmpm(2,2));
    ss(2,2)=ss(2,2)+ll*(tmpm(0,0)+tmpm(1,1)+tmpm(2,2));
}


void LinIsoElasticityCoefficient::EvalStrain(DenseMatrix &ee, ElementTransformation &T, const IntegrationPoint &ip)
{
    MFEM_ASSERT(ee.Size()==3,"The size of the strain tensor should be set to 3.");
    if(disp==nullptr)
    {
        ee=0.0;
    }
    else
    {
        disp->GetVectorGradient(T,tmpg);
        if(disp->VectorDim()==2)
        {
            ee(0,0)=tmpg(0,0);
            ee(0,1)=0.5*(tmpg(1,0)+tmpg(0,1));
            ee(0,2)=0.0;

            ee(1,0)=ee(0,1);
            ee(1,1)=tmpg(1,1);
            ee(1,2)=0.0;

            ee(2,0)=0.0;
            ee(2,1)=0.0;
            ee(2,2)=0.0;
        }
        else
        {
            ee(0,0)=tmpg(0,0);
            ee(0,1)=0.5*(tmpg(1,0)+tmpg(0,1));
            ee(0,2)=0.5*(tmpg(0,2)+tmpg(2,0));

            ee(1,0)=ee(0,1);
            ee(1,1)=tmpg(1,1);
            ee(1,2)=0.5*(tmpg(1,2)+tmpg(2,1));

            ee(2,0)=ee(0,2);
            ee(2,1)=ee(1,2);
            ee(2,2)=tmpg(2,2);
        }
    }
}

void LinIsoElasticityCoefficient::EvalResidual(Vector &rr, Vector &gradu, ElementTransformation &T, const IntegrationPoint &ip)
{
    MFEM_ASSERT(rr.Size()==9,"The size of the residual should be set to 9.");
    double EE=E->Eval(T,ip);
    double nnu=nu->Eval(T,ip);
    EvalRes(EE,nnu, gradu.GetData(), rr.GetData());

}

void LinIsoElasticityCoefficient::EvalTangent(DenseMatrix &mm, Vector &gradu, ElementTransformation &T, const IntegrationPoint &ip)
{
    MFEM_ASSERT(mm.Size()==9,"The size of the stiffness tensor should be set to 9.");
    double EE=E->Eval(T,ip);
    double nnu=nu->Eval(T,ip);
    Eval(EE,nnu,mm.GetData());
}

double LinIsoElasticityCoefficient::EvalEnergy(Vector &gradu, ElementTransformation &T, const IntegrationPoint &ip)
{
    double EE=E->Eval(T,ip);
    double nnu=nu->Eval(T,ip);
    double t1,t2,t3,t14,t18,t22,t31,t40;
    t1 = 1.0+nnu;
    t2 = 1/t1/2.0;
    t3 = t2*EE;
    t14 = (gradu[0]+gradu[4]+gradu[8])/(1.0-2.0*nnu)/t1*EE*nnu;
    t18 = gradu[1]+gradu[3];
    t22 = gradu[2]+gradu[6];
    t31 = gradu[5]+gradu[7];
    t40 = gradu[0]*(2.0*gradu[0]*t3+t14)/2.0+t18*t18*t2*EE/2.0+t22*t22*t2*EE/
2.0+gradu[4]*(2.0*gradu[4]*t3+t14)/2.0+t31*t31*t2*EE/2.0+gradu[8]*(2.0*gradu[8]
*t3+t14)/2.0;
    return t40;
}

void LinIsoElasticityCoefficient::EvalCompliance(DenseMatrix &C, Vector &stress,
                                                 ElementTransformation &T, const IntegrationPoint &ip)
{
    // the matrix is intended to be used with engineering strain
    double EE=E->Eval(T,ip);
    double nnu=nu->Eval(T,ip);
    if(T.GetDimension()==3){
        C.SetSize(6);
        C=0.0;
        double aa=1.0/EE;
        double bb=-nnu/EE;
        C(0,0)=aa; C(0,1)=bb; C(0,2)=bb;
        C(1,0)=bb; C(1,1)=aa; C(1,2)=bb;
        C(2,0)=bb; C(2,1)=bb; C(2,2)=aa;
        double cc=2.0*(1.0+nnu)/EE;
        C(3,3)=cc;
        C(4,4)=cc;
        C(5,5)=cc;
    }else{
        C.SetSize(3);
        C=0.0;
        double aa=1.0/EE;
        double bb=-nnu/EE;
        C(0,0)=aa; C(0,1)=bb;
        C(1,0)=bb; C(1,1)=aa;
        double cc=2.0*(1.0+nnu)/EE;
        C(2,2)=cc;
    }
}



