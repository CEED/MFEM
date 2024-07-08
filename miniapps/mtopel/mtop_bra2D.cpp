#include "mfem.hpp"
#include <fstream>
#include <iostream>
#include <random>
#include "MMA.hpp"

#include "mtop_coefficients.hpp"
#include "mtop_solvers.hpp"



class CoeffHoles:public mfem::Coefficient
{
public:
    CoeffHoles(double pr=0.5)
    {
        period=pr;
    }

    virtual
    double Eval(mfem::ElementTransformation &T, const mfem::IntegrationPoint &ip)
    {

        double x[3];
        mfem::Vector transip(x, T.GetSpaceDim());
        T.Transform(ip, transip);

        int nx=x[0]/period;
        int ny=x[1]/period;

        x[0]=x[0]-double(nx)*period-0.5*period;
        x[1]=x[1]-double(ny)*period-0.5*period;

        double r=sqrt(x[0]*x[0]+x[1]*x[1]);
        if(r<(0.45*period)){return 0.2;}
        return 0.8;
    }


private:
    double period;
};

class AlcoaBracket
{
public:
    AlcoaBracket(mfem::ParMesh* pmesh, int vorder=1):E(),nu(0.2)
    {
        esolv=new mfem::ElasticitySolver(pmesh,vorder);
        esolv->AddMaterial(new mfem::LinIsoElasticityCoefficient(E,nu));
        esolv->SetNewtonSolver(1e-8,1e-12,1,0);
        esolv->SetLinearSolver(1e-10,1e-12,400);

        dfes=nullptr;
        cobj=new mfem::ComplianceObjective();


    }

    void SetDesignFES(mfem::ParFiniteElementSpace* fes)
    {
        dfes=fes;
        pdens.SetSpace(dfes);
        vdens.SetSize(dfes->GetTrueVSize());
    }

    ~AlcoaBracket()
    {
        delete cobj;
        delete esolv;

    }

    void Solve()
    {
        //solve the problem for the base loads
        bsolx.resize(16);
        bsoly.resize(16);
        bsolz.resize(16);

        Solve(bsolx,0.5);
        Solve(bsoly,0.6);
        Solve(bsolz,0.7);

        //for(int i=0;i<16;i++){
        //    bsoly[i]*=0.1;
        //    bsolz[i]*=0.01;
        //}

    }

    void Solve(std::vector<mfem::ParGridFunction>& bsol,double eta)
    {

        E.SetProjParam(eta,8.0);
        //set all bc
        esolv->DelDispBC();
        for(int j=0;j<6;j++){esolv->AddDispBC(2+j,4,0.0);}
        esolv->AddSurfLoad(1,0.00,1.00,0.0);
        esolv->FSolve();
        esolv->GetSol(bsol[0]);

        for(int i=0;i<6;i++){
            esolv->DelDispBC();
            for(int j=0;j<6;j++){if(j!=i){ esolv->AddDispBC(2+j,4,0.0);}}
            esolv->AddSurfLoad(1,0.00,1.00,0.0);
            esolv->FSolve();
            esolv->GetSol(bsol[1+i]);
        }

        for(int i=0;i<5;i++){
            esolv->DelDispBC();
            for(int j=0;j<6;j++){if((j!=i)&&(j!=(i+1))){ esolv->AddDispBC(2+j,4,0.0);}}
            esolv->AddSurfLoad(1,0.00,1.00,0.0);
            esolv->FSolve();
            esolv->GetSol(bsol[7+i]);
        }

        for(int i=0;i<4;i++){
            esolv->DelDispBC();
            for(int j=0;j<6;j++){if((j!=i)&&(j!=(i+1))&&(j!=(i+2))){ esolv->AddDispBC(2+j,4,0.0);}}
            esolv->AddSurfLoad(1,0.00,1.00,0.0);
            esolv->FSolve();
            esolv->GetSol(bsol[12+i]);
        }
        // all solutions are stored in bsolx,bsoly,bsolz
    }

    void SetDensity(mfem::Vector& vdens_,
                    double eta=0.5, double beta=8.0,double pen=3.0){

        vdens=vdens_;
        pdens.SetFromTrueDofs(vdens);

        E.SetDens(&pdens);
        E.SetProjParam(eta,beta);
        E.SetEMaxMin(1e-6,1.0);
        E.SetPenal(pen);

        cobj->SetE(&E);
        cobj->SetDens(vdens);
        cobj->SetDesignFES(dfes);

    }

    /// Evaluates the compliance
    double Compliance(int i, double fx, double fy, double fz){

        return cobj->Eval(GetSol(i,fx,fy,fz));
    }

    void GetComplianceGrad(int i, double fx, double fy, double fz, mfem::Vector& grad){
        if(dfes==nullptr)
        {
             mfem::mfem_error("AlcoaBracket dfes is not defined!");
        }
        cobj->Grad(GetSol(i,fx,fy,fz),grad);
    }

    double MaxCompliance()
    {
        double rez=0.0;
        double vv;
        for(int i=0;i<16;i++){
                vv=Compliance(i,1.0,0.0,0.0); if(vv>rez){rez=vv;}
                vv=Compliance(i,0.0,1.0,0.0); if(vv>rez){rez=vv;}
                vv=Compliance(i,0.0,0.0,1.0); if(vv>rez){rez=vv;}
        }

        return rez;
    }


