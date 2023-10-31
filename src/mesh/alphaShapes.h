#ifndef _ALPHA_SHAPES_H_
#define _ALPHA_SHAPES_H_
#include <vector>

#include "GmshConfig.h"
#ifdef HAVE_HXT
#include "meshGRegionHxt.h"
#include "MVertex.h"
#include "MTetrahedron.h"
#include "MTriangle.h"
#include "MLine.h"
#include "GModel.h"
#include "Generator.h"
#include "meshGRegion.h"
#include "meshGFace.h"
#include "meshGEdge.h"
#include "meshPolyMesh.h"
#include "meshTriangulation.h"
#include "qualityMeasures.h"
#include "robustPredicates.h"

extern "C" {
#include "hxt_tetMesh.h"
#include "hxt_tetDelaunay.h"
#include "hxt_tetOpti.h"
#include "hxt_tetColor.h"
#include "hxt_tetFlag.h"
#include "hxt_tetRefine.h"
#include "hxt_alphashape.h"
}

int computeTetNeighbors_ (const std::vector<size_t> &tetrahedra, std::vector<size_t> &neigh);

int alphaShapes_ (const double threshold,
	const int dim,
	const std::vector<double> &pts,
    const std::vector<double> &nodalSize,
	std::vector<size_t> &elements, 
	std::vector<std::vector<size_t> > &domains,
	std::vector<std::vector<size_t> > &boundaries,
	std::vector<size_t> &neigh);

#endif

void constrainedAlphaShapes_(GModel* m, 
                            const int dim, 
                            const int tag,
                            const std::vector<double>& coord, 
                            const std::vector<size_t>& nodeTags,
                            const double alpha, 
                            const double meanValue,
                            std::vector<size_t> &tetrahedra, 
                            std::vector<std::vector<size_t> > &domains,
                            std::vector<std::vector<size_t> > &boundaries,
                            std::vector<size_t> &neigh,
                            double &hMean,
                            const std::vector<int> &controlTags);

void generateMesh_(const int dim, const int tag, const bool refine, const std::vector<double> &coord, const std::vector<size_t> &nodeTags);

void constrainedDelaunayRefinement_(const int dim, const int tag,
                                    const std::vector<size_t> &elementTags,
                                    const std::vector<size_t> &constrainedEdges,
                                    const std::vector<size_t> &nodeTags,
                                    const std::vector<double> &sizeField, 
                                    const double minRadius, 
                                    const double minQuality,
                                    std::vector<size_t> &newNodeTags, 
                                    std::vector<double>& newCoords, 
                                    std::vector<double>& newSizeField, 
                                    std::vector<std::vector<size_t>>& newConstrainedEdges,
                                    std::vector<size_t>& newElementsInRefinement);

void alphaShape_entity(const int dim, const int tag, const double alpha, const std::vector<size_t>& nodeTags, const std::vector<double>& sizeAtNodes, std::vector<std::vector<size_t>>& elementTags, std::vector<std::vector<size_t>>& edges);

void performAlphaShapeAndRefine_(const std::vector<size_t>& nodeTags, const std::vector<double>& coord, const std::vector<int>& nodesDimTags, const int refine, const std::vector<double>& sizeAtNodes, const double alpha, const double hMean, const int surfaceTag, const int volumeTag);

#endif
