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

#include "mesh_headers.hpp"
#include <unordered_set>

#ifdef MFEM_USE_NETCDF
#include "netcdf.h"

#define CHECK_NETCDF_CODE(return_code)\
({\
   if ((return_code) != NC_NOERR)\
   {\
      MFEM_ABORT("NetCDF error: " << nc_strerror((return_code)));\
   }\
})

#endif

namespace mfem
{

class ExodusIIWriter
{
public:
   /// @brief Default constructor. Opens ExodusII file.
   /// @param mesh The mesh to write to the file.
   ExodusIIWriter(Mesh & mesh) : _mesh{mesh} {}

   ExodusIIWriter() = delete;

   /// @brief Closes ExodusII file if it has been opened.
   ~ExodusIIWriter();

   /// @brief Writes the mesh to an ExodusII file.
   /// @param fpath The path to the file.
   /// @param flags NC_CLOBBER will overwrite existing file.
   void WriteExodusII(std::string fpath, int flags = NC_CLOBBER);

   /// @brief Static method for writing a mesh to an ExodusII file.
   /// @param mesh The mesh to write to the file.
   /// @param fpath The path to the file.
   /// @param flags NetCDF file flags.
   static void WriteExodusII(Mesh & mesh, std::string fpath,
                             int flags = NC_CLOBBER);

protected:
   /// @brief Closes any open file and creates a NetCDF file using selected flags.
   void OpenExodusII(std::string fpath, int flags);

   /// @brief Closes any open file.
   void CloseExodusII();

   /// @brief Generates blocks based on the elements in the mesh. We assume that
   /// this was originally an Exodus II mesh. Therefore, we iterate over the elements
   /// and use the attributes as the element blocks. We assume that all elements
   /// belonging to the same block will have the same attribute. We can perform a
   /// safety check as well by ensuring that all elements in the block have the same
   /// element type. If this is not the case then something has gone horribly wrong!
   void GenerateExodusIIElementBlocks();

   /// @brief Extracts boundary ids and determines the element IDs and side IDs
   /// (Exodus II) for  each boundary element.
   void GenerateExodusIIBoundaryInfo();

   /// @brief Iterates over the elements to extract a unique set of node IDs
   /// (or vertex IDs if first-order).
   std::unordered_set<int> GenerateUniqueNodeIDs();

   /// @brief Populates vectors with x, y, z coordinates from mesh.
   void ExtractVertexCoordinates(std::vector<double> & coordx,
                                 std::vector<double> & coordy,
                                 std::vector<double> & coordz);

   /// @brief Writes node connectivity for a particular block.
   /// @param block_id The block to write to the file.
   void WriteNodeConnectivityForBlock(const int block_id);

   /// @brief Writes boundary information to file. @a GenerateExodusIIBoundaryInfo
   /// must be called first.
   void WriteBoundaries();

   /// @brief Writes the block IDs to the file.
   void WriteBlockIDs();

   /// @brief Writes a title to the file.
   void WriteTitle();

   /// @brief Writes the number of elements in the mesh.
   void WriteNumOfElements();

   /// @brief Writes the floating-point word size (4 == float; 8 == double).
   void WriteFloatingPointWordSize();

   /// @brief Writes the API version.
   void WriteAPIVersion();

   /// @brief Writes the database version.
   void WriteDatabaseVersion();

   /// @brief Writes the maximum length of a line.
   void WriteMaxLineLength();

   /// @brief Writes the maximum length of a name.
   void WriteMaxNameLength();

   /// @brief  Writes the number of blocks.
   void WriteNumElementBlocks();

   /// @brief Writes all element block parameters.
   void WriteElementBlocks();

   /// @brief Called by @a WriteElementBlockParameters in for-loop.
   /// @param block_id Block to write parameters.
   void WriteElementBlockParameters(int block_id);

   /// @brief Writes the coordinates of nodes.
   void WriteNodalCoordinates();

   /// @brief Writes the file size (normal=0; large=1). Coordinates are specified
   /// separately as components for large files (i.e. xxx, yyy, zzz) as opposed to
   /// (xyz, xyz, xyz) for normal files.
   void WriteFileSize();

   /// @brief Writes the nodesets. Currently, we do not support nodesets.
   void WriteNodeSets();