    double CVarDual(double alpha, double gamma){

        // using lambda expression
        auto fsigm = [](double x, double p, double alpha)
        {
            double tmpv;
            if(x<1.0){
                tmpv=exp(x)/(1.0+exp(x));
                return tmpv*p/(1.0-alpha);
            }else{
                tmpv=1.0/(1.0+exp(-x));
                return tmpv*p/(1.0-alpha);
            }
        };

        auto isigm = [](double x, double p, double alpha)
        {
            //if(x<0.5){
                return log(x/(p/(1.0-alpha)-x));
            //}else{
            //    return log(p/(x*(1.0-alpha))-1.0);
            //}
        };

        std::vector<std::tuple<double,double,int,int>> vals;
        double prob[16]={1.0, 0.01,0.01,0.01,0.01,0.01,0.01,
                         0.001,0.001,0.001,0.001,0.001,
                         0.0001,0.0001,0.0001,0.0001};

        double tprob=(1.0+6*0.01+5*0.001+4*0.0001)*3.0;

        double vv;
        for(int i=0;i<16;i++){
                vv=Compliance(i,1.0,0.0,0.0); vals.push_back(std::tuple<double,double,int,int>(vv,prob[i],i,0));
                vv=Compliance(i,0.0,1.0,0.0); vals.push_back(std::tuple<double,double,int,int>(vv,prob[i],i,1));
                vv=Compliance(i,0.0,0.0,1.0); vals.push_back(std::tuple<double,double,int,int>(vv,prob[i],i,2));
        }


        //initialize q
        if(dualq.size()!=48){
            dualq.resize(48);
            for(int i=0;i<48;i++){
                std::get<1>(vals[i])=std::get<1>(vals[i])/tprob; //normalize the probabilities
                dualq[i]=std::get<1>(vals[i]);
            }
        }


        std::vector<double> &q=dualq;

        auto projf = [&vals,&q,alpha,gamma, &isigm,&fsigm](double t)
        {
            double rez=-1.0;
            double tmpv;
            for(size_t i=0;i<q.size();i++){
                tmpv=isigm(q[i], std::get<1>(vals[i]),alpha) +gamma* std::get<0>(vals[i])-t;
                rez=rez+fsigm(tmpv,std::get<1>(vals[i]),alpha);
            }
            return rez;
        };

        //find t
        bool rflag;
        double t,ft,err;
        double tmpv;

        int myrank;
        MPI_Comm_rank(pdens.ParFESpace()->GetComm(),&myrank );

        double rerr=1.0;
        double rezo=0.0;

        while(rerr>rezo/100.0)
        {
            //find t
            std::tie(rflag,t,ft,err)=mfem::BisectionRootSolver<double>(-30.0,30.0,projf,1e-14);
            if(myrank==0){
            std::cout<<"rflag="<<rflag<<" t="<<t<<" ft="<<ft<<" err="<<err<<std::endl;}
            for(size_t i=0;i<q.size();i++){
                tmpv=isigm(q[i], std::get<1>(vals[i]),alpha) +gamma* std::get<0>(vals[i])-t;
                q[i]=fsigm(tmpv,std::get<1>(vals[i]),alpha);
            }
            double rez=0.0;
            for(size_t i=0;i<q.size();i++)
            {
                rez=rez+q[i]*std::get<0>(vals[i]);
            }
            if(myrank==0){std::cout<<"rez="<<rez<<std::endl;}
            rerr=fabs(rez-rezo);
            rezo=rez;

        }


        double rez=0.0;
        for(size_t i=0;i<q.size();i++)
        {
            rez=rez+q[i]*std::get<0>(vals[i]);
        }

        return rez;

    }


    double CVar(){
        std::vector<std::tuple<double,double,int,int>> vals;
        double prob[16]={1.0, 0.01,0.01,0.01,0.01,0.01,0.01,
                         0.001,0.001,0.001,0.001,0.001,
                         0.0001,0.0001,0.0001,0.0001};

        double tprob=(1.0+6*0.01+5*0.001+4*0.0001)*3.0;

        double vv;
        for(int i=0;i<16;i++){
                vv=Compliance(i,1.0,0.0,0.0); vals.push_back(std::tuple<double,double,int,int>(vv,prob[i],i,0));
                vv=Compliance(i,0.0,1.0,0.0); vals.push_back(std::tuple<double,double,int,int>(vv,prob[i],i,1));
                vv=Compliance(i,0.0,0.0,1.0); vals.push_back(std::tuple<double,double,int,int>(vv,prob[i],i,2));
        }

        std::sort(vals.begin(),vals.end());
        double cdf[48]; cdf[0]=std::get<1>(vals[0]);
        double var;
        double cvar=0.0;
        double ww=0.0;
        for(int i=1;i<48;i++){
            cdf[i]=cdf[i-1]+std::get<1>(vals[i]);
            if(cdf[i]>0.98*tprob){
                var=std::get<0>(vals[i]);
                break;
            }
        }

        cdf[0]=std::get<1>(vals[0]);
        //evaluate the probability
        for(int i=1;i<48;i++){
            cdf[i]=cdf[i-1]+std::get<1>(vals[i]);
            if(!(std::get<0>(vals[i])<var)){
                cvar=cvar+std::get<0>(vals[i])*std::get<1>(vals[i]);
                ww=ww+std::get<1>(vals[i]);
            }

            /*
            if(dfes->GetMyRank()==0){
            std::cout<<std::get<0>(vals[i])<<" "<<std::get<1>(vals[i])<<" "<<cdf[i]<<
                       " "<<std::get<2>(vals[i])<<" "<<std::get<3>(vals[i])<<std::endl;}*/
        }

        return cvar/ww;
    }

