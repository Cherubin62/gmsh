#ifndef HXT_COLLAPSE_EDGE_V2_H
#define HXT_COLLAPSE_EDGE_V2_H

#include "hxt_tools.h"
#include "hxt_mesh.h"
#include "hxt_edge.h"

#include "hxt_point_gen_utils.h" // for parent 

// ATTENTION
//
// This files are similar to hxt_collapse_edge 
//
// just a quick fix with new functions to handle one-triangle cavities in collapse
// they should be merged at some point 

HXTStatus hxtRemoveBoundaryVertex_V2(HXTMesh *mesh,
                                     HXTEdges *edges,
                                     HXTPointGenParent *parent,
                                     uint32_t *flagE,
                                     uint32_t *lines2edges,
                                     uint64_t *edges2lines,
                                     uint64_t *lines2triangles,
                                     uint64_t maxNumTriToLine,
                                     uint64_t *vertices2lines,
                                     uint64_t maxNumLinesToVertex,
                                     uint32_t vd,  // vertex to be removed
                                     int *collapsed ); 

HXTStatus hxtRemoveInteriorVertex_V2(HXTMesh *mesh,
                                     HXTEdges *edges,
                                     HXTPointGenParent *parent,
                                     uint32_t *flagE,
                                     uint32_t *lines2edges,
                                     uint64_t *edges2lines,
                                     uint64_t *lines2triangles,
                                     uint64_t maxNumTriToLine,
                                     uint64_t *vertices2lines,
                                     uint64_t maxNumLinesToVertex,
                                     uint32_t vd,  // vertex to be removed
                                     int *collapsed );

#endif


