// HighOrderMeshOptimizer - Copyright (C) 2013-2023 UCLouvain-ULiege
//
// Permission is hereby granted, free of charge, to any person
// obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished
// to do so, provided that the above copyright notice(s) and this
// permission notice appear in all copies of the Software and that
// both the above copyright notice(s) and this permission notice
// appear in supporting documentation.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT OF THIRD PARTY RIGHTS. IN NO EVENT SHALL THE
// COPYRIGHT HOLDER OR HOLDERS INCLUDED IN THIS NOTICE BE LIABLE FOR
// ANY CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY
// DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
// WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
// ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.
//
// Contributors: Amaury Johnen

#include "BoundaryLayerCurver.h"
#include "MQuadrangle.h"
#include "MTriangle.h"
#include "BasisFactory.h"
#include "GFace.h"
#include "orthogonalBasis.h"
#include "bezierBasis.h"
#include "gmshVertex.h"
#include "Geo.h"
#include "MLine.h"
#include "GModel.h"
#include "Options.h"
#include "AnalyseMeshQuality.h"
#include "InnerVertexPlacement.h"
#include "pointsGenerators.h"
#include "qualityMeasuresJacobian.h"

#if defined(HAVE_POST)
#include "PView.h"
#endif

#if defined(HAVE_FLTK)
#include "FlGui.h"
#endif

namespace {
  void drawEquidistantPoints(GEdge *gedge, int N)
  {
    return;
    const std::size_t numLine = gedge->getNumMeshElements();
    for(std::size_t i = 0; i < numLine; ++i) {
      gedge->getMeshElement(i)->setVisibility(0);
    }

    const double umin = gedge->getLowerBound();
    const double umax = gedge->getUpperBound();
    const double step = (umax - umin) / (N - 1);

    for(int i = 0; i < N; ++i) {
      const double u = umin + i * step;
      const GPoint p = gedge->point(u);
      MVertex *v = new MVertex(p.x(), p.y(), p.z());
      gedge->addMeshVertex(v);
      gedge->addLine(new MLine(v, v));
    }
  }

  void drawBezierControlPolygon(const bezierCoeff &controlPoints, GEdge *gedge)
  {
    const int nVert = controlPoints.getNumCoeff();

    MVertex *previous = nullptr;
    for(int i = 0; i < nVert; ++i) {
      MVertex *v = new MVertex(controlPoints(i, 0), controlPoints(i, 1),
                               controlPoints(i, 2), gedge);
      if(previous) {
        MLine *line = new MLine(v, previous);
        gedge->addLine(line);
      }
      gedge->addMeshVertex(v);
      previous = v;
    }
  }

  void drawBezierControlPolygon(const std::vector<MVertex *> &vertices,
                                GEdge *gedge = nullptr)
  {
    if(!gedge) { gedge = *GModel::current()->firstEdge(); }

    const int nVert = (int)vertices.size();
    fullMatrix<double> xyz(nVert, 3);
    for(int i = 0; i < nVert; ++i) {
      xyz(i, 0) = vertices[i]->x();
      xyz(i, 1) = vertices[i]->y();
      xyz(i, 2) = vertices[i]->z();
    }

    bezierCoeff *controlPoints =
      new bezierCoeff(FuncSpaceData(TYPE_LIN, nVert - 1, false), xyz);
    std::vector<bezierCoeff *> allControlPoints(1, controlPoints);

    int numSubdivision = 0; // change this to choose num subdivision
    while(numSubdivision-- > 0) {
      std::vector<bezierCoeff *> gatherSubs;
      for(std::size_t i = 0; i < allControlPoints.size(); ++i) {
        std::vector<bezierCoeff *> tmp;
        allControlPoints[i]->subdivide(tmp);
        gatherSubs.insert(allControlPoints.end(), tmp.begin(), tmp.end());
      }
      allControlPoints.swap(gatherSubs);
    }

    for(std::size_t i = 0; i < allControlPoints.size(); ++i) {
      drawBezierControlPolygon(*allControlPoints[i], gedge);
    }
  }

  void draw3DFrame(SPoint3 &p, SVector3 &t, SVector3 &n, SVector3 &w,
                   double unitDimension, GFace *gFace = nullptr)
  {
    return;
    if(!gFace) gFace = *GModel::current()->firstFace();

    MVertex *v = new MVertex(p.x(), p.y(), p.z(), gFace);

    SPoint3 pnt = p + n * unitDimension * .75;
    MVertex *vn = new MVertex(pnt.x(), pnt.y(), pnt.z(), gFace);

    pnt = p + w * unitDimension * 2;
    MVertex *vw = new MVertex(pnt.x(), pnt.y(), pnt.z(), gFace);

    pnt = p + t * unitDimension;
    MVertex *vt = new MVertex(pnt.x(), pnt.y(), pnt.z(), gFace);

    gFace->addMeshVertex(v);
    gFace->addMeshVertex(vn);
    gFace->addMeshVertex(vw);

    MLine *line = new MLine(v, vn);
    gFace->edges().front()->addLine(line);

    line = new MLine(v, vw);
    gFace->edges().front()->addLine(line);

    line = new MLine(v, vt);
    gFace->edges().front()->addLine(line);
  }
} // namespace

namespace BoundaryLayerCurver {
  void projectVertexIntoGFace(MVertex *v, const GFace *gface)
  {
    SPoint3 p = v->point();
    double initialGuess[2];

    // Check if we can obtain parametric coordinates:
    if(!v->getParameter(0, initialGuess[0])) {
      Msg::Error("DEBUG no parametric vertex");
      // FIXME This is really annoying, why v is not MFaceVertex? It should be
      SPoint2 param = gface->parFromPoint(p);
      GPoint projected = gface->point(param);
      v->x() = projected.x();
      v->y() = projected.y();
      v->z() = projected.z();
      return;
    }
    if(!v->getParameter(1, initialGuess[1])) {
      Msg::Error("This should not happen. We should project on the GEdge...");
      return;
    }

    // We do have parametric coordinates and we can use robust functions:
    GPoint projected = gface->closestPoint(p, initialGuess);
    v->x() = projected.x();
    v->y() = projected.y();
    v->z() = projected.z();
    v->setParameter(0, projected.u());
    v->setParameter(1, projected.v());
  }

  void projectVerticesIntoGFace(const MEdgeN &edge, const GFace *gface,
                                bool alsoExtremity = true)
  {
    int i = alsoExtremity ? 0 : 2;
    for(; i < edge.getNumVertices(); ++i)
      projectVertexIntoGFace(edge.getVertex(i), gface);
  }

  void projectVerticesIntoGFace(const MFaceN &face, const GFace *gface,
                                bool alsoBoundary = true)
  {
    std::size_t i = alsoBoundary ? 0 : face.getNumVerticesOnBoundary();
    for(; i < face.getNumVertices(); ++i)
      projectVertexIntoGFace(face.getVertex(i), gface);
  }

  namespace EdgeCurver2D {
    // TODO: smooth normals if CAD not available
    // TODO: check quality of elements

    _Frame::_Frame(const MEdgeN *edge, const GFace *gface, const GEdge *gedge,
                   const SVector3 &normal)
      : _normalToTheMesh(normal), _gface(gface), _gedge(gedge),
        _edgeOnBoundary(edge)
    {
      const int nVert = (int)edge->getNumVertices();

      if(gface) {
        for(int i = 0; i < nVert; ++i) {
          SPoint2 param;
          bool success =
            reparamMeshVertexOnFace(edge->getVertex(i), gface, param, true);
          _paramVerticesOnGFace[2 * i + 0] = param[0];
          _paramVerticesOnGFace[2 * i + 1] = param[1];
          if(!success) {
            Msg::Warning("Could not compute param of node %d on surface %d",
                         edge->getVertex(i)->getNum(), gface->tag());
          }
          // TODO: Check if periodic face
        }
      }

      if(gedge) {
        for(int i = nVert - 1; i >= 0; --i) {
          bool success = reparamMeshVertexOnEdge(edge->getVertex(i), gedge,
                                                 _paramVerticesOnGEdge[i]);
          if(!success) {
            Msg::Warning("Could not compute param of node %d on edge %d",
                         edge->getVertex(i)->getNum(), gedge->tag());
          }
          else if(gedge->periodic(0) && gedge->getBeginVertex() &&
                  edge->getVertex(i) ==
                    gedge->getBeginVertex()->mesh_vertices[0]) {
            double u0 = gedge->getLowerBound();
            double un = gedge->getUpperBound();
            int k = (nVert == 2 ? 1 - i : (i == 0 ? 2 : nVert - 1));
            double uk = _paramVerticesOnGEdge[k];
            _paramVerticesOnGEdge[i] = uk - u0 < un - uk ? u0 : un;
          }
        }
      }
    }