   /// @brief Writes the mesh dimension.
   void WriteMeshDimension();

   /// @brief Writes the number of timesteps. Currently, we do not support multiple
   /// timesteps.
   void WriteTimesteps();

   /// @brief Writes a dummy variable. This is to circumvent a bug in LibMesh where
   /// it will skip the x-coordinate when reading in an ExodusII file if the id of
   /// the x-coordinates is 0. To prevent this, we define a dummy variable before
   /// defining the coordinates. This ensures that the coordinate variable IDs have
   /// values greater than zero.
   void WriteDummyVariable();

   /// @brief Wrapper around nc_def_dim with error handling.
   void DefineDimension(const char *name, size_t len, int *dim_id);

   /// @brief Wrapper around nc_def_var with error handling.
   void DefineVar(const char *name, nc_type xtype, int ndims, const int *dimidsp,
                  int *varidp);

   /// @brief Write variable data to the file. This is a wrapper around nc_put_var
   /// with error handling.
   void PutVar(int varid, const void * data);

   /// @brief Combine DefineVar with PutVar.
   void DefineAndPutVar(const char *name, nc_type xtype, int ndims,
                        const int *dimidsp, const void *data);


   /// @brief Write attribute to the file. This is a wrapper around nc_put_att with
   /// error handling.
   void PutAtt(int varid, const char *name, nc_type xtype, size_t len,
               const void * data);

   /// @brief Returns a pointer to a static buffer containing the character
   /// string with formatting. Used to generate variable labels.
   char * GenerateLabel(const char * format, ...);

   /// @brief Writes boiler-plate information for ExodusII file format including
   /// title, database version, file size etc.
   void WriteExodusIIFileInformation();

   /// @brief Writes all information about the mesh to the ExodusII file.
   void WriteExodusIIMeshInformation();

   /// @brief Sanity check. Higher-order meshes are currently not supported.
   void EnsureMeshIsFirstOrder();
private:
   // ExodusII file ID.
   int _exid{-1};

   /// Flag to check if a file is currently open.
   bool _file_open{false};

   // Reference to mesh we would like to write-out.
   Mesh & _mesh;

   // Block information.
   std::vector<int> _block_ids;
   std::map<int, Element::Type> _element_type_for_block_id;
   std::map<int, std::vector<int>> _element_ids_for_block_id;

