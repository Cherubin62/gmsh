// Gmsh - Copyright (C) 1997-2020 C. Geuzaine, J.-F. Remacle
//
// See the LICENSE.txt file for license information. Please report all
// issues on https://gitlab.onelab.info/gmsh/gmsh/issues.

#include <map>
#include <set>

#if defined(_OPENMP)
#include <omp.h>
#endif

#include "GmshConfig.h"
#include "meshGRegionHxt.h"
#include "Context.h"
#include "MVertex.h"
#include "GRegion.h"
#include "GFace.h"
#include "MTetrahedron.h"
#include "MTriangle.h"
#include "MLine.h"
#include "GmshMessage.h"
#include "BackgroundMeshTools.h"
#include "OS.h"

#if defined(HAVE_HXT)

extern "C" {
#include "hxt_tools.h"
#include "hxt_boundary_recovery.h"
#include "hxt_tetMesh.h"
}

static HXTStatus recoveryCallback(HXTMesh *mesh, void *userData)
{
  return hxt_boundary_recovery(mesh);
}

static HXTStatus messageCallback(HXTMessage *msg)
{
  if(msg) Msg::Info("%s", msg->string);
  return HXT_STATUS_OK;
}

static HXTStatus meshSizeCallBack(double* pts, size_t numPts, void *userData)
{
  GRegion *gr = (GRegion *)userData;

  for(size_t i=0; i<numPts; i++) {
    double lc = BGM_MeshSizeWithoutScaling(gr, 0, 0,
                                           pts[4 * i + 0],
                                           pts[4 * i + 1],
                                           pts[4 * i + 2]);
    if(lc != MAX_LC || !CTX::instance()->mesh.lcExtendFromBoundary) {
      // constrain by global lcMin and lcMax
      lc = std::max(lc, CTX::instance()->mesh.lcMin);
      lc = std::min(lc, CTX::instance()->mesh.lcMax);
      // apply global scaling
      if(gr->getMeshSizeFactor() != 1.0) lc *= gr->getMeshSizeFactor();
        lc *= CTX::instance()->mesh.lcFactor;

      // if(lc > 0.0)
      pts[4 * i + 3] = lc;
    }
    // else pts[4 * i + 3] already contains the size computed by HXT
  }

  return HXT_STATUS_OK;
}

static HXTStatus getAllFacesOfAllRegions(std::vector<GRegion *> &regions,
                                         HXTMesh *m,
                                         std::vector<GFace *> &allFaces)
{
  std::set<GFace *, GEntityPtrLessThan> allFacesSet;
  if(m) {
    m->brep.numVolumes = regions.size();
    HXT_CHECK(hxtAlignedMalloc(&m->brep.numSurfacesPerVolume,
                               m->brep.numVolumes * sizeof(uint32_t)));
  }
  uint32_t to_alloc = 0;
  for(std::size_t i = 0; i < regions.size(); i++) {
    std::vector<GFace *> const &f = regions[i]->faces();
    std::vector<GFace *> const &f_e = regions[i]->embeddedFaces();
    if(m) {
      m->brep.numSurfacesPerVolume[i] = f.size() + f_e.size();
      to_alloc += m->brep.numSurfacesPerVolume[i];
    }
    allFacesSet.insert(f.begin(), f.end());
    allFacesSet.insert(f_e.begin(), f_e.end());
  }
  allFaces.insert(allFaces.begin(), allFacesSet.begin(), allFacesSet.end());

  if(!m) return HXT_STATUS_OK;

  HXT_CHECK(
    hxtAlignedMalloc(&m->brep.surfacesPerVolume, to_alloc * sizeof(uint32_t)));

  uint32_t counter = 0;
  for(std::size_t i = 0; i < regions.size(); i++) {
    std::vector<GFace *> const &f = regions[i]->faces();
    std::vector<GFace *> const &f_e = regions[i]->embeddedFaces();
    for(size_t j = 0; j < f.size(); j++)
      m->brep.surfacesPerVolume[counter++] = f[j]->tag();
    for(size_t j = 0; j < f_e.size(); j++)
      m->brep.surfacesPerVolume[counter++] = f_e[j]->tag();
  }

  return HXT_STATUS_OK;
}