    void _Frame::computeFrame(double paramEdge, SVector3 &t, SVector3 &n,
                              SVector3 &w, bool atExtremity) const
    {
      if(_gedge) {
        double paramGeoEdge;
        if(atExtremity) {
          if(paramEdge < 0)
            paramGeoEdge = _paramVerticesOnGEdge[0];
          else
            paramGeoEdge = _paramVerticesOnGEdge[1];
        }
        else
          paramGeoEdge =
            _edgeOnBoundary->interpolate(_paramVerticesOnGEdge, paramEdge);

        t = _gedge->firstDer(paramGeoEdge);
        t.normalize();
        if(t.norm() == 0) // FIXMEDEBUG should not arrive
          std::cout << "aarg " << _gedge->tag() << " " << paramGeoEdge
                    << std::endl;
      }
      if(!_gedge || t.norm() == 0) { t = _edgeOnBoundary->tangent(paramEdge); }

      if(_gface) {
        SPoint2 paramGFace;
        if(atExtremity) {
          if(paramEdge < 0)
            paramGFace =
              SPoint2(_paramVerticesOnGFace[0], _paramVerticesOnGFace[1]);
          else
            paramGFace =
              SPoint2(_paramVerticesOnGFace[2], _paramVerticesOnGFace[3]);
        }
        else {
          paramGFace = SPoint2(
            _edgeOnBoundary->interpolate(_paramVerticesOnGFace, paramEdge, 2),
            _edgeOnBoundary->interpolate(_paramVerticesOnGFace + 1, paramEdge,
                                         2));
        }
        w = _gface->normal(paramGFace);
      }
      else {
        w = _normalToTheMesh;
      }
      if(w.norm() == 0) {
        Msg::Error("normal to the CAD or 2Dmesh is nul. BL curving will fail.");
      }
      n = crossprod(w, t);
    }

    SPoint3 _Frame::pnt(double u) const
    {
      if(!_gedge) return SPoint3();

      double paramGeoEdge =
        _edgeOnBoundary->interpolate(_paramVerticesOnGEdge, u);
      GPoint p = _gedge->point(paramGeoEdge);
      return SPoint3(p.x(), p.y(), p.z());
    }

    void _computeParameters(const MEdgeN &baseEdge, const MEdgeN &otherEdge,
                            const _Frame &frame, double coeffs[2][3])
    {
      SVector3 t, n, w, h;
      MVertex *vb, *vt;

      frame.computeFrame(-1, t, n, w, true);
      vb = baseEdge.getVertex(0);
      vt = otherEdge.getVertex(0);
      h = SVector3(vt->x() - vb->x(), vt->y() - vb->y(), vt->z() - vb->z());
      coeffs[0][0] = dot(h, n);
      coeffs[0][1] = dot(h, t);
      coeffs[0][2] = dot(h, w);

      SPoint3 p1 = frame.pnt(-1);
      //      SPoint3 p1(vb->x(), vb->y(), vb->z());
      draw3DFrame(p1, t, n, w, .0004);

      frame.computeFrame(1, t, n, w, true);
      vb = baseEdge.getVertex(1);
      vt = otherEdge.getVertex(1);
      h = SVector3(vt->x() - vb->x(), vt->y() - vb->y(), vt->z() - vb->z());
      coeffs[1][0] = dot(h, n);
      coeffs[1][1] = dot(h, t);
      coeffs[1][2] = dot(h, w);
      //
      SPoint3 p2 = frame.pnt(1);
      //      SPoint3 p2(vb->x(), vb->y(), vb->z());
      draw3DFrame(p2, t, n, w, .0004);
    }

    void _idealPositionEdge(const MEdgeN &baseEdge, const _Frame &frame,
                            double coeffs[2][3], int nbPoints,
                            const IntPt *points, fullMatrix<double> &xyz)
    {
      for(int i = 0; i < nbPoints; ++i) {
        double u = points[i].pt[0];
        SPoint3 p = baseEdge.pnt(u);
        SVector3 t, n, w;
        frame.computeFrame(u, t, n, w);

        //        draw3DFrame(p, t, n, w, .0002);
        SPoint3 pp = frame.pnt(u);
        draw3DFrame(pp, t, n, w, .0002);

        double interpolatedCoeffs[3];
        for(int j = 0; j < 3; ++j) {
          interpolatedCoeffs[j] =
            coeffs[0][j] * (1 - u) / 2 + coeffs[1][j] * (1 + u) / 2;
        }
        SVector3 h;
        h = interpolatedCoeffs[0] * n + interpolatedCoeffs[1] * t +
            interpolatedCoeffs[2] * w;
        xyz(i, 0) = p.x() + h.x();
        xyz(i, 1) = p.y() + h.y();
        xyz(i, 2) = p.z() + h.z();
      }
    }

    void _drawIdealPositionEdge(const MEdgeN &baseEdge, const _Frame &frame,
                                double coeffs[2][3], GEdge *gedge)
    {
      int N = 100;
      MVertex *previous = nullptr;

      for(int i = 0; i < N + 1; ++i) {
        const double u = (double)i / N * 2 - 1;
        SPoint3 p = baseEdge.pnt(u);
        SVector3 t, n, w;
        frame.computeFrame(u, t, n, w);

        double interpolatedCoeffs[3];
        for(int j = 0; j < 3; ++j) {
          interpolatedCoeffs[j] =
            coeffs[0][j] * (1 - u) / 2 + coeffs[1][j] * (1 + u) / 2;
        }
        SVector3 h;
        h = interpolatedCoeffs[0] * n + interpolatedCoeffs[1] * t +
            interpolatedCoeffs[2] * w;
        double x = p.x() + h.x();
        double y = p.y() + h.y();
        double z = p.z() + h.z();

        MVertex *current = new MVertex(x, y, z, gedge);
        gedge->addMeshVertex(current);
        if(previous) {
          MLine *line = new MLine(previous, current);
          gedge->addLine(line);
        }
        //        MVertex *base = new MVertex(p.x(), p.y(), p.z(), gedge);
        //        MLine *line = new MLine(base, current);
        //        gedge->addLine(line);
        previous = current;
      }
    }

    void curveEdge(const MEdgeN &baseEdge, MEdgeN &edge, const GFace *gface,
                   const GEdge *gedge, const SVector3 &normal)
    {
      _Frame frame(&baseEdge, gface, gedge, normal);

      double coeffs[2][3];
      _computeParameters(baseEdge, edge, frame, coeffs);

      const int orderCurve = baseEdge.getPolynomialOrder();
      const int orderGauss = orderCurve * 2;
      const int sizeSystem = getNGQLPts(orderGauss);
      const IntPt *gaussPnts = getGQLPts(orderGauss);

      // Least square projection
      fullMatrix<double> xyz(sizeSystem + 2, 3);
      _idealPositionEdge(baseEdge, frame, coeffs, sizeSystem, gaussPnts, xyz);
      //      _drawIdealPositionEdge(baseEdge, frame, coeffs, (GEdge*)gedge);
      for(int i = 0; i < 2; ++i) {
        xyz(sizeSystem + i, 0) = edge.getVertex(i)->x();
        xyz(sizeSystem + i, 1) = edge.getVertex(i)->y();
        xyz(sizeSystem + i, 2) = edge.getVertex(i)->z();
      }

      LeastSquareData *data =
        getLeastSquareData(TYPE_LIN, orderCurve, orderGauss);
      fullMatrix<double> newxyz(orderCurve + 1, 3);
      data->invA.mult(xyz, newxyz);

      for(int i = 2; i < edge.getNumVertices(); ++i) {
        edge.getVertex(i)->x() = newxyz(i, 0);
        edge.getVertex(i)->y() = newxyz(i, 1);
        edge.getVertex(i)->z() = newxyz(i, 2);
      }

      if(gface) projectVerticesIntoGFace(edge, gface, false);
    }

    void recoverQualityElements(std::vector<MEdgeN> &stackEdges,
                                std::vector<MFaceN> &stackFaces,
                                std::vector<MElement *> &stackElements,
                                int iFirst, int iLast, const GFace *gface)
    {
      std::vector<MEdgeN> subsetEdges(4);
      subsetEdges[0] = stackEdges[0];
      subsetEdges[1] = stackEdges[iFirst];
      subsetEdges[2] = stackEdges[iLast - 1];
      subsetEdges[3] = stackEdges[iLast];
      MEdgeN &lastEdge = stackEdges[iLast];

      // 1. Get sure that last element of the BL is of good quality
      MFaceN &lastFaceBL = stackFaces[iLast - 1];
      MElement *lastElementBL = stackElements[iLast - 1];
      MElement *linear = createPrimaryElement(lastElementBL);
      double qualLinear = jacobianBasedQuality::minIGEMeasure(linear);
      delete linear;

      // Compute curving and quality of last element of the BL
      InteriorEdgeCurver::curveEdges(subsetEdges, 1, 3, gface);
      repositionInnerVertices(lastFaceBL, gface, true);
      double qual = jacobianBasedQuality::minIGEMeasure(lastElementBL);

      // Reduce order until good quality or order 2
      int currentOrder = lastEdge.getPolynomialOrder();
      while(qual < .75 && qual < .8 * qualLinear && currentOrder > 2) {
        reduceOrderCurve(lastEdge, --currentOrder, gface);
        InteriorEdgeCurver::curveEdges(subsetEdges, 1, 3, gface);
        repositionInnerVertices(lastFaceBL, gface, true);
        qual = jacobianBasedQuality::minIGEMeasure(lastElementBL);
      }

      // If still not good quality, reduce curving
      int iter = 0;
      const int maxIter = 15;
      while(qual < .75 && qual < .8 * qualLinear && ++iter < maxIter) {
        reduceCurving(lastEdge, .25, gface);
        InteriorEdgeCurver::curveEdges(subsetEdges, 1, 3, gface);
        repositionInnerVertices(lastFaceBL, gface, true);
        qual = jacobianBasedQuality::minIGEMeasure(lastElementBL);
      }

      // 2. Get sure the external element is of good quality
      MFaceN &lastFace = stackFaces[iLast];
      MElement *lastElement = stackElements[iLast];
      linear = createPrimaryElement(lastElement);
      qualLinear = jacobianBasedQuality::minIGEMeasure(linear);
      delete linear;

      // Compute curving and quality of external element
      repositionInnerVertices(lastFace, gface, false);
      qual = jacobianBasedQuality::minIGEMeasure(lastElement);

      // Reduce curving
      while(qual < .75 && qual < .8 * qualLinear && ++iter < maxIter) {
        reduceCurving(lastEdge, .25, gface);
        repositionInnerVertices(lastFace, gface, false);
        qual = jacobianBasedQuality::minIGEMeasure(lastElement);
      }
      if(iter == maxIter) reduceCurving(lastEdge, 1, gface);
    }
  } // namespace EdgeCurver2D

