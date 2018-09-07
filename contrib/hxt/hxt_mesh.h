#ifndef _HEXTREME_MESH_
#define _HEXTREME_MESH_

#include "hxt_tools.h" // to have SIMD_ALIGN and stdint.h

#define HXT_GHOST_VERTEX UINT32_MAX
#define HXT_DELETED_COLOR (UINT16_MAX-1)

#define HXT_NO_ADJACENT UINT64_MAX

/* Element types, same as Gmsh */
typedef enum {
  HXT_NO_ELT = 0,
  HXT_LINE = 1,
  HXT_TRI = 2,
  HXT_QUAD = 3,
  HXT_TET = 4,
  HXT_HEX = 5,
  HXT_PRI = 6,
  HXT_PYR = 7
} HXT_ELT_TYPE;

struct hxtMeshStruct {
  HXTContext* ctx;

  // vertices
  struct {
    uint32_t num;
    uint32_t size;
    double* coord; // 3 coordinates + 1 double per vertex!
  } vertices;

  // tetrahedra
  struct {
    uint32_t* node;  // aligned (size = tetrahedra.size*4*sizeof(uint32_t))
    uint64_t* neigh; // aligned (size = tetrahedra.size*4*sizeof(uint64_t))
    char* neighType;
    uint16_t* colors;
    uint16_t* flag;
    uint64_t num;    // number of tetrahedra
    uint64_t size;   // reserved number of tetrahedra (size of the vector)
  } tetrahedra;

  // hexahedra
  struct {
    uint32_t* node;  // aligned (size = hexahedra.size*8*sizeof(uint32_t))
    uint64_t* neigh; // aligned (size = hexahedra.size*6*sizeof(uint64_t))
    char* neighType;
    uint16_t* colors;
    uint16_t* flag;
    uint64_t num;    // number of tetrahedra
    uint64_t size;   // reserved number of hexahedra (size of the vector)
  } hexahedra;

  // prisms
  struct {
    uint32_t* node;  // aligned (size = prisms.size*6*sizeof(uint32_t))
    uint64_t* neigh; // aligned (size = prisms.size*5*sizeof(uint64_t))
    char* neighType;
    uint16_t* colors;
    uint16_t* flag;
    uint64_t num;    // number of tetrahedra
    uint64_t size;   // reserved number of prisms (size of the vector)
  } prisms;

  // pyramids
  struct {
    uint32_t* node;  // aligned (size = pyramids.size*5*sizeof(uint32_t))
    uint64_t* neigh; // aligned (size = pyramids.size*5*sizeof(uint64_t))
    char* neighType;
    uint16_t* colors;
    uint16_t* flag;
    uint64_t num;    // number of tetrahedra
    uint64_t size;   // reserved number of pyramids (size of the vector)
  } pyramids;

  // triangles // TODO: consider writing a array of structure...
  struct {
    uint32_t* node;
    uint64_t* neigh;
    uint16_t* colors;
    uint64_t num;
    uint64_t size;
  } triangles;

  // quads
  struct {
    uint32_t* node;
    uint16_t* colors;
    uint64_t num;
    uint64_t size;
  } quads;

  // lines // TODO: consider writing a array of structure...
  struct {
    uint32_t* node;
    uint16_t* colors;
    uint64_t num;
    uint64_t size;
  } lines;
  struct {
    uint32_t numVolumes;
    uint32_t *numSurfacesPerVolume;
    uint32_t *surfacesPerVolume;
    uint32_t numSurfaces;
    uint32_t *numCurvesPerSurface;
    uint32_t *curvesPerSurface;
    uint32_t numCurves;
    uint32_t *endPointsOfCurves;
    uint32_t numPoints;
    uint32_t *points;
  } brep;
};


#endif