static HXTStatus getAllEdgesOfAllFaces(std::vector<GFace *> &faces, HXTMesh *m,
                                       std::vector<GEdge *> &allEdges)
{
  if(m) {
    m->brep.numSurfaces = faces.size();
    HXT_CHECK(hxtAlignedMalloc(&m->brep.numCurvesPerSurface,
                               m->brep.numSurfaces * sizeof(uint32_t)));
  }
  uint32_t to_alloc = 0;

  std::set<GEdge *, GEntityPtrLessThan> allEdgesSet;
  for(std::size_t i = 0; i < faces.size(); i++) {
    std::vector<GEdge *> const &f = faces[i]->edges();
    std::vector<GEdge *> const &f_e = faces[i]->embeddedEdges();
    if(m) {
      m->brep.numCurvesPerSurface[i] = f.size() + f_e.size();
      to_alloc += m->brep.numCurvesPerSurface[i];
    }
    allEdgesSet.insert(f.begin(), f.end());
    allEdgesSet.insert(f_e.begin(), f_e.end());
  }
  allEdges.insert(allEdges.begin(), allEdgesSet.begin(), allEdgesSet.end());

  if(!m) return HXT_STATUS_OK;

  HXT_CHECK(
    hxtAlignedMalloc(&m->brep.curvesPerSurface, to_alloc * sizeof(uint32_t)));

  uint32_t counter = 0;
  for(std::size_t i = 0; i < faces.size(); i++) {
    std::vector<GEdge *> const &f = faces[i]->edges();
    std::vector<GEdge *> const &f_e = faces[i]->embeddedEdges();
    for(size_t j = 0; j < f.size(); j++)
      m->brep.curvesPerSurface[counter++] = f[j]->tag();
    for(size_t j = 0; j < f_e.size(); j++)
      m->brep.curvesPerSurface[counter++] = f_e[j]->tag();
  }
  return HXT_STATUS_OK;
}

