// Copyright (c) 2010-2022, Lawrence Livermore National Security, LLC. Produced
// at the Lawrence Livermore National Laboratory. All Rights reserved. See files
// LICENSE and NOTICE for details. LLNL-CODE-806117.
//
// This file is part of the MFEM library. For more information and source code
// availability visit https://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the BSD-3 license. We welcome feedback and contributions, see file
// CONTRIBUTING.md for details.

#include "mfem.hpp"
#include <fstream>
#include <iostream>
#include "mesh-optimizer.hpp"
using namespace std;
using namespace mfem;

// Used for exact surface alignment
double circle_level_set(const Vector &x)
{
   const int dim = x.Size();
   if (dim == 2)
   {
      const double xc = x(0) - 0.5, yc = x(1) - 0.5;
      const double r = sqrt(xc*xc + yc*yc);
      return r-0.25; // circle of radius 0.1
   }
   else
   {
      const double xc = x(0) - 0.5, yc = x(1) - 0.5, zc = x(2) - 0.5;
      const double r = sqrt(xc*xc + yc*yc + zc*zc);
      return r-0.3;
   }
}

double squircle_level_set(const Vector &x)
{
   const int dim = x.Size();
   if (dim == 2)
   {
      const double xc = x(0) - 0.5, yc = x(1) - 0.5;
      return std::pow(xc, 4.0) + std::pow(yc, 4.0) - std::pow(0.25, 4.0);
   }
   else
   {
      const double xc = x(0) - 0.5, yc = x(1) - 0.5, zc = x(2) - 0.5;
      const double r = sqrt(xc*xc + yc*yc + zc*zc);
      return r-0.3;
   }
}

double squircle_inside_circle_level_set(const Vector &x)
{
   const int dim = x.Size();
   double pwrr = 4.0;
   Vector xc = x;
   xc = 1.0;
   MFEM_VERIFY(dim == 2, "Only 2D supported for this level set");
   Vector x2 = x;
   x2 -= xc;
   double rcirc = 0.75;
   double rsqcirc = 0.4;

   double dcir = std::pow(x2(0), 2.0) + std::pow(x2(1), 2.0) - std::pow(rcirc,
                                                                        2.0);
   double dsqcir = std::pow(x2(0), pwrr) + std::pow(x2(1),
                                                    pwrr) - std::pow(rsqcirc, pwrr);

   //   return dcir;
   return std::max(dcir, -10*dsqcir);
}

double in_cube_smooth(const Vector &x, double xc, double yc, double zc,
                      double lx,
                      double ly, double lz)
{
   double dx = fabs(x(0) - xc);
   double dy = fabs(x(1) - yc);
   double dz = fabs(x(2) - zc);
   if (dx <= lx/2 && dy <= ly/2 && dz <= lz/2)
   {
      return 1.0;
   }
   else
   {
      return -1.0;
   }
}


double pipe_dist(const Vector &x, int pipedir, Vector x_pipe_center,
                 double radius, double minv, double maxv)
{
   Vector x_pipe_copy = x_pipe_center;
   x_pipe_copy -= x;
   x_pipe_copy(pipedir-1) = 0.0;
   double dist = x_pipe_copy.Norml2() - radius*radius;
   return dist;
}

double cube_dist(const Vector &x, Vector &center, double halfwidth)
{
   //    x.Print();
   Vector q = x;
   q -= center;
   Vector t1 = x;
   for (int i = 0; i < q.Size(); i++)
   {
      q(i) = std::fabs(q(i))-halfwidth;
      t1(i) = std::max(q(i), 0.0);
   }
   double sum1 = t1.Norml2();
   double sum2 = std::min(std::max(q(0),std::max(q(1), q(2))), 0.0);
   return sum1+sum2;
}

double csg_cubesph_smooth(const Vector &x)
{
   double pwrr = 4.0;
   Vector xcc = x;
   xcc = 0.5;
   const int dim = x.Size();
   MFEM_VERIFY(dim == 3, "Only 3D supported for this level set");
   Vector x2 = x;
   x2 -= xcc;
   double rsph = 0.375;
   double rcube = 0.3;
   double dsph = x2.Norml2() - rsph;;/*x2.Norml2()*x2.Norml2() - rsph*rsph;*/
   //      return dsph; //return here for sphere
   double dcube = cube_dist(x, xcc, rcube);
   return std::max(dsph, dcube); // return here for sphere + cube
}

double csg_cubecylsph_smooth(const Vector &x)
{
   double pwrr = 4.0;
   Vector xcc = x;
   xcc = 0.5;
   const int dim = x.Size();
   MFEM_VERIFY(dim == 3, "Only 3D supported for this level set");
   Vector x2 = x;
   x2 -= xcc;
   double rsph = 0.375;
   double rcube = 0.3;
   double dsph = x2.Norml2() - rsph;;/*x2.Norml2()*x2.Norml2() - rsph*rsph;*/
   //      return dsph; //return here for sphere
   double dcube = cube_dist(x, xcc, rcube);
   //    return std::max(dsph, dcube); // return here for sphere + cube
   double dist1 = std::max(dsph, dcube);

   //   return std::max(dsph, -10*dcube);
   //   double alpha = 10.0;
   //   double dist1 = std::min(1.0*alpha*dcube, dsph);

   int pipedir = 1;
   Vector x_pipe_center(3);
   x_pipe_center = 0.5;
   double xmin = 1.0-rsph;
   double xmax = 1.0+rsph;
   double pipe_radius = 0.25;
   double in_pipe_x = pipe_dist(x, pipedir, x_pipe_center, pipe_radius, xmin,
                                xmax);

   double dist2 = std::max(dist1, -in_pipe_x);
   //   return dist2;

   pipedir = 2;
   in_pipe_x = pipe_dist(x, pipedir, x_pipe_center, pipe_radius, xmin, xmax);
   double dist3 = std::max(dist2, -in_pipe_x);

   pipedir = 3;
   in_pipe_x = pipe_dist(x, pipedir, x_pipe_center, pipe_radius, xmin, xmax);
   double dist4 = std::max(dist3, -in_pipe_x);

   return dist4;
}