  void reduceCurving(MEdgeN &edge, double factor, const GFace *gface)
  {
    int order = edge.getPolynomialOrder();

    MVertex *v0 = edge.getVertex(0);
    MVertex *v1 = edge.getVertex(1);

    for(int i = 2; i < order + 1; ++i) {
      double f = (double)(i - 1) / order;
      MVertex *v = edge.getVertex(i);
      v->x() =
        (1 - factor) * v->x() + factor * ((1 - f) * v0->x() + f * v1->x());
      v->y() =
        (1 - factor) * v->y() + factor * ((1 - f) * v0->y() + f * v1->y());
      v->z() =
        (1 - factor) * v->z() + factor * ((1 - f) * v0->z() + f * v1->z());
    }
    if(gface) projectVerticesIntoGFace(edge, gface, false);
  }

  void reduceOrderCurve(MEdgeN &edge, int order, const GFace *gface)
  {
    const int orderCurve = edge.getPolynomialOrder();
    const int orderGauss = order * 2;
    const int sizeSystem = getNGQLPts(orderGauss);
    const IntPt *gaussPnts = getGQLPts(orderGauss);

    // Least square projection
    fullMatrix<double> xyz(sizeSystem + 2, 3);
    for(int i = 0; i < sizeSystem; ++i) {
      SPoint3 p = edge.pnt(gaussPnts[i].pt[0]);
      xyz(i, 0) = p.x();
      xyz(i, 1) = p.y();
      xyz(i, 2) = p.z();
    }
    for(int i = 0; i < 2; ++i) {
      xyz(sizeSystem + i, 0) = edge.getVertex(i)->x();
      xyz(sizeSystem + i, 1) = edge.getVertex(i)->y();
      xyz(sizeSystem + i, 2) = edge.getVertex(i)->z();
    }

    LeastSquareData *data = getLeastSquareData(TYPE_LIN, order, orderGauss);
    fullMatrix<double> newxyzLow(order + 1, 3);
    data->invA.mult(xyz, newxyzLow);

    std::vector<MVertex *> vertices = edge.getVertices();
    vertices.resize(static_cast<std::size_t>(order) + 1);
    MEdgeN lowOrderEdge(vertices);

    for(std::size_t i = 2; i < vertices.size(); ++i) {
      vertices[i]->x() = newxyzLow(i, 0);
      vertices[i]->y() = newxyzLow(i, 1);
      vertices[i]->z() = newxyzLow(i, 2);
    }

    const int tagLine = ElementType::getType(TYPE_LIN, orderCurve);
    const nodalBasis *nb = BasisFactory::getNodalBasis(tagLine);
    const fullMatrix<double> &refpnts = nb->getReferenceNodes();

    fullMatrix<double> newxyz(edge.getNumVertices(), 3);
    for(std::size_t i = 2; i < edge.getNumVertices(); ++i) {
      SPoint3 p = lowOrderEdge.pnt(refpnts(i, 0));
      newxyz(i, 0) = p.x();
      newxyz(i, 1) = p.y();
      newxyz(i, 2) = p.z();
    }

    for(int i = 2; i < edge.getNumVertices(); ++i) {
      edge.getVertex(i)->x() = newxyz(i, 0);
      edge.getVertex(i)->y() = newxyz(i, 1);
      edge.getVertex(i)->z() = newxyz(i, 2);
    }

    if(gface) projectVerticesIntoGFace(edge, gface, false);
  }

  template<class T>
  PositionerInternal<T>::PositionerInternal(std::vector<T> &v, GFace *gf)
  : _stack(v), _gface(gf)
  {
    if(v.size() < 3) return;
    _type = v[0].getType();
    _polynomialOrder = v[0].getPolynomialOrder();
    _numBoundaryVert = v[0].getNumVerticesOnBoundary();
    _numCornerVert = v[0].getNumCorners();
    _computeEtas();
    _computeTerms();

    bool touches = true;
    std::size_t i = 0;
    while(touches && ++i < v.size()) {
      touches = false;
      for(std::size_t j = 0; j < _numCornerVert; ++j) {
        if(v[i].getVertex(j) == v[0].getVertex(j)) touches = true;
      }
    }
    _iFirst = i;
  }

  template<class T>
  void PositionerInternal<T>::_computeEtas()
  {
    const std::size_t N = _stack.size();
    _etas.resize(_numCornerVert * N);
    std::vector<MVertex *> baseNodes(_numCornerVert);
    for(std::size_t i = 0; i < _numCornerVert; ++i) {
      _etas[i] = 0;
      baseNodes[i] = _stack[0].getVertex(i);
    }

    for(std::size_t i = 1; i < N; ++i) {
      for(std::size_t j = 0; j < _numCornerVert; ++j) {
        MVertex *v = _stack[i].getVertex(j);
        _etas[_numCornerVert * i + j] = baseNodes[j]->distance(v);
      }
    }
    for(std::size_t i = 1; i < N; ++i) {
      for(std::size_t j = 0; j < _numCornerVert; ++j) {
        _etas[_numCornerVert * i + j] /= _etas[_numCornerVert * (N - 1) + j];
      }
    }

    _avgEta1 = 0;
    for(std::size_t j = 0; j < _numCornerVert; ++j) {
      _avgEta1 += _etas[_numCornerVert * _iFirst + j];
    }
    _avgEta1 /= _numCornerVert;
  }

  template<class T>
  void PositionerInternal<T>::_computeTerms()
  {
    fullMatrix<double> delta0, delta1, deltaN;
    _computeDeltas(delta0, delta1, deltaN);

    fullMatrix<double> &term0 = _terms[0];
    fullMatrix<double> &term1d10 = _terms[1];
    fullMatrix<double> &term1d11 = _terms[2];
    fullMatrix<double> &term1dN0 = _terms[3];
    fullMatrix<double> &term1dN1 = _terms[4];
    fullMatrix<double> &term20 = _terms[5];
    fullMatrix<double> &term21 = _terms[6];
    fullMatrix<double> &term22 = _terms[7];

    const int numVertices = delta0.size1();

    fullMatrix<double> delta10 = delta1;
    delta10.add(delta0, -1);
    delta10.scale(1 / _avgEta1);
    fullMatrix<double> deltaN0 = deltaN;
    deltaN0.add(delta0, -1);

    term0.resize(numVertices, 3);
    term1d10.resize(numVertices, 3);
    term1d11.resize(numVertices, 3);
    term1dN0.resize(numVertices, 3);
    term1dN1.resize(numVertices, 3);
    term20.resize(numVertices, 3);
    term21.resize(numVertices, 3);
    term22.resize(numVertices, 3);

    // FIXME:NOW

    // TFIData *tfiData = _getTFIData(TYPE_LIN, numVertices - 1);
    //
    // term0.copy(delta0);
    // tfiData->T0.mult(delta10, term1d10);
    // tfiData->T1.mult(delta10, term1d11);
    // tfiData->T0.mult(deltaN0, term1dN0);
    // tfiData->T1.mult(deltaN0, term1dN1);
    // fullMatrix<double> diff(numVertices, 3);
    // fullMatrix<double> dum0(numVertices, 3);
    // fullMatrix<double> dum1(numVertices, 3);
    // diff.copy(deltaN0);
    // diff.add(delta10, -1);
    // tfiData->T0.mult(diff, dum0);
    // tfiData->T1.mult(diff, dum1);
    // tfiData->T0.mult(dum0, term20);
    // tfiData->T1.mult(dum0, term21);
    // tfiData->T1.mult(dum1, term22);
  }

  template<class T>
  void PositionerInternal<T>::_computeDeltas(fullMatrix<double> &delta0,
                                             fullMatrix<double> &delta1,
                                             fullMatrix<double> &deltaN) const
  {
    const std::size_t numVert = _stack[0].getNumVertices();

    fullMatrix<double> x0(numVert, 3);
    fullMatrix<double> x1(numVert, 3);
    fullMatrix<double> xN(numVert, 3);
    for(int k = 0; k < numVert; ++k) {
      x0(k, 0) = _stack[0].getVertex(k)->x();
      x0(k, 1) = _stack[0].getVertex(k)->y();
      x0(k, 2) = _stack[0].getVertex(k)->z();
      x1(k, 0) = _stack[_iFirst].getVertex(k)->x();
      x1(k, 1) = _stack[_iFirst].getVertex(k)->y();
      x1(k, 2) = _stack[_iFirst].getVertex(k)->z();
      xN(k, 0) = _stack.back().getVertex(k)->x();
      xN(k, 1) = _stack.back().getVertex(k)->y();
      xN(k, 2) = _stack.back().getVertex(k)->z();
    }

    fullMatrix<double> x0linear(numVert, 3);
    fullMatrix<double> x1linear(numVert, 3);
    fullMatrix<double> xNlinear(numVert, 3);
    _linearize(x0, x0linear);
    _linearize(x1, x1linear);
    _linearize(xN, xNlinear);
    delta0 = x0;
    delta0.add(x0linear, -1);
    delta1 = x1;
    delta1.add(x1linear, -1);
    deltaN = xN;
    deltaN.add(xNlinear, -1);
  }