static HXTStatus Hxt2Gmsh(std::vector<GRegion *> &regions, HXTMesh *m,
                          std::map<MVertex *, uint32_t> &v2c,
                          std::vector<MVertex *> &c2v)
{
  Msg::Debug("Start Hxt2Gmsh");
  std::vector<GFace *> allFaces;
  std::vector<GEdge *> allEdges;
  HXT_CHECK(getAllFacesOfAllRegions(regions, NULL, allFaces));
  HXT_CHECK(getAllEdgesOfAllFaces(allFaces, NULL, allEdges));
  std::map<uint32_t, GEdge *> i2e;
  std::map<uint32_t, GFace *> i2f;
  for(size_t i = 0; i < allFaces.size(); i++)
    i2f[allFaces[i]->tag()] = allFaces[i];
  for(size_t i = 0; i < allEdges.size(); i++)
    i2e[allEdges[i]->tag()] = allEdges[i];

  c2v.resize(m->vertices.num);
  for(size_t i = c2v.size(); i < m->vertices.num; i++) { c2v[i] = NULL; }

  for(size_t j = 0; j < allEdges.size(); j++) {
    GEdge *ge = allEdges[j];
    for(size_t i = 0; i < ge->lines.size(); i++) { delete ge->lines[i]; }
    ge->lines.clear();
  }

  for(size_t j = 0; j < allFaces.size(); j++) {
    GFace *gf = allFaces[j];
    for(size_t i = 0; i < gf->triangles.size(); i++) {
      delete gf->triangles[i];
    }
    gf->triangles.clear();
  }

  uint16_t warning = 0;

  for(size_t i = 0; i < m->lines.num; i++) {
    uint32_t i0 = m->lines.node[2 * i + 0];
    uint32_t i1 = m->lines.node[2 * i + 1];
    uint16_t c = m->lines.colors[i];
    MVertex *v0 = c2v[i0];
    MVertex *v1 = c2v[i1];
    std::map<uint32_t, GEdge *>::iterator ge = i2e.find(c);
    if(ge == i2e.end()) {
      if(warning != c) {
        warning = c;
        Msg::Warning("Could not find curve for HXT color %d", c);
      }
      continue;
    }
    if(!v0) {
      double *x = &m->vertices.coord[4 * i0];
      // FIXME compute true coordinates
      v0 = new MEdgeVertex(x[0], x[1], x[2], ge->second, 0);
    }
    if(!v1) {
      // FIXME compute true coordinates
      double *x = &m->vertices.coord[4 * i1];
      v1 = new MEdgeVertex(x[0], x[1], x[2], ge->second, 0);
    }
    ge->second->lines.push_back(new MLine(v0, v1));
  }

  for(size_t i = 0; i < m->triangles.num; i++) {
    uint32_t i0 = m->triangles.node[3 * i + 0];
    uint32_t i1 = m->triangles.node[3 * i + 1];
    uint32_t i2 = m->triangles.node[3 * i + 2];
    uint16_t c = m->triangles.colors[i];
    MVertex *v0 = c2v[i0];
    MVertex *v1 = c2v[i1];
    MVertex *v2 = c2v[i2];
    std::map<uint32_t, GFace *>::iterator gf = i2f.find(c);
    if(gf == i2f.end()) {
      if(warning != c) {
        warning = c;
        Msg::Warning("Could not find surface for HXT color %d", c);
      }
      continue;
    }
    if(!v0) {
      // FIXME compute true coordinates
      double *x = &m->vertices.coord[4 * i0];
      v0 = new MFaceVertex(x[0], x[1], x[2], gf->second, 0, 0);
    }
    if(!v1) {
      // FIXME compute true coordinates
      double *x = &m->vertices.coord[4 * i1];
      v1 = new MFaceVertex(x[0], x[1], x[2], gf->second, 0, 0);
    }
    if(!v2) {
      // FIXME compute true coordinates
      double *x = &m->vertices.coord[4 * i2];
      v2 = new MFaceVertex(x[0], x[1], x[2], gf->second, 0, 0);
    }
    gf->second->triangles.push_back(new MTriangle(v0, v1, v2));
  }
#if defined(_OPENMP)
#pragma omp parallel
#endif
  {
    std::vector<std::vector<MVertex *> > thread_local_vertex_vector(
      regions.size());
    std::vector<std::vector<MTetrahedron *> > thread_local_tetrahedron_vector(
      regions.size());
#if defined(_OPENMP)
#pragma omp for nowait
#endif
    for(size_t i = 0; i < m->tetrahedra.num; i++) {
      uint32_t *i0 = &m->tetrahedra.node[4 * i + 0];
      uint16_t c = m->tetrahedra.colors[i];
      if(c < regions.size()) {
        MVertex *vv[4];
        GRegion *gr = regions[c];
        for(int j = 0; j < 4; j++) {
          MVertex *v0 = c2v[i0[j]];
          if(!v0) {
            double *x = &m->vertices.coord[4 * i0[j]];
            v0 = new MVertex(x[0], x[1], x[2], gr);
            // Insert the points into the thread-local vector first and then
            // summarize at the end
            thread_local_vertex_vector[c].push_back(v0);
            c2v[i0[j]] = v0;
          }
          vv[j] = v0;
        }
        // same as the MVertex object
        thread_local_tetrahedron_vector[c].push_back(
          new MTetrahedron(vv[0], vv[1], vv[2], vv[3]));
      }
    }
// insert thread local vector into the global vector
#if defined(_OPENMP)
#pragma omp critical
#endif
    {
      for(size_t i = 0; i < regions.size(); i++) {
        GRegion *gr = regions[i];
        gr->mesh_vertices.insert(gr->mesh_vertices.end(),
                                 thread_local_vertex_vector[i].begin(),
                                 thread_local_vertex_vector[i].end());
        gr->tetrahedra.insert(gr->tetrahedra.end(),
                              thread_local_tetrahedron_vector[i].begin(),
                              thread_local_tetrahedron_vector[i].end());
      }
    }
  }
  Msg::Debug("End Hxt2Gmsh");
  return HXT_STATUS_OK;
}