double kabaria_smooth(const Vector &x)
{
   double pwrr = 4.0;
   Vector xcc = x;
   xcc = 0.5;
   const int dim = x.Size();
   Vector x2 = x;
   x2 -= xcc;


   double v1 = 8.0*std::pow(4*x(0)-2, 4.0) -
               8.0*std::pow(4*x(0)-2, 2.0);
   double v2 = 8.0*std::pow(4*x(1)-2, 4.0) -
               8.0*std::pow(4*x(1)-2, 2.0);
   double v3 = 0.0;
   if (dim == 3)
   {
      v3 = 8.0*std::pow(4.0*x(2)-2, 4.0) -
           8.0*std::pow(4.0*x(2)-2, 2.0);
   }
   return -(v1 + v2 + v3);
}

double dist_circle(const Vector &x, const Vector &x_center, double radius)
{
   Vector x_current = x;
   x_current -= x_center;
   double dist = x_current.Norml2() - radius;
   return dist;
}

double in_circle(const Vector &x, const Vector &x_center, double radius)
{
   Vector x_current = x;
   x_current -= x_center;
   double dist = x_current.Norml2();
   if (dist < radius)
   {
      return 1.0;
   }
   else if (dist == radius)
   {
      return 0.0;
   }
   else
   {
      return -1.0;
   }
   return 0.0;
}

double in_trapezium(const Vector &x, double a, double b, double l)
{
   double phi_t = x(1) + (a-b)*x(0)/l - a;
   if (phi_t <= 0.0)
   {
      return 1.0;
   }
   return -1.0;
}

double in_parabola(const Vector &x, double h, double k, double t)
{
   double phi_p1 = (x(0)-h-t/2) - k*x(1)*x(1);
   double phi_p2 = (x(0)-h+t/2) - k*x(1)*x(1);
   if (phi_p1 <= 0.0 && phi_p2 >= 0.0)
   {
      return 1.0;
   }
   return -1.0;
}

double in_rectangle(const Vector &x, double xc, double yc, double w, double h)
{
   double dx = fabs(x(0) - xc);
   double dy = fabs(x(1) - yc);
   if (dx <= w/2 && dy <= h/2)
   {
      return 1.0;
   }
   else
   {
      return -1.0;
   }
}

// Fischer-Tropsch like geometry
double reactor(const Vector &x)
{
   // Circle
   Vector x_circle1(2);
   x_circle1(0) = 0.0;
   x_circle1(1) = 0.0;
   double in_circle1_val = in_circle(x, x_circle1, 0.2);

   double r1 = 0.2;
   double r2 = 1.0;
   double in_trapezium_val = in_trapezium(x, 0.05, 0.1, r2-r1);

   double return_val = max(in_circle1_val, in_trapezium_val);

   double h = 0.4;
   double k = 2;
   double t = 0.15;
   double in_parabola_val = in_parabola(x, h, k, t);
   return_val = max(return_val, in_parabola_val);

   double in_rectangle_val = in_rectangle(x, 0.99, 0.0, 0.12, 0.35);
   return_val = max(return_val, in_rectangle_val);

   double in_rectangle_val2 = in_rectangle(x, 0.99, 0.5, 0.12, 0.28);
   return_val = max(return_val, in_rectangle_val2);
   return return_val;
}

double in_cube(const Vector &x, double xc, double yc, double zc, double lx,
               double ly, double lz)
{
   double dx = fabs(x(0) - xc);
   double dy = fabs(x(1) - yc);
   double dz = fabs(x(2) - zc);
   if (dx <= lx/2 && dy <= ly/2 && dz <= lz/2)
   {
      return 1.0;
   }
   else
   {
      return -1.0;
   }
}

double in_pipe(const Vector &x, int pipedir, Vector x_pipe_center,
               double radius, double minv, double maxv)
{
   Vector x_pipe_copy = x_pipe_center;
   x_pipe_copy -= x;
   x_pipe_copy(pipedir-1) = 0.0;
   double dist = x_pipe_copy.Norml2();
   double xv = x(pipedir-1);
   if (dist < radius && xv > minv && xv < maxv)
   {
      return 1.0;
   }
   else if (dist == radius || (xv == minv && dist < radius) || (xv == maxv &&
                                                                dist < radius))
   {
      return 0.0;
   }
   else
   {
      return -1.0;
   }
   return 0.0;
}

double r_intersect(double r1, double r2)
{
   return r1 + r2 - std::pow(r1*r1 + r2*r2, 0.5);
}

double r_union(double r1, double r2)
{
   return r1 + r2 + std::pow(r1*r1 + r2*r2, 0.5);
}

double r_remove(double r1, double r2)
{
   return r_intersect(r1, -r2);
}

double beam_level_set(const Vector &x)
{
   const int dim = x.Size();
   double val = 0.0;
   if (dim == 2)
   {
      double sf=1.0;
      val = 2.0*(0.5*(1+std::tanh(sf*(x(1)-2.1))) - 0.5*(1+std::tanh(sf*(x(
                                                                            1)-4.1))))-1.0;
      val *= 1.0;

      Vector xc(2);
      double radius = 0.5;
      double dist1;
      // add circles to it
      {
         xc(0) = 3.0;
         xc(1) = 3.0;
         dist1 = dist_circle(x, xc, radius);
      }
      val = r_intersect(dist1, val);

      {
         xc(0) = 6.0;
         xc(1) = 3.0;
         dist1 = dist_circle(x, xc, radius);
      }
      val = r_intersect(dist1, val);

      {
         xc(0) = 9.0;
         xc(1) = 3.0;
         dist1 = dist_circle(x, xc, radius);
      }
      val = r_intersect(dist1, val);

   }
   else
   {
      MFEM_ABORT("3D not implemented yet.");
   }
   return val;
}


