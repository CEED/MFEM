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

#ifndef MFEM_KERNEL_DISPATCH_T_HPP
#define MFEM_KERNEL_DISPATCH_T_HPP

#include "../config/config.hpp"
#include "kernel_reporter.hpp"
#include <unordered_map>
#include <tuple>
#include <cstddef>

#include "./tmop/tmop_pa.hpp"

namespace mfem
{
template <typename... Ts>
void printTypes()
{
   ((std::cout << "\033[33m" << typeid(Ts).name() << "\033[m" << std::endl),
    ...);
}

template <typename... Args>
void printValues(Args &&...args)
{
   std::cout << "\033[32m";
   (std::cout << ... << args) << std::endl;
   std::cout << "\033[m";
}

using metric_t = decltype(mfem::TMOP_PA_Metric_001{});

#define MFEM_PARAM_LIST(...) __VA_ARGS__

// P1 are the parameters, P2 are the optional (non-dispatch parameters), and P3
// is the concatenation of P1 and P2. We need to pass it as a separate argument
// to avoid a trailing comma in the case that P2 is empty.
#define MFEM_REGISTER_KERNELS_T(KernelName, KernelType, P1)            \
   class KernelName : public KernelDispatchTable<                      \
                         KernelName, KernelType,                       \
                         internal::KernelTypeList<metric_t, int, int>> \
   {                                                                   \
   public:                                                             \
      const char *kernel_name = MFEM_KERNEL_NAME(KernelName);          \
      using KernelSignature = KernelType;                              \
      template <auto...>                                               \
      static KernelSignature Kernel();                                 \
      static KernelSignature Fallback(metric_t, int, int);             \
      static KernelName &Get()                                         \
      {                                                                \
         static KernelName table;                                      \
         return table;                                                 \
      }                                                                \
   }

/// @brief Hashes variadic packs for which each type contained in the variadic
/// pack has a specialization of `std::hash` available.
///
/// For example, packs containing int, bool, enum values, etc.
template <typename... KernelParameters>
struct KernelDispatchKeyHash
{
private:
   template <int N>
   size_t operator()(std::tuple<KernelParameters...> value) const
   {
      return 0;
   }

   // The hashing formula here is taken directly from the Boost library, with
   // the magic number 0x9e3779b9 chosen to minimize hashing collisions.
   template <std::size_t N, typename THead, typename... TTail>
   size_t operator()(std::tuple<KernelParameters...> value) const
   {
      constexpr int Index = N - sizeof...(TTail) - 1;
      auto lhs_hash = std::hash<THead>()(std::get<Index>(value));
      auto rhs_hash = operator()<N, TTail...>(value);
      return lhs_hash ^
             (rhs_hash + 0x9e3779b9 + (lhs_hash << 6) + (lhs_hash >> 2));
   }

public:
   /// Returns the hash of the given @a value.
   size_t operator()(std::tuple<KernelParameters...> value) const
   {
      return operator()<sizeof...(KernelParameters), KernelParameters...>(
         value);
   }
};

namespace internal
{
template <typename... Types> struct KernelTypeList
{
};
} // namespace internal

template <typename... T> class KernelDispatchTable
{
};

template <typename Kernels, typename Signature> //, typename... Params>
class KernelDispatchTable<Kernels,
                          Signature,
                          //   internal::KernelTypeList<Params...>
                          internal::KernelTypeList<metric_t, int, int>>
{
   std::unordered_map<std::tuple<metric_t, int, int>,
                      Signature,
                      KernelDispatchKeyHash<metric_t, int, int>>
      table;

public:
   /// @brief Run the kernel with the given dispatch parameters and arguments.
   ///
   /// If a compile-time specialized version of the kernel with the given
   /// parameters has been registered, it will be called. Otherwise, the
   /// fallback kernel will be called.
   // static void Run(Params... params, Args &&...args)
   template <typename M, typename... Args>
   static void Run(M m, int d, int q, Args &&...args)
   {
      const auto &table = Kernels::Get().table;
      // const std::tuple<Params...> key = std::make_tuple(params...);
      // const std::tuple<M, int, int> key = std::make_tuple(M{}, d, q);
      const std::tuple key(M{}, d, q);
      const auto it = table.find(key);
      if (it != table.end()) { it->second(std::forward<Args>(args)...); }
      else
      {
         // KernelReporter::ReportFallback(Kernels::Get().kernel_name,
         // params...);
         // Kernels::Fallback(params...)(std::forward<Args>(args)...);
         Kernels::Fallback(m, d, q)(std::forward<Args>(args)...);
      }
   }

   /// Register a specialized kernel for dispatch.
   // template <Params... PARAMS>
   // template <typename... PARAMS>
   // template <typename M, int D, int Q>
   // template <typename M, int D, int Q>
   template <auto... PARAMS>
   struct Specialization
   {
      // Version without optional parameters
      static void Add()
      {
         printTypes<decltype(PARAMS)...>();
         printValues(PARAMS...);

         // std::tuple<Params...> param_tuple(PARAMS...);
         // std::tuple<M, int, int> param_tuple(M{}, D, Q);
         std::tuple param_tuple(PARAMS...);
         Kernels::Get().table[param_tuple] =
            // Kernels::template Kernel<PARAMS...>();
            Kernels::template Kernel<PARAMS...>();
      }
   };
};

} // namespace mfem

#endif // MFEM_KERNEL_DISPATCH_T_HPP