HXTStatus Gmsh2Hxt(std::vector<GFace *> &faces, HXTMesh *m,
       std::map<MVertex *, uint32_t> &v2c,
       std::vector<MVertex *> &c2v)
{
  std::vector<GEdge *> edges;
  HXT_CHECK(getAllEdgesOfAllFaces(faces, m, edges));
  std::set<MVertex *> all;


  uint64_t ntri = 0;
  uint64_t nedg = 0;

  for(size_t j = 0; j < edges.size(); j++) {
    GEdge *ge = edges[j];
    nedg += ge->lines.size();
    for(size_t i = 0; i < ge->lines.size(); i++) {
      all.insert(ge->lines[i]->getVertex(0));
      all.insert(ge->lines[i]->getVertex(1));
    }
  }

  for(size_t j = 0; j < faces.size(); j++) {
    GFace *gf = faces[j];
    ntri += gf->triangles.size();
    for(size_t i = 0; i < gf->triangles.size(); i++) {
      all.insert(gf->triangles[i]->getVertex(0));
      all.insert(gf->triangles[i]->getVertex(1));
      all.insert(gf->triangles[i]->getVertex(2));
    }
  }

  m->vertices.num = m->vertices.size = all.size();
  HXT_CHECK(
    hxtAlignedMalloc(&m->vertices.coord, 4 * m->vertices.num * sizeof(double)));

  size_t count = 0;
  c2v.resize(all.size());
  for(std::set<MVertex *>::iterator it = all.begin(); it != all.end(); it++) {
    m->vertices.coord[4 * count + 0] = (*it)->x();
    m->vertices.coord[4 * count + 1] = (*it)->y();
    m->vertices.coord[4 * count + 2] = (*it)->z();
    m->vertices.coord[4 * count + 3] = 0.0;
    v2c[*it] = count;
    c2v[count++] = *it;
  }
  all.clear();

  m->lines.num = m->lines.size = nedg;
  uint64_t index = 0;

  HXT_CHECK(
    hxtAlignedMalloc(&m->lines.node, (m->lines.num) * 2 * sizeof(uint32_t)));
  HXT_CHECK(
    hxtAlignedMalloc(&m->lines.colors, (m->lines.num) * sizeof(uint16_t)));

  for(size_t j = 0; j < edges.size(); j++) {
    GEdge *ge = edges[j];
    for(size_t i = 0; i < ge->lines.size(); i++) {
      m->lines.node[2 * index + 0] = v2c[ge->lines[i]->getVertex(0)];
      m->lines.node[2 * index + 1] = v2c[ge->lines[i]->getVertex(1)];
      m->lines.colors[index] = ge->tag();
      index++;
    }
  }

  m->triangles.num = m->triangles.size = ntri;
  HXT_CHECK(hxtAlignedMalloc(&m->triangles.node,
                             (m->triangles.num) * 3 * sizeof(uint32_t)));
  HXT_CHECK(hxtAlignedMalloc(&m->triangles.colors,
                             (m->triangles.num) * sizeof(uint16_t)));

  index = 0;
  for(size_t j = 0; j < faces.size(); j++) {
    GFace *gf = faces[j];
    for(size_t i = 0; i < gf->triangles.size(); i++) {
      m->triangles.node[3 * index + 0] = v2c[gf->triangles[i]->getVertex(0)];
      m->triangles.node[3 * index + 1] = v2c[gf->triangles[i]->getVertex(1)];
      m->triangles.node[3 * index + 2] = v2c[gf->triangles[i]->getVertex(2)];
      m->triangles.colors[index] = gf->tag();
      index++;
    }
  }
  return HXT_STATUS_OK;
}


HXTStatus Gmsh2Hxt(std::vector<GRegion *> &regions, HXTMesh *m,
		   std::map<MVertex *, uint32_t> &v2c,
		   std::vector<MVertex *> &c2v)
{
  std::vector<GFace *> faces;
  HXT_CHECK(getAllFacesOfAllRegions(regions, m, faces));
  HXT_CHECK(Gmsh2Hxt(faces,m,v2c,c2v));
  return HXT_STATUS_OK;
}


static HXTStatus _meshGRegionHxt(std::vector<GRegion *> &regions)
{
  HXT_CHECK(hxtSetMessageCallback(messageCallback));

  HXTMesh *mesh;
  HXT_CHECK(hxtMeshCreate(&mesh));

  std::map<MVertex *, uint32_t> v2c;
  std::vector<MVertex *> c2v;
  Gmsh2Hxt(regions, mesh, v2c, c2v);

  HXTTetMeshOptions options = {
    0, // int defaultThreads;
    0, // int delaunayThreads;
    0, // int improveThreads;
    1, // int reproducible;
    (Msg::GetVerbosity() > 5) ? 2 : 1, // int verbosity;
    1, // int stat;
    1, // int refine;
    CTX::instance()->mesh.optimize, // int optimize;
    CTX::instance()->mesh.optimizeThreshold, // double qualityMin;
    0, // double (*qualityFun)
    0, // void* qualityData;
    meshSizeCallBack,//&hxtMeshSizeGmshCallBack, // double (*meshSizeFun)
    regions[0], // void* meshSizeData;
    recoveryCallback, // HXTStatus (*recoveryFun)
    0 // void* recoveryData;
  };

  HXT_CHECK(hxtTetMesh(mesh, &options));

  // HXT_CHECK(hxtMeshWriteGmsh(mesh, "hxt.msh"));

  HXT_CHECK(Hxt2Gmsh(regions, mesh, v2c, c2v));
  HXT_CHECK(hxtMeshDelete(&mesh));
  return HXT_STATUS_OK;
}

int meshGRegionHxt(std::vector<GRegion *> &regions)
{
  HXTStatus status = _meshGRegionHxt(regions);
  if(status == HXT_STATUS_OK) return 0;
  return 1;
}

#else

int meshGRegionHxt(std::vector<GRegion *> &regions)
{
  Msg::Error("Gmsh should be compiled with Hxt3D to enable this option");
  return -1;
}

#endif