double csg_cubecylsph(const Vector &x)
{
   Vector xcc(3);
   xcc = 0.5;
   double cube_x = 0.25*2;
   double cube_y = 0.25*2;
   double cube_z = 0.25*2;

   double in_cube_val = in_cube(x, xcc(0), xcc(1), xcc(2), cube_x, cube_y, cube_z);

   Vector x_circle_c(3);
   x_circle_c = 0.5;

   double sphere_radius = 0.30;
   double in_sphere_val = in_circle(x, x_circle_c, sphere_radius);
   double in_return_val = std::min(in_cube_val, in_sphere_val);

   int pipedir = 1;
   Vector x_pipe_center(3);
   x_pipe_center = 0.5;
   double xmin = 0.5-sphere_radius;
   double xmax = 0.5+sphere_radius;
   double pipe_radius = 0.075;
   double in_pipe_x = in_pipe(x, pipedir, x_pipe_center, pipe_radius, xmin, xmax);

   in_return_val = std::min(in_return_val, -1*in_pipe_x);

   pipedir = 2;
   in_pipe_x = in_pipe(x, pipedir, x_pipe_center, pipe_radius, xmin, xmax);
   in_return_val = std::min(in_return_val, -1*in_pipe_x);

   pipedir = 3;
   in_pipe_x = in_pipe(x, pipedir, x_pipe_center, pipe_radius, xmin, xmax);
   in_return_val = std::min(in_return_val, -1*in_pipe_x);

   return in_return_val;
}
void SetMaterialsForFitting(GridFunction &surf_fit_gf0, GridFunction &mat)
{
   FiniteElementSpace *pfespace = surf_fit_gf0.FESpace();
   Mesh *pmesh = pfespace->GetMesh();
   Array<int> verts;
   Array<int> dofs;
   Array<int> dofsv;
   Vector vals;
   //Identify elements cut by levelset
   Vector elvals(pmesh->GetNE()*4); //Assume tetrahedron (4 vertices);
   for (int e = 0; e < pmesh->GetNE(); e++)
   {
      //        pfespace->GetElementDofs(e, dofs);
      pmesh->GetElementVertices(e, verts);
      dofsv.SetSize(0);
      for (int v = 0; v < verts.Size(); v++)
      {
         pfespace->GetVertexDofs(verts[v], dofs);
         dofsv.Append(dofs);
      }
      surf_fit_gf0.GetSubVector(dofsv, vals);
      for (int v = 0; v < vals.Size(); v++)
      {
         vals(v) = vals(v) == 0.0 ? 0.0 : std::fabs(vals(v))/vals(v);
         elvals(e*4 + v) = vals(v);
      }
      double maxv = vals.Max();
      double minv = vals.Min();
      if (maxv == 0.0 && minv == 0.0)
      {
         MFEM_ABORT("not all vertices can be 0.0");
      }
      else if (maxv > 0.0)
      {
         mat(e) = 1.0;
      }
      else if (minv < 0.0)
      {
         mat(e) = 0.0;
      }
      pmesh->SetAttribute(e, (int)(mat(e)+1));
   }
   pmesh->SetAttributes();
}

void SetMaterialsForFitting(ParGridFunction &surf_fit_gf0, ParGridFunction &mat)
{
   ParFiniteElementSpace *pfespace = surf_fit_gf0.ParFESpace();
   ParMesh *pmesh = pfespace->GetParMesh();
   Array<int> verts;
   Array<int> dofs;
   Array<int> dofsv;
   Vector vals;
   //Identify elements cut by levelset
   Vector elvals(pmesh->GetNE()*4); //Assume tetrahedron (4 vertices);
   for (int e = 0; e < pmesh->GetNE(); e++)
   {
      pmesh->GetElementVertices(e, verts);
      dofsv.SetSize(0);
      for (int v = 0; v < verts.Size(); v++)
      {
         pfespace->GetVertexDofs(verts[v], dofs);
         dofsv.Append(dofs);
      }
      surf_fit_gf0.GetSubVector(dofsv, vals);
      for (int v = 0; v < vals.Size(); v++)
      {
         vals(v) = vals(v) == 0.0 ? 0.0 : std::fabs(vals(v))/vals(v);
         elvals(e*4 + v) = vals(v);
      }
      double maxv = vals.Max();
      double minv = vals.Min();
      if (maxv == 0.0 && minv == 0.0)
      {
         MFEM_ABORT("not all vertices can be 0.0");
      }
      else if (maxv > 0.0)
      {
         mat(e) = 1.0;
      }
      else if (minv < 0.0)
      {
         mat(e) = 0.0;
      }
      pmesh->SetAttribute(e, (int)(mat(e)+1));
   }
   pmesh->SetAttributes();
}

void ModifyAttributeForMarkingDOFS(Mesh *mesh, GridFunction &mat,
                                   int attr_to_switch)
{
   // Switch attribute if all but 1 of the faces of an element will be marked?
   Array<int> element_attr(mesh->GetNE());
   element_attr = 0;
   for (int e = 0; e < mesh->GetNE(); e++)
   {
      Array<int> faces, ori;
      if (mesh->Dimension() == 2)
      {
         mesh->GetElementEdges(e, faces, ori);
      }
      else
      {
         mesh->GetElementFaces(e, faces, ori);
      }
      int inf1, inf2;
      int elem1, elem2;
      int diff_attr_count = 0;
      int attr1;
      int attr2;
      attr1 = mat(e);
      bool bdr_element = false;
      element_attr[e] = attr1;
      int target_attr = -1;
      for (int f = 0; f < faces.Size(); f++)
      {
         mesh->GetFaceElements(faces[f], &elem1, &elem2);
         if (elem2 >= 0)
         {
            attr2 = elem1 == e ? (int)(mat(elem2)) : (int)(mat(elem1));
            if (attr1 != attr2 && attr1 == attr_to_switch)
            {
               diff_attr_count += 1;
               target_attr = attr2;
            }
         }
         else
         {
            mesh->GetFaceInfos(faces[f], &inf1, &inf2);
            if (inf2 >= 0)
            {
               Vector dof_vals;
               Array<int> dofs;
               mat.GetElementDofValues(mesh->GetNE() + (-1-elem2), dof_vals);
               attr2 = (int)(dof_vals(0));
               if (attr1 != attr2 && attr1 == attr_to_switch)
               {
                  diff_attr_count += 1;
                  target_attr = attr2;
               }
            }
            else
            {
               bdr_element = true;
            }
         }
      }

      if (diff_attr_count == faces.Size()-1 && !bdr_element)
      {
         element_attr[e] = target_attr;
      }
   }
   for (int e = 0; e < mesh->GetNE(); e++)
   {
      mat(e) = element_attr[e];
      mesh->SetAttribute(e, element_attr[e]+1);
   }
   mesh->SetAttributes();
}