    double EVaR(mfem::Vector& grad, double beta=0.98)
    {
        grad=0.0;
        mfem::Vector cgrad(grad.Size()); cgrad=0.0;

        std::vector<std::tuple<double,double,int,int>> vals;
        double prob[16]={1.0, 0.01,0.01,0.01,0.01,0.01,0.01,
                         0.001,0.001,0.001,0.001,0.001,
                         0.0001,0.0001,0.0001,0.0001};

        double tprob=(1.0+6*0.01+5*0.001+4*0.0001)*3.0;

        double vv;
        for(int i=0;i<16;i++){
                prob[i]/=tprob;
                vv=Compliance(i,1.0,0.0,0.0); vals.push_back(std::tuple<double,double,int,int>(vv,prob[i],i,0));
                vv=Compliance(i,0.0,1.0,0.0); vals.push_back(std::tuple<double,double,int,int>(vv,prob[i],i,1));
                vv=Compliance(i,0.0,0.0,1.0); vals.push_back(std::tuple<double,double,int,int>(vv,prob[i],i,2));
        }

        int myrank;
        MPI_Comm_rank(pdens.ParFESpace()->GetComm(),&myrank );

        double tt=1.0;
        double shift=0.0;
        //find t
        {
            std::vector<double> vvals; vvals.resize(48);
            std::vector<double> vprob; vprob.resize(48);
            for(int i=0;i<48;i++){
                vvals[i]=std::get<0>(vals[i]);
                vprob[i]=std::get<1>(vals[i]);
                if(shift<vvals[i]){shift=vvals[i];}

            }
            mfem::RiskMeasures rmeas(vvals,vprob);
            if(flagt ==false){
                tt=rmeas.EVaR_Find_t(beta,1.0,1e-8,100);
                tprev=tt;
                flagt=true;
            }else{
                tt=rmeas.EVaR_Find_t(beta,tprev,1.0,1e-8,100);
                tprev=tt;
            }

            if(myrank==0){
                std::cout<<" tt="<<tt
                         <<" beta="<<beta
                         <<" VaR="<<rmeas.VaR(beta)
                         <<" CVaR="<<rmeas.CVaR(beta)
                         <<" EVaR="<<rmeas.EVaR(beta)
                         <<" aEVaR="<<rmeas.EVaR(1.0-beta)
                         <<" EVaRt="<<rmeas.EVaRt(beta,tt)
                         <<" Max="<<rmeas.Max()<<std::endl;
            }
        }


        double evar=0.0;
        for(int i=0;i<48;i++){
            double lw=(std::get<1>(vals[i]))*std::exp((std::get<0>(vals[i])-shift)/tt);
            evar=evar+lw;
            //compute the gradients
            int ii=std::get<2>(vals[i]);
            int pp=std::get<3>(vals[i]);
            double vv[3]={0.0,0.0,0.0}; vv[pp]=1.0;
            GetComplianceGrad(ii,vv[0],vv[1],vv[2],cgrad);
            grad.Add(lw,cgrad);
        }

        grad/=evar;
        evar=shift+tt*std::log(evar/(1.0-beta));
        if(myrank==0){std::cout<<"EVaR="<<evar<<std::endl;}

        return evar;

    }


    double CVaRe(mfem::Vector& grad, double beta=0.98, double ee=1.0)
    {
        grad=0.0;
        mfem::Vector cgrad(grad.Size()); cgrad=0.0;

        std::vector<std::tuple<double,double,int,int>> vals;
        double prob[16]={1.0, 0.01,0.01,0.01,0.01,0.01,0.01,
                         0.001,0.001,0.001,0.001,0.001,
                         0.0001,0.0001,0.0001,0.0001};

        double tprob=(1.0+6*0.01+5*0.001+4*0.0001)*3.0;

        double vv;
        for(int i=0;i<16;i++){
                prob[i]/=tprob;
                vv=Compliance(i,1.0,0.0,0.0); vals.push_back(std::tuple<double,double,int,int>(vv,prob[i],i,0));
                vv=Compliance(i,0.0,1.0,0.0); vals.push_back(std::tuple<double,double,int,int>(vv,prob[i],i,1));
                vv=Compliance(i,0.0,0.0,1.0); vals.push_back(std::tuple<double,double,int,int>(vv,prob[i],i,2));
        }

        int myrank;
        MPI_Comm_rank(pdens.ParFESpace()->GetComm(),&myrank );


        //find t-parameter
        double tt=0.0;
        {
            std::vector<double> vvals; vvals.resize(48);
            std::vector<double> vprob; vprob.resize(48);
            for(int i=0;i<48;i++){
                vvals[i]=std::get<0>(vals[i]);
                vprob[i]=std::get<1>(vals[i]);
            }

            mfem::RiskMeasures rmeas(vvals,vprob);

            tt=rmeas.CVaRe_Find_t(beta,ee,1.0,1e-8,10);

            if(myrank==0){
                std::cout<<"reg VaR="<<tt
                         <<" VaR="<<rmeas.VaR(beta)
                         <<" reg CVaR="<<rmeas.CVaRe(beta,ee)<<" "<<rmeas.CVaRet(beta,ee,tt)
                         <<" CVaR="<<rmeas.CVaR(beta)<<std::endl;
            }
        }

        //evaluate the gradients
        std::function<double(double)> rff=[&ee](double y)
        {
            if(y<0.0){
                return 0.0;
            }else if(y<ee){
                return y*y*y/(ee*ee)-y*y*y*y/(2.0*ee*ee*ee);
            }else{
                return y-ee/2.0;
            }
        };

        std::function<double(double)> rff3=[&ee,&rff](double y)
        {
            return   rff(y+ee/2.0);
        };


        std::function<double(double)> drff=[&ee](double y)
        {
            if(y<0.0){
                return 0.0;
            }else if(y<ee){
                return 3.0*y*y/(ee*ee)-4.0*y*y*y/(2.0*ee*ee*ee);
            }else{
                return 1.0;
            }
        };

        std::function<double(double)> drff3=[&ee,&drff](double y)
        {
            return drff(y+ee/2.0);
        };



        double cvar=0.0;
        for(int i=0;i<48;i++){
            cvar=cvar+(std::get<1>(vals[i]))*rff3(std::get<0>(vals[i])-tt);
            //compute the gradients
            int ii=std::get<2>(vals[i]);
            int pp=std::get<3>(vals[i]);
            double vv[3]={0.0,0.0,0.0}; vv[pp]=1.0;
            GetComplianceGrad(ii,vv[0],vv[1],vv[2],cgrad);
            grad.Add(std::get<1>(vals[i])*drff3(std::get<0>(vals[i])-tt),cgrad);
        }

        grad/=(1.0-beta);

        if(myrank==0){std::cout<<"reg CVaR="<<tt+cvar/(1.0-beta)<<std::endl;}

        return tt+cvar/(1.0-beta);
    }

