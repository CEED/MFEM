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

#include "hybridization_ext.hpp"
#include "hybridization.hpp"
#include "pfespace.hpp"
#include "../general/forall.hpp"
#include "../linalg/kernels.hpp"

namespace mfem
{

HybridizationExtension::HybridizationExtension(Hybridization &hybridization_)
   : h(hybridization_)
{ }

static int GetNFacesPerElement(const Mesh &mesh)
{
   const int dim = mesh.Dimension();
   switch (dim)
   {
      case 2: return mesh.GetElement(0)->GetNEdges();
      case 3: return mesh.GetElement(0)->GetNFaces();
      default: MFEM_ABORT("Invalid dimension.");
   }
}

void HybridizationExtension::ConstructC()
{
   Mesh &mesh = *h.fes.GetMesh();
   const int ne = mesh.GetNE();
   const int nf = mesh.GetNFbyType(FaceType::Interior);
   const int m = h.fes.GetFE(0)->GetDof(); // num hat dofs per el
   const int n = h.c_fes.GetFaceElement(0)->GetDof(); // num c dofs per face
   const int n_faces_per_el = GetNFacesPerElement(mesh);

   // Assemble Ct_mat using EA
   Vector emat(m * n * 2 * nf);
   h.c_bfi->AssembleEAInteriorFaces(h.c_fes, h.fes, emat, false);

   const auto *tbe = dynamic_cast<const TensorBasisElement*>(h.fes.GetFE(0));
   MFEM_VERIFY(tbe, "");
   // Note: copying the DOF map here (instead of using a reference) because
   // reading it on GPU can cause issues in other parts of the code when using
   // the debug device. The DOF map is accessed in other places without
   // explicitly calling HostRead, which fails on non-const access if the device
   // pointer is valid.
   Array<int> dof_map = tbe->GetDofMap();

   Ct_mat.SetSize(m * n * n_faces_per_el * ne);
   const auto d_emat = Reshape(emat.Read(), m, n, 2, nf);
   const int *d_dof_map = dof_map.Read();
   const auto d_face_to_el = Reshape(face_to_el.Read(), 2, 2, nf);
   auto d_Ct_mat = Reshape(Ct_mat.Write(), m, n, n_faces_per_el, ne);

   mfem::forall(m*n*2*nf, [=] MFEM_HOST_DEVICE (int idx)
   {
      const int i_lex = idx % m;
      const int j = (idx / m) % n;
      const int ie = (idx / m / n) % 2;
      const int f = idx / m / n / 2;

      const int e  = d_face_to_el(0, ie, f);
      const int fi = d_face_to_el(1, ie, f);

      // Skip elements belonging to face neighbors of shared faces
      if (e < ne)
      {
         // Convert to back to native MFEM ordering in the volume
         const int i_s = d_dof_map[i_lex];
         const int i = (i_s >= 0) ? i_s : -1 - i_s;

         d_Ct_mat(i, j, fi, e) = d_emat(i_lex, j, ie, f);
      }
   });
}

void HybridizationExtension::ConstructH()
{
   const Mesh &mesh = *h.fes.GetMesh();
   const int ne = mesh.GetNE();
   const int n_faces_per_el = GetNFacesPerElement(mesh);
   const int m = h.fes.GetFE(0)->GetDof();
   const int n = h.c_fes.GetFaceElement(0)->GetDof();

   Vector AhatInvCt_mat(Ct_mat);
   auto d_AhatInvCt = Reshape(AhatInvCt_mat.ReadWrite(), m, n, n_faces_per_el, ne);
   {
      const auto d_Ahat_inv = Reshape(Ahat_inv.Read(), m, m, ne);
      const auto d_Ahat_piv = Reshape(Ahat_piv.Read(), m, ne);

      mfem::forall(n*n_faces_per_el*ne, [=] MFEM_HOST_DEVICE (int idx)
      {
         const int j = idx % n;
         const int fi = (idx / n) % n_faces_per_el;
         const int e = idx / n / n_faces_per_el;

         // Potential optimization: can load lu and ipiv into shared memory, use
         // one block of n threads per matrix.
         const real_t *lu = &d_Ahat_inv(0, 0, e);
         const int *ipiv = &d_Ahat_piv(0, e);
         real_t *x = &d_AhatInvCt(0, j, fi, e);

         kernels::LUSolve(lu, m, ipiv, x);
      });
   }

   const int nf = h.fes.GetNFbyType(FaceType::Interior);
   const int n_face_connections = 2*n_faces_per_el - 1;
   Array<int> face_to_face(nf * n_face_connections);

   Array<real_t> CAhatInvCt(nf*n_face_connections*n*n);

   const auto d_Ct = Reshape(Ct_mat.Read(), m, n, n_faces_per_el, ne);
   const auto d_face_to_el = Reshape(face_to_el.Read(), 2, 2, nf);
   const auto d_el_to_face = Reshape(el_to_face.Read(), n_faces_per_el, ne);
   auto d_CAhatInvCt = Reshape(CAhatInvCt.Write(), n, n, n_face_connections, nf);
   auto d_face_to_face = Reshape(face_to_face.Write(), n_face_connections, nf);

   mfem::forall(n*n*n_face_connections*nf, [=] MFEM_HOST_DEVICE (int i)
   {
      d_CAhatInvCt[i] = 0.0;
   });

   mfem::forall(nf, [=] MFEM_HOST_DEVICE (int fi)
   {
      int idx = 0;
      for (int ei = 0; ei < 2; ++ei)
      {
         const int e = d_face_to_el(0, ei, fi);
         if (e < 0 || e >= ne) { continue; }
         const int fi_i = d_face_to_el(1, ei, fi);
         for (int fj_i = 0; fj_i < n_faces_per_el; ++fj_i)
         {
            const int fj = d_el_to_face(fj_i, e);
            // Explicitly allow fi == fj (self-connections)
            if (fj < 0) { continue; }

            // Have we seen this face before? It is possible in some
            // configurations to encounter the same neighboring face twice
            int idx_j = idx;
            for (int i = 0; i < idx; ++i)
            {
               if (d_face_to_face(i, fi) == fj)
               {
                  idx_j = i;
                  break;
               }
            }
            // This is a new face, record it and increment the counter
            if (idx_j == idx)
            {
               d_face_to_face(idx, fi) = fj;
               idx++;
            }

            const real_t *Ct_i = &d_Ct(0, 0, fi_i, e);
            const real_t *AhatInvCt_i = &d_AhatInvCt(0, 0, fj_i, e);
            real_t *CAhatInvCt_i = &d_CAhatInvCt(0, 0, idx_j, fi);
            kernels::AddMultAtB(m, n, n, Ct_i, AhatInvCt_i, CAhatInvCt_i);
         }
      }
      // Fill unused entries with -1 to indicate invalid
      for (; idx < n_face_connections; ++idx)
      {
         d_face_to_face(idx, fi) = -1;
      }
   });

   const int ncdofs = h.c_fes.GetVSize();
   const ElementDofOrdering ordering = ElementDofOrdering::NATIVE;
   const FaceRestriction *face_restr =
      h.c_fes.GetFaceRestriction( ordering, FaceType::Interior);
   const auto c_gather_map = Reshape(face_restr->GatherMap().Read(), n, nf);

   h.H.reset(new SparseMatrix);
   h.H->OverrideSize(ncdofs, ncdofs);

   h.H->GetMemoryI().New(ncdofs + 1, h.H->GetMemoryI().GetMemoryType());

   {
      int *I = h.H->WriteI();

      mfem::forall(ncdofs, [=] MFEM_HOST_DEVICE (int i) { I[i] = 0; });

      mfem::forall(nf*n, [=] MFEM_HOST_DEVICE (int idx_i)
      {
         const int i = idx_i % n;
         const int fi = idx_i / n;
         const int ii = c_gather_map(i, fi);

         for (int idx = 0; idx < n_face_connections; ++idx)
         {
            const int fj = d_face_to_face(idx, fi);
            if (fj < 0) { break; }
            for (int j = 0; j < n; ++j)
            {
               if (d_CAhatInvCt(i, j, idx, fi) != 0)
               {
                  I[ii]++;
               }
            }
         }
      });
   }

   // At this point, I[i] contains the number of nonzeros in row I. Perform a
   // partial sum to get I in CSR format. This is serial, so perform on host.
   //
   // At the same time, we find any empty rows and add a single nonzero (we will
   // put 1 on the diagonal) and record the row index.
   Array<int> empty_rows;
   {
      int *I = h.H->HostReadWriteI();
      int empty_row_count = 0;
      for (int i = 0; i < ncdofs; i++)
      {
         if (I[i] == 0) { empty_row_count++; }
      }
      empty_rows.SetSize(empty_row_count);

      int empty_row_idx = 0;
      int sum = 0;
      for (int i = 0; i < ncdofs; i++)
      {
         int nnz = I[i];
         if (nnz == 0)
         {
            empty_rows[empty_row_idx] = i;
            empty_row_idx++;
            nnz = 1;
         }
         I[i] = sum;
         sum += nnz;
      }
      I[ncdofs] = sum;
   }

   const int nnz = h.H->HostReadI()[ncdofs];
   h.H->GetMemoryJ().New(nnz, h.H->GetMemoryJ().GetMemoryType());
   h.H->GetMemoryData().New(nnz, h.H->GetMemoryData().GetMemoryType());

   {
      int *I = h.H->ReadWriteI();
      int *J = h.H->WriteJ();
      real_t *V = h.H->WriteData();

      mfem::forall(nf*n, [=] MFEM_HOST_DEVICE (int idx_i)
      {
         const int i = idx_i % n;
         const int fi = idx_i / n;
         const int ii = c_gather_map[i + fi*n];
         for (int idx = 0; idx < n_face_connections; ++idx)
         {
            const int fj = d_face_to_face(idx, fi);
            if (fj < 0) { break; }
            for (int j = 0; j < n; ++j)
            {
               const real_t val = d_CAhatInvCt(i, j, idx, fi);
               if (val != 0)
               {
                  const int k = I[ii];
                  const int jj = c_gather_map(j, fj);
                  I[ii]++;
                  J[k] = jj;
                  V[k] = val;
               }
            }
         }
      });

      const int *d_empty_rows = empty_rows.Read();
      mfem::forall(empty_rows.Size(), [=] MFEM_HOST_DEVICE (int idx)
      {
         const int i = d_empty_rows[idx];
         const int k = I[i];
         I[i]++;
         J[k] = i;
         V[k] = 1.0;
      });
   }

   // Shift back down (serial, done on host)
   {
      int *I = h.H->HostReadWriteI();
      for (int i = ncdofs - 1; i > 0; --i)
      {
         I[i] = I[i-1];
      }
      I[0] = 0;
   }

#ifdef MFEM_USE_MPI
   auto *c_pfes = dynamic_cast<ParFiniteElementSpace*>(&h.c_fes);
   if (c_pfes)
   {
      OperatorHandle pP(h.pH.Type()), dH(h.pH.Type());
      pP.ConvertFrom(c_pfes->Dof_TrueDof_Matrix());
      dH.MakeSquareBlockDiag(c_pfes->GetComm(),c_pfes->GlobalVSize(),
                             c_pfes->GetDofOffsets(), h.H.get());
      h.pH.MakePtAP(dH, pP);
      h.H.reset();
   }
#endif
}

void HybridizationExtension::MultCt(const Vector &x, Vector &y) const
{
   Mesh &mesh = *h.fes.GetMesh();
   const int ne = mesh.GetNE();
   const int nf = mesh.GetNFbyType(FaceType::Interior);

   const int n_hat_dof_per_el = h.fes.GetFE(0)->GetDof();
   const int n_c_dof_per_face = h.c_fes.GetFaceElement(0)->GetDof();
   const int n_faces_per_el = GetNFacesPerElement(mesh);

   const ElementDofOrdering ordering = ElementDofOrdering::NATIVE;
   const FaceRestriction *face_restr =
      h.c_fes.GetFaceRestriction(ordering, FaceType::Interior);

   Vector x_evec(face_restr->Height());
   face_restr->Mult(x, x_evec);

   const int *d_el_to_face = el_to_face.Read();
   const auto d_Ct = Reshape(Ct_mat.Read(), n_hat_dof_per_el, n_c_dof_per_face,
                             n_faces_per_el, ne);
   const auto d_x_evec = Reshape(x_evec.Read(), n_c_dof_per_face, nf);
   auto d_y = Reshape(y.Write(), n_hat_dof_per_el, ne);

   mfem::forall(ne * n_hat_dof_per_el, [=] MFEM_HOST_DEVICE (int idx)
   {
      const int e = idx / n_hat_dof_per_el;
      const int i = idx % n_hat_dof_per_el;
      d_y(i, e) = 0.0;
      for (int fi = 0; fi < n_faces_per_el; ++fi)
      {
         const int f = d_el_to_face[e*n_faces_per_el + fi];
         if (f < 0) { continue; }
         for (int j = 0; j < n_c_dof_per_face; ++j)
         {
            d_y(i, e) += d_Ct(i, j, fi, e)*d_x_evec(j, f);
         }
      }
   });
}

void HybridizationExtension::MultC(const Vector &x, Vector &y) const
{
   Mesh &mesh = *h.fes.GetMesh();
   const int ne = mesh.GetNE();
   const int nf = mesh.GetNFbyType(FaceType::Interior);

   const int n_hat_dof_per_el = h.fes.GetFE(0)->GetDof();
   const int n_c_dof_per_face = h.c_fes.GetFaceElement(0)->GetDof();
   const int n_faces_per_el = GetNFacesPerElement(mesh);

   const ElementDofOrdering ordering = ElementDofOrdering::NATIVE;
   const FaceRestriction *face_restr = h.c_fes.GetFaceRestriction(
                                          ordering, FaceType::Interior);

   Vector y_evec(face_restr->Height());
   const auto d_face_to_el = Reshape(face_to_el.Read(), 2, 2, nf);
   const auto d_Ct = Reshape(Ct_mat.Read(), n_hat_dof_per_el, n_c_dof_per_face,
                             n_faces_per_el, ne);
   auto d_x = Reshape(x.Read(), n_hat_dof_per_el, ne);
   auto d_y_evec = Reshape(y_evec.Write(), n_c_dof_per_face, nf);

   mfem::forall(nf * n_c_dof_per_face, [=] MFEM_HOST_DEVICE (int idx)
   {
      const int f = idx / n_c_dof_per_face;
      const int j = idx % n_c_dof_per_face;
      d_y_evec(j, f) = 0.0;
      for (int el_i = 0; el_i < 2; ++el_i)
      {
         const int e = d_face_to_el(0, el_i, f);
         const int fi = d_face_to_el(1, el_i, f);

         // Skip face neighbor elements of shared faces
         if (e >= ne) { continue; }

         for (int i = 0; i < n_hat_dof_per_el; ++i)
         {
            d_y_evec(j, f) += d_Ct(i, j, fi, e)*d_x(i, e);
         }
      }
   });

   y.SetSize(face_restr->Width());
   face_restr->MultTranspose(y_evec, y);
}

void HybridizationExtension::AssembleMatrix(int el, const DenseMatrix &elmat)
{
   const int n = elmat.Width();
   real_t *Ainv = Ahat_inv.GetData() + el*n*n;
   int *ipiv = Ahat_piv.GetData() + el*n;

   std::copy(elmat.GetData(), elmat.GetData() + n*n, Ainv);

   // Eliminate essential DOFs from the local matrix
   for (int i = 0; i < n; ++i)
   {
      const int idx = i + el*n;
      if (ess_hat_dof_marker[idx])
      {
         for (int j = 0; j < n; ++j)
         {
            Ainv[i + j*n] = 0.0;
            Ainv[j + i*n] = 0.0;
         }
         Ainv[i + i*n] = 1.0;
      }
   }

   LUFactors lu(Ainv, ipiv);
   lu.Factor(n);
}

void HybridizationExtension::AssembleElementMatrices(
   const class DenseTensor &el_mats)
{
   Ahat_inv.Write();
   Ahat_inv.GetMemory().CopyFrom(el_mats.GetMemory(), el_mats.TotalSize());

   const int ne = h.fes.GetNE();
   const int n = el_mats.SizeI();

   const bool *d_ess_hat_dof_marker = ess_hat_dof_marker.Read();
   auto d_Ahat_inv = Reshape(Ahat_inv.ReadWrite(), n, n, ne);
   mfem::forall(ne*n, [=] MFEM_HOST_DEVICE (int idx)
   {
      if (d_ess_hat_dof_marker[idx])
      {
         const int i = idx % n;
         const int e = idx / n;
         for (int j = 0; j < n; ++j)
         {
            d_Ahat_inv(i, j, e) = 0.0;
            d_Ahat_inv(j, i, e) = 0.0;
         }
         d_Ahat_inv(i, i, e) = 1.0;
      }
   });

   // TODO: better batched linear algebra (e.g. cuBLAS)
   DenseTensor Ahat_inv_dt;
   Ahat_inv_dt.NewMemoryAndSize(Ahat_inv.GetMemory(), n, n, ne, false);
   BatchLUFactor(Ahat_inv_dt, Ahat_piv);
}

void HybridizationExtension::Init(const Array<int> &ess_tdof_list)
{
   // Verify that preconditions for the extension are met
   const Mesh &mesh = *h.fes.GetMesh();
   const int dim = mesh.Dimension();
   const int ne = h.fes.GetNE();

   MFEM_VERIFY(!h.fes.IsVariableOrder(), "");
   MFEM_VERIFY(dim == 2 || dim == 3, "");
   MFEM_VERIFY(mesh.Conforming(), "");
   MFEM_VERIFY(UsesTensorBasis(h.fes), "");

   // Set up face info arrays
   const int n_faces_per_el = GetNFacesPerElement(mesh);
   el_to_face.SetSize(ne * n_faces_per_el);
   face_to_el.SetSize(4 * mesh.GetNFbyType(FaceType::Interior));
   el_to_face = -1;

   {
      int face_idx = 0;
      for (int f = 0; f < mesh.GetNumFaces(); ++f)
      {
         const Mesh::FaceInformation info = mesh.GetFaceInformation(f);
         if (!info.IsInterior()) { continue; }

         const int el1 = info.element[0].index;
         const int fi1 = info.element[0].local_face_id;
         el_to_face[el1 * n_faces_per_el + fi1] = face_idx;

         const int el2 = info.element[1].index;
         const int fi2 = info.element[1].local_face_id;
         if (!info.IsShared())
         {
            el_to_face[el2 * n_faces_per_el + fi2] = face_idx;
         }

         face_to_el[0 + 4*face_idx] = el1;
         face_to_el[1 + 4*face_idx] = fi1;
         face_to_el[2 + 4*face_idx] = info.IsShared() ? ne + el2 : el2;
         face_to_el[3 + 4*face_idx] = fi2;

         ++face_idx;
      }
   }

   // Count the number of dofs in the discontinuous version of fes:
   const int ndof_per_el = h.fes.GetFE(0)->GetDof();
   num_hat_dofs = ne*ndof_per_el;
   {
      h.hat_offsets.SetSize(ne + 1);
      int *d_hat_offsets = h.hat_offsets.Write();
      mfem::forall(ne + 1, [=] MFEM_HOST_DEVICE (int i)
      {
         d_hat_offsets[i] = i*ndof_per_el;
      });
   }

   Ahat_inv.SetSize(ne*ndof_per_el*ndof_per_el);
   Ahat_piv.SetSize(ne*ndof_per_el);

   ConstructC();

   const ElementDofOrdering ordering = ElementDofOrdering::NATIVE;
   const Operator *R_op = h.fes.GetElementRestriction(ordering);
   const auto *R = dynamic_cast<const ElementRestriction*>(R_op);
   MFEM_VERIFY(R, "");

   // Find out which "hat DOFs" are essential (depend only on essential Lagrange
   // multiplier DOFs).
   {
      const int ntdofs = h.fes.GetTrueVSize();
      // free_tdof_marker is 1 if the DOF is free, 0 if the DOF is essential
      Array<int> free_tdof_marker(ntdofs);
      {
         int *d_free_tdof_marker = free_tdof_marker.Write();
         mfem::forall(ntdofs, [=] MFEM_HOST_DEVICE (int i)
         {
            d_free_tdof_marker[i] = 1;
         });
         const int n_ess_dofs = ess_tdof_list.Size();
         const int *d_ess_tdof_list = ess_tdof_list.Read();
         mfem::forall(n_ess_dofs, [=] MFEM_HOST_DEVICE (int i)
         {
            d_free_tdof_marker[d_ess_tdof_list[i]] = 0;
         });
      }

      Array<int> free_vdofs_marker;
#ifdef MFEM_USE_MPI
      auto *pfes = dynamic_cast<ParFiniteElementSpace*>(&h.fes);
      if (pfes)
      {
         HypreParMatrix *P = pfes->Dof_TrueDof_Matrix();
         free_vdofs_marker.SetSize(h.fes.GetVSize());
         // TODO: would be nice to do this on device
         P->BooleanMult(1, free_tdof_marker.HostRead(),
                        0, free_vdofs_marker.HostWrite());
      }
      else
      {
         free_vdofs_marker.MakeRef(free_tdof_marker);
      }
#else
      free_vdofs_marker.MakeRef(free_tdof_marker);
#endif

      ess_hat_dof_marker.SetSize(num_hat_dofs);
      {
         // The gather map from the ElementRestriction operator gives us the
         // index of the L-dof corresponding to a given (element, local DOF)
         // index pair.
         const int *gather_map = R->GatherMap().Read();
         const int *d_free_vdofs_marker = free_vdofs_marker.Read();
         bool *d_ess_hat_dof_marker = ess_hat_dof_marker.Write();

         // Set the hat_dofs_marker to 1 or 0 according to whether the DOF is
         // "free" or "essential". (For now, we mark all free DOFs as free
         // interior as a placeholder). Then, as a later step, the "free" DOFs
         // will be further classified as "interior free" or "boundary free".
         mfem::forall(num_hat_dofs, [=] MFEM_HOST_DEVICE (int i)
         {
            const int j_s = gather_map[i];
            const int j = (j_s >= 0) ? j_s : -1 - j_s;
            d_ess_hat_dof_marker[i] = !d_free_vdofs_marker[j];
         });
      }
   }

   // Create the hat DOF gather map. This is used to apply the action of R and
   // R^T
   {
      const int vsize = h.fes.GetVSize();
      hat_dof_gather_map.SetSize(num_hat_dofs);
      const int *d_offsets = R->Offsets().Read();
      const int *d_indices = R->Indices().Read();
      int *d_hat_dof_gather_map = hat_dof_gather_map.Write();
      mfem::forall(num_hat_dofs, [=] MFEM_HOST_DEVICE (int i)
      {
         d_hat_dof_gather_map[i] = -1;
      });
      mfem::forall(vsize, [=] MFEM_HOST_DEVICE (int i)
      {
         const int offset = d_offsets[i];
         const int j_s = d_indices[offset];
         const int hat_dof_index = (j_s >= 0) ? j_s : -1 - j_s;
         // Note: -1 is used as a special value (invalid), so the negative
         // DOF indices start at -2.
         d_hat_dof_gather_map[hat_dof_index] = (j_s >= 0) ? i : (-2 - i);
      });
   }
}

void HybridizationExtension::MultR(const Vector &x_hat, Vector &x) const
{
   const Operator *R = h.fes.GetRestrictionOperator();

   // If R is null, then L-vector and T-vector are the same, and we don't need
   // an intermediate temporary variable.
   //
   // If R is not null, we first convert to intermediate L-vector (with the
   // correct BCs), and then from L-vector to T-vector.
   if (!R)
   {
      MFEM_ASSERT(x.Size() == h.fes.GetVSize(), "");
      tmp2.MakeRef(x, 0);
   }
   else
   {
      tmp2.SetSize(R->Width());
      R->MultTranspose(x, tmp2);
   }

   const ElementDofOrdering ordering = ElementDofOrdering::NATIVE;
   const auto *restr = static_cast<const ElementRestriction*>(
                          h.fes.GetElementRestriction(ordering));
   const int *gather_map = restr->GatherMap().Read();
   const bool *d_ess_hat_dof_marker = ess_hat_dof_marker.Read();
   const real_t *d_evec = x_hat.Read();
   real_t *d_lvec = tmp2.ReadWrite();
   mfem::forall(num_hat_dofs, [=] MFEM_HOST_DEVICE (int i)
   {
      // Skip essential DOFs
      if (d_ess_hat_dof_marker[i]) { return; }

      const int j_s = gather_map[i];
      const int sgn = (j_s >= 0) ? 1 : -1;
      const int j = (j_s >= 0) ? j_s : -1 - j_s;

      d_lvec[j] = sgn*d_evec[i];
   });


   // Convert from L-vector to T-vector.
   if (R) { R->Mult(tmp2, x); }
}

void HybridizationExtension::MultRt(const Vector &b, Vector &b_hat) const
{
   Vector b_lvec;
   const Operator *R = h.fes.GetRestrictionOperator();
   if (!R)
   {
      b_lvec.MakeRef(const_cast<Vector&>(b), 0, b.Size());
   }
   else
   {
      tmp1.SetSize(h.fes.GetVSize());
      b_lvec.MakeRef(tmp1, 0, tmp1.Size());
      R->MultTranspose(b, b_lvec);
   }

   b_hat.SetSize(num_hat_dofs);
   const int *d_hat_dof_gather_map = hat_dof_gather_map.Read();
   const real_t *d_b_lvec = b_lvec.Read();
   real_t *d_b_hat = b_hat.Write();
   mfem::forall(num_hat_dofs, [=] MFEM_HOST_DEVICE (int i)
   {
      const int j_s = d_hat_dof_gather_map[i];
      if (j_s == -1) // invalid
      {
         d_b_hat[i] = 0.0;
      }
      else
      {
         const int sgn = (j_s >= 0) ? 1 : -1;
         const int j = (j_s >= 0) ? j_s : -2 - j_s;
         d_b_hat[i] = sgn*d_b_lvec[j];
      }
   });
}

void HybridizationExtension::MultAhatInv(Vector &x) const
{
   const int ne = h.fes.GetMesh()->GetNE();
   const int n = h.fes.GetFE(0)->GetDof();

   // TODO: better batched linear algebra (e.g. cuBLAS)
   DenseTensor Ahat_inv_dt;
   Ahat_inv_dt.NewMemoryAndSize(Ahat_inv.GetMemory(), n, n, ne, false);
   BatchLUSolve(Ahat_inv_dt, Ahat_piv, x);
}

void HybridizationExtension::ReduceRHS(const Vector &b, Vector &b_r) const
{
   Vector b_hat(num_hat_dofs);
   MultRt(b, b_hat);
   MultAhatInv(b_hat);
   const Operator *P = h.c_fes.GetProlongationMatrix();
   if (P)
   {
      Vector bl(P->Height());
      b_r.SetSize(P->Width());
      MultC(b_hat, bl);
      P->MultTranspose(bl, b_r);
   }
   else
   {
      MultC(b_hat, b_r);
   }
}

void HybridizationExtension::ComputeSolution(
   const Vector &b, const Vector &sol_r, Vector &sol) const
{
   // tmp1 = A_hat^{-1} ( R^T b - C^T lambda )
   Vector b_hat(num_hat_dofs);
   MultRt(b, b_hat);

   tmp1.SetSize(num_hat_dofs);
   const Operator *P = h.c_fes.GetProlongationMatrix();
   if (P)
   {
      Vector sol_l(P->Height());
      P->Mult(sol_r, sol_l);
      MultCt(sol_l, tmp1);
   }
   else
   {
      MultCt(sol_r, tmp1);
   }
   add(b_hat, -1.0, tmp1, tmp1);
   // Eliminate essential DOFs
   const bool *d_ess_hat_dof_marker = ess_hat_dof_marker.Read();
   real_t *d_tmp1 = tmp1.ReadWrite();
   mfem::forall(num_hat_dofs, [=] MFEM_HOST_DEVICE (int i)
   {
      if (d_ess_hat_dof_marker[i])
      {
         d_tmp1[i] = 0.0;
      }
   });
   MultAhatInv(tmp1);
   MultR(tmp1, sol);
}

}