Mesh* TrimMeshAsSubMesh(Mesh &mesh, FunctionCoefficient &ls_coeff, int order,
                        Array<int> attr_to_keep, int splittype)
{
   // Note right now that we set new element attributes to original unless
   // el_attr_to_set is specified.
   // New boundary elements get attribute 1 unless bdr_attr_to_set is specified.
   const int dim = mesh.Dimension();

   H1_FECollection fec(order, dim);
   FiniteElementSpace fes_s(&mesh, &fec);
   GridFunction distance_s(&fes_s);
   distance_s.ProjectCoefficient(ls_coeff);
   L2_FECollection mat_coll(0, dim);
   FiniteElementSpace mat_fes(&mesh, &mat_coll);
   GridFunction mat(&mat_fes);

   for (int e = 0; e < mesh.GetNE(); e++)
   {
      mesh.SetAttribute(e, 1);
   }

   //   SetMaterialsForFitting(distance_s, mat);
   for (int e = 0; e < mesh.GetNE(); e++)
   {
      mat(e) = material_id(e, distance_s);
      mesh.SetAttribute(e, mat(e) + 1);
   }

   //Make materials consistent. 24tetmesh
   if (splittype > 0)
   {
      int num_elems_per_split = 12*splittype;
      MFEM_VERIFY(mesh.GetNE() % num_elems_per_split == 0, "Not a 24 tet mesh.");
      Vector groupmat(mesh.GetNE()/num_elems_per_split);
      groupmat = -1.0;
      for (int e = 0; e < mesh.GetNE(); e++)
      {
         int rem = e % num_elems_per_split;
         int idx = (e - rem)/num_elems_per_split;
         groupmat(idx) = std::max(groupmat(idx), mat(e));
      }
      for (int e = 0; e < mesh.GetNE(); e++)
      {
         int rem = e % num_elems_per_split;
         int idx = (e - rem)/num_elems_per_split;
         mat(e) = groupmat(idx);
         mesh.SetAttribute(e, mat(e) + 1);
      }
      mesh.SetAttributes();
   }

   int max_bdr_attr = mesh.bdr_attributes.Max();
   SubMesh *smesh = new SubMesh(SubMesh::CreateFromDomain(mesh, attr_to_keep));
   int smax_bdr_attr = smesh->bdr_attributes.Max();

   // no new boundaries are created.
   if (max_bdr_attr == smax_bdr_attr)
   {
      return smesh;
   }
   // only 1 boundary in new mesh
   int nbdr_attr = smesh->bdr_attributes.Size();
   if (nbdr_attr == 1)
   {
      for (int i = 0; i < smesh->GetNBE(); i++)
      {
         smesh->SetBdrAttribute(i, 1);
      }
   }
   else
   {
      for (int i = 0; i < smesh->GetNBE(); i++)
      {
         if (smesh->GetBdrAttribute(i) == smax_bdr_attr)
         {
            smesh->SetBdrAttribute(i, max_bdr_attr+1);
         }
      }
   }

   for (int i = 0; i < smesh->GetNE(); i++)
   {
      smesh->SetAttribute(i, 1);
   }

   smesh->SetAttributes();
   return smesh;
}