    double CVar(mfem::Vector& grad, double beta=0.98)
    {
        grad=0.0;
        mfem::Vector cgrad(grad.Size()); cgrad=0.0;

        std::vector<std::tuple<double,double,int,int>> vals;
        double prob[16]={1.0, 0.01,0.01,0.01,0.01,0.01,0.01,
                         0.001,0.001,0.001,0.001,0.001,
                         0.0001,0.0001,0.0001,0.0001};

        double tprob=(1.0+6*0.01+5*0.001+4*0.0001)*3.0;


        double vv;
        for(int i=0;i<16;i++){
                prob[i]/=tprob;
                vv=Compliance(i,1.0,0.0,0.0); vals.push_back(std::tuple<double,double,int,int>(vv,prob[i],i,0));
                vv=Compliance(i,0.0,1.0,0.0); vals.push_back(std::tuple<double,double,int,int>(vv,prob[i],i,1));
                vv=Compliance(i,0.0,0.0,1.0); vals.push_back(std::tuple<double,double,int,int>(vv,prob[i],i,2));
        }

        std::sort(vals.begin(),vals.end(),std::greater<std::tuple<double,double,int,int>>());
        double var;
        double cvar;
        int vi=47;
        double cp;
        for(int i=0;i<47;i++){
            if((cp+std::get<1>(vals[i]))>(1.0-beta)){
                vi=i;
                break;
            }else{
                cp=cp+std::get<1>(vals[i]);
            }
        }
        var=std::get<0>(vals[vi]);

        int myrank;
        MPI_Comm_rank(pdens.ParFESpace()->GetComm(),&myrank );

        if(myrank==0){
        std::cout<<"my var="<<var<<" ";}

        {
            std::vector<double> sampl; sampl.resize(48);
            std::vector<double> proba; proba.resize(48);
            for(int i=0;i<48;i++){
                sampl[i]=std::get<0>(vals[i]);
                proba[i]=std::get<1>(vals[i]);
            }

            mfem::RiskMeasures rmeas(sampl,proba);

            var=rmeas.VaR(beta);
        }
        if(myrank==0){
        std::cout<<"go var="<<var<<std::endl;}

        cvar=0.0;
        for(int i=0;i<vi;i++){
            cvar=cvar+(std::get<1>(vals[i]))*(std::get<0>(vals[i]));
            //compute the gradients
            int ii=std::get<2>(vals[i]);
            int pp=std::get<3>(vals[i]);
            double vv[3]={0.0,0.0,0.0}; vv[pp]=1.0;
            GetComplianceGrad(ii,vv[0],vv[1],vv[2],cgrad);
            grad.Add(std::get<1>(vals[i]),cgrad);
        }

        //add the contribution of VaR
        cvar=cvar+(1.0-beta-cp)*(std::get<0>(vals[vi]));
        {
            int ii=std::get<2>(vals[vi]);
            int pp=std::get<3>(vals[vi]);
            double vv[3]={0.0,0.0,0.0}; vv[pp]=1.0;
            GetComplianceGrad(ii,vv[0],vv[1],vv[2],cgrad);
            grad.Add(1.0-beta-cp,cgrad);
        }

        grad/=(1.0-beta);
        cvar/=(1.0-beta);

        if(myrank==0){
            std::cout<<"cvar="<<cvar<<std::endl;
        }



        return cvar;
    }

    double MeanSTD(double alpha=0.0)
    {
        double prob[16]={1.0, 0.01,0.01,0.01,0.01,0.01,0.01,
                         0.001,0.001,0.001,0.001,0.001,
                         0.0001,0.0001,0.0001,0.0001};

        double tprob=(1.0+6*0.01+5*0.001+4*0.0001)*3.0;

        double mean=0.0;
        double var=0.0;

        double rez=0.0;
        double vv;
        for(int i=0;i<16;i++){
                vv=Compliance(i,1.0,0.0,0.0); mean=mean+prob[i]*vv; var=var+prob[i]*vv*vv;
                vv=Compliance(i,0.0,1.0,0.0); mean=mean+prob[i]*vv; var=var+prob[i]*vv*vv;
                vv=Compliance(i,0.0,0.0,1.0); mean=mean+prob[i]*vv; var=var+prob[i]*vv*vv;
        }

        mean=mean/tprob;
        var=var/tprob;
        var=var-mean*mean;

        rez=alpha*mean+(1-alpha)*sqrt(var);
        return rez;

    }