   std::vector<int> _boundary_ids;
   std::map<int, std::vector<int>> _exodusII_element_ids_for_boundary_id;
   std::map<int, std::vector<int>> _exodusII_side_ids_for_boundary_id;
};

void ExodusIIWriter::DefineDimension(const char *name, size_t len, int *dim_id)
{
   nc_redef(_exid);
   CHECK_NETCDF_CODE(nc_def_dim(_exid, name, len, dim_id));
}

void ExodusIIWriter::DefineVar(const char *name, nc_type xtype, int ndims,
                               const int *dimidsp, int *varidp)
{
   nc_redef(_exid);  // Switch to define mode.
   CHECK_NETCDF_CODE(nc_def_var(_exid, name, xtype, ndims, dimidsp,
                                varidp));
}

void ExodusIIWriter::PutAtt(int varid, const char *name, nc_type xtype,
                            size_t len, const void * data)
{
   nc_redef(_exid);
   CHECK_NETCDF_CODE(nc_put_att(_exid, varid, name, xtype, len, data));
}

void ExodusIIWriter::PutVar(int varid, const void * data)
{
   nc_enddef(_exid); // Switch to data mode.
   CHECK_NETCDF_CODE(nc_put_var(_exid, varid, data));
}

void ExodusIIWriter::DefineAndPutVar(const char *name, nc_type xtype, int ndims,
                                     const int *dimidsp, const void *data)
{
   int varid;
   DefineVar(name, xtype, ndims, dimidsp, &varid);
   PutVar(varid, data);
}

const char * EXODUS_TITLE_LABEL = "title";
const char * EXODUS_NUM_ELEM_LABEL = "num_elem";
const char * EXODUS_FLOATING_POINT_WORD_SIZE_LABEL = "floating_point_word_size";
const char * EXODUS_API_VERSION_LABEL = "api_version";
const char * EXODUS_DATABASE_VERSION_LABEL = "version";
const char * EXODUS_MAX_NAME_LENGTH_LABEL = "maximum_name_length";
const char * EXODUS_MAX_LINE_LENGTH_LABEL = "maximum_line_length";
const char * EXODUS_NUM_BLOCKS_LABEL = "block_dim";
const char * EXODUS_COORDX_LABEL = "coordx";
const char * EXODUS_COORDY_LABEL = "coordy";
const char * EXODUS_COORDZ_LABEL = "coordz";
const char * EXODUS_NUM_BOUNDARIES_LABEL = "boundary_ids_dim";
const char * EXODUS_FILE_SIZE_LABEL = "file_size";
const char * EXODUS_NUM_DIM_LABEL = "num_dim";
const char * EXODUS_NUM_NODE_SETS_LABEL = "num_node_sets";
const char * EXODUS_TIME_STEP_LABEL = "time_step";

// Returns the Exodus II face ID for the MFEM face index.
const int mfem_to_exodusII_side_map_tet4[] =
{
   2, 3, 1, 4
};

const int mfem_to_exodusII_side_map_hex8[] =
{
   5, 1, 2, 3, 4, 6
};

const int mfem_to_exodusII_side_map_wedge6[] =
{
   4, 5, 1, 2, 3
};

const int mfem_to_exodusII_side_map_pyramid5[] =
{
   5, 1, 2, 3, 4
};

void ExodusIIWriter::WriteExodusIIFileInformation()
{
   WriteTitle();

   WriteDatabaseVersion();
   WriteAPIVersion();

   WriteFloatingPointWordSize();

   WriteFileSize();

   WriteMaxNameLength();
   WriteMaxLineLength();

   WriteDummyVariable();
}

void ExodusIIWriter::WriteExodusIIMeshInformation()
{
   WriteMeshDimension();
   WriteNumOfElements();

   WriteTimesteps();

   WriteNodalCoordinates();

   WriteElementBlocks();
   WriteBoundaries();
   WriteNodeSets();
}

void ExodusIIWriter::EnsureMeshIsFirstOrder()
{
   if (_mesh.GetNodalFESpace() != nullptr)
   {
      MFEM_ABORT("ExodusII writer does not currently support higher-order elements.");
   }
}

void ExodusIIWriter::WriteExodusII(std::string fpath, int flags)
{
   EnsureMeshIsFirstOrder();

   OpenExodusII(fpath, flags);

   WriteExodusIIFileInformation();
   WriteExodusIIMeshInformation();

   CloseExodusII();

   mfem::out << "Mesh successfully written to Exodus II file" << std::endl;
}

void ExodusIIWriter::WriteExodusII(Mesh & mesh, std::string fpath,
                                   int flags)
{
   ExodusIIWriter writer(mesh);

   writer.WriteExodusII(fpath, flags);
}

void Mesh::WriteExodusII(const std::string fpath)
{
   ExodusIIWriter::WriteExodusII(*this, fpath);
}

void ExodusIIWriter::OpenExodusII(std::string fpath, int flags)
{
   CloseExodusII();  // Close any open files.

   CHECK_NETCDF_CODE(nc_create(fpath.c_str(), flags, &_exid));

   _file_open = true;
}

void ExodusIIWriter::CloseExodusII()
{
   if (!_file_open) { return; }   // No files open.

   CHECK_NETCDF_CODE(nc_close(_exid));

   _file_open = false;
   _exid = (-1);  // Set to negative value (valid IDs are positive!)
}

ExodusIIWriter::~ExodusIIWriter()
{
   CloseExodusII();
}


void ExodusIIWriter::WriteTitle()
{
   const char *title = "MFEM mesh";

   PutAtt(NC_GLOBAL, EXODUS_TITLE_LABEL, NC_CHAR, strlen(title), title);
}

void ExodusIIWriter::WriteNumOfElements()
{
   int num_elem_id;
   DefineDimension(EXODUS_NUM_ELEM_LABEL, _mesh.GetNE(), &num_elem_id);
}

void ExodusIIWriter::WriteFloatingPointWordSize()
{
   const int word_size = 8;
   PutAtt(NC_GLOBAL, EXODUS_FLOATING_POINT_WORD_SIZE_LABEL,
          NC_INT, 1,
          &word_size);
}

void ExodusIIWriter::WriteAPIVersion()
{
   float version = 4.72;   // Current version as of 2024-03-21.
   PutAtt(NC_GLOBAL, EXODUS_API_VERSION_LABEL, NC_FLOAT, 1,
          &version);
}

void ExodusIIWriter::WriteDatabaseVersion()
{
   float database_version = 4.72;
   PutAtt(NC_GLOBAL, EXODUS_DATABASE_VERSION_LABEL, NC_FLOAT, 1,
          &database_version);
}

void ExodusIIWriter::WriteMaxNameLength()
{
   const int max_name_length = 80;
   PutAtt(NC_GLOBAL, EXODUS_MAX_NAME_LENGTH_LABEL, NC_INT, 1,
          &max_name_length);
}

void ExodusIIWriter::WriteMaxLineLength()
{
   const int max_line_length = 80;
   PutAtt(NC_GLOBAL, EXODUS_MAX_LINE_LENGTH_LABEL, NC_INT, 1,
          &max_line_length);
}

void ExodusIIWriter::WriteBlockIDs()
{
   int block_dim;
   DefineDimension(EXODUS_NUM_BLOCKS_LABEL, _block_ids.size(), &block_dim);

   DefineAndPutVar("eb_prop1", NC_INT, 1, &block_dim,
                   _block_ids.data());
}

void ExodusIIWriter::WriteElementBlocks()
{
   GenerateExodusIIElementBlocks();

   WriteNumElementBlocks();
   WriteBlockIDs();

   for (int block_id : _block_ids)
   {
      WriteElementBlockParameters(block_id);
   }
}

char * ExodusIIWriter::GenerateLabel(const char * format, ...)
{
   va_list arglist;
   va_start(arglist, format);

   const size_t buffer_size = 100;

   static char buffer[buffer_size];
   int nwritten = vsnprintf(buffer, buffer_size, format, arglist);

   bool ok = (nwritten > 0 && nwritten < buffer_size);
   if (!ok)
   {
      MFEM_ABORT("Unable to write characters to buffer.");
   }

   va_end(arglist);
   return buffer;
}

void ExodusIIWriter::WriteElementBlockParameters(int block_id)
{
   char * label{nullptr};

   const std::vector<int> & block_element_ids = _element_ids_for_block_id.at(
                                                   block_id);
   const Element * front_element = _mesh.GetElement(block_element_ids.front());

   //
   // Define # elements in the block.
   //
   label = GenerateLabel("num_el_in_blk%d", block_id);

   int num_el_in_blk_id;
   DefineDimension(label, block_element_ids.size(),
                   &num_el_in_blk_id);


   //
   // Define # nodes per element. NB: - assume first-order elements currently!!
   //
   label = GenerateLabel("num_nod_per_el%d", block_id);

   int num_node_per_el_id;
   DefineDimension(label, front_element->GetNVertices(),
                   &num_node_per_el_id);

   //
   // Define # edges per element:
   //
   label = GenerateLabel("num_edg_per_el%d", block_id);

   int num_edg_per_el_id;
   DefineDimension(label, front_element->GetNEdges(),
                   &num_edg_per_el_id);

   //
   // Define # faces per element.
   //
   label = GenerateLabel("num_fac_per_el%d", block_id);

   int num_fac_per_el_id;
   DefineDimension(label, front_element->GetNFaces(),
                   &num_fac_per_el_id);

   //
   // Define element node connectivity for block.
   //
   WriteNodeConnectivityForBlock(block_id);

   //
   // Define the element type.
   //
   std::string element_type;

   switch (front_element->GetType())
   {
      case Geometry::Type::CUBE:
         element_type = "hex";
         break;
      case Geometry::Type::TETRAHEDRON:
         element_type = "tet";
         break;
      case Geometry::Type::PRISM:
         element_type = "wedge";
         break;
      case Geometry::Type::PYRAMID:
         element_type = "pyramid";
         break;
      default:
         MFEM_ABORT("Unsupported MFEM element type: " << front_element->GetType());
   }

   label = GenerateLabel("connect%d", block_id);

   int connect_id;
   CHECK_NETCDF_CODE(nc_inq_varid(_exid, label, &connect_id));

   PutAtt(connect_id, "elem_type", NC_CHAR, element_type.length(),
          element_type.c_str());
}

void ExodusIIWriter::WriteNodalCoordinates()
{
   // 1. Generate the unique node IDs.
   std::unordered_set<int> unique_node_ids = GenerateUniqueNodeIDs();
   const size_t num_nodes = unique_node_ids.size();

   // 2. Define the "num_nodes" dimension.
   int num_nodes_id;
   DefineDimension("num_nodes", num_nodes, &num_nodes_id);

   // 3. Extract the nodal coordinates.
   // NB: assume doubles (could be floats!); ndims = 1 (vector).
   // https://docs.unidata.ucar.edu/netcdf-c/current/group__variables.html#gac7e8662c51f3bb07d1fc6d6c6d9052c8
   std::vector<double> coordx(num_nodes);
   std::vector<double> coordy(num_nodes);
   std::vector<double> coordz(_mesh.Dimension() == 3 ? num_nodes : 0);

   ExtractVertexCoordinates(coordx, coordy, coordz);

   // 4. Define and put the nodal coordinates.
   DefineAndPutVar(EXODUS_COORDX_LABEL, NC_DOUBLE, 1, &num_nodes_id,
                   coordx.data());
   DefineAndPutVar(EXODUS_COORDY_LABEL, NC_DOUBLE, 1, &num_nodes_id,
                   coordy.data());

   if (_mesh.Dimension() == 3)
   {
      DefineAndPutVar(EXODUS_COORDZ_LABEL, NC_DOUBLE, 1, &num_nodes_id,
                      coordz.data());
   }
}

void ExodusIIWriter::WriteBoundaries()
{
   // 0. Generate boundary info.
   GenerateExodusIIBoundaryInfo();

   // 1. Define the number of boundaries.
   int num_side_sets_ids;
   DefineDimension("num_side_sets", _boundary_ids.size(),
                   &num_side_sets_ids);

   // 2. Boundary IDs.
   int boundary_ids_dim;
   DefineDimension(EXODUS_NUM_BOUNDARIES_LABEL, _boundary_ids.size(),
                   &boundary_ids_dim);

   DefineAndPutVar("ss_prop1", NC_INT, 1, &boundary_ids_dim, _boundary_ids.data());

   // 3. Number of boundary elements.
   for (int boundary_id : _boundary_ids)
   {
      size_t num_elements_for_boundary = _exodusII_element_ids_for_boundary_id.at(
                                            boundary_id).size();

      char * label = GenerateLabel("num_side_ss%d", boundary_id);

      int num_side_ss_id;
      DefineDimension(label, num_elements_for_boundary,
                      &num_side_ss_id);
   }

   // 4. Boundary side IDs.
   for (int boundary_id : _boundary_ids)
   {
      const std::vector<int> & side_ids = _exodusII_side_ids_for_boundary_id.at(
                                             boundary_id);

      char * label = GenerateLabel("side_ss%d_dim", boundary_id);

      int side_id_dim;
      DefineDimension(label, side_ids.size(), &side_id_dim);

      label = GenerateLabel("side_ss%d", boundary_id);
      DefineAndPutVar(label, NC_INT, 1,  &side_id_dim, side_ids.data());
   }

   // 5. Boundary element IDs.
   for (int boundary_id : _boundary_ids)
   {
      const std::vector<int> & element_ids = _exodusII_element_ids_for_boundary_id.at(
                                                boundary_id);

      char * label = GenerateLabel("elem_ss%d_dim", boundary_id);

      int elem_ids_dim;
      DefineDimension(label, element_ids.size(), &elem_ids_dim);

      label = GenerateLabel("elem_ss%d", boundary_id);
      DefineAndPutVar(label, NC_INT, 1, &elem_ids_dim,
                      element_ids.data());
   }
}

void ExodusIIWriter::WriteNodeConnectivityForBlock(const int block_id)
{
   std::vector<int> block_node_connectivity;

   for (int element_id : _element_ids_for_block_id.at(block_id))
   {
      // NB: assume first-order elements only for now.
      // NB: - need to convert from 0-based indexing --> 1-based indexing.
      mfem::Array<int> element_vertices;
      _mesh.GetElementVertices(element_id, element_vertices);

      for (int vertex_id : element_vertices)
      {
         block_node_connectivity.push_back(vertex_id + 1);  // 1-based indexing.
      }
   }

   char * label = GenerateLabel("connect%d_dim", block_id);

   int node_connectivity_dim;
   DefineDimension(label, block_node_connectivity.size(),
                   &node_connectivity_dim);

   // NB: 1 == vector!; name is arbitrary; NC_INT or NCINT64??
   label = GenerateLabel("connect%d", block_id);
   DefineAndPutVar(label, NC_INT, 1, &node_connectivity_dim,
                   block_node_connectivity.data());
}


void ExodusIIWriter::ExtractVertexCoordinates(std::vector<double> &
                                              coordx, std::vector<double> & coordy,
                                              std::vector<double> & coordz)
{
   for (int ivertex = 0; ivertex < _mesh.GetNV(); ivertex++)
   {
      double * coordinates = _mesh.GetVertex(ivertex);

      coordx[ivertex] = coordinates[0];
      coordy[ivertex] = coordinates[1];

      if (_mesh.Dimension() == 3)
      {
         coordz[ivertex] = coordinates[2];
      }
   }
}

void ExodusIIWriter::WriteFileSize()
{
   // Store Exodus file size (normal==0; large==1). NB: coordinates specifed
   // separately as components for large file.
   const int file_size = 1;

   PutAtt(NC_GLOBAL, EXODUS_FILE_SIZE_LABEL, NC_INT, 1, &file_size);
}

void ExodusIIWriter::WriteMeshDimension()
{
   int num_dim_id;
   DefineDimension(EXODUS_NUM_DIM_LABEL, _mesh.Dimension(), &num_dim_id);
}

void ExodusIIWriter::WriteNodeSets()
{
   // Set # node sets - TODO: add this (currently, set to 0).
   int num_node_sets_ids;

   DefineDimension(EXODUS_NUM_NODE_SETS_LABEL, 0, &num_node_sets_ids);
}

void ExodusIIWriter::WriteTimesteps()
{
   // Set # timesteps (ASSUME no timesteps for initial verision)
   int timesteps_dim;

   DefineDimension(EXODUS_TIME_STEP_LABEL, 1, &timesteps_dim);
}

void ExodusIIWriter::WriteDummyVariable()
{
   // NB: LibMesh has a dodgy bug where it will skip the x-coordinate if
   // coordx_id == 0. To prevent this, the first variable to be defined will be
   // a dummy variable which will have a variable id of 0.
   int dummy_var_dim_id, dummy_value = 1;

   DefineDimension("dummy_var_dim", 1, &dummy_var_dim_id);

   DefineAndPutVar("dummy_var", NC_INT, 1, &dummy_var_dim_id,
                   &dummy_value);
}

void ExodusIIWriter::GenerateExodusIIElementBlocks()
{
   _block_ids.clear();
   _element_ids_for_block_id.clear();
   _element_type_for_block_id.clear();

   std::set<int> observed_block_ids;

   // Iterate over the elements in the mesh.
   for (int ielement = 0; ielement < _mesh.GetNE(); ielement++)
   {
      Element::Type element_type = _mesh.GetElementType(ielement);

      int block_id = _mesh.GetAttribute(ielement);

      if (observed_block_ids.count(block_id) == 0)
      {
         _block_ids.push_back(block_id);

         _element_type_for_block_id[block_id] = element_type;
         _element_ids_for_block_id[block_id] = { ielement };

         observed_block_ids.insert(block_id);
      }
      else
      {
         auto & block_element_ids = _element_ids_for_block_id.at(block_id);
         block_element_ids.push_back(ielement);

         // Safety check: ensure that the element type matches what we have on record
         // for the block.
         if (element_type != _element_type_for_block_id.at(block_id))
         {
            MFEM_ABORT("Multiple element types are defined for block: " << block_id);
         }
      }
   }
}

void ExodusIIWriter::WriteNumElementBlocks()
{
   int num_elem_blk_id;
   DefineDimension("num_el_blk", _block_ids.size(),
                   &num_elem_blk_id);
}


std::unordered_set<int> ExodusIIWriter::GenerateUniqueNodeIDs()
{
   std::unordered_set<int> unique_node_ids;

   const FiniteElementSpace * fespace = _mesh.GetNodalFESpace();

   mfem::Array<int> dofs;

   for (int ielement = 0; ielement < _mesh.GetNE(); ielement++)
   {
      if (fespace)   // Higher-order
      {
         fespace->GetElementDofs(ielement, dofs);

         for (int dof : dofs) { unique_node_ids.insert(dof); }
      }
      else
      {
         mfem::Array<int> vertex_indices;
         _mesh.GetElementVertices(ielement, vertex_indices);

         for (int vertex_index : vertex_indices)
         {
            unique_node_ids.insert(vertex_index);
         }
      }
   }

   return unique_node_ids;
}

void ExodusIIWriter::GenerateExodusIIBoundaryInfo()
{
   // Store the unique boundary IDs.
   _boundary_ids.clear();
   _exodusII_element_ids_for_boundary_id.clear();
   _exodusII_side_ids_for_boundary_id.clear();

   for (int bdr_attribute : _mesh.bdr_attributes)
   {
      _boundary_ids.push_back(bdr_attribute);
   }

   // Generate a mapping from the MFEM face index to the MFEM element ID.
   // Note that if we have multiple element IDs for a face index then the
   // face is shared between them and it cannot possibly be a boundary face
   // since that can only have a single element associated with it. Therefore
   // we remove it from the array.
   struct GlobalFaceIndexInfo
   {
      int element_index;
      int local_face_index;
   };

   std::unordered_map<int, GlobalFaceIndexInfo>
   mfem_face_index_info_for_global_face_index;
   std::unordered_set<int> blacklisted_global_face_indices;

   Array<int> global_face_indices, orient;
   for (int ielement = 0; ielement < _mesh.GetNE(); ielement++)
   {
      _mesh.GetElementFaces(ielement, global_face_indices, orient);

      for (int iface = 0; iface < global_face_indices.Size(); iface++)
      {
         int face_index = global_face_indices[iface];

         if (blacklisted_global_face_indices.count(face_index))
         {
            continue;
         }

         if (mfem_face_index_info_for_global_face_index.count(
                face_index)) // Now we've seen it twice!
         {
            blacklisted_global_face_indices.insert(face_index);
            mfem_face_index_info_for_global_face_index.erase(face_index);
            continue;
         }

         mfem_face_index_info_for_global_face_index[face_index] =
         {
            .element_index = ielement,
            .local_face_index = iface
         };
      }
   }

   for (int ibdr_element = 0; ibdr_element < _mesh.GetNBE(); ibdr_element++)
   {
      int boundary_id = _mesh.GetBdrAttribute(ibdr_element);
      int bdr_element_face_index = _mesh.GetBdrElementFaceIndex(ibdr_element);

      // Locate match.
      auto & element_face_info = mfem_face_index_info_for_global_face_index.at(
                                    bdr_element_face_index);

      int ielement = element_face_info.element_index;
      int iface = element_face_info.local_face_index;

      // 1. Convert MFEM 0-based element index to 1-based Exodus II element ID.
      int exodusII_element_id = ielement + 1;

      // 2. Convert 0-based MFEM face index to Exodus II 1-based face ID (different ordering).
      int exodusII_face_id;

      Element::Type element_type = _mesh.GetElementType(ielement);
      switch (element_type)
      {
         case Element::Type::TETRAHEDRON:
            exodusII_face_id = mfem_to_exodusII_side_map_tet4[iface];
            break;
         case Element::Type::HEXAHEDRON:
            exodusII_face_id = mfem_to_exodusII_side_map_hex8[iface];
            break;
         case Element::Type::WEDGE:
            exodusII_face_id = mfem_to_exodusII_side_map_wedge6[iface];
            break;
         case Element::Type::PYRAMID:
            exodusII_face_id = mfem_to_exodusII_side_map_pyramid5[iface];
            break;
         default:
            MFEM_ABORT("Cannot handle element of type " << element_type);
      }

      _exodusII_element_ids_for_boundary_id[boundary_id].push_back(
         exodusII_element_id);
      _exodusII_side_ids_for_boundary_id[boundary_id].push_back(exodusII_face_id);
   }
}

}