Mesh* TrimMesh(Mesh &mesh, FunctionCoefficient &ls_coeff, int order,
               int attr_to_trim, int el_attr_to_set = -1, int bdr_attr_to_set = -1)
{
   // Note right now that we set new element attributes to original unless
   // el_attr_to_set is specified.
   // New boundary elements get attribute 1 unless bdr_attr_to_set is specified.
   const int dim = mesh.Dimension();

   H1_FECollection fec(order, dim);
   FiniteElementSpace fes_s(&mesh, &fec);
   GridFunction distance_s(&fes_s);
   distance_s.ProjectCoefficient(ls_coeff);
   L2_FECollection mat_coll(0, dim);
   FiniteElementSpace mat_fes(&mesh, &mat_coll);
   GridFunction mat(&mat_fes);

   for (int e = 0; e < mesh.GetNE(); e++)
   {
      mesh.SetAttribute(e, 1);
   }
   for (int e = 0; e < mesh.GetNE(); e++)
   {
      mat(e) = material_id(e, distance_s);
      mesh.SetAttribute(e, mat(e) + 1);
   }

   ModifyAttributeForMarkingDOFS(&mesh, mat, 0);
   ModifyAttributeForMarkingDOFS(&mesh, mat, 1);

   mesh.SetAttributes();

   Array<int> attr(1);
   attr[0] = attr_to_trim;
   Array<int> bdr_attr;

   int max_attr     = mesh.attributes.Max();
   int max_bdr_attr = mesh.bdr_attributes.Max();

   if (bdr_attr.Size() == 0)
   {
      bdr_attr.SetSize(attr.Size());
      for (int i=0; i<attr.Size(); i++)
      {
         bdr_attr[i] = max_bdr_attr + attr[i];
      }
   }
   MFEM_VERIFY(attr.Size() == bdr_attr.Size(),
               "Size mismatch in attribute arguments.");

   Array<int> marker(max_attr);
   Array<int> attr_inv(max_attr);
   marker = 0;
   attr_inv = 0;
   for (int i=0; i<attr.Size(); i++)
   {
      marker[attr[i]-1] = 1;
      attr_inv[attr[i]-1] = i;
   }

   // Count the number of elements in the final mesh
   int num_elements = 0;
   for (int e=0; e<mesh.GetNE(); e++)
   {
      int elem_attr = mesh.GetElement(e)->GetAttribute();
      if (!marker[elem_attr-1]) { num_elements++; }
   }

   // Count the number of boundary elements in the final mesh
   int num_bdr_elements = 0;
   for (int f=0; f<mesh.GetNumFaces(); f++)
   {
      int e1 = -1, e2 = -1;
      mesh.GetFaceElements(f, &e1, &e2);

      int a1 = 0, a2 = 0;
      if (e1 >= 0) { a1 = mesh.GetElement(e1)->GetAttribute(); }
      if (e2 >= 0) { a2 = mesh.GetElement(e2)->GetAttribute(); }

      if (a1 == 0 || a2 == 0)
      {
         if (a1 == 0 && !marker[a2-1]) { num_bdr_elements++; }
         else if (a2 == 0 && !marker[a1-1]) { num_bdr_elements++; }
      }
      else
      {
         if (marker[a1-1] && !marker[a2-1]) { num_bdr_elements++; }
         else if (!marker[a1-1] && marker[a2-1]) { num_bdr_elements++; }
      }
   }

   cout << "Number of Elements:          " << mesh.GetNE() << " -> "
        << num_elements << endl;
   cout << "Number of Boundary Elements: " << mesh.GetNBE() << " -> "
        << num_bdr_elements << endl;

   Mesh *trimmed_mesh = new Mesh(mesh.Dimension(), mesh.GetNV(),
                                 num_elements, num_bdr_elements, mesh.SpaceDimension());
   //   Mesh trimmed_mesh(mesh.Dimension(), mesh.GetNV(),
   //                     num_elements, num_bdr_elements, mesh.SpaceDimension());

   // Copy vertices
   for (int v=0; v<mesh.GetNV(); v++)
   {
      trimmed_mesh->AddVertex(mesh.GetVertex(v));
   }

   // Copy elements
   for (int e=0; e<mesh.GetNE(); e++)
   {
      Element * el = mesh.GetElement(e);
      int elem_attr = el->GetAttribute();
      if (!marker[elem_attr-1])
      {
         Element * nel = mesh.NewElement(el->GetGeometryType());
         nel->SetAttribute(el_attr_to_set > 0 ? el_attr_to_set : elem_attr);
         nel->SetVertices(el->GetVertices());
         trimmed_mesh->AddElement(nel);
      }
   }

   // Copy selected boundary elements
   for (int be=0; be<mesh.GetNBE(); be++)
   {
      int e, info;
      mesh.GetBdrElementAdjacentElement(be, e, info);

      int elem_attr = mesh.GetElement(e)->GetAttribute();
      if (!marker[elem_attr-1])
      {
         Element * nbel = mesh.GetBdrElement(be)->Duplicate(trimmed_mesh);
         trimmed_mesh->AddBdrElement(nbel);
      }
   }

   // Create new boundary elements
   for (int f=0; f<mesh.GetNumFaces(); f++)
   {
      int e1 = -1, e2 = -1;
      mesh.GetFaceElements(f, &e1, &e2);

      int i1 = -1, i2 = -1;
      mesh.GetFaceInfos(f, &i1, &i2);

      int a1 = 0, a2 = 0;
      if (e1 >= 0) { a1 = mesh.GetElement(e1)->GetAttribute(); }
      if (e2 >= 0) { a2 = mesh.GetElement(e2)->GetAttribute(); }

      if (a1 != 0 && a2 != 0)
      {
         if (marker[a1-1] && !marker[a2-1])
         {
            Element * bel = (mesh.Dimension() == 1) ?
                            (Element*)new Point(&f) :
                            mesh.GetFace(f)->Duplicate(trimmed_mesh);
            //bel->SetAttribute(bdr_attr[attr_inv[a1-1]]);
            //            bel->SetAttribute(3);
            bel->SetAttribute(bdr_attr_to_set > 0 ? bdr_attr_to_set : 1);
            trimmed_mesh->AddBdrElement(bel);
         }
         else if (!marker[a1-1] && marker[a2-1])
         {
            Element * bel = (mesh.Dimension() == 1) ?
                            (Element*)new Point(&f) :
                            mesh.GetFace(f)->Duplicate(trimmed_mesh);
            //bel->SetAttribute(bdr_attr[attr_inv[a2-1]]);
            //            bel->SetAttribute(3);
            bel->SetAttribute(bdr_attr_to_set > 0 ? bdr_attr_to_set : 1);
            trimmed_mesh->AddBdrElement(bel);
         }
      }
   }

   trimmed_mesh->FinalizeTopology();
   trimmed_mesh->Finalize();
   trimmed_mesh->RemoveUnusedVertices();

   // Check for curved or discontinuous mesh
   if (mesh.GetNodes())
   {
      // Extract Nodes GridFunction and determine its type
      const GridFunction * Nodes = mesh.GetNodes();
      const FiniteElementSpace * fes = Nodes->FESpace();

      Ordering::Type ordering = fes->GetOrdering();
      int order = fes->FEColl()->GetOrder();
      int sdim = mesh.SpaceDimension();
      bool discont =
         dynamic_cast<const L2_FECollection*>(fes->FEColl()) != NULL;

      // Set curvature of the same type as original mesh
      trimmed_mesh->SetCurvature(order, discont, sdim, ordering);

      const FiniteElementSpace * trimmed_fes = trimmed_mesh->GetNodalFESpace();
      GridFunction * trimmed_nodes = trimmed_mesh->GetNodes();

      Array<int> vdofs;
      Array<int> trimmed_vdofs;
      Vector loc_vec;

      // Copy nodes to trimmed mesh
      int te = 0;
      for (int e = 0; e < mesh.GetNE(); e++)
      {
         Element * el = mesh.GetElement(e);
         int elem_attr = el->GetAttribute();
         if (!marker[elem_attr-1])
         {
            fes->GetElementVDofs(e, vdofs);
            Nodes->GetSubVector(vdofs, loc_vec);

            trimmed_fes->GetElementVDofs(te, trimmed_vdofs);
            trimmed_nodes->SetSubVector(trimmed_vdofs, loc_vec);
            te++;
         }
      }
   }

   return trimmed_mesh;
}

#ifdef MFEM_USE_MPI
void ModifyBoundaryAttributesForNodeMovement(ParMesh *pmesh, ParGridFunction &x)
{
   const int dim = pmesh->Dimension();
   for (int i = 0; i < pmesh->GetNBE(); i++)
   {
      mfem::Array<int> dofs;
      pmesh->GetNodalFESpace()->GetBdrElementDofs(i, dofs);
      mfem::Vector bdr_xy_data;
      mfem::Vector dof_xyz(dim);
      mfem::Vector dof_xyz_compare;
      mfem::Array<int> xyz_check(dim);
      for (int j = 0; j < dofs.Size(); j++)
      {
         for (int d = 0; d < dim; d++)
         {
            dof_xyz(d) = x(pmesh->GetNodalFESpace()->DofToVDof(dofs[j], d));
         }
         if (j == 0)
         {
            dof_xyz_compare = dof_xyz;
            xyz_check = 1;
         }
         else
         {
            for (int d = 0; d < dim; d++)
            {
               if (std::fabs(dof_xyz(d)-dof_xyz_compare(d)) < 1.e-10)
               {
                  xyz_check[d] += 1;
               }
            }
         }
      }
      if (dim == 2)
      {
         if (xyz_check[0] == dofs.Size())
         {
            pmesh->GetNodalFESpace()->GetMesh()->SetBdrAttribute(i, 1);
         }
         else if (xyz_check[1] == dofs.Size())
         {
            pmesh->GetNodalFESpace()->GetMesh()->SetBdrAttribute(i, 2);
         }
         else
         {
            pmesh->GetNodalFESpace()->GetMesh()->SetBdrAttribute(i, 4);
         }
      }
      else if (dim == 3)
      {
         if (xyz_check[0] == dofs.Size())
         {
            pmesh->GetNodalFESpace()->GetMesh()->SetBdrAttribute(i, 1);
         }
         else if (xyz_check[1] == dofs.Size())
         {
            pmesh->GetNodalFESpace()->GetMesh()->SetBdrAttribute(i, 2);
         }
         else if (xyz_check[2] == dofs.Size())
         {
            pmesh->GetNodalFESpace()->GetMesh()->SetBdrAttribute(i, 3);
         }
         else
         {
            pmesh->GetNodalFESpace()->GetMesh()->SetBdrAttribute(i, 4);
         }
      }
   }
}