    void MeanSTD(mfem::Vector& grad, double alpha=0.0)
    {
        grad=0.0;
        mfem::Vector cgrad(grad.Size()); cgrad=0.0;
        mfem::Vector lgrad(grad.Size()); lgrad=0.0;

        double prob[16]={1.0, 0.01,0.01,0.01,0.01,0.01,0.01,
                         0.001,0.001,0.001,0.001,0.001,
                         0.0001,0.0001,0.0001,0.0001};

        double tprob=(1.0+6*0.01+5*0.001+4*0.0001)*3.0;

        double mean=0.0;
        double var=0.0;

        double vv;
        for(int i=0;i<16;i++){
                vv=Compliance(i,1.0,0.0,0.0); mean=mean+prob[i]*vv; var=var+prob[i]*vv*vv;
                GetComplianceGrad(i,1.0,0.0,0.0,cgrad);
                grad.Add(prob[i],cgrad);
                lgrad.Add(prob[i]*vv,cgrad);


                vv=Compliance(i,0.0,1.0,0.0); mean=mean+prob[i]*vv; var=var+prob[i]*vv*vv;
                GetComplianceGrad(i,0.0,1.0,0.0,cgrad);
                grad.Add(prob[i],cgrad);
                lgrad.Add(prob[i]*vv,cgrad);

                vv=Compliance(i,0.0,0.0,1.0); mean=mean+prob[i]*vv; var=var+prob[i]*vv*vv;
                GetComplianceGrad(i,0.0,0.0,1.0,cgrad);
                grad.Add(prob[i],cgrad);
                lgrad.Add(prob[i]*vv,cgrad);
        }
        grad/=tprob;
        lgrad/=tprob;

        mean=mean/tprob;
        var=var/tprob;
        var=var-mean*mean;

        cgrad=grad;

        grad*=alpha;
        grad.Add((1.0-alpha)/sqrt(var),lgrad);
        grad.Add(-(1.0-alpha)*mean/sqrt(var),cgrad);

    }

    /// weight parameter tt
    double EntropicRisk(double tt=1.0)
    {
        double prob[16]={1.0, 0.01,0.01,0.01,0.01,0.01,0.01,
                         0.001,0.001,0.001,0.001,0.001,
                         0.0001,0.0001,0.0001,0.0001};

        double tprob=(1.0+6*0.01+5*0.001+4*0.0001)*3.0;

        double rez=0.0;
        double vv;
        for(int i=0;i<16;i++){
                vv=Compliance(i,1.0,0.0,0.0); rez=rez+prob[i]*exp(vv/tt);
                vv=Compliance(i,0.0,1.0,0.0); rez=rez+prob[i]*exp(vv/tt);
                vv=Compliance(i,0.0,0.0,1.0); rez=rez+prob[i]*exp(vv/tt);
        }

        rez=rez/tprob;
        rez=log(rez)*tt;
        return rez;
    }

    void EntropicRisk(mfem::Vector& grad, double tt=1.0)
    {
        grad=0.0;
        mfem::Vector cgrad(grad.Size()); cgrad=0.0;

        double prob[16]={1.0, 0.01,0.01,0.01,0.01,0.01,0.01,
                         0.001,0.001,0.001,0.001,0.001,
                         0.0001,0.0001,0.0001,0.0001};

        double tprob=(1.0+6*0.01+5*0.001+4*0.0001)*3.0;

        double rez=0.0;
        double vv;
        for(int i=0;i<16;i++){
                vv=Compliance(i,1.0,0.0,0.0); rez=rez+prob[i]*exp(vv/tt);
                GetComplianceGrad(i,1.0,0.0,0.0,cgrad);grad.Add(prob[i]*exp(vv/tt),cgrad);

                vv=Compliance(i,0.0,1.0,0.0); rez=rez+prob[i]*exp(vv/tt);
                GetComplianceGrad(i,0.0,1.0,0.0,cgrad);grad.Add(prob[i]*exp(vv/tt),cgrad);

                vv=Compliance(i,0.0,0.0,1.0); rez=rez+prob[i]*exp(vv/tt);
                GetComplianceGrad(i,0.0,0.0,1.0,cgrad);grad.Add(prob[i]*exp(vv/tt),cgrad);
        }

        //grad/=tprob;
        //rez=rez/tprob;
        grad/=rez;
    }


    double MeanCompliance()
    {
        double mean=0.0;
        mean=mean+1.0*Compliance(0,1.0,0.0,0.0);
        mean=mean+1.0*Compliance(0,0.0,1.0,0.0);
        mean=mean+1.0*Compliance(0,0.0,0.0,1.0);
        for(int i=1;i<7;i++){
            mean=mean+0.01*Compliance(i,1.0,0.0,0.0);
            mean=mean+0.01*Compliance(i,0.0,1.0,0.0);
            mean=mean+0.01*Compliance(i,0.0,0.0,1.0);
        }
        for(int i=7;i<12;i++){
            mean=mean+0.001*Compliance(i,1.0,0.0,0.0);
            mean=mean+0.001*Compliance(i,0.0,1.0,0.0);
            mean=mean+0.001*Compliance(i,0.0,0.0,1.0);
        }
        for(int i=12;i<16;i++){
            mean=mean+0.0001*Compliance(i,1.0,0.0,0.0);
            mean=mean+0.0001*Compliance(i,0.0,1.0,0.0);
            mean=mean+0.0001*Compliance(i,0.0,0.0,1.0);
        }

        double tprob=(1.0+6*0.01+5*0.001+4*0.0001)*3.0;
        return mean/tprob;
    }