  template<class T>
  void PositionerInternal<T>::_linearize(const fullMatrix<double> &curv,
                                         fullMatrix<double> &lin)
  {
    int N = curv.size1();
    lin.resize(N, curv.size2(), true);
    lin.copy(curv, 0, _numBoundaryVert, 0, 3, 0, 0);
    if(_type == TYPE_LIN) {
      lin.copy(curv, 0, 2, 0, 3, 0, 0);
      for(int i = 2; i < N; ++i) {
        double fact = ((double)i - 1) / (N - 1);
        for(int j = 0; j < 3; ++j)
          lin(i, j) = (1 - fact) * curv(0, j) + fact * curv(1, j);
      }
      return;
    }
    const fullMatrix<double> *placement;
    if(_type == TYPE_TRI)
      placement = InnerVertPlacementMatrices::triangle(_polynomialOrder, false);
    else
      placement = InnerVertPlacementMatrices::quadrangle(_polynomialOrder, false);

    for(std::size_t i = _numBoundaryVert; i < N; ++i) {
      for(int j = 0; j < placement->size2(); ++j) {
        const double &coeff = (*placement)(i - _numBoundaryVert, j);
        lin(i, 0) += coeff * lin(j, 0);
        lin(i, 1) += coeff * lin(j, 1);
        lin(i, 2) += coeff * lin(j, 2);
      }
    }
  }

  namespace InteriorEdgeCurver {
    static std::map<std::pair<int, int>, TFIData *> tfiData;

    TFIData *_constructTFIData(int typeElement, int order)
    {
      // This method constructs some transformation matrices for the problem
      // of curving boundary layers (BL). What the are is explained in the
      // following.
      // Let XI be the coordinates: (xi) for a 1D element and (xi, eta) for a
      // 2D element.
      // Suppose a function f(XI) and the 'n' order-1 Lagrange functions
      // N_i(XI), i=0,...,n-1 where n is the number of corner (2 for a line,
      // 3 for a triangle and 4 for a quadrangle).
      // Let F_i be the functions equal to f * N_i.
      // If 'f' is of order p, then the F_i are of order p+1.
      // Now, consider the functions f_i that are the projections of F_i into the
      // space of order-p functions.
      // The matrices computed here convert the Lagrange coefficients
      // of f into the Lagrange coefficients of f_i.
      // We choose the projection such that it minimizes the error between
      // F_i and f_i in the least square sense. But with some constraints that
      // are explained now.
      // For the problem of curving BL, 'f' is not a general function. It has
      // the property to be equal to zero on the boundary (the 2 extremity
      // points for a line and the 3 or 4 border edges for a 2D element).
      // The difficulty is to reduce the order of the F_i while keeping the
      // property. To handle this, we transform the Lagrange coefficients
      // into Legendre coefficients. In this basis, the property is fullfilled
      // if some easy constraints on the coefficients are fulfilled and
      // reducing the order con sists in setting the higher coefficients to zero.
      // For a line, the constraints are:
      //   c_0 + c_2 + c_4 + ... = 0
      //   c_1 + c_3 + ... = 0
      TFIData *data = new TFIData;
      int nbDof = order + 1;

      fullMatrix<double> Mh; // lagCoeff p_n -> lagCoeff p_(n+1)
      fullMatrix<double> M0; // c (= lagCoeff p_(n+1)) -> (1-xi) * c
      fullMatrix<double> M1; // c (= lagCoeff p_(n+1)) ->    xi  * c
      fullMatrix<double> Ml; // lagCoeff p_(n+1) -> lEgCoeff p_((n+1)-1)
      fullMatrix<double> Me; // lEgCoeff p_n -> lagCoeff p_n

      if(typeElement == TYPE_LIN) {
        int tagLine = ElementType::getType(TYPE_LIN, order);
        const nodalBasis *fs = BasisFactory::getNodalBasis(tagLine);
        const fullMatrix<double> &refNodes = fs->getReferenceNodes();
        const fullMatrix<double> refNodesh = gmshGeneratePointsLine(order + 1);

        int nbDofh = refNodesh.size1();

        //      refNodesh.print("refNodesh");

        Mh.resize(nbDofh, nbDof);
        for(int i = 0; i < nbDofh; ++i) {
          double sf[100];
          fs->f(refNodesh(i, 0), refNodesh(i, 1), refNodesh(i, 2), sf);
          for(int j = 0; j < nbDof; ++j) { Mh(i, j) = sf[j]; }
        }
        //      Mh.print("Mh");

        M0.resize(nbDofh, nbDofh, true);
        M1.resize(nbDofh, nbDofh, true);
        for(int i = 0; i < nbDofh; ++i) {
          M0(i, i) = .5 - refNodesh(i, 0) / 2;
          M1(i, i) = .5 + refNodesh(i, 0) / 2;
        }
        //      M0.print("M0");
        //      M1.print("M1");

        Ml.resize(nbDof, nbDofh);
        {
          fullMatrix<double> vandermonde(nbDofh, nbDofh);

          double *val = new double[nbDofh];
          for(int i = 0; i < nbDofh; ++i) {
            LegendrePolynomials::fc(order + 1, refNodesh(i, 0), val);
            for(int j = 0; j < nbDofh; ++j) { vandermonde(i, j) = val[j]; }
          }
          delete[] val;

          fullMatrix<double> tmp;
          vandermonde.invert(tmp);
          //        vandermonde.print("vandermonde");
          //        tmp.print("tmp");
          Ml.copy(tmp, 0, nbDof, 0, nbDofh, 0, 0);
        }
        //      Ml.print("Ml");

        Me.resize(nbDof, nbDof);
        {
          double *val = new double[nbDof];
          for(int i = 0; i < nbDof; ++i) {
            LegendrePolynomials::fc(order, refNodes(i, 0), val);
            for(int j = 0; j < nbDof; ++j) { Me(i, j) = val[j]; }
          }
          delete val;
        }
        //      Me.print("Me");

        fullMatrix<double> tmp0(nbDofh, nbDof);
        fullMatrix<double> tmp1(nbDofh, nbDof);
        M0.mult(Mh, tmp0);
        M1.mult(Mh, tmp1);
        fullMatrix<double> tmp(nbDof, nbDofh);
        Me.mult(Ml, tmp);
        //      tmp.print("tmp");
        data->T0.resize(nbDof, nbDof);
        data->T1.resize(nbDof, nbDof);
        tmp.mult(tmp0, data->T0);
        tmp.mult(tmp1, data->T1);

        //      data->T0.print("data->T0");
        //      data->T1.print("data->T1");
      }

      //    fullVector<double> x(nbDof);
      //    x.setAll(1);
      //    fullVector<double> b1(nbDof);
      //    fullVector<double> b2(nbDof);
      //    data->T0.mult(x, b1);
      //    b1.print("b");
      //    data->T1.mult(x, b1);
      //    b1.print("b");
      //
      //    x(0) = 0;
      //    x(2) = 1/6.;
      //    x(3) = 2/6.;
      //    x(4) = 3/6.;
      //    x(5) = 4/6.;
      //    x(6) = 5/6.;
      //    x(1) = 1;
      //    data->T0.mult(x, b1);
      //    b1.print("b1");
      //    data->T1.mult(x, b2);
      //    b2.print("b2");
      //    b1.axpy(b2);
      //    b1.print("b");
      //
      //    x(0) = 0;
      //    x(2) = 0.000021433470508;
      //    x(3) = 0.001371742112483;
      //    x(4) = 0.015625000000000;
      //    x(5) = 0.087791495198903;
      //    x(6) = 0.334897976680384;
      //    x(1) = 1.000000000000000;
      //    data->T0.mult(x, b1);
      //    b1.print("b1");
      //    data->T1.mult(x, b2);
      //    b2.print("b2");
      //    b1.axpy(b2);
      //    b1.print("b");

      return data;
    }

    TFIData *_getTFIData(int typeElement, int order)
    {
      std::pair<int, int> typeOrder(typeElement, order);
      std::map<std::pair<int, int>, TFIData *>::iterator it;
      it = tfiData.find(typeOrder);

      if(it != tfiData.end()) return it->second;

      TFIData *data = _constructTFIData(typeElement, order);

      tfiData[typeOrder] = data;
      return data;
    }

    void _linearize(const fullMatrix<double> &x, fullMatrix<double> &lin)
    {
      int n = x.size1();
      lin.copy(x, 0, 2, 0, 3, 0, 0);
      for(int i = 2; i < n; ++i) {
        double fact = ((double)i - 1) / (n - 1);
        for(int j = 0; j < 3; ++j)
          lin(i, j) = (1 - fact) * x(0, j) + fact * x(1, j);
      }
    }

    void _computeEtas(const std::vector<MEdgeN> &stack,
                      std::vector<double> &eta)
    {
      const std::size_t N = stack.size();
      eta.resize(2 * N);
      eta[0] = eta[1] = 0;

      MVertex *vb0 = stack[0].getVertex(0);
      MVertex *vb1 = stack[0].getVertex(1);

      for(std::size_t i = 1; i < N; ++i) {
        MVertex *v0 = stack[i].getVertex(0);
        MVertex *v1 = stack[i].getVertex(1);
        eta[2 * i] = vb0->distance(v0);
        eta[2 * i + 1] = vb1->distance(v1);
      }

      for(std::size_t i = 1; i < N; ++i) {
        eta[2 * i] /= eta[2 * N - 2];
        eta[2 * i + 1] /= eta[2 * N - 1];
      }
    }

