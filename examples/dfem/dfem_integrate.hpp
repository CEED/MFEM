#pragma once

#include "dfem_util.hpp"
#include <type_traits>

namespace mfem
{

template <typename output_t>
MFEM_HOST_DEVICE
void map_quadrature_data_to_fields_impl(
   DeviceTensor<2, double> &y,
   const DeviceTensor<3, double> &f,
   const output_t &output,
   const DofToQuadMap &dtq)
{
   auto B = dtq.B;
   auto G = dtq.G;
   // assuming the quadrature point residual has to "play nice with
   // the test function"
   if constexpr (is_value_fop<std::decay_t<output_t>>::value)
   {
      const auto [num_qp, cdim, num_dof] = B.GetShape();
      const int vdim = output.vdim > 0 ? output.vdim : cdim ;
      for (int dof = 0; dof < num_dof; dof++)
      {
         for (int vd = 0; vd < vdim; vd++)
         {
            double acc = 0.0;
            for (int qp = 0; qp < num_qp; qp++)
            {
               acc += B(qp, 0, dof) * f(vd, 0, qp);
            }
            y(dof, vd) += acc;
         }
      }
   }
   else if constexpr (
      is_gradient_fop<std::decay_t<output_t>>::value)
   {
      const auto [num_qp, dim, num_dof] = G.GetShape();
      const int vdim = output.vdim;
      for (int dof = 0; dof < num_dof; dof++)
      {
         for (int vd = 0; vd < vdim; vd++)
         {
            double acc = 0.0;
            for (int d = 0; d < dim; d++)
            {
               for (int qp = 0; qp < num_qp; qp++)
               {
                  acc += G(qp, d, dof) * f(vd, d, qp);
               }
            }
            y(dof, vd) += acc;
         }
      }
   }
   else if constexpr (is_one_fop<std::decay_t<output_t>>::value)
   {
      // This is the "integral over all quadrature points type" applying
      // B = 1 s.t. B^T * C \in R^1.
      const auto [num_qp, unused, unused1] = B.GetShape();
      auto cc = Reshape(&f(0, 0, 0), num_qp);
      for (int i = 0; i < num_qp; i++)
      {
         y(0, 0) += cc(i);
      }
   }
   else if constexpr (is_none_fop<std::decay_t<output_t>>::value)
   {
      const auto [num_qp, unused, num_dof] = B.GetShape();
      const auto vdim = output.vdim;
      auto cc = Reshape(&f(0, 0, 0), num_qp * vdim);
      auto yy = Reshape(&y(0, 0), num_qp * vdim);
      for (int i = 0; i < num_qp * vdim; i++)
      {
         yy(i) = cc(i);
      }
   }
   else
   {
      MFEM_ABORT("quadrature data mapping to field is not implemented for"
                 " this field descriptor");
   }
}

template <typename output_t>
MFEM_HOST_DEVICE
void map_quadrature_data_to_fields_tensor_impl(
   DeviceTensor<2, double> &y,
   const DeviceTensor<3, double> &f,
   const output_t &output,
   const DofToQuadMap &dtq,
   std::array<DeviceTensor<1>, 6> &scratch_mem)
{
   auto B = dtq.B;
   auto G = dtq.G;

   if constexpr (is_value_fop<std::decay_t<output_t>>::value)
   {
      const auto [q1d, unused, d1d] = B.GetShape();
      const int vdim = output.vdim;
      const int test_dim = output.size_on_qp / vdim;

      auto fqp = Reshape(&f(0, 0, 0), vdim, test_dim, q1d, q1d, q1d);
      auto yd = Reshape(&y(0, 0), d1d, d1d, d1d, vdim);

      auto s0 = Reshape(&scratch_mem[0](0), q1d, q1d, d1d);
      auto s1 = Reshape(&scratch_mem[1](0), q1d, d1d, d1d);

      for (int vd = 0; vd < vdim; vd++)
      {
         MFEM_FOREACH_THREAD(qy, y, q1d)
         {
            MFEM_FOREACH_THREAD(dx, x, d1d)
            {
               MFEM_FOREACH_THREAD(qz, z, q1d)
               {
                  double acc = 0.0;
                  for (int qx = 0; qx < q1d; qx++)
                  {
                     acc += fqp(vd, 0, qx, qy, qz) * B(qx, 0, dx);
                  }
                  s0(qz, qy, dx) = acc;
               }
            }
         }
         MFEM_SYNC_THREAD;

         MFEM_FOREACH_THREAD(dy, y, d1d)
         {
            MFEM_FOREACH_THREAD(dx, x, d1d)
            {
               MFEM_FOREACH_THREAD(qz, z, q1d)
               {
                  double acc = 0.0;
                  for (int qy = 0; qy < q1d; qy++)
                  {
                     acc += s0(qz, qy, dx) * B(qy, 0, dy);
                  }
                  s1(qz, dy, dx) = acc;
               }
            }
         }
         MFEM_SYNC_THREAD;


         MFEM_FOREACH_THREAD(dy, y, d1d)
         {
            MFEM_FOREACH_THREAD(dx, x, d1d)
            {
               MFEM_FOREACH_THREAD(dz, z, d1d)
               {
                  double acc = 0.0;
                  for (int qz = 0; qz < q1d; qz++)
                  {
                     acc += s1(qz, dy, dx) * B(qz, 0, dz);
                  }
                  yd(dx, dy, dz, vd) += acc;
               }
            }
         }
         MFEM_SYNC_THREAD;
      }
   }
   else if constexpr (is_gradient_fop<std::decay_t<output_t>>::value)
   {
      const auto [q1d, unused, d1d] = G.GetShape();
      const int vdim = output.vdim;
      const int test_dim = output.size_on_qp / vdim;
      auto fqp = Reshape(&f(0, 0, 0), vdim, test_dim, q1d, q1d, q1d);
      auto yd = Reshape(&y(0, 0), d1d, d1d, d1d, vdim);

      auto s0 = Reshape(&scratch_mem[0](0), q1d, q1d, d1d);
      auto s1 = Reshape(&scratch_mem[1](0), q1d, q1d, d1d);
      auto s2 = Reshape(&scratch_mem[2](0), q1d, q1d, d1d);
      auto s3 = Reshape(&scratch_mem[3](0), q1d, d1d, d1d);
      auto s4 = Reshape(&scratch_mem[4](0), q1d, d1d, d1d);
      auto s5 = Reshape(&scratch_mem[5](0), q1d, d1d, d1d);

      for (int vd = 0; vd < vdim; vd++)
      {
         MFEM_FOREACH_THREAD(qz, z, q1d)
         {
            MFEM_FOREACH_THREAD(qy, y, q1d)
            {
               MFEM_FOREACH_THREAD(dx, x, d1d)
               {
                  real_t uvw[3] = {0.0, 0.0, 0.0};
                  for (int qx = 0; qx < q1d; qx++)
                  {
                     uvw[0] += fqp(vd, 0, qx, qy, qz) * G(qx, 0, dx);
                     uvw[1] += fqp(vd, 1, qx, qy, qz) * B(qx, 0, dx);
                     uvw[2] += fqp(vd, 2, qx, qy, qz) * B(qx, 0, dx);
                  }
                  s0(qz, qy, dx) = uvw[0];
                  s1(qz, qy, dx) = uvw[1];
                  s2(qz, qy, dx) = uvw[2];
               }
            }
         }
         MFEM_SYNC_THREAD;

         MFEM_FOREACH_THREAD(qz, z, q1d)
         {
            MFEM_FOREACH_THREAD(dy, y, d1d)
            {
               MFEM_FOREACH_THREAD(dx, x, d1d)
               {
                  real_t uvw[3] = {0.0, 0.0, 0.0};
                  for (int qy = 0; qy < q1d; qy++)
                  {
                     uvw[0] += s0(qz, qy, dx) * B(qy, 0, dy);
                     uvw[1] += s1(qz, qy, dx) * G(qy, 0, dy);
                     uvw[2] += s2(qz, qy, dx) * B(qy, 0, dy);
                  }
                  s3(qz, dy, dx) = uvw[0];
                  s4(qz, dy, dx) = uvw[1];
                  s5(qz, dy, dx) = uvw[2];
               }
            }
         }
         MFEM_SYNC_THREAD;

         MFEM_FOREACH_THREAD(dz, z, d1d)
         {
            MFEM_FOREACH_THREAD(dy, y, d1d)
            {
               MFEM_FOREACH_THREAD(dx, x, d1d)
               {
                  real_t uvw[3] = {0.0, 0.0, 0.0};
                  for (int qz = 0; qz < q1d; qz++)
                  {
                     uvw[0] += s3(qz, dy, dx) * B(qz, 0, dz);
                     uvw[1] += s4(qz, dy, dx) * B(qz, 0, dz);
                     uvw[2] += s5(qz, dy, dx) * G(qz, 0, dz);
                  }
                  yd(dx, dy, dz, vd) += uvw[0] + uvw[1] + uvw[2];
               }
            }
         }
         MFEM_SYNC_THREAD;
      }
   }
   else if constexpr (is_none_fop<std::decay_t<output_t>>::value)
   {
      const auto [q1d, unused, d1d] = B.GetShape();
      auto fqp = Reshape(&f(0, 0, 0), output.size_on_qp, q1d, q1d, q1d);
      auto yqp = Reshape(&y(0, 0), output.size_on_qp, q1d, q1d, q1d);

      for (int sq = 0; sq < output.size_on_qp; sq++)
      {
         MFEM_FOREACH_THREAD(qx, x, q1d)
         {
            MFEM_FOREACH_THREAD(qy, y, q1d)
            {
               MFEM_FOREACH_THREAD(qz, z, q1d)
               {
                  yqp(sq, qx, qy, qz) = fqp(sq, qx, qy, qz);
               }
            }
         }
         MFEM_SYNC_THREAD;
      }
   }
   else
   {
      MFEM_ABORT("quadrature data mapping to field is not implemented for"
                 " this field descriptor with sum factorization on tensor product elements");
   }
}

template <typename output_t>
MFEM_HOST_DEVICE
void map_quadrature_data_to_fields(
   DeviceTensor<2, double> &y,
   const DeviceTensor<3, double> &f,
   const output_t &output,
   const DofToQuadMap &dtq,
   std::array<DeviceTensor<1>, 6> &scratch_mem,
   const bool &use_sum_factorization)
{
   if (use_sum_factorization)
   {
      map_quadrature_data_to_fields_tensor_impl(y, f, output, dtq, scratch_mem);
   }
   else
   {
      map_quadrature_data_to_fields_impl(y, f, output, dtq);
   }
}

}
