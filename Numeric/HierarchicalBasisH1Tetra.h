#ifndef _HierarchicalBasisH1Tetra_H_
#define _HierarchicalBasisH1Tetra_H_

#include "HierarchicalBasisH1.h"
#include <algorithm>

/*
 * MTetrahedron
 *
 *                      v
 *                    .
 *                  ,/
 *                 /
 *              2
 *            ,/|`\
 *          ,/  |  `\
 *        ,/    '.   `\
 *      ,/       |     `\
 *    ,/         |       `\
 *   0-----------'.--------1 --> u
 *    `\.         |      ,/
 *       `\.      |    ,/
 *          `\.   '. ,/
 *             `\. |/
 *                `3
 *                   `\.
 *                      ` w
 *
 *
 *  Oriented Edges:
 * e0={0, 1}, e1={1, 2}, e2={2, 0}, e3={3, 0}, e4={3, 2}, e5={3, 1}
 *
 *
 * Oritented Surface:
 *  s0={0, 1, 2}, s1={0, 1, 3}, s2={0, 2, 3}, s3={1, 2, 3}
 * Local (directional) orders on mesh faces are not allowed to exceed the mini-
 * mum of the (appropriate directional) orders of approximation associated with
 * the interior of the adjacent elements. Local orders of approximation on mesh
 * edges are limited by the minimum of all (appropriate directional) orders cor-
 * responding to faces sharing that edge
 */
class HierarchicalBasisH1Tetra : public HierarchicalBasisH1 {
public:
  HierarchicalBasisH1Tetra(int order);
  virtual ~HierarchicalBasisH1Tetra();
  // vertexBasis=[v0,...,v3]
  // edgeBasis=[phie0_{2},...phie0_{pe0-1},phie1_{2},...phie1_{pe1-1}...]
  // faceBasis=[phif0_{1,1},...,phif0_{1,pF0-2},phif0_{2,1}...,phif0_{2,pF0-3},...,phief0_{pF-2,1},phif1_{1,1}...]
  // bubbleBasis=[phieb_{1,1,1},...,phieb_{1,1,pb-3},...]   n1+n2+n3<=pb-1
  virtual void generateBasis(double const &u, double const &v, double const &w,
                             std::vector<double> &vertexBasis,
                             std::vector<double> &edgeBasis,
                             std::vector<double> &faceBasis,
                             std::vector<double> &bubbleBasis);
  virtual void
  generateGradientBasis(double const &u, double const &v, double const &w,
                        std::vector<std::vector<double> > &gradientVertex,
                        std::vector<std::vector<double> > &gradientEdge,
                        std::vector<std::vector<double> > &gradientFace,
                        std::vector<std::vector<double> > &gradientBubble);

  virtual void orientateEdge(int const &flagOrientation, int const &edgeNumber,
                             std::vector<double> &edgeBasis);

  virtual void
  orientateEdgeGrad(int const &flagOrientation, int const &edgeNumber,
                    std::vector<std::vector<double> > &gradientEdge);

  virtual void orientateFace(double const &u, double const &v, double const &w,
                             int const &flag1, int const &flag2,
                             int const &flag3, int const &faceNumber,
                             std::vector<double> &faceBasis);
  virtual void
  orientateFaceGrad(double const &u, double const &v, double const &w,
                    int const &flag1, int const &flag2, int const &flag3,
                    int const &faceNumber,
                    std::vector<std::vector<double> > &gradientFace);
  virtual void reverseFaceBubbleFor3D(const bool belongBoundary){};
  virtual void reverseEdgeBubbleFor2D(const bool belongBoundary){};

private:
  int _pb; // bubble function order
  int _pOrderEdge[6]; // Edge functions order (pOrderEdge[0] matches the order
                      // of the edge 0)
  int _pOrderFace[4]; // Face functions order in direction
  static double
  _affineCoordinate(const int &j, const double &u, const double &v,
                    const double &w); // affine coordinate lambdaj j=1..4
};
#endif