    void _computeDeltaForTFI(const std::vector<MEdgeN> &stack, int iFirst,
                             int iLast, fullMatrix<double> &delta0,
                             fullMatrix<double> &delta1,
                             fullMatrix<double> &deltaN)
    {
      const int numVertices = stack[0].getNumVertices();

      fullMatrix<double> x0(numVertices, 3);
      fullMatrix<double> x1(numVertices, 3);
      fullMatrix<double> xN(numVertices, 3);
      for(int k = 0; k < numVertices; ++k) {
        x0(k, 0) = stack[0].getVertex(k)->x();
        x0(k, 1) = stack[0].getVertex(k)->y();
        x0(k, 2) = stack[0].getVertex(k)->z();
        x1(k, 0) = stack[iFirst].getVertex(k)->x();
        x1(k, 1) = stack[iFirst].getVertex(k)->y();
        x1(k, 2) = stack[iFirst].getVertex(k)->z();
        xN(k, 0) = stack[iLast].getVertex(k)->x();
        xN(k, 1) = stack[iLast].getVertex(k)->y();
        xN(k, 2) = stack[iLast].getVertex(k)->z();
      }

      fullMatrix<double> x0linear(numVertices, 3);
      fullMatrix<double> x1linear(numVertices, 3);
      fullMatrix<double> xNlinear(numVertices, 3);
      _linearize(x0, x0linear);
      _linearize(x1, x1linear);
      _linearize(xN, xNlinear);
      delta0 = x0;
      delta0.add(x0linear, -1);
      delta1 = x1;
      delta1.add(x1linear, -1);
      deltaN = xN;
      deltaN.add(xNlinear, -1);
    }

    void _computeTerms(const fullMatrix<double> &delta0,
                       const fullMatrix<double> &delta1,
                       const fullMatrix<double> &deltaN, double eta1,
                       fullMatrix<double> terms[8])
    {
      fullMatrix<double> &term0 = terms[0];
      fullMatrix<double> &term1d10 = terms[1];
      fullMatrix<double> &term1d11 = terms[2];
      fullMatrix<double> &term1dN0 = terms[3];
      fullMatrix<double> &term1dN1 = terms[4];
      fullMatrix<double> &term20 = terms[5];
      fullMatrix<double> &term21 = terms[6];
      fullMatrix<double> &term22 = terms[7];

      const int numVertices = delta0.size1();

      fullMatrix<double> delta10 = delta1;
      delta10.add(delta0, -1);
      delta10.scale(1 / eta1);
      fullMatrix<double> deltaN0 = deltaN;
      deltaN0.add(delta0, -1);

      term0.resize(numVertices, 3);
      term1d10.resize(numVertices, 3);
      term1d11.resize(numVertices, 3);
      term1dN0.resize(numVertices, 3);
      term1dN1.resize(numVertices, 3);
      term20.resize(numVertices, 3);
      term21.resize(numVertices, 3);
      term22.resize(numVertices, 3);

      TFIData *tfiData = _getTFIData(TYPE_LIN, numVertices - 1);

      term0.copy(delta0);
      tfiData->T0.mult(delta10, term1d10);
      tfiData->T1.mult(delta10, term1d11);
      tfiData->T0.mult(deltaN0, term1dN0);
      tfiData->T1.mult(deltaN0, term1dN1);
      fullMatrix<double> diff(numVertices, 3);
      fullMatrix<double> dum0(numVertices, 3);
      fullMatrix<double> dum1(numVertices, 3);
      diff.copy(deltaN0);
      diff.add(delta10, -1);
      tfiData->T0.mult(diff, dum0);
      tfiData->T1.mult(diff, dum1);
      tfiData->T0.mult(dum0, term20);
      tfiData->T1.mult(dum0, term21);
      tfiData->T1.mult(dum1, term22);
    }

    void _generalTFI(std::vector<MEdgeN> &stack, int iLast,
                     const std::vector<double> &eta,
                     const fullMatrix<double> terms[8], double coeffHermite,
                     const GFace *gface)
    {
      // Let L() be the linear TFI transformation
      // Let H() be the semi-Hermite TFI transformation
      // This function return (1-coeffHermite) * L() + coeffHermite * H()

      const fullMatrix<double> &term0 = terms[0];
      const fullMatrix<double> &term1d10 = terms[1];
      const fullMatrix<double> &term1d11 = terms[2];
      const fullMatrix<double> &term1dN0 = terms[3];
      const fullMatrix<double> &term1dN1 = terms[4];
      const fullMatrix<double> &term20 = terms[5];
      const fullMatrix<double> &term21 = terms[6];
      const fullMatrix<double> &term22 = terms[7];

      int numVertices = stack[0].getNumVertices();

      for(std::size_t i = 1; i < stack.size(); ++i) {
        if(i == iLast) continue;
        // we want to change stack[iFirst] but not stack[iLast]

        fullMatrix<double> x(numVertices, 3);
        for(int j = 0; j < numVertices; ++j) {
          MVertex *v = stack[i].getVertex(j);
          x(j, 0) = v->x();
          x(j, 1) = v->y();
          x(j, 2) = v->z();
        }
        _linearize(x, x);

        const double &c = coeffHermite;
        const double &eta1 = eta[2 * i];
        const double &eta2 = eta[2 * i + 1];
        x.axpy(term0);
        x.axpy(term1d10, c * eta1);
        x.axpy(term1d11, c * eta2);
        x.axpy(term1dN0, (1 - c) * eta1);
        x.axpy(term1dN1, (1 - c) * eta2);
        x.axpy(term20, c * eta1 * eta1);
        x.axpy(term21, c * 2 * eta1 * eta2);
        x.axpy(term22, c * eta2 * eta2);

        for(int j = 2; j < numVertices; ++j) {
          MVertex *v = stack[i].getVertex(j);
          v->x() = x(j, 0);
          v->y() = x(j, 1);
          v->z() = x(j, 2);
        }
        if(gface) projectVerticesIntoGFace(stack[i], gface, false);
      }
    }

    void _computeEtaAndTerms(std::vector<MEdgeN> &stack, int iFirst, int iLast,
                             std::vector<double> &eta,
                             fullMatrix<double> terms[8])
    {
      // Compute eta_i^k, k=0,1
      _computeEtas(stack, eta);

      // Precompute Delta(x_i), i=0,1,n
      fullMatrix<double> delta0, delta1, deltaN;
      _computeDeltaForTFI(stack, iFirst, iLast, delta0, delta1, deltaN);

      // Compute terms
      double eta1 = .5 * (eta[2 * iFirst] + eta[2 * iFirst] + 1);
      _computeTerms(delta0, delta1, deltaN, eta1, terms);
    }

    void curveEdges(std::vector<MEdgeN> &stack, int iFirst, int iLast,
                    const GFace *gface)
    {
      std::vector<double> eta;
      fullMatrix<double> terms[8];
      _computeEtaAndTerms(stack, iFirst, iLast, eta, terms);

      _generalTFI(stack, iLast, eta, terms, 1, gface);
    }

    void curveEdgesAndPreserveQuality(std::vector<MEdgeN> &stackEdges,
                                      std::vector<MFaceN> &stackFacesBL,
                                      std::vector<MElement *> &stackElements,
                                      int iFirst, int iLast, const GFace *gface)
    {
      std::vector<double> eta;
      fullMatrix<double> terms[8];
      _computeEtaAndTerms(stackEdges, iFirst, iLast, eta, terms);

      // Compute quality of primary elements
      unsigned long numElements = stackElements.size() - 1;
      std::vector<double> qualitiesLinear(numElements);
      for(std::size_t i = 0; i < numElements; ++i) {
        MElement *linear = createPrimaryElement(stackElements[i]);
        qualitiesLinear[i] = jacobianBasedQuality::minIGEMeasure(linear);
        delete linear;
      }

      static double coeffHermite[11] = {1,  .9, .8, .7, .6, .5,
                                        .4, .3, .2, .1, 0};
      for(int i = 0; i < 11; ++i) {
        _generalTFI(stackEdges, iLast, eta, terms, coeffHermite[i], gface);
        repositionInnerVertices(stackFacesBL, gface, true);

        bool allOk = true;
        if(coeffHermite[i]) {
          for(std::size_t i = 0; i < numElements; ++i) {
            double qual = jacobianBasedQuality::minIGEMeasure(stackElements[i]);
            if(qual < .5 && qual < .8 * qualitiesLinear[i]) {
              allOk = false;
              break;
            }
          }
        }

        if(allOk) return;
      }
    }

    void curveEdgesAndPreserveQualityTri(std::vector<MEdgeN> &stackEdges,
                                         std::vector<MFaceN> &stackFacesBL,
                                         std::vector<MElement *> &stackElements,
                                         int iFirst, int iLast,
                                         const GFace *gface, const GEdge *gedge,
                                         SVector3 normal)
    {
      std::vector<double> eta;
      fullMatrix<double> terms[8];
      _computeEtaAndTerms(stackEdges, iFirst, iLast, eta, terms);

      // Compute quality of primary elements
      unsigned long numElements = stackElements.size() - 1;
      std::vector<double> qualitiesLinear(numElements);
      for(unsigned int i = 0; i < numElements; ++i) {
        MElement *linear = createPrimaryElement(stackElements[i]);
        qualitiesLinear[i] = jacobianBasedQuality::minIGEMeasure(linear);
        delete linear;
      }

      static double coeffHermite[11] = {1,  .9, .8, .7, .6, .5,
                                        .4, .3, .2, .1, 0};
      for(int i = 0; i < 11; ++i) {
        _generalTFI(stackEdges, iLast, eta, terms, coeffHermite[i], gface);
        for(unsigned int j = 0; j < stackEdges.size() - 2; j += 2) {
          EdgeCurver2D::curveEdge(stackEdges[j], stackEdges[j + 1], gface,
                                  nullptr, normal);
        }
        repositionInnerVertices(stackFacesBL, gface, true);

        bool allOk = true;
        if(coeffHermite[i]) {
          for(unsigned int i = 0; i < numElements; ++i) {
            double qual = jacobianBasedQuality::minIGEMeasure(stackElements[i]);
            if(qual < .5 && qual < .8 * qualitiesLinear[i]) {
              allOk = false;
              break;
            }
          }
        }

        if(allOk) return;
      }
    }
  } // namespace InteriorEdgeCurver