void ModifyAttributeForMarkingDOFS(ParMesh *pmesh, ParGridFunction &mat,
                                   int attr_to_switch)
{
   mat.ExchangeFaceNbrData();
   // Switch attribute if all but 1 of the faces of an element will be marked?
   Array<int> element_attr(pmesh->GetNE());
   element_attr = 0;
   for (int e = 0; e < pmesh->GetNE(); e++)
   {
      Array<int> faces, ori;
      if (pmesh->Dimension() == 2)
      {
         pmesh->GetElementEdges(e, faces, ori);
      }
      else
      {
         pmesh->GetElementFaces(e, faces, ori);
      }
      int inf1, inf2;
      int elem1, elem2;
      int diff_attr_count = 0;
      int attr1;
      int attr2;
      attr1 = mat(e);
      bool bdr_element = false;
      element_attr[e] = attr1;
      int target_attr = -1;
      for (int f = 0; f < faces.Size(); f++)
      {
         pmesh->GetFaceElements(faces[f], &elem1, &elem2);
         if (elem2 >= 0)
         {
            attr2 = elem1 == e ? (int)(mat(elem2)) : (int)(mat(elem1));
            if (attr1 != attr2 && attr1 == attr_to_switch)
            {
               diff_attr_count += 1;
               target_attr = attr2;
            }
         }
         else
         {
            pmesh->GetFaceInfos(faces[f], &inf1, &inf2);
            if (inf2 >= 0)
            {
               Vector dof_vals;
               Array<int> dofs;
               mat.GetElementDofValues(pmesh->GetNE() + (-1-elem2), dof_vals);
               attr2 = (int)(dof_vals(0));
               if (attr1 != attr2 && attr1 == attr_to_switch)
               {
                  diff_attr_count += 1;
                  target_attr = attr2;
               }
            }
            else
            {
               bdr_element = true;
            }
         }
      }

      if (diff_attr_count == faces.Size()-1 && !bdr_element)
      {
         element_attr[e] = target_attr;
      }
   }
   for (int e = 0; e < pmesh->GetNE(); e++)
   {
      mat(e) = element_attr[e];
      pmesh->SetAttribute(e, element_attr[e]+1);
   }
   mat.ExchangeFaceNbrData();
   pmesh->SetAttributes();
}

void OptimizeMeshWithAMRAroundZeroLevelSet(ParMesh &pmesh,
                                           FunctionCoefficient &ls_coeff,
                                           int amr_iter,
                                           ParGridFunction &distance_s,
                                           const int quad_order = 5,
                                           Array<ParGridFunction *> *pgf_to_update = NULL)
{
   mfem::H1_FECollection h1fec(distance_s.ParFESpace()->FEColl()->GetOrder(),
                               pmesh.Dimension());
   mfem::ParFiniteElementSpace h1fespace(&pmesh, &h1fec);
   mfem::ParGridFunction x(&h1fespace);

   mfem::L2_FECollection l2fec(0, pmesh.Dimension());
   mfem::ParFiniteElementSpace l2fespace(&pmesh, &l2fec);
   mfem::ParGridFunction el_to_refine(&l2fespace);

   mfem::H1_FECollection lhfec(1, pmesh.Dimension());
   mfem::ParFiniteElementSpace lhfespace(&pmesh, &lhfec);
   mfem::ParGridFunction lhx(&lhfespace);

   x.ProjectCoefficient(ls_coeff);
   x.ExchangeFaceNbrData();

   IntegrationRules irRules = IntegrationRules(0, Quadrature1D::GaussLobatto);
   for (int iter = 0; iter < amr_iter; iter++)
   {
      el_to_refine = 0.0;
      for (int e = 0; e < pmesh.GetNE(); e++)
      {
         Array<int> dofs;
         Vector x_vals;
         DenseMatrix x_grad;
         h1fespace.GetElementDofs(e, dofs);
         const IntegrationRule &ir = irRules.Get(pmesh.GetElementGeometry(e),
                                                 quad_order);
         x.GetValues(e, ir, x_vals);
         double min_val = x_vals.Min();
         double max_val = x_vals.Max();
         // If the zero level set cuts the elements, mark it for refinement
         if (min_val < 0 && max_val >= 0)
         {
            el_to_refine(e) = 1.0;
         }
      }

      // Refine an element if its neighbor will be refined
      for (int inner_iter = 0; inner_iter < 1; inner_iter++)
      {
         el_to_refine.ExchangeFaceNbrData();
         GridFunctionCoefficient field_in_dg(&el_to_refine);
         lhx.ProjectDiscCoefficient(field_in_dg, GridFunction::ARITHMETIC);
         for (int e = 0; e < pmesh.GetNE(); e++)
         {
            Array<int> dofs;
            Vector x_vals;
            lhfespace.GetElementDofs(e, dofs);
            const IntegrationRule &ir =
               irRules.Get(pmesh.GetElementGeometry(e), quad_order);
            lhx.GetValues(e, ir, x_vals);
            double max_val = x_vals.Max();
            if (max_val > 0)
            {
               el_to_refine(e) = 1.0;
            }
         }
      }

      // Make the list of elements to be refined
      Array<int> el_to_refine_list;
      for (int e = 0; e < el_to_refine.Size(); e++)
      {
         if (el_to_refine(e) > 0.0)
         {
            el_to_refine_list.Append(e);
         }
      }

      int loc_count = el_to_refine_list.Size();
      int glob_count = loc_count;
      MPI_Allreduce(&loc_count, &glob_count, 1, MPI_INT, MPI_SUM,
                    pmesh.GetComm());
      MPI_Barrier(pmesh.GetComm());
      if (glob_count > 0)
      {
         pmesh.GeneralRefinement(el_to_refine_list, 1);
      }

      // Update
      h1fespace.Update();
      x.Update();
      x.ProjectCoefficient(ls_coeff);

      l2fespace.Update();
      el_to_refine.Update();

      lhfespace.Update();
      lhx.Update();

      distance_s.ParFESpace()->Update();
      distance_s.Update();

      if (pgf_to_update != NULL)
      {
         for (int i = 0; i < pgf_to_update->Size(); i++)
         {
            (*pgf_to_update)[i]->ParFESpace()->Update();
            (*pgf_to_update)[i]->Update();
         }
      }
   }
}