    void MeanCompliance(mfem::Vector& grad)
    {
        grad=0.0;
        mfem::Vector cgrad(grad.Size()); cgrad=0.0;

        GetComplianceGrad(0,1.0,0.0,0.0,cgrad);grad.Add(1.0,cgrad);
        GetComplianceGrad(0,0.0,1.0,0.0,cgrad);grad.Add(1.0,cgrad);
        GetComplianceGrad(0,0.0,0.0,1.0,cgrad);grad.Add(1.0,cgrad);

        for(int i=1;i<7;i++){
            GetComplianceGrad(i,1.0,0.0,0.0,cgrad);grad.Add(0.01,cgrad);
            GetComplianceGrad(i,0.0,1.0,0.0,cgrad);grad.Add(0.01,cgrad);
            GetComplianceGrad(i,0.0,0.0,1.0,cgrad);grad.Add(0.01,cgrad);
        }
        for(int i=7;i<12;i++){
            GetComplianceGrad(i,1.0,0.0,0.0,cgrad);grad.Add(0.001,cgrad);
            GetComplianceGrad(i,0.0,1.0,0.0,cgrad);grad.Add(0.001,cgrad);
            GetComplianceGrad(i,0.0,0.0,1.0,cgrad);grad.Add(0.001,cgrad);
        }
        for(int i=12;i<16;i++){
            GetComplianceGrad(i,1.0,0.0,0.0,cgrad);grad.Add(0.0001,cgrad);
            GetComplianceGrad(i,0.0,1.0,0.0,cgrad);grad.Add(0.0001,cgrad);
            GetComplianceGrad(i,0.0,0.0,1.0,cgrad);grad.Add(0.0001,cgrad);
        }

        double tprob=(1.0+6*0.01+5*0.001+4*0.0001)*3.0;
        grad/=tprob;
    }

    mfem::ParGridFunction& GetSol(int i, double fx, double fy, double fz)
    {

        sol.SetSpace((esolv->GetDisplacements()).ParFESpace()); sol=0.0;
        sol.Add(fx,bsolx[i]);
        sol.Add(fy,bsoly[i]);
        sol.Add(fz,bsolz[i]);
        return sol;
    }

    void GetSol(int i, double fx, double fy, double fz, mfem::ParGridFunction& msol)
    {
        msol.SetSpace((esolv->GetDisplacements()).ParFESpace()); msol=0.0;
        msol.Add(fx,bsolx[i]);
        msol.Add(fy,bsoly[i]);
        msol.Add(fz,bsolz[i]);
    }

private:
    mfem::YoungModulus E;
    double nu;

    mfem::ParFiniteElementSpace* dfes; //design FES
    mfem::ParGridFunction pdens;
    mfem::Vector vdens;

    mfem::ElasticitySolver* esolv;
    mfem::ComplianceObjective* cobj;

    //base solution vectors x,y,z direction loads
    std::vector<mfem::ParGridFunction> bsolx;
    std::vector<mfem::ParGridFunction> bsoly;
    std::vector<mfem::ParGridFunction> bsolz;


    mfem::ParGridFunction sol;


    std::vector<double> dualq;

    double flagt=false;
    double tprev;

};