  MElement *createPrimaryElement(MElement *el)
  {
    int tagLinear = ElementType::getType(el->getType(), 1);
    std::vector<MVertex *> vertices;
    el->getVertices(vertices);
    MElementFactory f;
    return f.create(tagLinear, vertices, -1);
  }

  LeastSquareData *constructLeastSquareData(int typeElement, int order,
                                            int orderGauss)
  {
    // invM1 gives
    //     value of coefficients in Legendre basis
    //   + value of Lagrange multipliers
    // from
    //     Ij + value of function f at extremities
    // M2 gives
    //     Ij = integral of product function f with Legendre polynomial j
    //   + value of function f at extremities
    // from
    //     the values of function f at Gauss points
    //   + value of function f at extremities

    orthogonalBasis basis(typeElement, order);
    LeastSquareData *data = new LeastSquareData;

    if(typeElement == TYPE_LIN) {
      data->nbPoints = getNGQLPts(orderGauss);
      data->intPoints = getGQLPts(orderGauss);

      const int szSpace = order + 1;
      const int nGP = data->nbPoints;

      double *val = new double[szSpace];

      fullMatrix<double> M2(szSpace + 2, nGP + 2, true);
      {
        for(int j = 0; j < nGP; ++j) {
          basis.f(data->intPoints[j].pt[0], 0, 0, val);
          for(int i = 0; i < szSpace; ++i) {
            M2(i, j) = val[i] * data->intPoints[j].weight;
          }
        }
        M2(szSpace, nGP) = M2(szSpace + 1, nGP + 1) = 1;
      }

      fullMatrix<double> M1(szSpace + 2, szSpace + 2, true);
      {
        basis.L2Norms(val);
        for(int k = 0; k < szSpace; ++k) M1(k, k) = val[k];

        basis.f(-1, 0, 0, val);
        for(int k = 0; k < szSpace; ++k)
          M1(szSpace, k) = M1(k, szSpace) = val[k];

        basis.f(1, 0, 0, val);
        for(int k = 0; k < szSpace; ++k)
          M1(szSpace + 1, k) = M1(k, szSpace + 1) = val[k];
      }
      fullMatrix<double> invM1;
      M1.invert(invM1);

      fullMatrix<double> Leg2Lag(szSpace, szSpace, true);
      {
        int tagLine = ElementType::getType(TYPE_LIN, order);
        const nodalBasis *fs = BasisFactory::getNodalBasis(tagLine);
        const fullMatrix<double> &refNodes = fs->getReferenceNodes();
        for(int i = 0; i < szSpace; ++i) {
          basis.f(refNodes(i, 0), 0, 0, val);
          for(int j = 0; j < szSpace; ++j) { Leg2Lag(i, j) = val[j]; }
        }
      }

      delete val;

      fullMatrix<double> tmp(szSpace + 2, nGP + 2, false);
      invM1.mult(M2, tmp);
      fullMatrix<double> tmp2(szSpace, nGP + 2, false);
      tmp2.copy(tmp, 0, szSpace, 0, nGP + 2, 0, 0);

      data->invA.resize(szSpace, nGP + 2, false);
      Leg2Lag.mult(tmp2, data->invA);
      return data;
    }

    else if(typeElement == TYPE_QUA) {
      data->nbPoints = getNGQQPts(orderGauss);
      data->intPoints = getGQQPts(orderGauss);

      fullMatrix<double> refNodes = gmshGeneratePointsQuadrangle(order);

      const int szSpace = (order + 1) * (order + 1);
      const int nGP = data->nbPoints;
      const int nConstraint = 4 * order;

      double *val = new double[szSpace];

      fullMatrix<double> M2(szSpace + nConstraint, nGP + nConstraint, true);
      {
        for(int j = 0; j < nGP; ++j) {
          basis.f(data->intPoints[j].pt[0], data->intPoints[j].pt[1], 0, val);
          for(int i = 0; i < szSpace; ++i) {
            M2(i, j) = val[i] * data->intPoints[j].weight;
          }
        }
        for(int i = 0; i < nConstraint; ++i) { M2(szSpace + i, nGP + i) = 1; }
      }

      fullMatrix<double> M1(szSpace + nConstraint, szSpace + nConstraint, true);
      {
        basis.L2Norms(val);
        for(int k = 0; k < szSpace; ++k) M1(k, k) = val[k];

        for(int i = 0; i < nConstraint; ++i) {
          basis.f(refNodes(i, 0), refNodes(i, 1), 0, val);
          for(int k = 0; k < szSpace; ++k) {
            M1(szSpace + i, k) = M1(k, szSpace + i) = val[k];
          }
        }
      }
      fullMatrix<double> invM1;
      M1.invert(invM1);

      fullMatrix<double> Leg2Lag(szSpace, szSpace, true);
      {
        for(int i = 0; i < szSpace; ++i) {
          basis.f(refNodes(i, 0), refNodes(i, 1), 0, val);
          for(int j = 0; j < szSpace; ++j) { Leg2Lag(i, j) = val[j]; }
        }
      }

      delete val;

      fullMatrix<double> tmp(szSpace + nConstraint, nGP + nConstraint, false);
      invM1.mult(M2, tmp);
      fullMatrix<double> tmp2(szSpace, nGP + nConstraint, false);
      tmp2.copy(tmp, 0, szSpace, 0, nGP + nConstraint, 0, 0);

      data->invA.resize(szSpace, nGP + nConstraint, false);
      Leg2Lag.mult(tmp2, data->invA);
      return data;
    }

    else if(typeElement == TYPE_TRI) {
      data->nbPoints = getNGQTPts(orderGauss);
      data->intPoints = getGQTPts(orderGauss);

      fullMatrix<double> refNodes = gmshGeneratePointsTriangle(order);

      const int szSpace = (order + 1) * (order + 2) / 2;
      const int nGP = data->nbPoints;
      const int nConstraint = 3 * order;

      double *val = new double[szSpace];

      fullMatrix<double> M2(szSpace + nConstraint, nGP + nConstraint, true);
      {
        for(int j = 0; j < nGP; ++j) {
          basis.f(data->intPoints[j].pt[0], data->intPoints[j].pt[1], 0, val);
          for(int i = 0; i < szSpace; ++i) {
            M2(i, j) = val[i] * data->intPoints[j].weight;
          }
        }
        for(int i = 0; i < nConstraint; ++i) { M2(szSpace + i, nGP + i) = 1; }
      }

      fullMatrix<double> M1(szSpace + nConstraint, szSpace + nConstraint, true);
      {
        basis.L2Norms(val);
        for(int k = 0; k < szSpace; ++k) M1(k, k) = val[k];

        for(int i = 0; i < nConstraint; ++i) {
          basis.f(refNodes(i, 0), refNodes(i, 1), 0, val);
          for(int k = 0; k < szSpace; ++k) {
            M1(szSpace + i, k) = M1(k, szSpace + i) = val[k];
          }
        }
      }
      fullMatrix<double> invM1;
      M1.invert(invM1);

      fullMatrix<double> Leg2Lag(szSpace, szSpace, true);
      {
        for(int i = 0; i < szSpace; ++i) {
          basis.f(refNodes(i, 0), refNodes(i, 1), 0, val);
          for(int j = 0; j < szSpace; ++j) { Leg2Lag(i, j) = val[j]; }
        }
      }

      delete val;

      fullMatrix<double> tmp(szSpace + nConstraint, nGP + nConstraint, false);
      invM1.mult(M2, tmp);
      fullMatrix<double> tmp2(szSpace, nGP + nConstraint, false);
      tmp2.copy(tmp, 0, szSpace, 0, nGP + nConstraint, 0, 0);

      data->invA.resize(szSpace, nGP + nConstraint, false);
      Leg2Lag.mult(tmp2, data->invA);
      return data;
    }
  }

  LeastSquareData *getLeastSquareData(int typeElement, int order,
                                      int orderGauss)
  {
    TupleLeastSquareData typeOrder(typeElement,
                                   std::make_pair(order, orderGauss));
    std::map<TupleLeastSquareData, LeastSquareData *>::iterator it;
    it = leastSquareData.find(typeOrder);

    if(it != leastSquareData.end()) return it->second;

    LeastSquareData *data =
      constructLeastSquareData(typeElement, order, orderGauss);
    leastSquareData[typeOrder] = data;
    return data;
  }

  bool computeCommonEdge(MElement *el1, MElement *el2, MEdge &e)
  {
    for(int i = 0; i < el1->getNumEdges(); ++i) {
      e = el1->getEdge(i);
      for(int j = 0; j < el2->getNumEdges(); ++j) {
        MEdge e2 = el2->getEdge(j);
        if(e == e2) return true;
      }
    }
    e = MEdge();
    return false;
  }

