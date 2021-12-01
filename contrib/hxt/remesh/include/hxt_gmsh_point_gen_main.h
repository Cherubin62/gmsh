#ifndef HXT_GMSH_POINT_GEN_MAIN_H
#define HXT_GMSH_POINT_GEN_MAIN_H

#include "hxt_tools.h"
#include "hxt_mesh.h"
#include "hxt_point_gen_options.h"

/**
 * \brief Generate points on an initial mesh, given a crossfield and a sizemap.
 * \details blah blah blah \n
 *
 * \param mesh: a valid Delaunay initial mesh.
 * \param opt: options to give to the point generation algorithm \ref HXTPointGenOptions
 * \param sizemap: the mesh size on each vertex of the initial mesh.
 * \param directions: the directions on each vertex of the initial mesh.
 * \param[out] fmesh: the resulting mesh containing the generated vertices.
 */

HXTStatus hxtGmshPointGenMain(HXTMesh *mesh, 
                              HXTPointGenOptions *opt,
                              double *data,
                              HXTMesh *fmesh);

HXTStatus hxtGmshVolumePointGen(HXTMesh *mesh, 
                                int verbosity,
                                double *sizemap,
                                double *directions,
                                size_t *npts,
                                double **pts,
                                uint32_t **binaryIndices);
#endif
