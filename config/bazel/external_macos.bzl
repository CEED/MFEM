"""Modules for dependencies not included in the Bazel Central Registry"""
# https://bazel.build/rules/lib/repo/local

load("@bazel_tools//tools/build_defs/repo:local.bzl", "new_local_repository")

def _external(_):
    new_local_repository(
        name = "config",
        build_file_content = """
cc_library(
    name = "config",
    defines = ["MFEM_CONFIG_FILE=\\\\\\"config/bazel.hpp\\\\\\""],
    visibility = ["//visibility:public"],
)""",
        path = "",
    )
    new_local_repository(
        name = "mpi",
        build_file_content = """
cc_library(
    name = "mpi",
    srcs = ["lib/libmpi.dylib"],
    hdrs = glob(["include/**/*.h"]),
    includes = ["include"],
    visibility = ["//visibility:public"],
    deps = ["@hypre", "@metis"],
)""",
        path = "/opt/homebrew/Cellar/open-mpi/5.0.3_1",
    )
    new_local_repository(
        name = "hypre",
        build_file_content = """
cc_library(
    name = "hypre",
    srcs = ["lib/libHYPRE.a"],
    hdrs = glob(["include/*.h"]),
    includes = ["include"],
    visibility = ["//visibility:public"],
)""",
        path = "/opt/homebrew/Cellar/hypre/2.31.0",
    )
    new_local_repository(
        name = "metis",
        build_file_content = """
cc_library(
    name = "metis",
    srcs = ["lib/libmetis.dylib"],
    hdrs = glob(["include/metis.h"]),
    includes = ["include"],
    visibility = ["//visibility:public"],
)""",
        path = "/opt/homebrew/Cellar/metis/5.1.0",
    )

external = module_extension(implementation = _external)