int main(int argc, char *argv[])
{
   // Initialize MPI.
   int nprocs, myrank;
   MPI_Init(&argc, &argv);
   MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
   MPI_Comm_rank(MPI_COMM_WORLD, &myrank);

   // Parse command-line options.
   const char *mesh_file = "../../data/star.mesh";
   int order = 1;
   bool static_cond = false;
   int ser_ref_levels = 0;
   int par_ref_levels = 0;
   double rel_tol = 1e-7;
   double abs_tol = 1e-15;
   double fradius = 0.05;
   int tot_iter = 100;
   int max_it = 51;
   int print_level = 1;
   bool visualization = false;
   const char *petscrc_file = "";
   int restart=0;

   mfem::OptionsParser args(argc, argv);
   args.AddOption(&mesh_file, "-m", "--mesh",
                  "Mesh file to use.");
   args.AddOption(&ser_ref_levels,
                  "-rs",
                  "--refine-serial",
                  "Number of times to refine the mesh uniformly in serial.");
   args.AddOption(&par_ref_levels,
                  "-rp",
                  "--refine-parallel",
                  "Number of times to refine the mesh uniformly in parallel.");
   args.AddOption(&order, "-o", "--order",
                  "Finite element order (polynomial degree) or -1 for"
                  " isoparametric space.");
   args.AddOption(&visualization,
                  "-vis",
                  "--visualization",
                  "-no-vis",
                  "--no-visualization",
                  "Enable or disable GLVis visualization.");
   args.AddOption(&static_cond, "-sc", "--static-condensation", "-no-sc",
                  "--no-static-condensation", "Enable static condensation.");
   args.AddOption(&rel_tol,
                  "-rel",
                  "--relative-tolerance",
                  "Relative tolerance for the Newton solve.");
   args.AddOption(&abs_tol,
                  "-abs",
                  "--absolute-tolerance",
                  "Absolute tolerance for the Newton solve.");
   args.AddOption(&tot_iter,
                  "-it",
                  "--linear-iterations",
                  "Maximum iterations for the linear solve.");
   args.AddOption(&max_it,
                  "-mit",
                  "--max-optimization-iterations",
                  "Maximum iterations for the linear optimizer.");
   args.AddOption(&fradius,
                  "-r",
                  "--radius",
                  "Filter radius");
   args.AddOption(&petscrc_file, "-petscopts", "--petscopts",
                     "PetscOptions file to use.");
   args.AddOption(&restart,
                     "-rstr",
                     "--restart",
                     "Restart the optimization from previous design.");
   args.Parse();
   if (!args.Good())
   {
      if (myrank == 0)
      {
         args.PrintUsage(std::cout);
      }
      MPI_Finalize();
      return 1;
   }

   if (myrank == 0)
   {
      args.PrintOptions(std::cout);
   }
   mfem::MFEMInitializePetsc(NULL,NULL,petscrc_file,NULL);

   // Read the (serial) mesh from the given mesh file on all processors.  We
   // can handle triangular, quadrilateral, tetrahedral, hexahedral, surface
   // and volume meshes with the same code.
   mfem::Mesh mesh(mesh_file, 1, 1);
   int dim = mesh.Dimension();
   {
       mfem::Vector vert;
       mesh.GetVertices(vert);
       vert*=0.01;
       mesh.SetVertices(vert);
       mfem::Vector xmin(dim), xmax(dim);
       mesh.GetBoundingBox(xmin,xmax);
       if(myrank==0){
           std::cout<<"Xmin:";xmin.Print(std::cout);
           std::cout<<"Xmax:";xmax.Print(std::cout);
       }
   }

   // Refine the serial mesh on all processors to increase the resolution. In
   // this example we do 'ref_levels' of uniform refinement. We choose
   // 'ref_levels' to be the largest number that gives a final mesh with no
   // more than 10,000 elements.
   {
      int ref_levels =
         (int)floor(log(10000./mesh.GetNE())/log(2.)/dim);

      for (int l = 0; l < ref_levels; l++)
      {
         mesh.UniformRefinement();
      }
   }

   // Define a parallel mesh by a partitioning of the serial mesh. Refine
   // this mesh further in parallel to increase the resolution. Once the
   // parallel mesh is defined, the serial mesh can be deleted.
   mfem::ParMesh pmesh(MPI_COMM_WORLD, mesh);
   mesh.Clear();
   {
       for (int l = 0; l < par_ref_levels; l++)
       {
          pmesh.UniformRefinement();
       }
   }

   if(myrank==0)
   {
       std::cout<<"num el="<<pmesh.GetNE()<<std::endl;
   }


   //allocate the filter
   mfem::FilterSolver* fsolv=new mfem::FilterSolver(0.07,&pmesh);
   fsolv->SetSolver(1e-8,1e-12,100,0);
   fsolv->AddBC(1,1.0);
   fsolv->AddBC(2,1.0);
   fsolv->AddBC(3,1.0);
   fsolv->AddBC(4,1.0);
   fsolv->AddBC(5,1.0);
   fsolv->AddBC(6,1.0);
   fsolv->AddBC(7,1.0);
   fsolv->AddBC(8,0.0);

   mfem::ParGridFunction pgdens(fsolv->GetFilterFES());
   mfem::ParGridFunction oddens(fsolv->GetDesignFES());
   mfem::ParGridFunction spdegf(fsolv->GetFilterFES());
   mfem::Vector vdens; vdens.SetSize(fsolv->GetFilterFES()->GetTrueVSize()); vdens=0.0;
   mfem::Vector vtmpv; vtmpv.SetSize(fsolv->GetDesignFES()->GetTrueVSize()); vtmpv=0.5;

   fsolv->Mult(vtmpv,vdens);
   pgdens.SetFromTrueDofs(vdens);


   AlcoaBracket* alco=new AlcoaBracket(&pmesh,1);
   alco->SetDesignFES(pgdens.ParFESpace());
   alco->SetDensity(vdens);
   alco->Solve();
   //mfem::ParGridFunction disp;
   //alco->GetSol(4,1,1,1,disp);

   //check gradients
   /*
   {
       mfem::Vector prtv;
       mfem::Vector tmpv;
       mfem::Vector tgrad;
       mfem::Vector fgrad;
       prtv.SetSize(vtmpv.Size());
       tmpv.SetSize(vtmpv.Size());
       tgrad.SetSize(vtmpv.Size());
       fgrad.SetSize(vdens.Size()); fgrad=0.0;
       double val=alco->MeanCompliance();
       alco->MeanCompliance(fgrad);
       fsolv->MultTranspose(fgrad,tgrad);

       prtv.Randomize();
       double nd=mfem::InnerProduct(pmesh.GetComm(),prtv,prtv);
       double td=mfem::InnerProduct(pmesh.GetComm(),prtv,tgrad);
       td=td/nd;
       double lsc=1.0;
       double lqoi;

       for(int l=0;l<10;l++){
           lsc/=10.0;
           prtv/=10.0;
           add(prtv,vtmpv,tmpv);
           fsolv->Mult(tmpv,vdens);
           alco->SetDensity(vdens);
           alco->Solve();
           lqoi=alco->MeanCompliance();
           double ld=(lqoi-val)/lsc;
           if(myrank==0){
               std::cout << "dx=" << lsc <<" FD approximation=" << ld/nd
                         << " adjoint gradient=" << td
                         << " err=" << std::fabs(ld/nd-td) << std::endl;
           }
       }
   }*/

   mfem::PVolumeQoI* vobj=new mfem::PVolumeQoI(fsolv->GetFilterFES());
   //mfem::VolumeQoI* vobj=new mfem::VolumeQoI(fsolv->GetFilterFES());
   vobj->SetProjection(0.5,8.0);//threshold 0.2

   //compute the total volume
   double tot_vol;
   {
       vdens=1.0;
       tot_vol=vobj->Eval(vdens);
   }
   double max_vol=0.4*tot_vol;
   if(myrank==0){ std::cout<<"tot vol="<<tot_vol<<std::endl;}

   //intermediate volume
   mfem::VolumeQoI* ivobj=new mfem::VolumeQoI(fsolv->GetFilterFES());
   ivobj->SetProjection(0.5,32);

   //gradients with respect to the filtered field
   mfem::Vector ograd(fsolv->GetFilterFES()->GetTrueVSize()); ograd=0.0; //of the objective
   mfem::Vector vgrad(fsolv->GetFilterFES()->GetTrueVSize()); vgrad=0.0; //of the volume contr.

   //the input design field and the filtered one might not have the same dimensionality
   mfem::Vector ogrado(fsolv->GetDesignFES()->GetTrueVSize()); ogrado=0.0;
   mfem::Vector vgrado(fsolv->GetDesignFES()->GetTrueVSize()); vgrado=0.0;

   mfem::Vector xxmax(fsolv->GetDesignFES()->GetTrueVSize()); xxmax=1.0;
   mfem::Vector xxmin(fsolv->GetDesignFES()->GetTrueVSize()); xxmin=0.0;

   mfem::NativeMMA* mma;
   {
       double a=0.0;
       double c=1000.0;
       double d=0.0;
       mma=new mfem::NativeMMA(MPI_COMM_WORLD,1, ogrado,&a,&c,&d);
   }

   double max_ch=0.1; //max design change

   double cpl; //compliance
   double vol; //volume
   double ivol; //intermediate volume
   double dcpl;


   mfem::ParGridFunction solx;
   mfem::ParGridFunction soly;

   {
      mfem::ParaViewDataCollection paraview_dc("TopOpt", &pmesh);
      paraview_dc.SetPrefixPath("ParaView");
      paraview_dc.SetLevelsOfDetail(order);
      paraview_dc.SetDataFormat(mfem::VTKFormat::BINARY);
      paraview_dc.SetHighOrderOutput(true);
      paraview_dc.SetCycle(0);
      paraview_dc.SetTime(0.0);

      paraview_dc.RegisterField("design",&pgdens);

      //alco->GetSol(6,0.0,1.0,0.0,solx);
      //alco->GetSol(1,0.0,1.0,0.0,soly);
      //paraview_dc.RegisterField("solx",&solx);
      //paraview_dc.RegisterField("soly",&soly);

      //spdegf.ProjectCoefficient(spderf);
      //paraview_dc.RegisterField("reta",&spdegf);

      paraview_dc.Save();

      CoeffHoles holes;
      oddens.ProjectCoefficient(holes);
      oddens.GetTrueDofs(vtmpv);
      vtmpv=0.3;
      fsolv->Mult(vtmpv,vdens);
      pgdens.SetFromTrueDofs(vdens);

      double cvar;
      double erisk;
      double meanstd;
      double std;
      double maxcpl;

      for(int i=1;i<max_it;i++){

          /*
          if((i%11)==0){
              spderf.Generate();
              spdegf.ProjectCoefficient(spderf);
          }*/

          vobj->SetProjection(0.5,8.0);
          alco->SetDensity(vdens,0.5,8.0,1.0);
          /*
          if(i<11){alco->SetDensity(vdens,0.5,1.0,3.0); vobj->SetProjection(0.5,1.0); }
          else if(i<80){alco->SetDensity(vdens,0.5,8.0,3.0);}
          else if(i<120){alco->SetDensity(vdens,0.5,8.0,3.0);}
          else if(i<160){alco->SetDensity(vdens,0.6,8.0,3.0); vobj->SetProjection(0.4,8.0); }
          else {alco->SetDensity(vdens,0.7,8.0,3.0);  vobj->SetProjection(0.3,8.0);}
          */
          alco->Solve();

          dcpl=alco->Compliance(0,0.0,0.0,1.0);
          cpl=alco->MeanCompliance();
          cvar=alco->CVar();
          erisk=alco->EntropicRisk(100.0);
          meanstd=alco->MeanSTD(0.5);
          std=alco->MeanSTD();
          vol=vobj->Eval(vdens);
          ivol=ivobj->Eval(vdens);

          maxcpl=alco->MaxCompliance();

          alco->CVarDual(0.98,1.0/maxcpl);

          if(myrank==0){
              std::cout<<"it: "<<i<<" obj="<<cpl<<" vol="<<vol<<" cvol="<<max_vol<<" ivol="<<ivol<<
                      " cvar="<<cvar<<" erisk="<<erisk<<" mstd="<<meanstd
                      <<" std="<<std<<" dcompl="<<dcpl<<" maxcpl="<<maxcpl<<std::endl;
          }
          //compute the gradients
          //alco->MeanCompliance(ograd);
          //alco->CVar(ograd);
          //alco->MeanSTD(ograd,0.5);
          //alco->GetComplianceGrad(0,0.0,0.0,1.0,ograd);
          //alco->CVar(ograd);
          //alco->CVaRe(ograd,0.98,1.0);
          alco->EVaR(ograd,0.98);
          vobj->Grad(vdens,vgrad);
          //compute the original gradients
          fsolv->MultTranspose(ograd,ogrado);
          fsolv->MultTranspose(vgrad,vgrado);

          {
              //set xxmin and xxmax
              xxmin=vtmpv; xxmin-=max_ch;
              xxmax=vtmpv; xxmax+=max_ch;
              for(int li=0;li<xxmin.Size();li++){
                  if(xxmin[li]<0.0){xxmin[li]=0.0;}
                  if(xxmax[li]>1.0){xxmax[li]=1.0;}
              }
          }

          double con=vol-max_vol;
          mma->Update(vtmpv,ogrado,&con,&vgrado,xxmin,xxmax);

          fsolv->Mult(vtmpv,vdens);
          pgdens.SetFromTrueDofs(vdens);

          //alco->GetSol(1,0.0,1.0,0.0,solx);
          //alco->GetSol(6,0.0,1.0,0.0,soly);

          //paraview_dc.RegisterField("solx",&solx);
          //paraview_dc.RegisterField("soly",&soly);

          //save the design
          if(i%4==0)
          {
              paraview_dc.SetCycle(i);
              paraview_dc.SetTime(i*1.0);
              paraview_dc.Save();
          }
      }

   }


   delete mma;
   delete vobj;
   delete ivobj;
   delete alco;
   delete fsolv;

   mfem::MFEMFinalizePetsc();
   MPI_Finalize();
   return 0;
}