void ComputeScalarDistanceFromLevelSet(ParMesh &pmesh,
                                       Coefficient &ls_coeff,
                                       ParGridFunction &distance_s,
                                       const int nDiffuse = 2,
                                       const int pLapOrder = 4,
                                       const int pLapNewton = 50)
{
   mfem::H1_FECollection h1fec(distance_s.ParFESpace()->FEColl()->GetOrder(),
                               pmesh.Dimension());
   mfem::ParFiniteElementSpace h1fespace(&pmesh, &h1fec);
   mfem::ParGridFunction x(&h1fespace);

   x.ProjectCoefficient(ls_coeff);
   x.ExchangeFaceNbrData();

   //Now determine distance
   const double dx = AvgElementSize(pmesh);
   DistanceSolver *dist_solver = NULL;

   const int p = pLapOrder;
   const int newton_iter = pLapNewton;
   auto ds = new PLapDistanceSolver(p, newton_iter);
   //   auto ds = new NormalizationDistanceSolver();
   dist_solver = ds;

   ParFiniteElementSpace pfes_s(*distance_s.ParFESpace());

   // Smooth-out Gibbs oscillations from the input level set. The smoothing
   // parameter here is specified to be mesh dependent with length scale dx.
   ParGridFunction filt_gf(&pfes_s);
   PDEFilter filter(pmesh, 1.0 * dx);
   filter.Filter(ls_coeff, filt_gf);
   GridFunctionCoefficient ls_filt_coeff(&filt_gf);

   dist_solver->ComputeScalarDistance(ls_filt_coeff, distance_s);
   distance_s.SetTrueVector();
   distance_s.SetFromTrueVector();

   DiffuseField(distance_s, nDiffuse);
   distance_s.SetTrueVector();
   distance_s.SetFromTrueVector();
}

// loops over all the faces. For parents faces, checks wether the children across
// that face have same material
int CheckMaterialConsistency(ParMesh *pmesh, ParGridFunction &mat)
{
   mat.ExchangeFaceNbrData();
   const int NElem = pmesh->GetNE();
   MFEM_VERIFY(mat.Size() == NElem, "Material GridFunction should be a piecewise"
               "constant function over the mesh.");
   int matcheck = 1;
   int pass = 1;
   for (int f = 0; f < pmesh->GetNumFaces(); f++ )
   {
      Array<int> nbrs;
      pmesh->GetFaceAdjacentElements(f,nbrs);
      Vector matvals;
      Array<int> vdofs;
      Vector vec;
      //if there is more than 1 element across the face.
      matvals.SetSize(nbrs.Size()-1);
      if (nbrs.Size() > 2)
      {
         for (int j = 1; j < nbrs.Size(); j++)
         {
            if (nbrs[j] < NElem)
            {
               matvals(j-1) = mat(nbrs[j]);
            }
            else
            {
               const int Elem2NbrNo = nbrs[j] - NElem;
               mat.ParFESpace()->GetFaceNbrElementVDofs(Elem2NbrNo, vdofs);
               mat.FaceNbrData().GetSubVector(vdofs, vec);
               matvals(j-1) = vec(0);
            }
         }
         double minv = matvals.Min(),
                maxv = matvals.Max();
         matcheck = minv == maxv;
      }
      if (matcheck == 0) { pass = 0; break; }
   }
   int global_pass = pass;
   MPI_Allreduce(&pass, &global_pass, 1, MPI_INT, MPI_MIN,
                 pmesh->GetComm());
   return global_pass;
}

void GetMaterialInterfaceFaces(ParMesh *pmesh, ParGridFunction &mat,
                               Array<int> &intf)
{
   intf.SetSize(0);
   mat.ExchangeFaceNbrData();
   const int NElem = pmesh->GetNE();
   MFEM_VERIFY(mat.Size() == NElem, "Material GridFunction should be a piecewise"
               "constant function over the mesh.");
   for (int f = 0; f < pmesh->GetNumFaces(); f++ )
   {
      Array<int> nbrs;
      pmesh->GetFaceAdjacentElements(f,nbrs);
      Vector matvals;
      Array<int> vdofs;
      Vector vec;
      //if there is more than 1 element across the face.
      if (nbrs.Size() > 1)
      {
         matvals.SetSize(2);
         for (int j = 0; j < 2; j++)
         {
            if (nbrs[j] < NElem)
            {
               matvals(j) = mat(nbrs[j]);
            }
            else
            {
               const int Elem2NbrNo = nbrs[j] - NElem;
               mat.ParFESpace()->GetFaceNbrElementVDofs(Elem2NbrNo, vdofs);
               mat.FaceNbrData().GetSubVector(vdofs, vec);
               matvals(j) = vec(0);
            }
         }
         if (matvals(0) != matvals(1))
         {
            intf.Append(f);
         }
      }
   }
}

int GetRank(const Array<int> & offsets, int n)
{
   for (int i = 0; i<offsets.Size()-1; i++)
   {
      if (n>= offsets[i] && n<offsets[i+1])
      {
         return i;
      }
   }
   return offsets.Size()-1;
}

