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

#include "forall.hpp"
#include "occa.hpp"
#ifdef MFEM_USE_CEED
#include "../fem/ceed/interface/util.hpp"
#endif
#ifdef MFEM_USE_MPI
#include "../linalg/hypre.hpp"
#endif

#include <unordered_map>
#include <string>
#include <map>

#define DBG_COLOR ::debug::kHotPink
#include "general/debug.hpp"

namespace mfem
{

// Place the following variables in the mfem::internal namespace, so that they
// will not be included in the doxygen documentation.
namespace internal
{

#ifdef MFEM_USE_OCCA
// Default occa::device used by MFEM.
occa::device occaDevice;
#endif

#ifdef MFEM_USE_CEED
Ceed ceed = NULL;

ceed::BasisMap ceed_basis_map;
ceed::RestrMap ceed_restr_map;
#endif

// Backends listed by priority, high to low:
static const Backend::Id backend_list[Backend::NUM_BACKENDS] =
{
   Backend::CEED_CUDA, Backend::OCCA_CUDA, Backend::RAJA_CUDA, Backend::CUDA,
   Backend::CEED_HIP, Backend::RAJA_HIP, Backend::HIP, Backend::DEBUG_DEVICE,
   Backend::OCCA_OMP, Backend::RAJA_OMP, Backend::OMP,
   Backend::CEED_CPU, Backend::OCCA_CPU, Backend::RAJA_CPU,
   Backend::METAL, Backend::CPU
};

// Backend names listed by priority, high to low:
static const char *backend_name[Backend::NUM_BACKENDS] =
{
   "ceed-cuda", "occa-cuda", "raja-cuda", "cuda",
   "ceed-hip", "raja-hip", "hip", "debug",
   "occa-omp", "raja-omp", "omp",
   "ceed-cpu", "occa-cpu", "raja-cpu", "metal",
   "cpu"
};

} // namespace mfem::internal


// Initialize the unique global Device variable.
Device Device::device_singleton;
bool Device::device_env = false;
bool Device::mem_host_env = false;
bool Device::mem_device_env = false;
bool Device::mem_types_set = false;

Device::Device()
{
   if (getenv("MFEM_MEMORY") && !mem_host_env && !mem_device_env)
   {
      std::string mem_backend(getenv("MFEM_MEMORY"));
      if (mem_backend == "host")
      {
         mem_host_env = true;
         host_mem_type = MemoryType::HOST;
         device_mem_type = MemoryType::HOST;
      }
      else if (mem_backend == "host32")
      {
         mem_host_env = true;
         host_mem_type = MemoryType::HOST_32;
         device_mem_type = MemoryType::HOST_32;
      }
      else if (mem_backend == "host64")
      {
         mem_host_env = true;
         host_mem_type = MemoryType::HOST_64;
         device_mem_type = MemoryType::HOST_64;
      }
      else if (mem_backend == "umpire")
      {
         mem_host_env = true;
         host_mem_type = MemoryType::HOST_UMPIRE;
         // Note: device_mem_type will be set to MemoryType::DEVICE_UMPIRE only
         // when an actual device is configured -- this is done later in
         // Device::UpdateMemoryTypeAndClass().
         device_mem_type = MemoryType::HOST_UMPIRE;
      }
      else if (mem_backend == "debug")
      {
         mem_host_env = true;
         host_mem_type = MemoryType::HOST_DEBUG;
         // Note: device_mem_type will be set to MemoryType::DEVICE_DEBUG only
         // when an actual device is configured -- this is done later in
         // Device::UpdateMemoryTypeAndClass().
         device_mem_type = MemoryType::HOST_DEBUG;
      }
      else if (false
#ifdef MFEM_USE_CUDA
               || mem_backend == "cuda"
#endif
#ifdef MFEM_USE_HIP
               || mem_backend == "hip"
#endif
              )
      {
         mem_host_env = true;
         host_mem_type = MemoryType::HOST;
         mem_device_env = true;
         device_mem_type = MemoryType::DEVICE;
      }
      else if (mem_backend == "uvm")
      {
         mem_host_env = true;
         mem_device_env = true;
         host_mem_type = MemoryType::MANAGED;
         device_mem_type = MemoryType::MANAGED;
      }
      else
      {
         MFEM_ABORT("Unknown memory backend!");
      }
      mm.Configure(host_mem_type, device_mem_type);
   }

   if (getenv("MFEM_DEVICE"))
   {
      std::string device(getenv("MFEM_DEVICE"));
      Configure(device);
      device_env = true;
   }
}


Device::~Device()
{
   if ( device_env && !destroy_mm) { return; }
   if (!device_env &&  destroy_mm && !mem_host_env)
   {
      free(device_option);
#ifdef MFEM_USE_CEED
      // Destroy FES -> CeedBasis, CeedElemRestriction hash table contents
      for (auto entry : internal::ceed_basis_map)
      {
         CeedBasisDestroy(&entry.second);
      }
      internal::ceed_basis_map.clear();
      for (auto entry : internal::ceed_restr_map)
      {
         CeedElemRestrictionDestroy(&entry.second);
      }
      internal::ceed_restr_map.clear();
      // Destroy Ceed context
      CeedDestroy(&internal::ceed);
#endif
      mm.Destroy();
   }
   Get().ngpu = -1;
   Get().mode = SEQUENTIAL;
   Get().backends = Backend::CPU;
   Get().host_mem_type = MemoryType::HOST;
   Get().host_mem_class = MemoryClass::HOST;
   Get().device_mem_type = MemoryType::HOST;
   Get().device_mem_class = MemoryClass::HOST;
}

void Device::Configure(const std::string &device, const int device_id)
{
   dbg();

   // If a device was configured via the environment, skip the configuration,
   // and avoid the 'singleton_device' to destroy the mm.
   if (device_env)
   {
      std::memcpy(this, &Get(), sizeof(Device));
      Get().destroy_mm = false;
      return;
   }

   std::map<std::string, Backend::Id> bmap;
   for (int i = 0; i < Backend::NUM_BACKENDS; i++)
   {
      bmap[internal::backend_name[i]] = internal::backend_list[i];
   }
   std::string::size_type beg = 0, end, option;
   while (true)
   {
      end = device.find(',', beg);
      end = (end != std::string::npos) ? end : device.size();
      const std::string bname = device.substr(beg, end - beg);
      option = bname.find(':');
      if (option==std::string::npos) // No option
      {
         const std::string backend = bname;
         auto it = bmap.find(backend);
         MFEM_VERIFY(it != bmap.end(), "invalid backend name: '" << backend << '\'');
         Get().MarkBackend(it->second);
      }
      else
      {
         const std::string backend = bname.substr(0, option);
         const std::string boption = bname.substr(option+1);
         Get().device_option = strdup(boption.c_str());
         auto it = bmap.find(backend);
         MFEM_VERIFY(it != bmap.end(), "invalid backend name: '" << backend << '\'');
         Get().MarkBackend(it->second);
      }
      if (end == device.size()) { break; }
      beg = end + 1;
   }

   // OCCA_CUDA and CEED_CUDA need CUDA or RAJA_CUDA:
   if (Allows(Backend::OCCA_CUDA|Backend::CEED_CUDA) &&
       !Allows(Backend::RAJA_CUDA))
   {
      Get().MarkBackend(Backend::CUDA);
   }
   // CEED_HIP needs HIP:
   if (Allows(Backend::CEED_HIP))
   {
      Get().MarkBackend(Backend::HIP);
   }
   // OCCA_OMP will use OMP or RAJA_OMP unless MFEM_USE_OPENMP=NO:
#ifdef MFEM_USE_OPENMP
   if (Allows(Backend::OCCA_OMP) && !Allows(Backend::RAJA_OMP))
   {
      Get().MarkBackend(Backend::OMP);
   }
#endif

   // Perform setup.
   Get().Setup(device_id);

   // Enable the device
   Enable();

   // Copy all data members from the global 'singleton_device' into '*this'.
   if (this != &Get()) { std::memcpy(this, &Get(), sizeof(Device)); }

   // Only '*this' will call the MemoryManager::Destroy() method.
   destroy_mm = true;

#ifdef MFEM_USE_MPI
   Hypre::InitDevice();
#endif
}

// static method
void Device::SetMemoryTypes(MemoryType h_mt, MemoryType d_mt)
{
   // If the device and/or the MemoryTypes are configured through the
   // environment (variables 'MFEM_DEVICE', 'MFEM_MEMORY'), ignore calls to this
   // method.
   if (mem_host_env || mem_device_env || device_env) { return; }

   MFEM_VERIFY(!IsConfigured(), "the default MemoryTypes can only be set before"
               " Device construction and configuration");
   MFEM_VERIFY(IsHostMemory(h_mt),
               "invalid host MemoryType, h_mt = " << (int)h_mt);
   MFEM_VERIFY(IsDeviceMemory(d_mt) || d_mt == h_mt,
               "invalid device MemoryType, d_mt = " << (int)d_mt
               << " (h_mt = " << (int)h_mt << ')');

   Get().host_mem_type = h_mt;
   Get().device_mem_type = d_mt;
   mem_types_set = true;

   // h_mt and d_mt will be set as dual to each other during configuration by
   // the call mm.Configure(...) in UpdateMemoryTypeAndClass()
}

void Device::Print(std::ostream &os)
{
   os << "Device configuration: ";
   bool add_comma = false;
   for (int i = 0; i < Backend::NUM_BACKENDS; i++)
   {
      if (backends & internal::backend_list[i])
      {
         if (add_comma) { os << ','; }
         add_comma = true;
         os << internal::backend_name[i];
      }
   }
   os << '\n';
#ifdef MFEM_USE_CEED
   if (Allows(Backend::CEED_MASK))
   {
      const char *ceed_backend;
      CeedGetResource(internal::ceed, &ceed_backend);
      os << "libCEED backend: " << ceed_backend << '\n';
   }
#endif
   os << "Memory configuration: "
      << MemoryTypeName[static_cast<int>(host_mem_type)];
   if (Device::Allows(Backend::DEVICE_MASK))
   {
      os << ',' << MemoryTypeName[static_cast<int>(device_mem_type)];
   }
   os << std::endl;
}

void Device::UpdateMemoryTypeAndClass()
{
   const bool debug = Device::Allows(Backend::DEBUG_DEVICE);

   const bool device = Device::Allows(Backend::DEVICE_MASK);

#ifdef MFEM_USE_UMPIRE
   // If MFEM has been compiled with Umpire support, use it as the default
   if (!mem_host_env && !mem_types_set)
   {
      host_mem_type = MemoryType::HOST_UMPIRE;
      if (!mem_device_env)
      {
         device_mem_type = MemoryType::HOST_UMPIRE;
      }
   }
#endif

   // Enable the device memory type
   if (device)
   {
      if (!mem_device_env)
      {
         if (mem_host_env)
         {
            switch (host_mem_type)
            {
               case MemoryType::HOST_UMPIRE:
                  device_mem_type = MemoryType::DEVICE_UMPIRE;
                  break;
               case MemoryType::HOST_DEBUG:
                  device_mem_type = MemoryType::DEVICE_DEBUG;
                  break;
               default:
                  device_mem_type = MemoryType::DEVICE;
            }
         }
         else if (!mem_types_set)
         {
#ifndef MFEM_USE_UMPIRE
            device_mem_type = MemoryType::DEVICE;
#else
            device_mem_type = MemoryType::DEVICE_UMPIRE;
#endif
         }
      }
      device_mem_class = MemoryClass::DEVICE;
   }

   // Enable the UVM shortcut when requested
   if (device && device_option && !strcmp(device_option, "uvm"))
   {
      host_mem_type = MemoryType::MANAGED;
      device_mem_type = MemoryType::MANAGED;
   }

   // Enable the DEBUG mode when requested
   if (debug)
   {
      host_mem_type = MemoryType::HOST_DEBUG;
      device_mem_type = MemoryType::DEVICE_DEBUG;
   }

   MFEM_VERIFY(!device || IsDeviceMemory(device_mem_type),
               "invalid device memory configuration!");

   // Update the memory manager with the new settings
   mm.Configure(host_mem_type, device_mem_type);
}

void Device::Enable()
{
   const bool accelerated = Get().backends & ~(Backend::CPU);
   if (accelerated) { Get().mode = Device::ACCELERATED;}
   Get().UpdateMemoryTypeAndClass();
}

#ifdef MFEM_USE_CUDA
static void DeviceSetup(const int dev, int &ngpu)
{
   ngpu = CuGetDeviceCount();
   MFEM_VERIFY(ngpu > 0, "No CUDA device found!");
   MFEM_GPU_CHECK(cudaSetDevice(dev));
}
#endif

static void CudaDeviceSetup(const int dev, int &ngpu)
{
#ifdef MFEM_USE_CUDA
   DeviceSetup(dev, ngpu);
#else
   MFEM_CONTRACT_VAR(dev);
   MFEM_CONTRACT_VAR(ngpu);
#endif
}

static void MetalDeviceSetup(const int dev, int &ngpu)
{
   dbg();
   MFEM_CONTRACT_VAR(dev);
   MFEM_CONTRACT_VAR(ngpu);
   auto metal = MTL::CreateSystemDefaultDevice();
   dbg("Running on {}", metal->name()->utf8String());
}

static void HipDeviceSetup(const int dev, int &ngpu)
{
#ifdef MFEM_USE_HIP
   MFEM_GPU_CHECK(hipGetDeviceCount(&ngpu));
   MFEM_VERIFY(ngpu > 0, "No HIP device found!");
   MFEM_GPU_CHECK(hipSetDevice(dev));
#else
   MFEM_CONTRACT_VAR(dev);
   MFEM_CONTRACT_VAR(ngpu);
#endif
}

static void RajaDeviceSetup(const int dev, int &ngpu)
{
#ifdef MFEM_USE_CUDA
   if (ngpu <= 0) { DeviceSetup(dev, ngpu); }
#elif defined(MFEM_USE_HIP)
   HipDeviceSetup(dev, ngpu);
#else
   MFEM_CONTRACT_VAR(dev);
   MFEM_CONTRACT_VAR(ngpu);
#endif
}

static void OccaDeviceSetup(const int dev)
{
#ifdef MFEM_USE_OCCA
   const int cpu  = Device::Allows(Backend::OCCA_CPU);
   const int omp  = Device::Allows(Backend::OCCA_OMP);
   const int cuda = Device::Allows(Backend::OCCA_CUDA);
   if (cpu + omp + cuda > 1)
   {
      MFEM_ABORT("Only one OCCA backend can be configured at a time!");
   }
   if (cuda)
   {
#if OCCA_CUDA_ENABLED
      std::string mode("mode: 'CUDA', device_id : ");
      internal::occaDevice.setup(mode.append(1,'0'+dev));
#else
      MFEM_ABORT("the OCCA CUDA backend requires OCCA built with CUDA!");
#endif
   }
   else if (omp)
   {
#if OCCA_OPENMP_ENABLED
      internal::occaDevice.setup("mode: 'OpenMP'");
#else
      MFEM_ABORT("the OCCA OpenMP backend requires OCCA built with OpenMP!");
#endif
   }
   else
   {
      internal::occaDevice.setup("mode: 'Serial'");
   }

   std::string mfemDir;
   if (occa::io::exists(MFEM_INSTALL_DIR "/include/mfem/"))
   {
      mfemDir = MFEM_INSTALL_DIR "/include/mfem/";
   }
   else if (occa::io::exists(MFEM_SOURCE_DIR))
   {
      mfemDir = MFEM_SOURCE_DIR;
   }
   else
   {
      MFEM_ABORT("Cannot find OCCA kernels in MFEM_INSTALL_DIR or MFEM_SOURCE_DIR");
   }

   occa::io::addLibraryPath("mfem", mfemDir);
   occa::loadKernels("mfem");
#else
   MFEM_CONTRACT_VAR(dev);
   MFEM_ABORT("the OCCA backends require MFEM built with MFEM_USE_OCCA=YES");
#endif
}

static void CeedDeviceSetup(const char* ceed_spec)
{
#ifdef MFEM_USE_CEED
   CeedInit(ceed_spec, &internal::ceed);
   const char *ceed_backend;
   CeedGetResource(internal::ceed, &ceed_backend);
   if (strcmp(ceed_spec, ceed_backend) && strcmp(ceed_spec, "/cpu/self") &&
       strcmp(ceed_spec, "/gpu/hip"))
   {
      mfem::out << std::endl << "WARNING!!!\n"
                "libCEED is not using the requested backend!!!\n"
                "WARNING!!!\n" << std::endl;
   }
#ifdef MFEM_DEBUG
   CeedSetErrorHandler(internal::ceed, CeedErrorStore);
#endif
#else
   MFEM_CONTRACT_VAR(ceed_spec);
#endif
}

void Device::Setup(const int device_id)
{
   dbg();
   MFEM_VERIFY(ngpu == -1, "the mfem::Device is already configured!");

   ngpu = 0;
   dev = device_id;
#ifndef MFEM_USE_CUDA
   MFEM_VERIFY(!Allows(Backend::CUDA_MASK),
               "the CUDA backends require MFEM built with MFEM_USE_CUDA=YES");
#endif
#ifndef MFEM_USE_HIP
   MFEM_VERIFY(!Allows(Backend::HIP_MASK),
               "the HIP backends require MFEM built with MFEM_USE_HIP=YES");
#endif
#ifndef MFEM_USE_RAJA
   MFEM_VERIFY(!Allows(Backend::RAJA_MASK),
               "the RAJA backends require MFEM built with MFEM_USE_RAJA=YES");
#endif
#ifndef MFEM_USE_OPENMP
   MFEM_VERIFY(!Allows(Backend::OMP|Backend::RAJA_OMP),
               "the OpenMP and RAJA OpenMP backends require MFEM built with"
               " MFEM_USE_OPENMP=YES");
#endif
#ifndef MFEM_USE_CEED
   MFEM_VERIFY(!Allows(Backend::CEED_MASK),
               "the CEED backends require MFEM built with MFEM_USE_CEED=YES");
#else
   int ceed_cpu  = Allows(Backend::CEED_CPU);
   int ceed_cuda = Allows(Backend::CEED_CUDA);
   int ceed_hip  = Allows(Backend::CEED_HIP);
   MFEM_VERIFY(ceed_cpu + ceed_cuda + ceed_hip <= 1,
               "Only one CEED backend can be enabled at a time!");
#endif
   if (Allows(Backend::CUDA)) { CudaDeviceSetup(dev, ngpu); }
   if (Allows(Backend::METAL)) { MetalDeviceSetup(dev, ngpu); }
   if (Allows(Backend::HIP)) { HipDeviceSetup(dev, ngpu); }
   if (Allows(Backend::RAJA_CUDA) || Allows(Backend::RAJA_HIP))
   { RajaDeviceSetup(dev, ngpu); }
   // The check for MFEM_USE_OCCA is in the function OccaDeviceSetup().
   if (Allows(Backend::OCCA_MASK)) { OccaDeviceSetup(dev); }
   if (Allows(Backend::CEED_CPU))
   {
      if (!device_option)
      {
         CeedDeviceSetup("/cpu/self");
      }
      else
      {
         CeedDeviceSetup(device_option);
      }
   }
   if (Allows(Backend::CEED_CUDA))
   {
      if (!device_option)
      {
         // NOTE: libCEED's /gpu/cuda/gen backend is non-deterministic!
         CeedDeviceSetup("/gpu/cuda/gen");
      }
      else
      {
         CeedDeviceSetup(device_option);
      }
   }
   if (Allows(Backend::CEED_HIP))
   {
      if (!device_option)
      {
         CeedDeviceSetup("/gpu/hip");
      }
      else
      {
         CeedDeviceSetup(device_option);
      }
   }
   if (Allows(Backend::DEBUG_DEVICE)) { ngpu = 1; }
}

} // mfem