  // Warning: returns the opposite, possibly degenerate edge, i.e. returns
  // a MEdge whose vertices are the unique opposite vertex for triangles
  bool computeOppositeEdge(MElement *el, MEdge &edge, MEdge &other)
  {
    if(el->getNumEdges() == 3) {
      for(int i = 0; i < 3; ++i) {
        MVertex *v = el->getVertex(i);
        if(edge.getVertex(0) != v && edge.getVertex(1) != v) {
          other = MEdge(v, v);
          return true;
        }
      }
    }
    else {
      MVertex *vertices[4];
      int k = 0;
      for(int i = 0; i < 4; ++i) {
        MVertex *v = el->getVertex(i);
        if(edge.getVertex(0) != v && edge.getVertex(1) != v) {
          vertices[k++] = v;
        }
      }
      if(k != 2) {
        other = MEdge();
        return false;
      }
      other = MEdge(vertices[0], vertices[1]);
      return true;
    }
  }

  bool isPrimaryVertex(MElement *el, MVertex *v)
  {
    for(int i = 0; i < el->getNumPrimaryVertices(); ++i) {
      if(v == el->getVertex(i)) return true;
    }
    return false;
  }

  // Compute a stack of primary vertices corresponding to each layer of the
  // column. The last layer can be degenerate (e.g. when the external element
  // is a triangle or a tetrahedron).
  void computeStackPrimaryVertices(const PairMElemVecMElem &column,
                                   std::vector<MVertex *> &stack)
  {
    MElement *bottomElement = column.first;
    const std::vector<MElement *> &stackElements = column.second;
    const int numVertexPerLayer = bottomElement->getNumPrimaryVertices();
    std::size_t numLayers = stackElements.size();
    stack.assign(numVertexPerLayer * (numLayers + 1), nullptr);

    std::size_t k = 0;
    for(int i = 0; i < numVertexPerLayer; ++i) {
      stack[k++] = bottomElement->getVertex(i);
    }
    for(std::size_t i = 0; i < numLayers; ++i) {
      MElement *currentElement = stackElements[i];
      // Find all the edges that touch one node of current layer but is not
      // an edge of the current layer nor of the next one.
      for(int j = 0; j < currentElement->getNumEdges(); ++j) {
        MEdge edge = currentElement->getEdge(j);
        MVertex *v0 = edge.getVertex(0);
        MVertex *v1 = edge.getVertex(1);

        // Check that the edge is not part of the next element
        if(i < numLayers - 1) {
          MElement *nextElement = stackElements[i + 1];
          if(isPrimaryVertex(nextElement, v0) &&
             isPrimaryVertex(nextElement, v1))
            continue;
        }

        // Check if the edge links the two layers and update 'stack'
        int idxv0AtBottom = -1;
        for(int m = 0; m < numVertexPerLayer; ++m) {
          if(v0 == stack[k - 1 - m]) {
            idxv0AtBottom = k - 1 - m;
            break;
          }
        }
        int idxv1AtBottom = -1;
        for(std::size_t m = 0; m < (std::size_t)numVertexPerLayer; ++m) {
          if(v1 == stack[k - 1 - m]) {
            idxv1AtBottom = k - 1 - m;
            break;
          }
        }
        if(idxv0AtBottom == -1 && idxv1AtBottom != -1) {
          stack[idxv1AtBottom + numVertexPerLayer] = v0;
        }
        else if(idxv0AtBottom != -1 && idxv1AtBottom == -1) {
          stack[idxv0AtBottom + numVertexPerLayer] = v1;
        }
      }

      // If there remains nullptr values, it is because the vertex is the same
      // on both layers (because of a non-tensorial element).
      for(int l = k; l < k + numVertexPerLayer; ++l) {
        if(stack[l] == nullptr) {
          stack[l] = stack[l - numVertexPerLayer];
        }
      }

      k += numVertexPerLayer;
    }
  }
  void computeStackPrimaryVerticesNew(const Column3DBL &column,
                                   std::vector<MVertex *> &stack)
  {
    MElement *bottomElement = column.getBoundaryElement();
    const int numVertexPerLayer = bottomElement->getNumPrimaryVertices();
    std::size_t numLayers = column.getNumBLElements();
    stack.assign(numVertexPerLayer * (numLayers + 1), nullptr);

    std::size_t k = 0;
    for(int i = 0; i < numVertexPerLayer; ++i) {
      stack[k++] = bottomElement->getVertex(i);
    }
    for(std::size_t i = 0; i < numLayers; ++i) {
      MElement *currentElement = column.getBLElement(i);
      // Find all the edges that touch one node of current layer but is not
      // an edge of the current layer nor of the next one.
      for(int j = 0; j < currentElement->getNumEdges(); ++j) {
        MEdge edge = currentElement->getEdge(j);
        MVertex *v0 = edge.getVertex(0);
        MVertex *v1 = edge.getVertex(1);

        // Check that the edge is not part of the next element
        if(i < numLayers - 1) {
          MElement *nextElement = column.getBLElement(i + 1);
          if(isPrimaryVertex(nextElement, v0) &&
             isPrimaryVertex(nextElement, v1))
            continue;
        }

        // Check if the edge links the two layers and update 'stack'
        int idxv0AtBottom = -1;
        for(int m = 0; m < numVertexPerLayer; ++m) {
          if(v0 == stack[k - 1 - m]) {
            idxv0AtBottom = k - 1 - m;
            break;
          }
        }
        int idxv1AtBottom = -1;
        for(int m = 0; m < numVertexPerLayer; ++m) {
          if(v1 == stack[k - 1 - m]) {
            idxv1AtBottom = k - 1 - m;
            break;
          }
        }
        if(idxv0AtBottom == -1 && idxv1AtBottom != -1) {
          stack[idxv1AtBottom + numVertexPerLayer] = v0;
        }
        else if(idxv0AtBottom != -1 && idxv1AtBottom == -1) {
          stack[idxv0AtBottom + numVertexPerLayer] = v1;
        }
      }

      // If there remains nullptr values, it is because the vertex is the same
      // on both layers (because of a non-tensorial element).
      for(int l = k; l < k + numVertexPerLayer; ++l) {
        if(stack[l] == nullptr) { stack[l] = stack[l - numVertexPerLayer]; }
      }

      k += numVertexPerLayer;
    }
  }

  void  computeStackHOEdgesFaces(const PairMElemVecMElem &column,
                                 std::vector<MEdgeN> &stackEdges,
                                 std::vector<MFaceN> &stackFaces)
  {
    // stackEdges is the stack of bottom edge of each face in stackFaces. Each
    // edge in stackEdges have the same orientation
    const std::vector<MElement *> &stackElements = column.second;
    const int numElements = (int)stackElements.size();
    stackEdges.resize(numElements);
    stackFaces.resize(numElements);

    std::vector<MVertex *> primVert;
    computeStackPrimaryVertices(column, primVert);

    for(std::size_t i = 0; i < numElements; ++i) {
      MEdge e(primVert[2 * i + 0], primVert[2 * i + 1]);
      stackEdges[i] = stackElements[i]->getHighOrderEdge(e);
    }
    for(std::size_t i = 0; i < numElements; ++i) {
      MFace face;
      MVertex *&v0 = primVert[2 * i + 0];
      MVertex *&v1 = primVert[2 * i + 1];
      MVertex *&v2 = primVert[2 * i + 2];
      MVertex *&v3 = primVert[2 * i + 3];
      // Note: we don't care about the orientation since we do not compute the
      // quality from the MFaceN but from the MElement. However, the acute
      // angle of the triangles should be every time at the same node (the 2nd).
      if(v2 == v3 || v1 == v3)
        face = MFace(v0, v1, v2);
      else if(v0 == v2)
        face = MFace(v1, v0, v3);
      else
        face = MFace(v0, v1, v3, v2);
      stackFaces[i] = stackElements[i]->getHighOrderFace(face);
    }
  }

  bool edgesShareVertex(MEdgeN &e0, MEdgeN &e1)
  {
    MVertex *v = e0.getVertex(0);
    MVertex *v0 = e1.getVertex(0);
    if(v == v0) return true;
    MVertex *v1 = e1.getVertex(1);
    if(v == v1) return true;
    v = e0.getVertex(1);
    if(v == v0 || v == v1) return true;
    return false;
  }

  void repositionInnerVertices(const MFaceN &face, const GFace *gface,
                               bool linearInYDir)
  {
    int order = face.getPolynomialOrder();
    const fullMatrix<double> *placement;
    if(face.getType() == TYPE_QUA) {
      placement = InnerVertPlacementMatrices::quadrangle(order, linearInYDir);
    }
    else {
      placement = InnerVertPlacementMatrices::triangle(order, linearInYDir);
    }
    face.repositionInnerVertices(placement);
    if(gface) projectVerticesIntoGFace(face, gface, false);
  }

  void repositionInnerVertices(const std::vector<MFaceN> &stackFaces,
                               const GFace *gface, bool linearInYDir)
  {
    if(stackFaces.empty()) return;

    int order = stackFaces[0].getPolynomialOrder();
    const fullMatrix<double> *placementTri, *placementQua;

    placementTri = InnerVertPlacementMatrices::triangle(order, linearInYDir);
    placementQua = InnerVertPlacementMatrices::quadrangle(order, linearInYDir);

    for(std::size_t i = 0; i < stackFaces.size() - 1; ++i) {
      const MFaceN &face = stackFaces[i];
      if(face.getType() == TYPE_QUA)
        face.repositionInnerVertices(placementQua);
      else
        face.repositionInnerVertices(placementTri);
      if(gface) projectVerticesIntoGFace(face, gface, false);
    }
  }