// Set material to same for all children.
// rule = 0 min of all children
void MakeMaterialsConsistent(ParMesh *pmesh, ParGridFunction &mat, int rule = 0)
{
   mat.ExchangeFaceNbrData();
   const int NElem = pmesh->GetNE();
   MFEM_VERIFY(mat.Size() == NElem, "Material GridFunction should be a piecewise"
               "constant function over the mesh.");
   const int num_procs = pmesh->GetNRanks();
   const int myid = pmesh->GetMyRank();
   Array<int> &fn_global_num = mat.ParFESpace()->face_nbr_glob_dof_map;

   ParNCMesh *pncmesh = pmesh->pncmesh;
   if (!pncmesh) { return; }

   // This can help get rank from global element number
   Array<int> offsets(num_procs);
   int offset = mat.ParFESpace()->GetMyDofOffset();
   MPI_Allgather(&offset, 1, MPI_INT, offsets.GetData(),
                 1, MPI_INT, MPI_COMM_WORLD);
   Array<int> vdofs;
   Vector vec;
   //    const int print_rank = 0;

   Array<int> send_count(num_procs);
   Array<Array<int> * > sendbufs(num_procs);
   for (int i = 0; i<num_procs; i++)
   {
      sendbufs[i]= new Array<int>();
   }
   send_count = 0;

   //    pmesh->ComputeGlobalElementOffset();
   pmesh->GetGlobalElementNum(0);

   for (int f = 0; f < pmesh->GetNumFaces(); f++ )
   {
      Array<int> nbrs;
      pmesh->GetFaceAdjacentElements(f,nbrs);
      int elem1no, elem2no, elem1inf, elem2inf, ncface;
      pmesh->GetFaceElements(f, &elem1no, &elem2no);
      pmesh->GetFaceInfos(f, &elem1inf, &elem2inf, &ncface);
      Array<double> mats;
      Array<double> globnums;
      Array<int> ranks;
      if (ncface >= 0 & elem2inf < 0)
      {
         //non-conforming master face
         for (int j = 0; j < nbrs.Size(); j++)
         {
            if (nbrs[j] < pmesh->GetNE())
            {
               mats.Append(mat(nbrs[j]));
               globnums.Append(pmesh->GetGlobalElementNum(nbrs[j]));
               ranks.Append(myid);
            }
            else
            {
               const int Elem2NbrNo = nbrs[j] - NElem;
               mat.ParFESpace()->GetFaceNbrElementVDofs(Elem2NbrNo, vdofs);
               mat.FaceNbrData().GetSubVector(vdofs, vec);
               mats.Append(vec(0));
               globnums.Append(fn_global_num[Elem2NbrNo]);
               ranks.Append(GetRank(offsets, fn_global_num[Elem2NbrNo]));
            }
         }
         double minv = mats.Min();
         double maxv = mats.Max();
         //             mats = minv;
         if (minv != maxv)
         {
            double val_to_impose = 1-mats[0];
            for (int j = 1; j < mats.Size(); j++)
            {
               if (mats[j] != val_to_impose)
               {
                  if (ranks[j] == myid)
                  {
                     mat(nbrs[j]) = val_to_impose;
                  }
                  else
                  {
                     send_count[ranks[j]] += 2;
                     sendbufs[ranks[j]]->Append(globnums[j]);
                     sendbufs[ranks[j]]->Append((int)val_to_impose);
                  }
               }
            }
         }
      }
   }
   Array<int> recv_count(num_procs);
   recv_count = 0;
   MPI_Alltoall(&send_count[0], 1, MPI_INT, &recv_count[0], 1,
                MPI_INT, MPI_COMM_WORLD);

   Array<Array<int> * > recvbufs(num_procs);
   for (int i = 0; i<num_procs; i++)
   {
      recvbufs[i] = new Array<int>();
      recvbufs[i]->SetSize(recv_count[i]);
   }

   int send_recv_proc_count = 0;
   for (int i = 0; i < num_procs; i++)
   {
      send_recv_proc_count += send_count[i] > 0;
   }
   for (int i = 0; i < num_procs; i++)
   {
      send_recv_proc_count += recv_count[i] > 0;
   }

   MPI_Status *statuses = NULL;
   MPI_Request *requests = NULL;
   if (send_recv_proc_count > 0)
   {
      statuses = new MPI_Status[send_recv_proc_count];
      requests = new MPI_Request[send_recv_proc_count];
   }

   // Post receives
   int send_recv_index = 0;
   int tag = 123421231;
   for (int i = 0; i < num_procs; i++)
   {
      if (recv_count[i] > 0)
      {
         MPI_Irecv(recvbufs[i]->GetData(), recv_count[i], MPI_INT, i,
                   tag, MPI_COMM_WORLD, &requests[send_recv_index++]);
      }
   }

   for (int i = 0; i < num_procs; i++)
   {
      if (send_count[i] > 0)
      {
         MPI_Isend(sendbufs[i]->GetData(), send_count[i], MPI_INT, i, tag,
                   MPI_COMM_WORLD, &requests[send_recv_index++]);
      }
   }

   if (send_recv_index > 0)
   {
      MPI_Waitall(send_recv_index, requests, statuses);
   }

   for (int i = 0; i<num_procs; i++)
   {
      const int cnt = recvbufs[i]->Size()/2;
      for (int j = 0; j < cnt; j++)
      {
         const int elem = (*(recvbufs[i]))[j*2];
         const int matv = (*(recvbufs[i]))[j*2+1];
         const int lelem = pmesh->GetLocalElementNum(elem);
         mat(lelem) = matv*1.0;
      }
   }

   for (int i=0; i<num_procs; i++)
   {
      delete recvbufs[i];
      delete sendbufs[i];
   }

   delete [] statuses;
   delete [] requests;

   mat.ExchangeFaceNbrData();
}

class HRefUpdater
{
protected:
   Array<ParGridFunction *> pgridfuncarr;
   Array<ParFiniteElementSpace *> pfespacearr;

public:
   HRefUpdater() {}

   void AddGridFunctionForUpdate(ParGridFunction *pgf_)
   {
      pgridfuncarr.Append(pgf_);
   }
   void AddFESpaceForUpdate(ParFiniteElementSpace *pfes_)
   {
      pfespacearr.Append(pfes_);
   }

   void Update();
};

void HRefUpdater::Update()
{
   // Update FESpace
   for (int i = 0; i < pfespacearr.Size(); i++)
   {
      pfespacearr[i]->Update();
   }
   // Update nodal GF
   for (int i = 0; i < pgridfuncarr.Size(); i++)
   {
      pgridfuncarr[i]->Update();
      pgridfuncarr[i]->SetTrueVector();
      pgridfuncarr[i]->SetFromTrueVector();
   }
}

#endif
