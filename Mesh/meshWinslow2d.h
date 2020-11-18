#ifndef _MESH_WINSLOW_2D_
#define _MESH_WINSLOW_2D_

#include <vector>
class GModel;
class GFace;
class Field;
class MQuadrangle;
class MVertex;
class SurfaceProjector;
void meshWinslow2d (GModel * gm, int nIter=100, Field *f = NULL);
void meshWinslow2d (GFace  * gf, int nIter=100, Field *f = NULL, bool remove = false, SurfaceProjector* sp = NULL);
void meshWinslow2d (GFace  * gf, const std::vector<MQuadrangle*>& quads, const std::vector<MVertex*>& freeVertices, int nIter=100, Field *f = NULL, bool remove = false, SurfaceProjector* sp = NULL);

#include <map>
#include <set>
#include "MVertex.h"

class MElement;

int remeshCavity (GFace *gf,
			 int index,
			 std::set<MElement*> &cavity,
			 std::vector<MVertex*> &bnd,
       std::map<MVertex *, std::vector<MElement *>, MVertexPtrLessThan>& adj,
			 std::map<MVertex*,int, MVertexPtrLessThan> &newSings);

#endif
