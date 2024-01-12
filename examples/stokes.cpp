//                       MFEM Example 1 - Parallel Version
//
// Compile with: make stokes
//
// Sample runs:

#include "mfem.hpp"
#include <fstream>
#include <iostream>

using namespace std;
using namespace mfem;

void rhs_func(const Vector & x, Vector &f)
{
   f.SetSize(x.Size());
   f = 0.0;
}

void u0_func(const Vector & x, Vector &u0)
{
   u0.SetSize(x.Size());
   u0 = 0.0;
   // u0[0] = -sin(M_PI * x[1]);
   u0[0] = -x[1]*(1-x[1])*x[2]*(1-x[2]);
}

int main(int argc, char *argv[])
{
   // 1. Initialize MPI and HYPRE.
   Mpi::Init();
   int num_procs = Mpi::WorldSize();
   int myid = Mpi::WorldRank();
   Hypre::Init();

   // 2. Parse command-line options.
   const char *mesh_file = "../data/inline-hex.mesh";
   int sref = 0;
   int pref = 0;
   bool visualization = true;


   OptionsParser args(argc, argv);
   args.AddOption(&mesh_file, "-m", "--mesh",
                  "Mesh file to use.");
   args.AddOption(&sref, "-sr", "--refinements-serial",
                  "Number of serial refinements");
   args.AddOption(&pref, "-pr", "--refinements-parallel",
                  "Number of parallel refinements");
   args.AddOption(&visualization, "-vis", "--visualization", "-no-vis",
                  "--no-visualization",
                  "Enable or disable GLVis visualization.");
   args.Parse();
   if (!args.Good())
   {
      if (myid == 0)
      {
         args.PrintUsage(cout);
      }
      return 1;
   }
   if (myid == 0)
   {
      args.PrintOptions(cout);
   }

   // Mesh mesh(mesh_file, 1, 1);

   Mesh mesh = Mesh::MakeCartesian3D(8,1,1,mfem::Element::HEXAHEDRON,8.0,1.0,1.0);
   int dim = mesh.Dimension();

   for (int l = 0; l < sref; l++)
   {
      mesh.UniformRefinement();
   }

   ParMesh pmesh(MPI_COMM_WORLD, mesh);
   mesh.Clear();

   for (int l = 0; l < pref; l++)
   {
      pmesh.UniformRefinement();
   }

   FiniteElementCollection *ufec = new H1_FECollection(2,dim);
   FiniteElementCollection *pfec = new H1_FECollection(1,dim);

   ParFiniteElementSpace ufes(&pmesh, ufec,dim,mfem::Ordering::byVDIM);
   ParFiniteElementSpace pfes(&pmesh, pfec);
   Array<ParFiniteElementSpace *> pfespaces;
   pfespaces.Append(&ufes);
   pfespaces.Append(&pfes);
   HYPRE_BigInt usize = ufes.GlobalTrueVSize();
   HYPRE_BigInt psize = pfes.GlobalTrueVSize();
   if (myid == 0)
   {
      cout << "Number of velocity dofs: " << usize << endl;
      cout << "Number of pressure dofs: " << psize << endl;
   }

   ParGridFunction u_gf(&ufes); u_gf = 0.0;
   ParGridFunction p_gf(&pfes); p_gf = 0.0;
   VectorFunctionCoefficient u0_cf(dim, u0_func);
   Array<int> ess_bdr_in(pmesh.bdr_attributes.Max()); ess_bdr_in = 0;
   Array<int> ess_bdr(pmesh.bdr_attributes.Max()); ess_bdr = 1;
   ess_bdr_in[2] = 1; // x = 1 plane
   ess_bdr[2] = 0; // x = 1 plane
   ess_bdr[4] = 0; // x = 0 plane
   Vector vzero(dim); vzero = 0.0;
   VectorConstantCoefficient zero_cf(vzero);
   u_gf.ProjectBdrCoefficient(zero_cf,ess_bdr);
   u_gf.ProjectBdrCoefficient(u0_cf,ess_bdr_in);

   // essential dofs;
   Array<int> ess_tdof_list;
   Array<int> ess_tdof_listp;
   if (pmesh.bdr_attributes.Size())
   {
      Array<int> essbdr(pmesh.bdr_attributes.Max());
      essbdr = 1; essbdr[4] = 0;
      ufes.GetEssentialTrueDofs(essbdr, ess_tdof_list);
      essbdr = 0; essbdr[4] = 1;
      pfes.GetEssentialTrueDofs(essbdr, ess_tdof_listp);
   }

   ParBilinearForm prec_pq(&pfes);
   ConstantCoefficient one(1.0);
   prec_pq.AddDomainIntegrator(new MassIntegrator(one));
   prec_pq.Assemble();
   HypreParMatrix Mpq;
   prec_pq.FormSystemMatrix(ess_tdof_listp,Mpq);

   for (int i = 0; i < ess_tdof_listp.Size(); i++)
   {
      ess_tdof_listp[i] += ufes.GetTrueVSize();
   }
   ess_tdof_list.Append(ess_tdof_listp);

   // linear and bilinear forms
   ParLinearForm b_v(&ufes);
   VectorFunctionCoefficient rhs_cf(dim, rhs_func);
   b_v.AddDomainIntegrator(new VectorDomainLFIntegrator(rhs_cf));
   b_v.Assemble();

   ParLinearForm b_q(&pfes);
   b_q.Assemble();


   ConstantCoefficient negone(-1.0);
   // (∇ u, ∇ v)
   ParBilinearForm a_uv(&ufes);
   a_uv.AddDomainIntegrator(new VectorDiffusionIntegrator(one));

   ParMixedBilinearForm a_pv(&pfes,&ufes);
   a_pv.AddDomainIntegrator(new TransposeIntegrator(new VectorDivergenceIntegrator(
                                                       negone)));

   ParMixedBilinearForm a_uq(&ufes,&pfes);
   a_uq.AddDomainIntegrator(new VectorDivergenceIntegrator(negone));

   ParBilinearForm a_pq(&pfes);
   ConstantCoefficient zero(0.0);
   a_pq.AddDomainIntegrator(new MassIntegrator(zero));

   ParBlockForm bform(pfespaces);
   bform.SetBlock(&a_uv,0,0);
   bform.SetBlock(&a_pv,0,1);
   bform.SetBlock(&a_uq,1,0);
   bform.SetBlock(&a_pq,1,1);

   Array<int> tdof_offsets(3);
   tdof_offsets[0] = 0;
   tdof_offsets[1] = ufes.TrueVSize();
   tdof_offsets[2] = pfes.TrueVSize();
   tdof_offsets.PartialSum();
   Array<int> dof_offsets(3);
   dof_offsets[0] = 0;
   dof_offsets[1] = ufes.GetVSize();
   dof_offsets[2] = pfes.GetVSize();
   dof_offsets.PartialSum();

   Vector b(dof_offsets.Last());
   b.SetVector(b_v,0);
   b.SetVector(b_q,dof_offsets[1]);
   Vector x(dof_offsets.Last());
   x.SetVector(u_gf,0);
   x.SetVector(p_gf,dof_offsets[1]);

   OperatorPtr Ah;
   Vector B,X;
   bform.Assemble();
   bform.FormLinearSystem(ess_tdof_list,x, b, Ah, X, B);

   BlockOperator * A = Ah.As<BlockOperator>();

   BlockDiagonalPreconditioner prec(tdof_offsets);
   HypreBoomerAMG amg_v((HypreParMatrix&)A->GetBlock(0,0));
   amg_v.SetSystemsOptions(dim);

   HypreBoomerAMG amg_p(Mpq);

   prec.SetDiagonalBlock(0,&amg_v);
   prec.SetDiagonalBlock(1,&amg_p);


   // ((HypreParMatrix*)&A->GetBlock(0,0))->Print("A00.mat");
   // // ((HypreParMatrix*)&A->GetBlock(0,1))->Print("A01.mat");
   // ((HypreParMatrix*)&A->GetBlock(1,0))->Print("A10.mat");
   // // ((HypreParMatrix*)&A->GetBlock(1,1))->Print("A11.mat");
   // return 0;


   MINRESSolver solver(MPI_COMM_WORLD);
   solver.SetRelTol(1e-12);
   solver.SetMaxIter(60);
   solver.SetPrintLevel(1);
   solver.SetPreconditioner(prec);
   solver.SetOperator(*A);
   solver.Mult(B, X);


   BlockVector Xb(X.GetData(),tdof_offsets);
   u_gf.SetFromTrueDofs(Xb.GetBlock(0));
   p_gf.SetFromTrueDofs(Xb.GetBlock(1));

   if (visualization)
   {
      char vishost[] = "localhost";
      int  visport   = 19916;
      socketstream sol_sock(vishost, visport);
      sol_sock << "parallel " << num_procs << " " << myid << "\n";
      sol_sock.precision(8);
      sol_sock << "solution\n" << pmesh << u_gf << flush;

      socketstream solp_sock(vishost, visport);
      solp_sock << "parallel " << num_procs << " " << myid << "\n";
      solp_sock.precision(8);
      solp_sock << "solution\n" << pmesh << p_gf << flush;

   }

   delete ufec;
   delete pfec;

   return 0;
}