  bool curve2Dcolumn(PairMElemVecMElem &column, const GFace *gface,
                     const GEdge *gedge, const SVector3 &normal)
  {
    // Here, either gface is defined and not normal, or the normal
    // is defined and not gface!

    if(column.second.size() < 2) return true;

    // Compute stack high order edges and faces
    std::vector<MEdgeN> stackEdges;
    std::vector<MFaceN> stackFaces;
    computeStackHOEdgesFaces(column, stackEdges, stackFaces);

    // Curve topEdge of first element and last edge
    std::size_t iFirst = 1, iLast = stackEdges.size() - 1;
    MEdgeN &baseEdge = stackEdges[0];
    MEdgeN &firstEdge = stackEdges[iFirst];
    if(edgesShareVertex(baseEdge, firstEdge)) {
      iFirst = 2;
      firstEdge = stackEdges[iFirst];
    }
    MEdgeN &topEdge = stackEdges[iLast];

    EdgeCurver2D::curveEdge(baseEdge, firstEdge, gface, gedge, normal);
    EdgeCurver2D::curveEdge(baseEdge, topEdge, gface, gedge, normal);
    EdgeCurver2D::recoverQualityElements(stackEdges, stackFaces, column.second,
                                         iFirst, iLast, gface);

    // Curve interior edges and inner vertices
    stackFaces.pop_back();
    InteriorEdgeCurver::curveEdgesAndPreserveQuality(
      stackEdges, stackFaces, column.second, iFirst, iLast, gface);
//    InteriorEdgeCurver::curveEdgesAndPreserveQualityTri(
//      stackEdges, stackFaces, column.second, iFirst, iLast, gface, gedge,
//      normal);
    return true;
  }

  void computeThicknessQuality(std::vector<MVertex *> &bottomVertices,
                               std::vector<MVertex *> &topVertices,
                               std::vector<double> &thickness, SVector3 &w)
  {
    int nVertices = (int)bottomVertices.size();
    int tagLine = ElementType::getType(TYPE_LIN, nVertices - 1);
    const nodalBasis *fs = BasisFactory::getNodalBasis(tagLine);

    for(int i = 0; i < nVertices; ++i) {
      const MVertex *v0 = bottomVertices[i];
      const MVertex *v1 = topVertices[i];
      SVector3 t, n, h;
      h = SVector3(v1->x() - v0->x(), v1->y() - v0->y(), v1->z() - v0->z());

      double xi = fs->points(i, 0);
      double xc = 0, yc = 0, zc = 0;
      double dx = 0, dy = 0, dz = 0;
      {
        double f[100];
        double sf[100][3];
        fs->f(xi, 0, 0, f);
        fs->df(xi, 0, 0, sf);
        for(int j = 0; j < fs->getNumShapeFunctions(); j++) {
          const MVertex *v = bottomVertices[j];
          xc += f[j] * v->x();
          yc += f[j] * v->y();
          zc += f[j] * v->z();
          dx += sf[j][0] * v->x();
          dy += sf[j][0] * v->y();
          dz += sf[j][0] * v->z();
        }
      }
      t = SVector3(dx, dy, dz).unit();
      n = crossprod(w, t);
      thickness.push_back(dot(h, n));
    }

    double t0 = thickness[0];
    double t1 = thickness[1];
    thickness[0] = 1;
    thickness[1] = 1;
    for(int j = 2; j < nVertices; ++j) {
      double fact = ((double)j - 1) / (nVertices - 1);
      double idealThickness = (1 - fact) * t0 + fact * t1;
      int sign = gmsh_sign(idealThickness);
      if(sign * thickness[j] < 0)
        thickness[j] = 0;
      else if(sign * thickness[j] < sign * idealThickness)
        thickness[j] = thickness[j] / idealThickness;
      else
        thickness[j] = idealThickness / thickness[j];
    }
  }

  void computeThicknessPView(MElement *el, SVector3 &w,
                             std::map<int, std::vector<double> > &data)
  {
    //    if (el->getType() == TYPE_QUA) {
    //      std::vector<MVertex *> bottomVertices, topVertices;
    //
    //      el->getEdgeVertices(0, bottomVertices);
    //      el->getEdgeVertices(2, topVertices);
    //      std::reverse(topVertices.begin(), topVertices.begin() + 2);
    //      std::reverse(topVertices.begin() + 2, topVertices.end());
    //
    //      std::vector<double> thickness[2];
    //      computeThicknessQuality(bottomVertices, topVertices, thickness[0],
    //      w); computeThicknessQuality(topVertices, bottomVertices,
    //      thickness[1], w);
    //
    //      std::map<int, double> v2q;
    //      v2q[0] = thickness[0][0];
    //      v2q[1] = thickness[0][1];
    //      v2q[2] = thickness[1][1];
    //      v2q[3] = thickness[1][0];
    //      int nEdgeVertex = (int)topVertices.size()-2;
    //      for (int i = 2; i < (int)thickness[0].size(); ++i) {
    //        v2q[4+i-2] = thickness[0][i];
    //        v2q[3+3*nEdgeVertex-i+2] = thickness[1][i];
    //      }
    //
    //      double q00 = v2q[0];
    //      double q10 = v2q[1];
    //      double q11 = v2q[2];
    //      double q01 = v2q[3];
    //      for (int i = 0; i < nEdgeVertex; ++i) {
    //        double fact = ((double)i+1)/(nEdgeVertex+1);
    //        v2q[4+nEdgeVertex+i]   = (1-fact) * q10 + fact * q11;
    //        v2q[4+3*nEdgeVertex+i] = (1-fact) * q01 + fact * q00;
    //      }
    //
    //      int tag = el->getTypeForMSH();
    //      InteriorPlacementData *pData;
    //      std::map<int, InteriorPlacementData*>::iterator it;
    //      it = interiorPlacementData.find(tag);
    //      if (it != interiorPlacementData.end()) pData = it->second;
    //      else {
    //        pData = constructInteriorPlacementData(tag);
    //        interiorPlacementData[tag] = pData;
    //      }
    //      for (int i = 0; i < pData->iToMove.size(); ++i) {
    //        int idx = pData->iToMove[i];
    //        double q0 = v2q[pData->i0[i]];
    //        double q1 = v2q[pData->i1[i]];
    //        v2q[idx] = pData->factor[i] * q0 + (1-pData->factor[i]) * q1;
    //      }
    //
    //      std::vector<double> &vData = data[el->getNum()];
    //      std::map<int, double>::iterator it2;
    //      for (it2 = v2q.begin(); it2 != v2q.end(); ++it2) {
    //        vData.push_back(it2->second);
    //      }
    //    }
  }
} // namespace BoundaryLayerCurver

void curve2DBoundaryLayer(VecPairMElemVecMElem &columns, SVector3 normal,
                          const GEdge *gedge)
{
  double length = normal.normalize();
  if(length == 0) {
    Msg::Error("normal must be non-zero for boundary layer curving");
    return;
  }

  // // FIXMEDEBUG for visualisation
  // for(int i = 0; i < columns.size(); ++i) {
  //   columns[i].first->setVisibility(1);
  //   for(std::size_t j = 0; j < columns[i].second.size(); ++j) {
  //     columns[i].second[j]->setVisibility(1);
  //   }
  // }

  for(int i = 0; i < columns.size(); ++i) {
//    if(columns[i].first->getNum() != 205) continue; // t161
//    if(columns[i].first->getNum() != 316) continue; // t161
//    if(columns[i].first->getNum() != 1156) continue; // trimesh
//    if(   columns[i].first->getNum() != 1156
//       && columns[i].first->getNum() != 1079
//       && columns[i].first->getNum() != 1102
//       && columns[i].first->getNum() != 1119) continue;
//    std::cout << std::endl;
//    std::cout << "column " << columns[i].first->getNum() << std::endl;
//    if(columns[i].first->getNum() != 1079) continue; // Good
//    if(columns[i].first->getNum() != 1078) continue; // Next to good
//    if(columns[i].first->getNum() != 1099) continue; // Long on corner
//    if(columns[i].first->getNum() != 1102) continue; // Bad HO
//    if(columns[i].first->getNum() != 1136) continue; // Bad linear
//    if(columns[i].first->getNum() != 1149) continue; // shorter
//    if(columns[i].first->getNum() != 1150) continue; // concave
//    if(columns[i].first->getNum() != 1151) continue; // symetric of concave
//    if(columns[i].first->getNum() != 1156) continue; // Strange
//    if(columns[i].first->getNum() != 1157) continue; // next to Strange

    // FIXMEDEBUG for visualisation
    columns[i].first->setVisibility(1);
    for(std::size_t j = 0; j < columns[i].second.size(); ++j) {
      columns[i].second[j]->setVisibility(1);
    }
    BoundaryLayerCurver::curve2Dcolumn(columns[i], nullptr, gedge, normal);
  }
}

void curve2DBoundaryLayer(VecPairMElemVecMElem &columns,
                          const GFace *gface, const GEdge *gedge)
{
  if(!gface) {
    Msg::Error("gface is needed for boundary layer curving");
    return;
  }

  //  for (int i = 0; i < columns.size(); ++i) {
  //    columns[i].first->setVisibility(1);
  //    for (std::size_t j = 0; j < columns[i].second.size(); ++j) {
  //      columns[i].second[j]->setVisibility(1);
  //    }
  //  }

  for(int i = 0; i < columns.size(); ++i)
    BoundaryLayerCurver::curve2Dcolumn(columns[i], gface, gedge,
                                       SVector3());
}
