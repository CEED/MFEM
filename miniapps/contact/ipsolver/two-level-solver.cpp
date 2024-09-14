// Copyright (c) 2010-2024, Lawrence Livermore National Security, LLC. Produced
// at the Lawrence Livermore National Laboratory. All Rights reserved. See files
// LICENSE and NOTICE for details. LLNL-CODE-806117.
//
// This file is part of the MFEM library. For more information and source code
// availability visit https://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the BSD-3 license. We welcome feedback and contributions, see file
// CONTRIBUTING.md for details.

#include "two-level-solver.hpp"

namespace mfem
{

TwoLevelAMGSolver::TwoLevelAMGSolver(MPI_Comm comm_): Solver()
{
    Init(comm_);
}

TwoLevelAMGSolver::TwoLevelAMGSolver(const Operator & Op, const Operator & P_)
: Solver()
{
   auto APtr = dynamic_cast<const HypreParMatrix *>(&Op);
   MFEM_VERIFY(APtr, "Operator: Not a compatible matrix type");
   Init(APtr->GetComm());
   auto PPtr = dynamic_cast<const HypreParMatrix *>(&P_);
   MFEM_VERIFY(PPtr, "Transfer Map: not a compatible matrix type");
   
   SetOperator(Op);
   SetTransferMap(P_);
}

void TwoLevelAMGSolver::Init(MPI_Comm comm_)
{
    comm=comm_;
    MPI_Comm_size(comm, &numProcs);
    MPI_Comm_rank(comm, &myid);
}

void TwoLevelAMGSolver::SetOperator(const Operator & Op)
{
    A = dynamic_cast<const HypreParMatrix *>(&Op);
    height = A->Height();
    width = A->Width();
    InitAMG();
}

void TwoLevelAMGSolver::SetTransferMap(const Operator & P_)
{
    P = dynamic_cast<const HypreParMatrix *>(&P_);
    InitMumps();
}

void TwoLevelAMGSolver::InitAMG()
{
    amg = new HypreBoomerAMG(*A);
    amg->SetPrintLevel(0);
    amg->SetSystemsOptions(3);
    amg->SetRelaxType(relax_type);
}

void TwoLevelAMGSolver::InitMumps()
{
    Ac = RAP(A, P);
    M = new MUMPSSolver(comm);
    M->SetPrintLevel(0);
    M->SetMatrixSymType(MUMPSSolver::MatType::UNSYMMETRIC);
    M->SetOperator(*Ac);
}


void TwoLevelAMGSolver::Mult(const Vector & b, Vector & x) const
{
    MFEM_VERIFY(b.Size() == x.Size(), "Inconsistent x and y size");
    

    x = 0.0;
    Vector z(x);
    amg->Mult(b, z);
    x+=z;
    Vector rc(P->Width());
    Vector xc(P->Width());
    if (additive)
    {
        P->MultTranspose(b,rc);
        M->Mult(rc,xc);
        P->Mult(xc,z);
    }
    else
    {
        Vector r(b.Size());
        // 2. Compute Residual r = b - A x
        A->Mult(x,r);
        r.Neg(); r+=b;
        // 3. Restrict to subspace
        P->MultTranspose(r,rc);
        // 4. Solve on the subspace
        M->Mult(rc,xc);
        // 5. Transfer to fine space
        P->Mult(xc,z);
        // 6. Update Correction
        x+=z;
        // 7. Compute Residual r = b - A x
        A->Mult(x,r);
        r.Neg(); r+=b;
        // 8. Post V-Cycle 
        amg->Mult(r, z);
    }
    x+= z;
}

} // namespace
