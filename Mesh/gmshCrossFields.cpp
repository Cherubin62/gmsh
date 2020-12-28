// Gmsh - Copyright (C) 1997-2019 C. Geuzaine, J.-F. Remacle
//
// See the LICENSE.txt file for license information. Please report all
// issues on https://gitlab.onelab.info/gmsh/gmsh/issues.

#include <vector>
#include <stack>
#include <queue>
#include "OS.h"
#include "GmshConfig.h"
#include "gmshCrossFields.h"
#include "GModel.h"
#include "GFace.h"
#include "MEdge.h"
#include "MLine.h"
#include "MTriangle.h"
#include "GmshMessage.h"
#include "Context.h"
#include "meshGFaceOptimize.h"
#include "discreteEdge.h"
#include "Numeric.h"
#include "GModelParametrize.h"

#ifdef HAVE_QUADMESHINGTOOLS
#include "quad_meshing_tools.h"
#include "qmt_cross_field.h"
#include "qmt_utils.hpp" /* for debug print macro */
#include "geolog.h" /* for debug print macro */
#include "gmsh.h"
#include "fastScaledCrossField.h"
#endif

#if defined(HAVE_HXT)
extern "C" {
// #include "hxt_api.h"
#include "hxt_quadMultiBlock.h"
}
#endif
#include "meshGRegionHxt.h"

#if defined(HAVE_POST)
#include "PView.h"
#include "PViewOptions.h"
#include "PViewDataGModel.h"
#include "PViewDataList.h"
#endif

#if defined(_OPENMP)
#include <omp.h>
#endif

#if defined(HAVE_SOLVER) && defined(HAVE_POST)

#include "dofManager.h"
#include "laplaceTerm.h"
#include "linearSystemGmm.h"
#include "linearSystemMUMPS.h"
#include "linearSystemCSR.h"
#include "linearSystemFull.h"
#include "linearSystemPETSc.h"

#include "conformalMapping.h"

// static inline double lifting(double a, double _a)
// {
//   double D = M_PI * .5;
//   if(fabs(_a - a) < fabs(_a - (a + D)) && fabs(_a - a) < fabs(_a - (a - D))) {
//     return a;
//   }
//   else if(fabs(_a - (a + D)) < fabs(_a - a) &&
//           fabs(_a - (a + D)) < fabs(_a - (a - D))) {
//     return a + D;
//   }
//   else {
//     return a - D;
//   }
// }

static void getFacesOfTheModel(GModel *gm, std::vector<GFace *> &f)
{
  for(GModel::fiter it = gm->firstFace(); it != gm->lastFace(); ++it) {
    GFace *gf = *it;
    f.push_back(gf);
  }
}


static inline double compat_orientation_extrinsic(const double *o0,
                                                  const double *n0,
                                                  const double *o1,
                                                  const double *n1, double *a1,
                                                  double *b1)
{
  double t0[3] = {n0[1] * o0[2] - n0[2] * o0[1], n0[2] * o0[0] - n0[0] * o0[2],
                  n0[0] * o0[1] - n0[1] * o0[0]};
  double t1[3] = {n1[1] * o1[2] - n1[2] * o1[1], n1[2] * o1[0] - n1[0] * o1[2],
                  n1[0] * o1[1] - n1[1] * o1[0]};

  const size_t permuts[8][2] = {{0, 0}, {1, 0}, {2, 0}, {3, 0},
                                {0, 1}, {1, 1}, {2, 1}, {3, 1}};
  const double A[4][3] = {{o0[0], o0[1], o0[2]},
                          {t0[0], t0[1], t0[2]},
                          {-o0[0], -o0[1], -o0[2]},
                          {-t0[0], -t0[1], -t0[2]}};
  const double B[2][3] = {{o1[0], o1[1], o1[2]}, {t1[0], t1[1], t1[2]}};

  double maxx = -1;
  int index = 0;
  for(size_t i = 0; i < 8; i++) {
    const size_t II = permuts[i][0];
    const size_t JJ = permuts[i][1];
    const double xx =
      A[II][0] * B[JJ][0] + A[II][1] * B[JJ][1] + A[II][2] * B[JJ][2];
    if(xx > maxx) {
      index = i;
      maxx = xx;
    }
  }
  a1[0] = A[permuts[index][0]][0];
  a1[1] = A[permuts[index][0]][1];
  a1[2] = A[permuts[index][0]][2];
  b1[0] = B[permuts[index][1]][0];
  b1[1] = B[permuts[index][1]][1];
  b1[2] = B[permuts[index][1]][2];
  //  b1 = B[permuts[index][1]];
  return maxx;
}

static inline double compat_orientation_extrinsic(const SVector3 &o0,
                                                  const SVector3 &n0,
                                                  const SVector3 &o1,
                                                  const SVector3 &n1,
                                                  SVector3 &a1, SVector3 &b1)
{
  SVector3 t0 = crossprod(n0, o0);
  SVector3 t1 = crossprod(n1, o1);

  const size_t permuts[8][2] = {{0, 0}, {1, 0}, {2, 0}, {3, 0},
                                {0, 1}, {1, 1}, {2, 1}, {3, 1}};
  SVector3 A[4]{o0, t0, -o0, -t0};
  SVector3 B[2]{o1, t1};

  double maxx = -1;
  int index = 0;
  for(size_t i = 0; i < 8; i++) {
    const double xx = dot(A[permuts[i][0]], B[permuts[i][1]]);
    if(xx > maxx) {
      index = i;
      maxx = xx;
    }
  }
  a1 = A[permuts[index][0]];
  b1 = B[permuts[index][1]];
  return maxx;
}

class cross2d {
public:
  MEdge _e;
  bool inCutGraph;
  bool inBoundary;
  bool inInternalBoundary;
  bool rotation;
  //  int cutGraphPart;
  size_t counter;
  SVector3 o_i; // orientation vector
  SVector3 _nrml, _tgt, _tgt2; // local system of coordinates
  std::vector<MEdge> _neighbors;
  std::vector<cross2d *> _cneighbors;
  double da[4];
  double _C[4], _S[4];
  // euler angles
  double _a, _b, _c;
  double _atemp, _btemp, _ctemp;
  std::vector<MTriangle *> _t;
  cross2d(MEdge &e, MTriangle *r, MEdge &e1, MEdge &e2)
    : _e(e), inCutGraph(false), inBoundary(false), inInternalBoundary(false),
      _a(0), _b(0), _c(0)
  {
    _t.push_back(r);
    _neighbors.push_back(e1);
    _neighbors.push_back(e2);
  }
  void normalize(double &a)
  {
    double D = M_PI * .5;
    if(a < 0)
      while(a < 0) a += D;
    if(a >= D)
      while(a >= D) a -= D;
  }
  void finish2()
  {
    if(_cneighbors.size() == 4) {
      SVector3 x(0, 1, 0);
      _a = _atemp = atan2(dot(_tgt2, x), dot(_tgt, x));
      if(!inBoundary && !inInternalBoundary) {
        _b = _btemp = sin(4 * _a);
        _c = _ctemp = cos(4 * _a);
      }
      else {
        _b = _btemp = 0;
        _c = _ctemp = 1;
      }
      for(size_t i = 0; i < 4; i++) {
        da[i] = atan2(dot(_tgt2, _cneighbors[i]->_tgt),
                      dot(_tgt, _cneighbors[i]->_tgt));
        _C[i] = cos(4 * da[i]);
        _S[i] = sin(4 * da[i]);
      }
    }
  }

  void finish(std::map<MEdge, cross2d, MEdgeLessThan> &C)
  {
    _tgt = SVector3(1, 0, 0);
    _tgt2 = SVector3(0, 1, 0);
    if(_t.size() <= 2) {
      SVector3 v10(_t[0]->getVertex(1)->x() - _t[0]->getVertex(0)->x(),
                   _t[0]->getVertex(1)->y() - _t[0]->getVertex(0)->y(),
                   _t[0]->getVertex(1)->z() - _t[0]->getVertex(0)->z());
      SVector3 v20(_t[0]->getVertex(2)->x() - _t[0]->getVertex(0)->x(),
                   _t[0]->getVertex(2)->y() - _t[0]->getVertex(0)->y(),
                   _t[0]->getVertex(2)->z() - _t[0]->getVertex(0)->z());
      SVector3 xx = crossprod(v20, v10);
      xx.normalize();
      SVector3 yy = xx;
      if(_t.size() == 2) {
        SVector3 v10b(_t[1]->getVertex(1)->x() - _t[1]->getVertex(0)->x(),
                      _t[1]->getVertex(1)->y() - _t[1]->getVertex(0)->y(),
                      _t[1]->getVertex(1)->z() - _t[1]->getVertex(0)->z());
        SVector3 v20b(_t[1]->getVertex(2)->x() - _t[1]->getVertex(0)->x(),
                      _t[1]->getVertex(2)->y() - _t[1]->getVertex(0)->y(),
                      _t[1]->getVertex(2)->z() - _t[1]->getVertex(0)->z());
        yy = crossprod(v20b, v10b);
        yy.normalize();
        //        if(dot(xx, yy) < .5) inInternalBoundary = 1;
      }
      _nrml = xx + yy;
      _nrml.normalize();
      SVector3 vt(_e.getVertex(1)->x() - _e.getVertex(0)->x(),
                  _e.getVertex(1)->y() - _e.getVertex(0)->y(),
                  _e.getVertex(1)->z() - _e.getVertex(0)->z());
      _tgt = vt;
      _tgt.normalize();
      _tgt2 = crossprod(_nrml, _tgt);
    }

    if(_t.size() == 1) { inBoundary = true; }
    else if(_t.size() >= 2) {
      if(inBoundary) {
        //        printf("Internal boundary\n");
        inBoundary = false;
        inInternalBoundary = true;
      }
    }

    for(size_t i = 0; i < _neighbors.size(); i++) {
      std::map<MEdge, cross2d, MEdgeLessThan>::iterator it =
        C.find(_neighbors[i]);
      if(it == C.end())
        Msg::Error("impossible situation");
      else
        _cneighbors.push_back(&(it->second));
    }
    if(_cneighbors.size() != 4) {
      _a = 0;
      _atemp = _a;
    }
    _neighbors.clear();
    _b = _btemp = sin(4 * _a);
    _c = _ctemp = cos(4 * _a);
  }
  double average_init()
  {
    if(!inBoundary && !inInternalBoundary) {
      _btemp = 0;
      _ctemp = 0;
      for(int i = 0; i < 4; i++) {
        _ctemp += (_cneighbors[i]->_c * _C[i] - _cneighbors[i]->_b * _S[i]);
        _btemp += (_cneighbors[i]->_c * _S[i] + _cneighbors[i]->_b * _C[i]);
      }
      _btemp *= 0.25;
      _ctemp *= 0.25;
      _b = _btemp;
      _c = _ctemp;
    }
    return 1;
  }

  double grad()
  {
    if(!inBoundary && !inInternalBoundary) {
      double D = M_PI * .5;
      double a[4] = {_cneighbors[0]->_a, _cneighbors[1]->_a, _cneighbors[2]->_a,
                     _cneighbors[3]->_a};
      for(int i = 0; i < 4; i++) {
        a[i] += da[i];
        normalize(a[i]);
      }

      double b[4];
      for(int i = 0; i < 4; i++) {
        if(fabs(_a - a[i]) < fabs(_a - (a[i] + D)) &&
           fabs(_a - a[i]) < fabs(_a - (a[i] - D))) {
          b[i] = a[i];
        }
        else if(fabs(_a - (a[i] + D)) < fabs(_a - (a[i])) &&
                fabs(_a - (a[i] + D)) < fabs(_a - (a[i] - D))) {
          b[i] = a[i] + D;
        }
        else {
          b[i] = a[i] - D;
        }
      }
      return fabs(_a - b[0]) + fabs(_a - b[1]) + fabs(_a - b[2]) +
             fabs(_a - b[3]);
    }
    return 0;
  }

  double lifting(double a)
  {
    double D = M_PI * .5;
    if(fabs(_a - a) < fabs(_a - (a + D)) && fabs(_a - a) < fabs(_a - (a - D))) {
      return a;
    }
    else if(fabs(_a - (a + D)) < fabs(_a - a) &&
            fabs(_a - (a + D)) < fabs(_a - (a - D))) {
      return a + D;
    }
    else {
      return a - D;
    }
  }

  void computeVector()
  {
    _a = _atemp = 0.25 * atan2(_btemp, _ctemp);
    normalize(_atemp);
    o_i = _tgt * cos(_atemp) + _tgt2 * sin(_atemp);
  }

  void computeAngle()
  {
    if(_cneighbors.size() != 4) return;
    double _anew = atan2(dot(_tgt2, o_i), dot(_tgt, o_i));
    normalize(_anew);
    _a = _atemp = _anew;
  }

  double average2()
  {
    // TEMPORARY VERSION, slow but correct
    // Instant field-aligned meshes
    if(_cneighbors.size() != 4) return 0;
    double weight = 0.0;
    SVector3 o_i_old = o_i;
    SVector3 n_i = _nrml;
    for(int i = 0; i < 4; i++) {
      SVector3 o_j = _cneighbors[i]->o_i;
      SVector3 n_j = _cneighbors[i]->_nrml;
      SVector3 x, y;
      compat_orientation_extrinsic(o_i, n_i, o_j, n_j, x, y);
      o_i = x * weight + y;
      o_i -= n_i * dot(o_i, n_i);
      o_i.normalize();
      weight += 1.0;
    }
    return std::min(1. - fabs(dot(o_i, o_i_old)), fabs(dot(o_i, o_i_old)));
  }

  double average()
  {
    if(_cneighbors.size() == 4) {
      double D = M_PI * .5;
      double a[4] = {_cneighbors[0]->_a, _cneighbors[1]->_a, _cneighbors[2]->_a,
                     _cneighbors[3]->_a};
      for(int i = 0; i < 4; i++) {
        a[i] += da[i];
        normalize(a[i]);
      }

      double b[4];
      double avg = 0.0;
      for(int i = 0; i < 4; i++) {
        if(fabs(_a - a[i]) < fabs(_a - (a[i] + D)) &&
           fabs(_a - a[i]) < fabs(_a - (a[i] - D))) {
          b[i] = a[i];
        }
        else if(fabs(_a - (a[i] + D)) < fabs(_a - (a[i])) &&
                fabs(_a - (a[i] + D)) < fabs(_a - (a[i] - D))) {
          b[i] = a[i] + D;
        }
        else {
          b[i] = a[i] - D;
        }
      }
      avg = 0.25 * (b[0] + b[1] + b[2] + b[3]);

      normalize(avg);

      double d = fabs(_a - avg);
      _a = _atemp = avg;

      return d;
    }
    return 0;
  }
};

struct cross2dPtrLessThan {
  MEdgeLessThan l;
  bool operator()(const cross2d *v1, const cross2d *v2) const
  {
    return l(v1->_e, v2->_e);
  }
};

// ---------------------------------------------
// TODO : MAKE IT PARALLEL AND SUPERFAST
//        DO IT ON SURFACES
// ---------------------------------------------


static double closest_diff(const SVector3 &n,
			   const cross2d &c1,
			   const cross2d &c2,
			   const SVector3 &nn)
{

  SVector3 o_1 = c1.o_i;
  o_1 -= n * dot(n, o_1);
  o_1.normalize();

  SVector3 o_2 = c2.o_i;
  o_2 -= n * dot(n, o_2);
  o_2.normalize();
  SVector3 x0, x1;  
  compat_orientation_extrinsic(o_1, n, o_2, n, x0, x1);
  double diff = acos(dot(x0,x1));
  SVector3 s_ = crossprod(x0,x1);
  double sign = dot(s_,nn) > 0 ? 1 : -1;
  return diff * sign;      
}



static void closest(const cross2d &c1, const cross2d &c2, double &a2)
{

  double P = M_PI / 2;

  SVector3 d = c1._tgt * cos(c1._atemp + 0*P) + c1._tgt2 * sin(c1._atemp + 0*P);

  SVector3 d1 = c2._tgt * cos(c2._atemp) + c2._tgt2 * sin(c2._atemp);
  SVector3 d2 = c2._tgt * cos(c2._atemp + P) + c2._tgt2 * sin(c2._atemp + P);
  SVector3 d3 =
    c2._tgt * cos(c2._atemp + 2 * P) + c2._tgt2 * sin(c2._atemp + 2 * P);
  SVector3 d4 =
    c2._tgt * cos(c2._atemp + 3 * P) + c2._tgt2 * sin(c2._atemp + 3 * P);

  double D1 = dot(d, d1);
  double D2 = dot(d, d2);
  double D3 = dot(d, d3);
  double D4 = dot(d, d4);
  if(D1 > D2 && D1 > D3 && D1 > D4) {
    a2 = c2._atemp;
  }
  else if(D2 > D1 && D2 > D3 && D2 > D4) {
    a2 = c2._atemp + P;
  }
  else if(D3 > D1 && D3 > D2 && D3 > D4) {
    a2 = c2._atemp + 2 * P;
  }
  else {
    a2 = c2._atemp + 3 * P;
  }  
}

static void
computeUniqueVectorPerTriangle(GModel *gm, std::vector<GFace *> &f,
                               std::map<MEdge, cross2d, MEdgeLessThan> &C,
                               std::map<MTriangle *, SVector3> &d0,
                               std::map<MTriangle *, SVector3> &d1)
{
  for(size_t i = 0; i < f.size(); i++) {
    for(size_t j = 0; j < f[i]->triangles.size(); j++) {
      MTriangle *t = f[i]->triangles[j];
      SVector3 a0(0, 0, 0);
      SVector3 b0(0, 0, 0);
      int n = 0;

      cross2d *FIRST = NULL;
      for(int k = 0; k < 3; k++) {
        MEdge e = t->getEdge(k);
        std::map<MEdge, cross2d, MEdgeLessThan>::iterator it = C.find(e);
        if(it != C.end()) {
          if(!it->second.inCutGraph) {
            double angle;
            if (FIRST == NULL){
              FIRST = &it->second;
              angle = it->second._atemp;
            }
            else {
              closest (*FIRST, it->second, angle);
            }
            n++;
            SVector3 aa = it->second._tgt * cos(angle) + it->second._tgt2 * sin(angle);
            SVector3 bb = it->second._tgt * sin(angle) - it->second._tgt2 * cos(angle);
            a0 += aa;
            b0 += bb;
          }
        }
        else {
          printf("ERROR\n");
        }
      }
      if(n) {
        a0.normalize();
        b0.normalize();
      }
      d0[t] = a0;
      d1[t] = b0;
    }
  }
}

static bool isSingular (MTriangle *t, std::set<MVertex *, MVertexPtrLessThan> &sing){
  int count =0;
  for (size_t i=0;i<3;i++){
    MVertex *v = t->getVertex(i);
    if (sing.find(v) != sing.end())count++;
  }
  return count != 0;
}

// fix all crosses

void computeLocalOrientationsOfABoundaryGroup (cross2d *reference,
                                               SVector3 &ref0, SVector3 &ref1,
                                               std::map<MTriangle *, SVector3> &d0,
                                               std::map<MTriangle *, SVector3> &d1,                           
                                               std::vector<cross2d *> &g,
                                               std::set<MTriangle*> &visited,
                                               std::stack<MTriangle*> &_stack){

  std::vector<std::vector<MVertex *> > vsorted;
  std::map<MEdge, cross2d*,MEdgeLessThan> inv;
  std::vector<MEdge> edges;
  for(size_t i = 0; i < g.size(); ++i) {
    cross2d *c = g[i];
    inv[c->_e] = c;
    edges.push_back(c->_e);
  }
  
  SortEdgeConsecutive(edges, vsorted);
  if (vsorted[0][0] == vsorted[0][vsorted[0].size()-1])
    vsorted[0].resize(vsorted[0].size()-1);

  int ori0=0, ori1=0;
  
  for(size_t i = 0; i < vsorted[0].size() - 1; ++i) {
    MEdge e ( vsorted[0][i] , vsorted[0][i+1] );
    cross2d *c = inv[e];
    if (c == reference){
      SVector3 tgt (e.getVertex(1)->x()-e.getVertex(0)->x(),
                    e.getVertex(1)->y()-e.getVertex(0)->y(),
                    e.getVertex(1)->z()-e.getVertex(0)->z());
      SVector3 tgt2 = crossprod (c->_nrml, tgt);
      double a = dot (tgt,ref0);
      double b = dot (tgt2,ref0);      
      double c = dot (tgt*(-1.0) ,ref0);
      double d = dot (tgt2*(-1.0),ref0);
      // ref0 is aligned with tgt
      if (a > b && a > c && a > d){
        ori0 = 0;
      }    
      // ref0 is aligned with tgt2
      else if (b > a && b > c && b > d){
        ori0 = 1;
      }    
      // ref0 is aligned with -tgt
      else if (c > a && c > b && c > d){
        ori0 = 2;
      }    
      // ref0 is aligned with -tgt2
      else {
        ori0 = 3;
      }
      a = dot (tgt,ref1);
      b = dot (tgt2,ref1);      
      c = dot (tgt*(-1.0) ,ref1);
      d = dot (tgt2*(-1.0),ref1);
      // ref1 is aligned with tgt
      if (a > b && a > c && a > d){
        ori1 = 0;
      }    
      // ref1 is aligned with tgt2
      else if (b > a && b > c && b > d){
        ori1 = 1;
      }    
      // ref1 is aligned with -tgt
      else if (c > a && c > b && c > d){
        ori1 = 2;
      }    
      // ref1 is aligned with -tgt2
      else {
        ori1 = 3;
      }
    }
  }
  
  for(size_t i = 0; i < vsorted[0].size() - 1; ++i) {
    MEdge e ( vsorted[0][i] , vsorted[0][i+1] );
    cross2d *c = inv[e];
    SVector3 tgt (e.getVertex(1)->x()-e.getVertex(0)->x(),
                  e.getVertex(1)->y()-e.getVertex(0)->y(),
                  e.getVertex(1)->z()-e.getVertex(0)->z());
    tgt.normalize();
    SVector3 tgt2 = crossprod (c->_nrml, tgt);
    tgt2.normalize();
    MTriangle *t = c->_t[0];

    if (visited.find(t) == visited.end()){
      visited.insert(t);
      _stack.push (t);
    }
    
    if      (ori0 == 0)d0[t] = tgt;
    else if (ori0 == 1)d0[t] = tgt2;
    else if (ori0 == 2)d0[t] = tgt*(-1.0);
    else               d0[t] = tgt2*(-1.0);
    if      (ori1 == 0)d1[t] = tgt;
    else if (ori1 == 1)d1[t] = tgt2;
    else if (ori1 == 2)d1[t] = tgt*(-1.0);
    else               d1[t] = tgt2*(-1.0);
  }
}

void treatmentOfBoundary (cross2d *reference,
                          SVector3 &ref0, SVector3 &ref1,
                          std::map<MTriangle *, SVector3> &d0,
                          std::map<MTriangle *, SVector3> &d1,
                          std::vector<std::vector<cross2d *> > &groups,
                          std::map<cross2d *, size_t> &_groupIds,
                          std::set<size_t> &_done,
                          std::set<MTriangle*> &visited,
                          std::stack<MTriangle*> &_stack){
  size_t id = _groupIds[reference];
  if (_done.find(id) == _done.end()){
    _done.insert(id);
    computeLocalOrientationsOfABoundaryGroup (reference,ref0,ref1,d0,d1,groups[id],visited,_stack);
  }
}
 
static cross2d* isBndry (MTriangle *t , std::map<MEdge, cross2d, MEdgeLessThan> &C){
  for (size_t i=0;i<3;i++){
    MEdge e = t->getEdge(i);
    std::map<MEdge, cross2d, MEdgeLessThan>::iterator it = C.find(e);
    if (it->second.inBoundary){
      return &it->second;
    }
  }
  return NULL;
}
 
static void computeLifting(GModel *gm, std::vector<GFace *> &f,
                           std::map<MEdge, cross2d, MEdgeLessThan> &C,
                           std::map<MTriangle *, SVector3> &d0,
                           std::map<MTriangle *, SVector3> &d1,
                           std::set<MEdge, MEdgeLessThan> &cutG,
                           std::set<MVertex *, MVertexPtrLessThan> &sing,
                           std::vector<std::vector<cross2d *> > &groups){

  std::map<cross2d *, size_t> _groupIds;
  std::set<size_t> _done;
  for (size_t i=0;i<groups.size();i++){
    for (size_t j=0;j<groups[i].size();j++){
      _groupIds[groups[i][j]] = i;
    }
  }
  
  computeUniqueVectorPerTriangle(gm, f,C,d0,d1);
  
  std::set<MTriangle*> visited;
 
  while (visited.size() != d0.size()){  
    std::stack<MTriangle*> _stack;
    std::map<MTriangle *, SVector3>::iterator it = d0.begin();
    MTriangle *current = NULL;
    for (; it != d0.end(); ++it){
      if (visited.find(it->first)==visited.end()){
        if (!isSingular(it->first,sing) && !isBndry(it->first,C)){
          current =it->first;
          break;
        }
      }
    }
    if (current == NULL)break;
    _stack.push(current);
    visited.insert(current);
    while (!_stack.empty()){
      current  = _stack.top();
      SVector3 ref0 = d0[current];
      SVector3 ref1 = d1[current];
      _stack.pop();
      
      // std::map<MEdge, cross2d, MEdgeLessThan>::iterator its[3];
      
      for (size_t i=0;i<3;i++){
        MEdge e = current->getEdge(i);
        std::map<MEdge, cross2d, MEdgeLessThan>::iterator it = C.find(e);
        if (cutG.find(e) == cutG.end() && it->second._t.size() == 2){
          MTriangle *t = it->second._t[0] == current ? it->second._t[1] : it->second._t[0]; 
          if (visited.find(t) == visited.end()){
            cross2d* bnd = isBndry (t,C);
            if (bnd){
              treatmentOfBoundary (bnd, ref0, ref1, d0, d1, groups,
                                   _groupIds, _done, visited, _stack);
            }
            else {
              visited.insert(t);
              _stack.push (t);
              SVector3 dir0 = d0[t];
              SVector3 dir1 = d1[t];
              SVector3 dir2 = dir0*(-1.0);
              SVector3 dir3 = dir1*(-1.0);
              double dot_0 = dot (dir0,ref0); 
              double dot_1 = dot (dir1,ref0); 
              double dot_2 = dot (dir2,ref0); 
              double dot_3 = dot (dir3,ref0);
              if (dot_0 > dot_1 && dot_0 > dot_2 && dot_0 > dot_3)d0[t] = dir0;
              else if (dot_1 > dot_0 && dot_1 > dot_2 && dot_1 > dot_3)d0[t] = dir1;
              else if (dot_2 > dot_0 && dot_2 > dot_1 && dot_2 > dot_3)d0[t] = dir2;
              else d0[t] = dir3;
              dot_0 = dot (dir0,ref1); 
              dot_1 = dot (dir1,ref1); 
              dot_2 = dot (dir2,ref1); 
              dot_3 = dot (dir3,ref1);
              if (dot_0 > dot_1 && dot_0 > dot_2 && dot_0 > dot_3)d1[t] = dir0;
              else if (dot_1 > dot_0 && dot_1 > dot_2 && dot_1 > dot_3)d1[t] = dir1;
              else if (dot_2 > dot_0 && dot_2 > dot_1 && dot_2 > dot_3)d1[t] = dir2;
              else d1[t] = dir3;
            }
          }
        }
        else if (it->second._t.size() > 2){
          Msg::Error ("to be continued %s at line %d",__FILE__,__LINE__);
        }
      }    
    }
  }
  if (visited.size() != d0.size())Msg::Debug ("not all triangles visited %lu vs %lu (FILE %s at line %d)",
                                              visited.size(), d0.size(),__FILE__,__LINE__);
}


static void computeLifting(cross2d *first, int branch,
                           std::set<MEdge, MEdgeLessThan> &cutG,
                           std::set<MVertex *, MVertexPtrLessThan> &sing,
                           std::set<MVertex *, MVertexPtrLessThan> &bnd,
                           std::set<cross2d *> &visited,
                           int ITER)
{
  FILE *_f = NULL;
  if(Msg::GetVerbosity() == 99) {
    _f = fopen("visited.pos", "w");
    fprintf(_f, "View\"\"{\n");
  }
  
  std::queue<cross2d *> _s;
  _s.push(first);
  first->_atemp = first->_a + branch * M_PI / 2.0;
  first->_btemp = 10000.;
  visited.insert(first);
  int COUNTER = 1;
  while(!_s.empty()) {
    cross2d *c = _s.front();
    _s.pop();
    if(cutG.find(c->_e) == cutG.end()) {
      for(size_t i = 0; i < c->_cneighbors.size(); i++) {
        double a2;
        cross2d *n = c->_cneighbors[i];
        closest(*c, *n, a2);
        if(n->_btemp < 1000) {
          n->_btemp = 10000;

          bool s0 = sing.find(n->_e.getVertex(0)) != sing.end();
          bool s1 = sing.find(n->_e.getVertex(1)) != sing.end();

          if(ITER || (!s0 && !s1)) {
            _s.push(n);
            visited.insert(n);
          }
          //          else //          printf("%12.5E %12.5E %12.5E\n",n->a,n->_atemp,c->_atemp);
          n->_atemp = a2;
          SVector3 d = n->_tgt * cos(n->_atemp) + n->_tgt2 * sin(n->_atemp);
	  if(Msg::GetVerbosity() == 99) {
	    fprintf(_f, "VL(%g,%g,%g,%g,%g,%g){%g,%g,%g,%g,%g,%g};\n",
		    n->_e.getVertex(0)->x(), n->_e.getVertex(0)->y(),
		    n->_e.getVertex(0)->z(), n->_e.getVertex(1)->x(),
		    n->_e.getVertex(1)->y(), n->_e.getVertex(1)->z(), COUNTER*d.x(),
		    COUNTER*d.y(), COUNTER*d.z(), d.x(), d.y(), d.z());
	  }
          COUNTER++;
        }
      }
    }
  }
  if(Msg::GetVerbosity() == 99) {
    fprintf(_f, "};\n");
    fclose(_f);
  }
}

struct groupOfCross2d {
  int groupId;
  bool rot;
  double mat[2][2];
  double jump1,jump2;
  std::vector<MVertex *> vertices;
  std::vector<MVertex *> singularities;
  std::vector<MVertex *> left;
  std::vector<MVertex *> right;
  std::vector<cross2d *> crosses;
  std::vector<MTriangle *> side;
  void print(FILE *f)
  {

    for(size_t i = 0; i < crosses.size(); i++) {
      fprintf(
        f, "SL(%g,%g,%g,%g,%g,%g){%d,%d};\n", crosses[i]->_e.getVertex(0)->x(),
        crosses[i]->_e.getVertex(0)->y(), crosses[i]->_e.getVertex(0)->z(),
        crosses[i]->_e.getVertex(1)->x(), crosses[i]->_e.getVertex(1)->y(),
        crosses[i]->_e.getVertex(1)->z(), groupId, groupId);
    }
    for(size_t i = 0; i < side.size(); i++) {
      fprintf(f, "ST(%g,%g,%g,%g,%g,%g,%g,%g,%g){%lu,%lu,%lu};\n",
              side[i]->getVertex(0)->x(), side[i]->getVertex(0)->y(),
              side[i]->getVertex(0)->z(), side[i]->getVertex(1)->x(),
              side[i]->getVertex(1)->y(), side[i]->getVertex(1)->z(),
              side[i]->getVertex(2)->x(), side[i]->getVertex(2)->y(),
              side[i]->getVertex(2)->z(),
              side[i]->getVertex(0) -> getNum(),
              side[i]->getVertex(1) -> getNum(),
              side[i]->getVertex(2) -> getNum());
    }
  };
  groupOfCross2d(int id) : groupId(id), jump1(0), jump2(0)  {}
};

static void unDuplicateNodesInCutGraph(
  std::vector<GFace *> &f,
  std::map<MVertex *, MVertex *, MVertexPtrLessThan> &new2old)
{
  for(size_t i = 0; i < f.size(); i++) {
    for(size_t j = 0; j < f[i]->triangles.size(); j++) {
      MTriangle *t = f[i]->triangles[j];
      for(size_t k = 0; k < 3; k++) {
        std::map<MVertex *, MVertex *, MVertexPtrLessThan>::iterator it =
          new2old.find(t->getVertex(k));
        if(it != new2old.end()) t->setVertex(k, it->second);
      }
    }
  }
}


static void duplicateNodesInCutGraph(
  std::vector<GFace *> &f, std::map<MEdge, cross2d, MEdgeLessThan> &C,
  std::map<MVertex *, MVertex *, MVertexPtrLessThan> &new2old,
  std::multimap<MVertex *, MVertex *, MVertexPtrLessThan> &old2new,
  std::map<MEdge, MEdge, MEdgeLessThan> &duplicateEdges,
  std::set<MVertex *, MVertexPtrLessThan> &sing, v2t_cont &adj,
  std::vector<groupOfCross2d> &G)
{
  FILE *_f = NULL;
  if(Msg::GetVerbosity() == 99) {
    _f = fopen("nodes.pos", "w");
    fprintf(_f, "View \" nodes \"{\n");
  }

  v2t_cont::iterator it = adj.begin();
  std::set<MElement *> touched;
  std::set<MVertex *, MVertexPtrLessThan> vtouched;

  std::vector<std::pair<MElement *, std::pair<int, MVertex *> > > replacements;

  while(it != adj.end()) {
    std::vector<MElement *> els = it->second;
    int ITER = 0;
    while(!els.empty()) {
      std::vector<MElement *> _side;
      std::stack<MElement *> _s;
      _s.push(els[0]);
      _side.push_back(els[0]);
      els.erase(els.begin());
      while(!_s.empty()) {
        MElement *t = _s.top();
        _s.pop();
        for(int i = 0; i < 3; i++) {
          MEdge e0 = t->getEdge(i);
          std::map<MEdge, cross2d, MEdgeLessThan>::iterator it0 = C.find(e0);
          if(!it0->second.inCutGraph) {
            for(size_t j = 0; j < it0->second._t.size(); j++) {
              std::vector<MElement *>::iterator ite =
                std::find(els.begin(), els.end(), it0->second._t[j]);
              if(ite != els.end()) {
                els.erase(ite);
                _side.push_back(it0->second._t[j]);
                _s.push(it0->second._t[j]);
              }
            }
          }
        }
      }

     
      if(ITER) {
        MVertex *v =
          new MVertex(it->first->x(), it->first->y(), it->first->z(), f[0]);
        std::pair<MVertex *, MVertex *> p = std::make_pair(it->first, v);
        old2new.insert(p);
        f[0]->mesh_vertices.push_back(v);
        for(size_t i = 0; i < _side.size(); i++) {
          for(size_t j = 0; j < 3; j++) {
            if(_side[i]->getVertex(j) == it->first) {
              std::pair<int, MVertex *> r = std::make_pair(j, v);
              std::pair<MElement *, std::pair<int, MVertex *> > r2 =
                std::make_pair(_side[i], r);
              replacements.push_back(r2);
            }
          }
        }
      }
      ++ITER;
    }
    if(Msg::GetVerbosity() == 99) {
      fprintf(_f, "SP(%g,%g,%g){%lu};\n", it->first->x(), it->first->y(),
	      it->first->z(), old2new.count(it->first));
    }
    ++it;
  }

  if(Msg::GetVerbosity() == 99) {
    fprintf(_f, "};\n");
    fclose(_f);
  }
  
  for(size_t i = 0; i < replacements.size(); i++) {
    MElement *e = replacements[i].first;
    int j = replacements[i].second.first;
    MVertex *v = replacements[i].second.second;
    MVertex *old = e->getVertex(j);
    for(int j = 0; j < e->getNumEdges(); j++) {
      MEdge ed = e->getEdge(j);
      if(ed.getVertex(0) == old) {
        duplicateEdges[ed] = MEdge(v, ed.getVertex(1));
      }
      else if(ed.getVertex(1) == old) {
        duplicateEdges[ed] = MEdge(ed.getVertex(0), v);
      }
    }
    new2old[v] = old;
    e->setVertex(j, v);
  }
}


static void createLagrangeMultipliers(dofManager<double> &myAssembler,
                                      groupOfCross2d &g)
{
  // return;
  if(g.crosses[0]->inInternalBoundary) return;
  for(size_t K = 1 ; K < g.left.size(); K++) {
    if (g.left[K] != g.left[0]){
      myAssembler.numberVertex(g.left[K], 0, 3 + 100 * (g.groupId+1));
      myAssembler.numberVertex(g.left[K], 0, 4 + 100 * (g.groupId+1));
    }
  }
}

static void LagrangeMultipliers3(dofManager<double> &myAssembler,
                                 groupOfCross2d &g,
                                 std::map<MTriangle *, SVector3> &d0,
                                 bool assemble)
{
  //  return;
  // internal boundaries --> constant u or v on each side 
  if(g.crosses[0]->inInternalBoundary) {
    if(!assemble) {
      for(size_t K = 1; K < g.left.size(); K++) {
        if (g.left[K] != g.left[0]){
          myAssembler.numberVertex(g.left[K], 0, 12112123 + 100 * (g.groupId+1));
          myAssembler.numberVertex(g.left[K], 0, 12112124 + 100 * (g.groupId+1));
        }
        else {
          printf("WAZA %lu\n",g.left[K]->getNum());
        }
      }
      return;
    }



    double S1 = 0 , S0 = 0;
    {
      size_t N = g.crosses.size();
      for(size_t j = 0; j < N ; j++) {
	MTriangle *t0 = g.crosses[j]->_t[0];
	MTriangle *t1 = g.crosses[j]->_t[1];
	SVector3 dir0 = d0[t0];
	SVector3 dir1 = d0[t1];
	if(std::find(g.side.begin(), g.side.end(), t1) != g.side.end()) {
	  dir0 = d0[t1];
	  dir1 = d0[t0];
	}
	S0 += fabs(dot(g.crosses[j]->_tgt, dir0));
	S1 += fabs(dot(g.crosses[j]->_tgt, dir1));
      }
      S0 /= N;
      S1 /= N;
    }

    //    printf("GROUP %d %12.5E %12.5E (%lu,%lu)\n",g.groupId, S0, S1, g.left[0]->getNum(), g.right[0]->getNum());
    
    Dof U1R(g.left[0]->getNum(), Dof::createTypeWithTwoInts(0, 1));
    Dof U2R(g.right[0]->getNum(), Dof::createTypeWithTwoInts(0, 1));
    Dof V1R(g.left[0]->getNum(), Dof::createTypeWithTwoInts(0, 2));
    Dof V2R(g.right[0]->getNum(), Dof::createTypeWithTwoInts(0, 2));
    for(size_t K = 1; K < g.left.size(); K++) {
      //      if (g.left[K] == g.left[0])continue;
      //      printf("(%lu,%lu) ", g.left[K]->getNum(), g.right[K]->getNum());
      Dof E1(g.left[K]->getNum(),
             Dof::createTypeWithTwoInts(0, 12112123 + 100 * (g.groupId+1)));
      Dof E2(g.left[K]->getNum(),
             Dof::createTypeWithTwoInts(0, 12112124 + 100 * (g.groupId+1)));
      Dof U1(g.left[K]->getNum(), Dof::createTypeWithTwoInts(0, 1));
      Dof U2(g.right[K]->getNum(), Dof::createTypeWithTwoInts(0, 1));
      Dof V1(g.left[K]->getNum(), Dof::createTypeWithTwoInts(0, 2));
      Dof V2(g.right[K]->getNum(), Dof::createTypeWithTwoInts(0, 2));

      if(S0 < .2) {
        myAssembler.assembleSym(E1, U1, 1.0);
        //        myAssembler.assemble(U1, E1, 1.0);
        myAssembler.assembleSym(E1, U1R, -1.0);
        //        myAssembler.assemble(U1R, E1, -1.0);
      }
      else {
        myAssembler.assembleSym(E1, V1, 1.0);
        //        myAssembler.assemble(V1, E1, 1.0);
        myAssembler.assembleSym(E1, V1R, -1.0);
        //        myAssembler.assemble(V1R, E1, -1.0);
      }

      if(S1 < .2) {
        //        printf("HAHAHA %d\n",g.groupId);
        myAssembler.assembleSym(E2, U2, 1.0);
        //        myAssembler.assemble(U2, E2, 1.0);
        myAssembler.assembleSym(E2, U2R, -1.0);
        //        myAssembler.assemble(U2R, E2, -1.0);
      }
      else {
        //        printf("HAHAHO %d\n",g.groupId);
        myAssembler.assembleSym(E2, V2, 1.0);
        //        myAssembler.assemble(V2, E2, 1.0);
        myAssembler.assembleSym(E2, V2R, -1.0);
        //        myAssembler.assemble(V2R, E2, -1.0);
      }
    }
    //    printf("\n");
  }
}

static void assembleLagrangeMultipliers(dofManager<double> &myAssembler,
                                        groupOfCross2d &g)
{
  //  printf("group %d\n",g.groupId);
  Dof U1R(g.left[0]->getNum(), Dof::createTypeWithTwoInts(0, 1));
  Dof V1R(g.left[0]->getNum(), Dof::createTypeWithTwoInts(0, 2));
  Dof U2R(g.right[0]->getNum(), Dof::createTypeWithTwoInts(0, 1));
  Dof V2R(g.right[0]->getNum(), Dof::createTypeWithTwoInts(0, 2));

  //  if (g.groupId == 1){
  //    g.mat[0][0]=-1;
  //    g.mat[1][1]=-1;
  //  }

  if(g.singularities.size() == 1) {
    //    printf("%lu %lu --> %lu\n",g.left[0]->getNum(),g.right[0]->getNum(),
    //           g.singularities[0]->getNum());
    Dof E1(g.singularities[0]->getNum(), Dof::createTypeWithTwoInts(0, 33+ 100 * (g.groupId+1)));
    Dof E2(g.singularities[0]->getNum(), Dof::createTypeWithTwoInts(0, 34+ 100 * (g.groupId+1)));
    Dof U(g.singularities[0]->getNum(), Dof::createTypeWithTwoInts(0, 1));
    Dof V(g.singularities[0]->getNum(), Dof::createTypeWithTwoInts(0, 2));

    //    myAssembler.assemble(E1, E1, 1.e-12);
    //    myAssembler.assemble(E2, E2, 1.e-12);
    
    myAssembler.assembleSym(E1, U, 1.0);
    myAssembler.assembleSym(E1, U1R, -1.0);

    myAssembler.assembleSym(E1, U, -g.mat[0][0]);
    myAssembler.assembleSym(E1, V, -g.mat[0][1]);
    myAssembler.assembleSym(E1, U2R, g.mat[0][0]);
    myAssembler.assembleSym(E1, V2R, g.mat[0][1]);

    myAssembler.assembleSym(E2, V, 1.0);
    myAssembler.assembleSym(E2, V1R, -1.0);

    myAssembler.assembleSym(E2, U, -g.mat[1][0]);
    myAssembler.assembleSym(E2, V, -g.mat[1][1]);
    myAssembler.assembleSym(E2, U2R, g.mat[1][0]);
    myAssembler.assembleSym(E2, V2R, g.mat[1][1]);
  }

  for(size_t K = 1; K < g.left.size(); K++) {
    //    printf("%3lu %3lu\n",g.left[K]->getNum(),g.right[K]->getNum());
    // EQUATION IDS (Lagrange multipliers)
    Dof E1(g.left[K]->getNum(),
           Dof::createTypeWithTwoInts(0, 3 + 100 * (g.groupId+1)));
    Dof E2(g.left[K]->getNum(),
           Dof::createTypeWithTwoInts(0, 4 + 100 * (g.groupId+1)));

    // DOF IDS
    Dof U1(g.left[K]->getNum(), Dof::createTypeWithTwoInts(0, 1));
    Dof V1(g.left[K]->getNum(), Dof::createTypeWithTwoInts(0, 2));
    Dof U2(g.right[K]->getNum(), Dof::createTypeWithTwoInts(0, 1));
    Dof V2(g.right[K]->getNum(), Dof::createTypeWithTwoInts(0, 2));

    myAssembler.assembleSym(E1, U1, 1.0);
    myAssembler.assembleSym(E1, U1R, -1.0);

    myAssembler.assembleSym(E1, U2, -g.mat[0][0]);
    myAssembler.assembleSym(E1, V2, -g.mat[0][1]);
    myAssembler.assembleSym(E1, U2R, g.mat[0][0]);
    myAssembler.assembleSym(E1, V2R, g.mat[0][1]);

    myAssembler.assembleSym(E2, V1, 1.0);
    myAssembler.assembleSym(E2, V1R, -1.0);

    myAssembler.assembleSym(E2, U2, -g.mat[1][0]);
    myAssembler.assembleSym(E2, V2, -g.mat[1][1]);
    myAssembler.assembleSym(E2, U2R, g.mat[1][0]);
    myAssembler.assembleSym(E2, V2R, g.mat[1][1]);
  }
}


static MEdge get2Vertices (cross2d * c,
                           std::map<MVertex *, MVertex *, MVertexPtrLessThan> &new2old){
  MTriangle *t = c->_t[0];
  MVertex *v0 = new2old.find (t->getVertex(0)) == new2old.end() ? t->getVertex(0) : new2old[t->getVertex(0)];
  MVertex *v1 = new2old.find (t->getVertex(1)) == new2old.end() ? t->getVertex(1) : new2old[t->getVertex(1)];
  MVertex *v2 = new2old.find (t->getVertex(2)) == new2old.end() ? t->getVertex(2) : new2old[t->getVertex(2)];
  MEdge e0 (v0,v1);
  MEdge e1 (v1,v2);
  MEdge e2 (v2,v0);
  if (e0 == c->_e)return MEdge (t->getVertex(0),t->getVertex(1));
  else if (e1 == c->_e)return MEdge (t->getVertex(1),t->getVertex(2));
  else if (e2 == c->_e)return MEdge (t->getVertex(2),t->getVertex(0));
  else {
    Msg::Error ("Error in potential computation");
    return MEdge (NULL,NULL);
  }
}


static void
LagrangeMultipliers2(dofManager<double> &myAssembler, int NUMDOF,
                     std::map<MEdge, cross2d, MEdgeLessThan> &C,
                     std::vector<std::vector<cross2d *> > &groups,
                     bool assemble, std::map<MTriangle *, SVector3> &lift,
                     std::map<MVertex *, MVertex *, MVertexPtrLessThan> &new2old  )
{
  for(size_t i = 0; i < groups.size(); i++) {
    size_t N = groups[i].size();

    MEdge ed = get2Vertices(groups[i][0],new2old);

    /*
    MEdge ed = groups[i][0]->_e;    
    std::map<MEdge, MEdge, MEdgeLessThan>::iterator ite =
    duplicateEdges.find(ed);
    if(ite != duplicateEdges.end()) ed = ite->second;
    */

    double S = 0;
    std::map<MEdge, cross2d, MEdgeLessThan>::iterator it;
    for(size_t j = 0; j < N; j++) {
      it = C.find(groups[i][j]->_e);
      SVector3 aaa = lift[it->second._t[0]];      
      S += fabs(dot(it->second._tgt, aaa));
    }
    S /= N;
    
    it = C.find(groups[i][0]->_e);
    
    MVertex *v = ed.getVertex(0);
    
    if(it->second.inInternalBoundary)continue; 

    //    printf("group %d DIR %d S = %12.5E %d edges reference vertex %lu assemble %d\n",i,NUMDOF, S, N,v->getNum(), assemble);

    if(S < .2 /*sqrt(2)/2.0*/) {
      for(size_t j = 0; j < N; j++) {
        MEdge ed = get2Vertices(groups[i][j],new2old);
        //        ed = groups[i][j]->_e;
        //        ite = duplicateEdges.find(ed);
        //        if(ite != duplicateEdges.end()) ed = ite->second;
        //        if (i == 20)    printf("%lu %lu\n",ed.getVertex(0)->getNum(),ed.getVertex(1)->getNum());
        for(int k = 0; k < 2; k++) {
          MVertex *vk = ed.getVertex(k);
          if(vk != v) {
            if(!assemble) { myAssembler.numberVertex(vk, 0, 5 + 100 * i); }
            else {
              Dof Eref(vk->getNum(),
                       Dof::createTypeWithTwoInts(0, 5 + 100 * i));
              Dof Uref(vk->getNum(), Dof::createTypeWithTwoInts(0, NUMDOF));
              Dof U(v->getNum(), Dof::createTypeWithTwoInts(0, NUMDOF));
              //              printf("group %d : %lu equalsd %lu\n",i, v->getNum(),vk->getNum());
              myAssembler.assembleSym(Eref, Uref, 1.0);
              myAssembler.assembleSym(Eref, U, -1.0);
              //              myAssembler.assemble(Uref, Eref, 1.0);
              //              myAssembler.assemble(U, Eref, -1.0);
            }
          }
        }
      }
    }
  }
}

const size_t MAX_PASSAGES = 200;

struct cutGraphPassage {
  enum TYPE {SING_TO_SING,SING_TO_BDRY,SING_TO_NOTHING,REDUNDANT,UNKNOWN};
  TYPE _type;
  int COUNTER;
  int DIR;
  int DIR_CONN;
  std::vector<SPoint3> pts;
  std::vector<double> vals;
  std::vector<MEdge> eds;
  std::vector<SPoint3> pts_on_eds;
  std::vector<std::pair<int,int> > cuts;
  std::vector<int> groups;
  std::vector<int> signs;
  MVertex *sing, *V1;
  MVertex *sing_conn, *V2;
  bool close;
  double diff,d;
  int true_passages;
  cutGraphPassage (int C, int D, MVertex *s) : _type(UNKNOWN),COUNTER(C), DIR(D), sing(s), V1(NULL), sing_conn(NULL), V2(NULL), 
                                               close(false), diff (1.e22),true_passages(0)
  {
  }

  void setClose (bool cl) {
    close = cl;
  }

  void addPassage (int pot, int id) {
    cuts.push_back(std::make_pair(pot,id));
  }

  double angleTot() const {
    double angle = 0;
    for (size_t i=1;i<pts.size()-1;i++){
      SVector3 v1 (pts[i-1],pts[i]);
      SVector3 v2 (pts[i],pts[i+1]);
      v1.normalize();
      v2.normalize();
      SVector3 xx = crossprod(v1, v2);
      double ccos = dot(v1, v2);
      double ANGLE = atan2(xx.norm(), ccos);
      angle += ANGLE;
    }
    return angle;
  }
  void create(dofManager<double> &myAssembler, std::vector<groupOfCross2d> &G){
    if (V1 == V2 && pts.size() < 3)return;
    if (eds.empty() && sing == sing_conn)return;
    if (!sing_conn) return;
    if (DIR == 0)
      myAssembler.numberVertex(G[0].left[0], COUNTER, 10201020 ); 
    else
      myAssembler.numberVertex(G[0].left[0], COUNTER, 10201021 ); 
  }
  
  void assemble(dofManager<double> &myAssembler, std::vector<groupOfCross2d> &G){

    if (V1 == V2 && pts.size() < 3)return;
    if (!sing_conn) return;

    Print("Assembling");
    
    Dof E1(G[0].left[0]->getNum(), Dof::createTypeWithTwoInts(COUNTER, 10201020));    
    Dof E2(G[0].left[0]->getNum(), Dof::createTypeWithTwoInts(COUNTER, 10201021));    

    Dof DOF_UL(sing->getNum(), Dof::createTypeWithTwoInts(0, 1));    
    Dof DOF_VL(sing->getNum(), Dof::createTypeWithTwoInts(0, 2));    

    Dof DOF_UR(sing_conn->getNum(), Dof::createTypeWithTwoInts(0, 1));    
    Dof DOF_VR(sing_conn->getNum(), Dof::createTypeWithTwoInts(0, 2));    

    myAssembler.assembleSym(E1, DOF_UL,  -1.0);
    myAssembler.assembleSym(E2, DOF_VL,  -1.0 );

    bool _U = DIR == 0;

    double M[2][2] = {{1,0},{0,1}};
    
    int counter = 0;
    for (size_t i=0 ; i <  eds.size() ; i++){
      if (i){
              SVector3 temp (pts_on_eds[i],pts_on_eds[i-1]);
              if (temp.norm() < 1.e-8) continue;
      }
    
      int GROUP = groups[counter];
      int SIGN  = signs[counter++];

      groupOfCross2d &g = G[GROUP];

      double m00 = M[0][0]*g.mat[0][0]+ M[0][1]*g.mat[1][0]; 
      double m01 = M[0][0]*g.mat[0][1]+ M[0][1]*g.mat[1][1]; 
      double m10 = M[1][0]*g.mat[0][0]+ M[1][1]*g.mat[1][0]; 
      double m11 = M[1][0]*g.mat[0][1]+ M[1][1]*g.mat[1][1]; 
      
      if (SIGN == -1){
        m00 = M[0][0]*g.mat[0][0]+ M[0][1]*g.mat[0][1]; 
        m01 = M[0][0]*g.mat[1][0]+ M[0][1]*g.mat[1][1]; 
        m10 = M[1][0]*g.mat[0][0]+ M[1][1]*g.mat[0][1]; 
        m11 = M[1][0]*g.mat[1][0]+ M[1][1]*g.mat[1][1]; 
      }
      
      MVertex *ref_LEFT  =  SIGN ==  1 ? g.left[0] : g.right[0] ;      
      MVertex *ref_RIGHT =  SIGN == -1 ? g.left[0] : g.right[0] ;      
      
      Dof DOF_ULEFT(ref_LEFT->getNum(), Dof::createTypeWithTwoInts(0, 1));    
      Dof DOF_VLEFT(ref_LEFT->getNum(), Dof::createTypeWithTwoInts(0, 2));    
      if (fabs(g.mat[1][0]) > .8) _U = _U ? false : true;

      Dof DOF_URIGHT(ref_RIGHT->getNum(), Dof::createTypeWithTwoInts(0, 1));    
      Dof DOF_VRIGHT(ref_RIGHT->getNum(), Dof::createTypeWithTwoInts(0, 2));    
      
      //      printf("coucou %d %lu %d %d %lu %lu\n",COUNTER,i,GROUP,SIGN,
      //             ref_LEFT->getNum(),ref_RIGHT->getNum());

      myAssembler.assembleSym(E1, DOF_ULEFT , M[0][0]);
      myAssembler.assembleSym(E1, DOF_VLEFT,  M[0][1]);
      myAssembler.assembleSym(E2, DOF_ULEFT,  M[1][0]);
      myAssembler.assembleSym(E2, DOF_VLEFT,  M[1][1]);
      myAssembler.assembleSym(E1, DOF_URIGHT, -m00);
      myAssembler.assembleSym(E1, DOF_VRIGHT, -m01);
      myAssembler.assembleSym(E2, DOF_URIGHT, -m10);
      myAssembler.assembleSym(E2, DOF_VRIGHT, -m11);
      M[0][0] = m00;M[1][0] = m10;M[0][1] = m01;M[1][1] = m11;
    }

    //    printf("FINISH WITH %g %g %g %g\n",
    //           M[0][0],M[0][1],M[1][0],M[1][1]);
    
    myAssembler.assembleSym(E1, DOF_UR,  M[0][0]);
    myAssembler.assembleSym(E1, DOF_VR,  M[0][1]);
    myAssembler.assembleSym(E2, DOF_UR,  M[1][0]);
    myAssembler.assembleSym(E2, DOF_VR,  M[1][1]);
  }


  void analyze (std::map<MVertex *, double> &potU,
                std::map<MVertex *, double> &potV,
                std::vector<groupOfCross2d> &G,
                std::map<MVertex *, MVertex *, MVertexPtrLessThan> &new2old){
    V1 =  (new2old.find(sing) == new2old.end()) ? sing : new2old[sing];
    
    bool _U = DIR == 0;
        
    double U_INIT = potU [sing];
    double V_INIT = potV [sing];
    
    if (!sing_conn)return;


    groups.clear();
    signs.clear();
    
    true_passages = 0;
    // double M[2][2] = {{1,0},{0,1}};
    for (size_t i=0 ; i <  eds.size() ; i++){
      if (i){
        SVector3 temp (pts_on_eds[i],pts_on_eds[i-1]);
        if (temp.norm() < 1.e-8) continue;
      }
      true_passages++;
      int iGroup = cuts[i].second;
      groupOfCross2d &g = G[iGroup];
      int sign  = std::find(g.left.begin(), g.left.end(), eds[i].getVertex(0)) == g.left.end() ? -1 : 1;
      groups.push_back(iGroup);
      signs.push_back(sign);
      
      if (fabs(g.mat[1][0]) > .8) _U = _U ? false : true;

      // double m00 = g.mat[0][0]*M[0][0]+ g.mat[0][1]*M[1][0]; 
      // double m01 = g.mat[0][0]*M[0][1]+ g.mat[0][1]*M[1][1]; 
      // double m10 = g.mat[1][0]*M[0][0]+ g.mat[1][1]*M[1][0]; 
      // double m11 = g.mat[1][0]*M[0][1]+ g.mat[1][1]*M[1][1]; 
      // if (sign == -1){
      //   m00 = g.mat[0][0]*M[0][0] - g.mat[1][0]*M[1][0]; 
      //   m01 = g.mat[0][0]*M[0][1] - g.mat[1][0]*M[1][1]; 
      //   m10 = -g.mat[0][1]*M[0][0]+ g.mat[1][1]*M[1][0]; 
      //   m11 = -g.mat[0][1]*M[0][1]+ g.mat[1][1]*M[1][1]; 
      // }


      
      if (sign == -1){
        double u = g.mat[0][0] * U_INIT + g.mat[0][1] * V_INIT + g.jump1; 
        double v = g.mat[1][0] * U_INIT + g.mat[1][1] * V_INIT + g.jump2;
        U_INIT = u;
        V_INIT = v;
      }
      else {

        double X = U_INIT - g.jump1;
        double Y = V_INIT - g.jump2;
        U_INIT = g.mat[0][0] * X + g.mat[1][0] * Y ; 
        V_INIT = g.mat[0][1] * X + g.mat[1][1] * Y ;
      }
      //      if (COUNTER == 156411){
      //        printf("     %c = %12.5E\n",_U ? 'U' : 'V', _U ? U_INIT : V_INIT);
      //      }
    }
    diff = _U ? fabs(potU [sing_conn] - U_INIT) : fabs(potV [sing_conn] - V_INIT) ;
    DIR_CONN = _U ? 0 : 1; 

    for (std::map<MVertex *, MVertex *, MVertexPtrLessThan>::iterator it = new2old.begin() ;
         it != new2old.end() ; ++it){
      if (it->second == sing_conn){
        double diff2 = _U ? fabs (potU [it->first]-U_INIT) : fabs (potV [it->first]-V_INIT);
        if (diff2 < diff){
          diff = diff2;
          sing_conn =it->first;
        }
      }
    }

    V2 =  (new2old.find(sing_conn) == new2old.end()) ? sing_conn : new2old[sing_conn];
    
    //      if (COUNTER == 156411){
    //        printf("ISO %8d (%8lu,%8lu) : %c (%12.5E,%12.5E) diff %12.5E | %3lu cut graph cuts\n",
    //               COUNTER, V1->getNum(), V2->getNum(), _U ? 'U' : 'V', _U ? U_INIT : V_INIT,
    //               _U ? potU [sing_conn] : potV [sing_conn], diff,eds.size() );
    //      }
  }

  double length () const {
    int CHUNK = sing == sing_conn ? 1 : 0;
    double l = 0;
    for (size_t i=1;i<pts.size()-CHUNK;i++){
      SVector3 v = pts[i]-pts[i-1];
      l += v.norm();
    }
    return l;
  }
  void PrintFile () const {
    //    if (diff > 1.e-10)return;
    if(Msg::GetVerbosity() == 99) {
      char name[245];
      sprintf(name,"p_%d.pos",COUNTER);
      FILE *F = fopen(name,"w");
      fprintf(F,"View\"\"{\n");
      int CHUNK = sing == sing_conn ? 1 : 0;
      for (size_t i=1;i<pts.size()-CHUNK;i++){
	fprintf(F,"SL(%g,%g,%g,%g,%g,%g) {%g,%g};\n",pts[i-1].x(),pts[i-1].y(),pts[i-1].z(),pts[i].x(),pts[i].y(),pts[i].z(),vals[i-1],vals[i]);
	fprintf(F,"VP(%g,%g,%g) {%g,%g,%g};\n",pts[i-1].x(),pts[i-1].y(),pts[i-1].z(),
		pts[i].x()-pts[i-1].x(),pts[i].y()-pts[i-1].y(),pts[i].z()-pts[i-1].z());
      }
      fprintf(F,"};\n");
      fclose(F);
    }
  }

  
  void Print (const char *t) const {
    if (_type == cutGraphPassage::SING_TO_SING && V1 && V2)
      printf("%s : ISO %8d exactly connects (%8lu,%8lu) : %c --> %c -- diff %22.15E %4d passages %4lu points L = %12.5E DIST %12.5E\n",t,
             COUNTER, V1->getNum(), V2->getNum(), DIR==0 ? 'U' : 'V', DIR_CONN==0 ? 'U' : 'V', diff, true_passages,
             pts.size(),length(),d);
    else if (_type == cutGraphPassage::SING_TO_NOTHING && V1 && V2)
      printf("%s : ISO %8d approximatively connects (%8lu,%8lu) : %c --> %c -- diff %22.15E %4d passages %4lu points L = %12.5E DIST %12.5E\n",t,
             COUNTER, V1->getNum(), V2->getNum(), DIR==0 ? 'U' : 'V', DIR_CONN==0 ? 'U' : 'V', diff, true_passages,
             pts.size(),length(),d);
    else if (_type == cutGraphPassage::SING_TO_BDRY)
      printf("%s : ISO %8d (%8lu,BDRY) %4d passages\n",t,COUNTER,V1->getNum(),true_passages);      
    else if (_type == cutGraphPassage::REDUNDANT)
      printf("%s : ISO %8d is redundant\n",t,COUNTER);      
    else
      printf("%s : ISO %8d is strange %d %p %p\n",t,COUNTER,_type,V1,V2);      
  }

  bool operator == (const cutGraphPassage & g) const{
     if (V1 && V2 && g.V1 && g.V2){
       size_t a[2] = {std::max(V1->getNum(),V2->getNum()),std::min(V1->getNum(),V2->getNum())};
       size_t b[2] = {std::max(g.V1->getNum(),g.V2->getNum()),std::min(g.V1->getNum(),g.V2->getNum())};
       if (a[0] == b[0] && a[1] == b[1]){
         if (g.DIR == DIR_CONN && g.DIR_CONN == DIR /*&& g.eds.size() == eds.size()*/){
           if (fabs (g.diff - diff) / (g.diff+diff) < 1e-1)
             return true;
         }
       }
     }
     return false;
  }

  bool operator < (const cutGraphPassage & g) const{
    return COUNTER < g.COUNTER;
    if (_type < g._type) return false; 
    if (_type > g._type) return true; 
    if (V1 && V2 && g.V1 && g.V2){
      size_t a[2] = {std::max(V1->getNum(),V2->getNum()),std::min(V1->getNum(),V2->getNum())};
      size_t b[2] = {std::max(g.V1->getNum(),g.V2->getNum()),std::min(g.V1->getNum(),g.V2->getNum())};
      if (a[0] > b[0]) return true;
      if (a[0] < b[0]) return false;
      if (a[1] > b[1]) return true;
      if (a[1] < b[1]) return false;
      if (DIR + DIR_CONN < g.DIR + g.DIR_CONN)return true;
      if (DIR + DIR_CONN > g.DIR + g.DIR_CONN)return false;
      //      if (DIR > DIR_CONN)return false;
      return diff < g.diff;
    }
    if (V1 && V2)return true;
    if (g.V1 && g.V2)return false;
    
  }

};


static void createDofs(dofManager<double> &myAssembler, int NUMDOF,
                       std::set<MVertex *, MVertexPtrLessThan> &vs)
{
  for(std::set<MVertex *, MVertexPtrLessThan>::iterator it = vs.begin();
      it != vs.end(); ++it)
    myAssembler.numberVertex(*it, 0, NUMDOF);
}

void createExtraConnexions (dofManager<double> &myAssembler,
                            std::vector<groupOfCross2d> &G,
                            std::vector<cutGraphPassage> &passages){
  // TRY WITH THE FIRST PASSAGE
  //return;
  if (passages.empty())return;
  for (size_t i=0;i<passages.size();i++){
    passages[i].create (myAssembler,G);
  }
}

void assembleExtraConnexions (dofManager<double> &myAssembler,
                              std::vector<groupOfCross2d> &G,
                              std::vector<cutGraphPassage> &passages){
  if (passages.empty())return;
  for (size_t i=0;i<passages.size();i++){
    passages[i].assemble (myAssembler,G);
  }
}


static bool computePotential(
  GModel *gm, std::vector<GFace *> &f, dofManager<double> &dof,
  std::map<MEdge, cross2d, MEdgeLessThan> &C,
  std::map<MVertex *, MVertex *, MVertexPtrLessThan> &new2old,
  std::vector<std::vector<cross2d *> > &groups,
  std::map<MTriangle *, SVector3> &lift, std::map<MTriangle *, SVector3> &lift2,
  std::vector<groupOfCross2d> &G,
  std::map<MVertex *, double> &res,
  std::map<MVertex *, double> &res2,
  std::vector<cutGraphPassage> &passages)
{

  if(Msg::GetVerbosity() == 99) {
    gm->writeMSH("split.msh", 4.0, false, true);
  }
  
  double a[3];
  std::set<MVertex *, MVertexPtrLessThan> vs;
  for(size_t i = 0; i < f.size(); i++) {
    for(size_t j = 0; j < f[i]->triangles.size(); j++) {
      MTriangle *t = f[i]->triangles[j];
      for(size_t k = 0; k < 3; k++) { vs.insert(t->getVertex(k)); }
    }
  }

#if defined(HAVE_MUMPS)
  linearSystemMUMPS<double> *_lsys = new linearSystemMUMPS<double>;
#elif defined(HAVE_PETSC)
  linearSystemPETSc<double> *_lsys = new linearSystemPETSc<double>;
  //linearSystemGmm<double> *_lsys = new linearSystemGmm<double>;
  //  _lsys->setParameter("symmetry","symmetric");

#else
  linearSystemFull<double> *_lsys = new linearSystemFull<double>;
#endif

  dofManager<double> myAssembler(_lsys);

  //  int NUMDOF = dir+1;


  
  createDofs(myAssembler, 1, vs);
  createDofs(myAssembler, 2, vs);
  LagrangeMultipliers2(myAssembler, 1, C, groups, false, lift, new2old);
  LagrangeMultipliers2(myAssembler, 2, C, groups, false, lift2, new2old);

  createExtraConnexions (myAssembler, G, passages);

  for(size_t i = 0; i < G.size(); i++) {
    createLagrangeMultipliers(myAssembler, G[i]);
    LagrangeMultipliers3(myAssembler, G[i], lift, false);
  }

#if 1
  // AVERAGE
  myAssembler.numberVertex(*vs.begin(), 9696, 1);
  myAssembler.numberVertex(*vs.begin(), 9696, 2);
  Dof EAVG1((*vs.begin())->getNum(), Dof::createTypeWithTwoInts(9696, 1));
  Dof EAVG2((*vs.begin())->getNum(), Dof::createTypeWithTwoInts(9696, 2));

  for(std::set<MVertex *, MVertexPtrLessThan>::iterator it = vs.begin();
      it != vs.end(); ++it){
    Dof E1((*it)->getNum(), Dof::createTypeWithTwoInts(0, 1));
    Dof E2((*it)->getNum(), Dof::createTypeWithTwoInts(0, 2));

    myAssembler.assembleSym(EAVG1, E1, 1.0);
    myAssembler.assembleSym(EAVG2, E2, 1.0);
  }
  myAssembler.assemble(EAVG1, EAVG1, 0.0); //for petsc
  myAssembler.assemble(EAVG2, EAVG2, 0.0); //for petsc
#endif
  
  
  LagrangeMultipliers2(myAssembler, 1, C, groups, true, lift, new2old);
  LagrangeMultipliers2(myAssembler, 2, C, groups, true, lift2, new2old);
  for(size_t i = 0; i < G.size(); i++) {
    assembleLagrangeMultipliers(myAssembler, G[i]);
    LagrangeMultipliers3(myAssembler, G[i], lift, true);
  }

  assembleExtraConnexions (myAssembler, G, passages);
  
  simpleFunction<double> ONE(1.0);
  laplaceTerm l(NULL, 1, &ONE);
  laplaceTerm l2(NULL, 2, &ONE);

  for(size_t i = 0; i < f.size(); i++) {
    for(size_t j = 0; j < f[i]->triangles.size(); j++) {
      MTriangle *t = f[i]->triangles[j];
      SElement se(t);
      l.addToMatrix(myAssembler, &se);
      l2.addToMatrix(myAssembler, &se);
      SVector3 a0 = lift[t];
      SVector3 a1 = lift2[t];
      double va, vb, vc;
      std::map<MVertex *, MVertex *, MVertexPtrLessThan>::iterator itx =
        new2old.find(t->getVertex(0));
      dof.getDofValue(itx == new2old.end() ? t->getVertex(0) : itx->second, 0,
                      1, va);
      itx = new2old.find(t->getVertex(1));
      dof.getDofValue(itx == new2old.end() ? t->getVertex(1) : itx->second, 0,
                      1, vb);
      itx = new2old.find(t->getVertex(2));
      dof.getDofValue(itx == new2old.end() ? t->getVertex(2) : itx->second, 0,
                      1, vc);

      double F = (exp(va) + exp(vb) + exp(vc)) / 3.0;

      a0 *= F;
      a1 *= F;

      SPoint3 pp = t->barycenter();
      double G1[3] = {a0.x(), a0.y(), a0.z()};
      double G2[3] = {a0.x(), a0.y(), a0.z()};
      double G3[3] = {a0.x(), a0.y(), a0.z()};
      double G11[3] = {a1.x(), a1.y(), a1.z()};
      double G21[3] = {a1.x(), a1.y(), a1.z()};
      double G31[3] = {a1.x(), a1.y(), a1.z()};
      double g1[3];
      a[0] = 1;
      a[1] = 0;
      a[2] = 0;
      t->interpolateGrad(a, 0, 0, 0, g1);
      double RHS1 = g1[0] * G1[0] + g1[1] * G1[1] + g1[2] * G1[2];
      double RHS11 = g1[0] * G11[0] + g1[1] * G11[1] + g1[2] * G11[2];
      a[0] = 0;
      a[1] = 1;
      a[2] = 0;
      t->interpolateGrad(a, 0, 0, 0, g1);
      double RHS2 = g1[0] * G2[0] + g1[1] * G2[1] + g1[2] * G2[2];
      double RHS21 = g1[0] * G21[0] + g1[1] * G21[1] + g1[2] * G21[2];
      a[0] = 0;
      a[1] = 0;
      a[2] = 1;
      t->interpolateGrad(a, 0, 0, 0, g1);
      double RHS3 = g1[0] * G3[0] + g1[1] * G3[1] + g1[2] * G3[2];
      double RHS31 = g1[0] * G31[0] + g1[1] * G31[1] + g1[2] * G31[2];
      int num1 = myAssembler.getDofNumber(l.getLocalDofR(&se, 0));
      int num2 = myAssembler.getDofNumber(l.getLocalDofR(&se, 1));
      int num3 = myAssembler.getDofNumber(l.getLocalDofR(&se, 2));
      int num11 = myAssembler.getDofNumber(l2.getLocalDofR(&se, 0));
      int num21 = myAssembler.getDofNumber(l2.getLocalDofR(&se, 1));
      int num31 = myAssembler.getDofNumber(l2.getLocalDofR(&se, 2));

      double V = t->getVolume();
      //      printf("%12.5E %12.5E %12.5E\n",RHS1,RHS2,RHS3);
      _lsys->addToRightHandSide(num1, RHS1 * V);
      _lsys->addToRightHandSide(num2, RHS2 * V);
      _lsys->addToRightHandSide(num3, RHS3 * V);
      _lsys->addToRightHandSide(num11, RHS11 * V);
      _lsys->addToRightHandSide(num21, RHS21 * V);
      _lsys->addToRightHandSide(num31, RHS31 * V);
    }
  }
  double A = Cpu();
  try {
    _lsys->systemSolve();
  }
  catch (...){
    Msg::Info("Computing potentials (%d unknowns) failed",
              myAssembler.sizeOfR());
    //    return false;
  }
  double B = Cpu();
  Msg::Info("Computing potentials (%d unknowns) in %3lf seconds",
            myAssembler.sizeOfR(), B - A);

  FILE *F, *F2;
  F = NULL;
  F2 = NULL;
  if(Msg::GetVerbosity() == 99) {
    F = fopen("map.pos", "w");
    F2 = fopen("mapstr.pos", "w");
    fprintf(F, "View \"MAP\"{\n");
    fprintf(F2, "View \"MAPSTR\"{\n");
  }
  for(size_t i = 0; i < f.size(); i++) {
    for(size_t j = 0; j < f[i]->triangles.size(); j++) {
      MTriangle *t = f[i]->triangles[j];
      double a, b, c;
      double a1, b1, c1;
      myAssembler.getDofValue(t->getVertex(0), 0, 1, a);
      myAssembler.getDofValue(t->getVertex(1), 0, 1, b);
      myAssembler.getDofValue(t->getVertex(2), 0, 1, c);
      myAssembler.getDofValue(t->getVertex(0), 0, 2, a1);
      myAssembler.getDofValue(t->getVertex(1), 0, 2, b1);
      myAssembler.getDofValue(t->getVertex(2), 0, 2, c1);
      if(Msg::GetVerbosity() == 99) {
	fprintf(F, "ST(%g,%g,%g,%g,%g,%g,%g,%g,%g){%g,%g,%g,%g,%g,%g};\n", a, a1,
		0., b, b1, 0., c, c1, 0., a, b, c, a1, b1, c1);
	fprintf(F2, "ST(%g,%g,%g,%g,%g,%g,%g,%g,%g){%g,%g,%g,%g,%g,%g};\n",
		.2 * a + t->getVertex(0)->x(), -.2 * a1 + t->getVertex(0)->y(),
		0., .2 * b + t->getVertex(1)->x(),
		-.2 * b1 + t->getVertex(1)->y(), 0.,
		.2 * c + t->getVertex(2)->x(), -.2 * c1 + t->getVertex(2)->y(),
		0., a, b, c, a1, b1, c1);
      }
      res[t->getVertex(0)] = a;
      res[t->getVertex(1)] = b;
      res[t->getVertex(2)] = c;
      res2[t->getVertex(0)] = a1;
      res2[t->getVertex(1)] = b1;
      res2[t->getVertex(2)] = c1;
    }
  }
  if(Msg::GetVerbosity() == 99) {    
    fprintf(F, "};\n");
    fclose(F);
    fprintf(F2, "};\n");
    fclose(F2);
  }
  return true;
}

static double distance(MTriangle *t,
                       std::set<MVertex *, MVertexPtrLessThan> &boundaries)
{
  //  return drand48();
  SPoint3 p = t->barycenter();
  double dmin = 1.e22;
  for(std::set<MVertex *, MVertexPtrLessThan>::iterator it = boundaries.begin();
      it != boundaries.end(); ++it) {
    SPoint3 pp((*it)->x(), (*it)->y(), (*it)->z());
    double d = p.distance(pp);
    if(d < dmin) { dmin = d; }
  }
  return -dmin;
}

struct temp_comp {
  cross2d *cr;
  double a;
  temp_comp(MVertex *v, cross2d *c, cross2d *ref, SVector3 &n) : cr(c)
  {
    MVertex *tref =
      ref->_e.getVertex(0) == v ? ref->_e.getVertex(1) : ref->_e.getVertex(0);
    MVertex *tc =
      c->_e.getVertex(0) == v ? c->_e.getVertex(1) : c->_e.getVertex(0);

    SVector3 t1(tref->x() - v->x(), tref->y() - v->y(), tref->z() - v->z());
    SVector3 t2(tc->x() - v->x(), tc->y() - v->y(), tc->z() - v->z());
    t1.normalize();
    t2.normalize();

    double cosTheta = dot(t1, t2);
    double sinTheta;
    SVector3 cc = crossprod(t1, t2);
    if(dot(cc, n) > 0)
      sinTheta = norm(crossprod(t1, t2));
    else
      sinTheta = -norm(crossprod(t1, t2));
    a = atan2(sinTheta, cosTheta);
  }
  bool operator<(const temp_comp &other) const { return a < other.a; }
};

/*static bool isSingular(MVertex *v, std::vector<cross2d *> &adj, double &MAX)
{
  const std::size_t TEST = 0;
  if(v->getNum() == TEST) printf("VERTEX %lu\n", v->getNum());
  SVector3 n(0, 0, 0);
  for(size_t i = 0; i < adj.size(); i++) {
    if(adj[i]->inBoundary || adj[i]->inInternalBoundary) return false;
    n += adj[i]->_nrml;
  }
  n.normalize();

  std::vector<temp_comp> cc;
  for(size_t i = 0; i < adj.size(); i++) {
    cc.push_back(temp_comp(v, adj[i], adj[0], n));
  }
  std::sort(cc.begin(), cc.end());
  SVector3 ref = cc[0].cr->_tgt * cos(cc[0].cr->_atemp) +
                 cc[0].cr->_tgt2 * sin(cc[0].cr->_atemp);
  SVector3 ref0 = ref;
  for(size_t i = 1; i < cc.size() + 1; i++) {
    cross2d &c2 = *(cc[i % cc.size()].cr);
    double P = M_PI / 2;
    SVector3 d1 = c2._tgt * cos(c2._atemp) + c2._tgt2 * sin(c2._atemp);
    SVector3 d2 = c2._tgt * cos(c2._atemp + P) + c2._tgt2 * sin(c2._atemp + P);
    SVector3 d3 =
      c2._tgt * cos(c2._atemp + 2 * P) + c2._tgt2 * sin(c2._atemp + 2 * P);
    SVector3 d4 =
      c2._tgt * cos(c2._atemp + 3 * P) + c2._tgt2 * sin(c2._atemp + 3 * P);
    double D1 = dot(ref, d1);
    double D2 = dot(ref, d2);
    double D3 = dot(ref, d3);
    double D4 = dot(ref, d4);
    if(D1 > D2 && D1 > D3 && D1 > D4)
      ref = d1;
    else if(D2 > D1 && D2 > D3 && D2 > D4)
      ref = d2;
    else if(D3 > D1 && D3 > D2 && D3 > D4)
      ref = d3;
    else
      ref = d4;
  }

  if(v->getNum() == TEST)
    printf("VERTEX %lu %12.5E %12.5E %12.5E\n", v->getNum(), n.x(), n.y(),
           n.z());
  SVector3 t0, b0;
  std::vector<double> angles;
  for(size_t i = 0; i < adj.size(); i++) {
    if(i == 0) {
      SVector3 t =
        (adj[i]->_e.getVertex(0) == v) ? -adj[i]->_tgt : adj[i]->_tgt;
      t -= n * (dot(t, n));
      t.normalize();
      SVector3 b = crossprod(n, t);
      b0 = b;
      t0 = t;
    }

    SVector3 repr = adj[i]->o_i - n * dot(adj[i]->o_i, n);
    repr.normalize();
    // t * dot (,adj[i]->_tgt) +
    //      b * dot (adj[i]->o_i,adj[i]->_tgt2) ;
    double angle = atan2(dot(repr, t0), dot(repr, b0));
    adj[i]->normalize(angle);
    angles.push_back(angle);
    if(v->getNum() == TEST) {
      printf("EDGE %lu %lu\n", adj[i]->_e.getVertex(0)->getNum(),
             adj[i]->_e.getVertex(1)->getNum());
      printf("o %12.5E %12.5E %12.5E\n", adj[i]->o_i.x(), adj[i]->o_i.y(),
             adj[i]->o_i.z());
      printf("ANGLE = %12.5E %12.5E\n", angle * 180 / M_PI,
             lifting(angles[0], angles[i]) * 180 / M_PI);
    }
  }

  MAX = 0;
  for(size_t i = 0; i < angles.size(); i++) {
    if(v->getNum() == TEST) printf("%12.5E ", angles[i]);
    for(size_t j = 0; j < i; j++) {
      MAX = std::max(MAX, fabs(angles[i] - lifting(angles[j], angles[i])));
    }
  }
  if(v->getNum() == TEST) printf("\n");
  if(v->getNum() == TEST)
    printf("vertex %lu %lu edges %12.5E\n", v->getNum(), adj.size(), MAX);
  //  if (MAX > .5)printf("vertex %lu %lu edges %12.5E -- new method %12.5E\n",
  //  v->getNum(), adj.size(), MAX,dot(ref,ref0));
  return MAX > .5;
}
*/
/*
void isMinMax(MVertex *v, std::vector<cross2d *> adj, dofManager<double> *dof,
              bool &isMin, bool &isMax)
{
  
  for(size_t i = 0; i < adj.size(); i++) {
    if(adj[i]->inBoundary || adj[i]->inInternalBoundary) {
      isMin = isMax = false;
      return;
    }      
  }
  double aa;
  isMin = isMax = true;
  dof->getDofValue(v, 0, 1, aa);
  for(size_t i = 0; i < adj.size(); i++) {
    double a;
    dof->getDofValue(adj[i]->_e.getVertex(0) == v ? adj[i]->_e.getVertex(1) :
                     adj[i]->_e.getVertex(0),
                     0, 1, a);
    if(a < aa) isMin = false;
    if(a > aa) isMax = false;
  }
}
*/
/*
static void
computeSingularities(std::map<MEdge, cross2d, MEdgeLessThan> &C,
                     std::set<MVertex *, MVertexPtrLessThan> &singularities,
                     std::map<MVertex *, int> &indices, dofManager<double> *dof)
{
  FILE *f_ = fopen("sing.pos", "w");
  fprintf(f_, "View \"S\"{\n");
  std::multimap<MVertex *, cross2d *, MVertexPtrLessThan> conn;
  for(std::map<MEdge, cross2d, MEdgeLessThan>::iterator it = C.begin();
      it != C.end(); ++it) {
    std::pair<MVertex *, cross2d *> p =
      std::make_pair(it->first.getVertex(0), &it->second);
    conn.insert(p);
    p = std::make_pair(it->first.getVertex(1), &it->second);
    conn.insert(p);
  }
  size_t nb_pos = 0;
  size_t nb_neg = 0;
  size_t nb_unknown = 0;
  MVertex *v = NULL;
  std::vector<cross2d *> adj;
  for(std::multimap<MVertex *, cross2d *, MVertexPtrLessThan>::iterator it =
        conn.begin();  true    ; ++it) {
    if(it != conn.end() && it->first == v) { adj.push_back(it->second); }
    else {
      double MAX;
      bool isMin = false, isMax = false;
      if (v){
        isMinMax(v, adj, dof, isMin, isMax);
      }      
      if(v && (isSingular(v, adj, MAX))  ) {
        singularities.insert(v);
        //        isMinMax(v, adj, dof, isMin, isMax);
        if(isMax)
          indices[v] = 1;
        else if(isMin)
          indices[v] = -1;
        // else
        //   printf("ERROR -- \n");

        fprintf(f_, "SP(%g,%g,%g){%d};\n", v->x(), v->y(), v->z(), indices[v]);
        if (isMax) {
          nb_pos += 1;
        } else if (isMin) {
          nb_neg += 1;
        } else {
          nb_unknown += 1;
        }
      }
      adj.clear();
      if (it != conn.end()){
        v = it->first;
        adj.push_back(it->second);
      }
    }
    if (it == conn.end())break;
  }
  fprintf(f_, "};\n");
  fclose(f_);
  printf("singularity dection: %li with positive index, %li with negative, %li unknown\n", nb_pos, nb_neg, nb_unknown);
}
*/
/*
static SVector3 computeAverage (cross2d &c0, cross2d &c1, cross2d &c2){

  double angle = c0._atemp;
  double diff;
  
  SVector3 a0 = c0._tgt * cos(angle) + c0._tgt2 * sin(angle);  
  closest (c0, c1, angle, diff);
  SVector3 a1 = c1._tgt * cos(angle) + c1._tgt2 * sin(angle);
  closest (c0, c2, angle, diff);
  SVector3 a2 = c2._tgt * cos(angle) + c2._tgt2 * sin(angle);
  
  SVector3 a = a0+a1+a2;
  a.normalize();
  return a;  
  }
*/

static bool isSingular (MVertex *v,
                        std::set<MVertex *, MVertexPtrLessThan> &singularities,
                        const std::vector<MElement *> &lt,
                        std::map<MTriangle *, SVector3> &d0,
                        std::map<MTriangle *, SVector3> &d1,
                        std::map<MVertex *, double> &K){
  std::vector<MEdge> contour;
  std::map<MFace,SVector3,MFaceLessThan> ds0;
  std::map<MFace,SVector3,MFaceLessThan> ds1;
  for (size_t i=0;i<lt.size();++i){
    ds0[lt[i]->getFace (0)] = d0[(MTriangle*)lt[i]];
    ds1[lt[i]->getFace (0)] = d1[(MTriangle*)lt[i]];
    for (size_t j=0;j<3;++j){
      MEdge e = lt[i]->getEdge (j);
      if (e.getVertex(0) !=v && e.getVertex(1) !=v)
        contour.push_back(e);
    }
  }
  std::vector<std::vector<MVertex *> > vsorted;
  SortEdgeConsecutive(contour, vsorted);
  // should be periodic
  bool periodic = false;
  if (vsorted.size() > 0) periodic = vsorted[0][0] == vsorted[0][vsorted[0].size()-1];
  if (periodic){
    vsorted[0].resize(vsorted[0].size()-1);
    double diffs = 0;
    size_t N = periodic ?  vsorted[0].size()  :  vsorted[0].size() -2;
    //    if (v->getNum() == 45)printf("NODE %lu periodic %d N %lu \n",v->getNum(),periodic, N);
    for(size_t i = 0; i < N; ++i) {
      MVertex *v0 = vsorted[0][i%vsorted[0].size()];
      MVertex *v1 = vsorted[0][(i+1)%vsorted[0].size()];
      MVertex *v2 = vsorted[0][(i+2)%vsorted[0].size()];
      MFace f0 (v0,v1,v);
      MFace f1 (v1,v2,v);
      SVector3 dir0 = ds0[f0];
      SVector3 dir1 = ds0[f1];
      double diff0 = acos( fabs(dot(dir0,dir1)));
      SVector3 pv = crossprod(dir0,dir1);
      double diff1 = acos(pv.norm());
      //      if (v->getNum() == 45)printf("(%lu %lu %lu %g,%g,%g) %g %g %g -- %g %g %g\n",v0->getNum(),v1->getNum(),v2->getNum(),
      //                                   diff0,diff1, std::min(diff0,diff1),dir0.x(),dir0.y(),dir0.z(),dir1.x(),dir1.y(),dir1.z());
      diffs += std::min(diff0,diff1);    
    }
    
    double curvature = 2*M_PI - K[v];

    
    double x = fabs (diffs - curvature);

    if (x > .95*M_PI/2) {
      //      printf("%lu %12.5E %12.5E\n",v->getNum(),diffs,2*M_PI -K[v]);
      return true;
    }
  }
  return false;
}

static void
computeSingularities(std::vector<GFace*> &f,
                     std::map<MTriangle *, SVector3> &d0,
                     std::map<MTriangle *, SVector3> &d1,
                     std::set<MVertex *, MVertexPtrLessThan> &singularities,
                     std::map<MVertex *, int> &indices,
                     std::map<MVertex *, double> &K, bool packAlgo = false)
{

  FILE *f_ = NULL;
  if(Msg::GetVerbosity() == 99) {
    f_  = fopen("sing.pos", "w");
    fprintf(f_, "View \"S\"{\n");
  }
  std::set<MVertex *, MVertexPtrLessThan> singularities2;
  
  v2t_cont adj;
  for(size_t i = 0; i < f.size(); i++) {
    buildVertexToElement(f[i]->triangles, adj);
  }

  v2t_cont::iterator it = adj.begin();
  while(it != adj.end()) {
    MVertex *v = it->first;
    const std::vector<MElement *> &lt = it->second;
    if (isSingular (v, singularities, lt, d0, d1, K)){
      singularities2.insert(v);
    }
    ++it;
  }

  std::set<MEdge,MEdgeLessThan> allEdges;
  
  for(size_t i = 0; i < f.size(); i++) {
    for (size_t j=0 ; j< f[i]->triangles.size();j++){
      MVertex *v0 = f[i]->triangles[j]->getVertex(0);
      MVertex *v1 = f[i]->triangles[j]->getVertex(1);
      MVertex *v2 = f[i]->triangles[j]->getVertex(2);
      allEdges.insert(f[i]->triangles[j]->getEdge(0));
      allEdges.insert(f[i]->triangles[j]->getEdge(1));
      allEdges.insert(f[i]->triangles[j]->getEdge(2));
      std::set<MVertex *, MVertexPtrLessThan>::iterator it0 = singularities.find(v0);
      std::set<MVertex *, MVertexPtrLessThan>::iterator it1 = singularities.find(v1);
      std::set<MVertex *, MVertexPtrLessThan>::iterator it2 = singularities.find(v2);
      bool v0_singular = it0 != singularities.end();
      bool v1_singular = it1 != singularities.end();
      bool v2_singular = it2 != singularities.end();
      if (v0_singular && v1_singular && v2_singular){
        if (singularities2.find(v0) != singularities2.end() &&
            singularities2.find(v1) == singularities2.end() &&
            singularities2.find(v2) == singularities2.end() ){
          singularities.erase(it1);
          singularities.erase(it2);
        }
        else if (singularities2.find(v1) != singularities2.end() &&
                 singularities2.find(v0) == singularities2.end() &&
                 singularities2.find(v2) == singularities2.end() ){
          singularities.erase(it0);
          singularities.erase(it2);
        }
        else if (singularities2.find(v2) != singularities2.end() &&
                 singularities2.find(v0) == singularities2.end() &&
                 singularities2.find(v1) == singularities2.end() ){
          singularities.erase(it0);
          singularities.erase(it1);
        }
        else {
	  if (packAlgo) {
	    singularities.erase(it0);
	    singularities.erase(it1);
	  }
	  else
	    Msg::Warning("triangle %d (%lu %lu %lu) is singular",f[i]->triangles[j]->getNum(),v0->getNum(),v1->getNum(),v2->getNum());
        }
      }          
    }
  }
  {
    std::set<MEdge,MEdgeLessThan>::iterator it =  allEdges.begin();
    for (;it != allEdges.end();++it){
      MVertex *v0 = it->getVertex(0);
      MVertex *v1 = it->getVertex(1);
      std::set<MVertex *, MVertexPtrLessThan>::iterator it0 = singularities.find(v0);
      std::set<MVertex *, MVertexPtrLessThan>::iterator it1 = singularities.find(v1);
      bool v0_singular = it0 != singularities.end();
      bool v1_singular = it1 != singularities.end();
      if (v0_singular && v1_singular){
        if (singularities2.find(v0) != singularities2.end() &&
            singularities2.find(v1) == singularities2.end()){
          singularities.erase(it1);
        }
        else if (singularities2.find(v1) != singularities2.end() &&
                 singularities2.find(v0) == singularities2.end() ){
          singularities.erase(it0);
        }
        else {
	  if (packAlgo) singularities.erase(it0);
	  else
	    Msg::Warning("edge (%lu %lu) is singular",v0->getNum(),v1->getNum());
        }
      }
    }
  }

  std::set<MVertex *, MVertexPtrLessThan>::iterator its = singularities.begin();
  for (; its != singularities.end(); ++its){
    MVertex *v = *its;
    if (f_)fprintf(f_, "SP(%g,%g,%g){%12.5E};\n", v->x(), v->y(), v->z(), 1.0);    
  }
  
  if (f_)fprintf(f_, "};\n");
  if (f_)fclose(f_);  
}



static void
computeSingularities(std::map<MEdge, cross2d, MEdgeLessThan> &C,
                     std::set<MVertex *, MVertexPtrLessThan> &singularities,
                     std::map<MVertex *, int> &indices,
                     std::vector<GFace*> &f,
                     std::map<MVertex *, double> &K,
                     std::map<MVertex *, double> &source)
{
  FILE *f_ = NULL;
  if(Msg::GetVerbosity() == 99) {
    f_= fopen("sing2.pos", "w");
    fprintf(f_, "View \"S\"{\n");
  }

  v2t_cont adj;
  for(size_t i = 0; i < f.size(); i++) {
    buildVertexToElement(f[i]->triangles, adj);
  }

  v2t_cont::iterator it = adj.begin();
  while(it != adj.end()) {
    MVertex *v = it->first;
    const std::vector<MElement *> &lt = it->second;
    std::vector<MEdge> contour;
    for (size_t i=0;i<lt.size();++i){
      for (size_t j=0;j<3;++j){
        MEdge e = lt[i]->getEdge (j);
        if (e.getVertex(0) !=v && e.getVertex(1) !=v)
          contour.push_back(e);
      }
    }
    std::vector<std::vector<MVertex *> > vsorted;
    SortEdgeConsecutive(contour, vsorted);

    bool periodic = false;
    if (vsorted.size() > 0) periodic = vsorted[0][0] == vsorted[0][vsorted[0].size()-1];

    if (periodic){
      vsorted[0].resize(vsorted[0].size()-1);
      double diffs_external = 0.0;
      size_t N = periodic ?  vsorted[0].size()  :  vsorted[0].size() -1;
      for(size_t i = 0; i < N; ++i) {
        MVertex *v0 = vsorted[0][i%vsorted[0].size()];
        MVertex *v1 = vsorted[0][(i+1)%vsorted[0].size()];
        MVertex *v2 = vsorted[0][(i+2)%vsorted[0].size()];
        MEdge e01 (v0,v1);
        MEdge e12 (v1,v2);
        MEdge e1 (v,v1);
        SVector3 v_01 (v1->x()-v0->x(),v1->y()-v0->y(),v1->z()-v0->z());
        SVector3 v_12 (v2->x()-v1->x(),v2->y()-v1->y(),v2->z()-v1->z());
        SVector3 nn = crossprod(v_12,v_01);
        std::map<MEdge, cross2d, MEdgeLessThan>::iterator it01 = C.find(e01);
        std::map<MEdge, cross2d, MEdgeLessThan>::iterator it12 = C.find(e12);
        std::map<MEdge, cross2d, MEdgeLessThan>::iterator it1 = C.find(e1);
        if (it01 != C.end()  && it12 != C.end() && it1 != C.end()){
          double diff=closest_diff (it1->second._nrml, it01->second, it12->second, nn);           
          diffs_external += diff;
        }
      }
      double curvature = 2*M_PI - K[v];

      source[v] = diffs_external-curvature;

      if (fabs(diffs_external/*-curvature*/) > .95*M_PI/2) {
        //	printf("%12.5E\n",diffs_external);
        if (diffs_external < 0) indices[v] = 3;
        else indices[v] = 5;
        singularities.insert(v);
      }
    }
    ++it;
  }

  if (f_){
    std::set<MVertex *, MVertexPtrLessThan>::iterator its = singularities.begin();
    for (; its != singularities.end(); ++its){
      MVertex *v = *its;
      fprintf(f_, "SP(%g,%g,%g){%12.5E};\n", v->x(), v->y(), v->z(), 0.0);    
    }
    
    fprintf(f_, "};\n");
    fclose(f_);
  }  
}



static void cutGraph(std::map<MEdge, cross2d, MEdgeLessThan> &C,
                     std::set<MEdge, MEdgeLessThan> &cutG,
                     std::set<MVertex *, MVertexPtrLessThan> &singularities,
                     std::set<MVertex *, MVertexPtrLessThan> &boundaries)
{
  std::set<MTriangle *, MElementPtrLessThan> touched;
  std::vector<cross2d *> tree;
  std::vector<MEdge> cotree;
  std::set<std::pair<double, MTriangle *> > _distances;
  {
    std::map<MEdge, cross2d, MEdgeLessThan>::iterator it = C.begin();
    for(; it != C.end(); ++it) {
      if(it->second._t.size() == 1) {
        boundaries.insert(it->first.getVertex(0));
        boundaries.insert(it->first.getVertex(1));
      }
    }
  }

  constexpr bool SHOW_SINGULARITIES = true;
  if (SHOW_SINGULARITIES) {
    for (MVertex* v: singularities) {
      SVector3 p = v->point();
      GeoLog::add({p},0.,"singularities");
    }
    GeoLog::flush();
  }

  std::set<MVertex *, MVertexPtrLessThan> _all = boundaries;
  _all.insert(singularities.begin(), singularities.end());

  FILE *fff2 = NULL;
  if(Msg::GetVerbosity() == 99) {
    fff2 = fopen("tree.pos", "w");
    fprintf(fff2, "View \"sides\"{\n");
  }
  
  MTriangle *t = (C.begin())->second._t[0];
  std::pair<double, MTriangle *> pp = std::make_pair(distance(t, _all), t);
  _distances.insert(pp);
  touched.insert(t);
  while(!_distances.empty()) {
    t = (_distances.begin()->second);
    _distances.erase(_distances.begin());

    for(int i = 0; i < 3; i++) {
      MEdge e = t->getEdge(i);
      std::map<MEdge, cross2d, MEdgeLessThan>::iterator it = C.find(e);
      for(size_t j = 0; j < it->second._t.size(); j++) {
        MTriangle *tt = it->second._t[j];
        if(touched.find(tt) == touched.end()) {
          std::pair<double, MTriangle *> pp =
            std::make_pair(distance(t, _all), tt);
          _distances.insert(pp);
          touched.insert(tt);
          tree.push_back(&it->second);
        }
      }
    }
  }

  std::sort(tree.begin(), tree.end());
  std::map<MEdge, cross2d, MEdgeLessThan>::iterator it = C.begin();
  std::map<MVertex *, std::vector<MEdge>, MVertexPtrLessThan> _graph;
  for(; it != C.end(); ++it) {
    if(!std::binary_search(tree.begin(), tree.end(), &it->second)) {
      for(int i = 0; i < 2; i++) {
        std::map<MVertex *, std::vector<MEdge>, MVertexPtrLessThan>::iterator
          it0 = _graph.find(it->first.getVertex(i));
        if(it0 == _graph.end()) {
          std::vector<MEdge> ee;
          ee.push_back(it->first);
          _graph[it->first.getVertex(i)] = ee;
        }
        else
          it0->second.push_back(it->first);
      }
      cotree.push_back(it->first);
      if (fff2)
	fprintf(fff2, "SL(%g,%g,%g,%g,%g,%g){%d,%d};\n",
		it->first.getVertex(0)->x(), it->first.getVertex(0)->y(),
		it->first.getVertex(0)->z(), it->first.getVertex(1)->x(),
		it->first.getVertex(1)->y(), it->first.getVertex(1)->z(), 1, 1);
    }
  }
  if (fff2){
    fprintf(fff2, "};\n");
    fclose(fff2);
  }

  while(1) {
    bool somethingDone = false;
    std::map<MVertex *, std::vector<MEdge> >::iterator it = _graph.begin();
    for(; it != _graph.end(); ++it) {
      if(it->second.size() == 1) {
        MVertex *v1 = it->second[0].getVertex(0);
        MVertex *v2 = it->second[0].getVertex(1);
        if(boundaries.find(it->first) == boundaries.end() &&
           singularities.find(it->first) == singularities.end()) {
          somethingDone = true;
          std::map<MVertex *, std::vector<MEdge>, MVertexPtrLessThan>::iterator
            it2 = _graph.find(v1 == it->first ? v2 : v1);
          std::vector<MEdge>::iterator position =
            std::find(it2->second.begin(), it2->second.end(), it->second[0]);
          it2->second.erase(position);
          it->second.clear();
        }
      }
    }
    if(!somethingDone) break;
  }

  FILE *fff = NULL;
  if (fff2){
    fff = fopen("cotree.pos", "w");
    fprintf(fff, "View \"sides\"{\n");
  }
  {
    std::map<MVertex *, std::vector<MEdge> >::iterator it = _graph.begin();
    for(; it != _graph.end(); ++it) {
      for(size_t i = 0; i < it->second.size(); i++) {
        MEdge e = it->second[i];
        if(boundaries.find(e.getVertex(0)) == boundaries.end() ||
           boundaries.find(e.getVertex(1)) == boundaries.end()) {
          cutG.insert(e);
        }
      }
    }
  }

  // Add internal boundaries to the cut graph
  {
    std::map<MEdge, cross2d, MEdgeLessThan>::iterator it = C.begin();
    for(; it != C.end(); ++it) {
      // FIXME!!!!!!
      if(it->second._t.size() > 1 && it->second.inInternalBoundary) {
        cutG.insert(it->second._e);
      }
    }
  }

  {
    std::set<MEdge, MEdgeLessThan>::iterator it = cutG.begin();
    for(; it != cutG.end(); ++it) {
      MEdge e = *it;
      if (fff)fprintf(fff, "SL(%g,%g,%g,%g,%g,%g){%d,%d};\n", e.getVertex(0)->x(),
		      e.getVertex(0)->y(), e.getVertex(0)->z(), e.getVertex(1)->x(),
		      e.getVertex(1)->y(), e.getVertex(1)->z(), 1, 1);
    }
  }
  if (fff){
    fprintf(fff, "};\n");
    fclose(fff);
  }
}

static void
groupBoundaries(GModel *gm, std::map<MEdge, cross2d, MEdgeLessThan> &C,
                std::vector<std::vector<cross2d *> > &groups,
                std::set<MVertex *, MVertexPtrLessThan> singularities,
                std::map<MVertex *, double> &gaussianCurvatures,
                std::set<MVertex *, MVertexPtrLessThan> &corners,
                bool cutGraph = false)
{
  std::set<MVertex *, MVertexPtrLessThan> cutgraph;
  std::set<MVertex *, MVertexPtrLessThan> boundaries;
  for(std::map<MEdge, cross2d, MEdgeLessThan>::iterator it = C.begin();
      it != C.end(); ++it) {
    MVertex *v0 = it->first.getVertex(0);
    MVertex *v1 = it->first.getVertex(1);
    if(it->second.inBoundary) {
      boundaries.insert(v0);
      boundaries.insert(v1);
    }
    else if(it->second.inCutGraph) {
      cutgraph.insert(v0);
      cutgraph.insert(v1);
    }
  }

  std::set<cross2d *> _all;

  std::multimap<MVertex *, cross2d *> conn;
  for(std::map<MEdge, cross2d, MEdgeLessThan>::iterator it = C.begin();
      it != C.end(); ++it) {
    std::pair<MVertex *, cross2d *> p =
      std::make_pair(it->first.getVertex(0), &it->second);
    conn.insert(p);
    p = std::make_pair(it->first.getVertex(1), &it->second);
    conn.insert(p);
  }

  for(std::set<MVertex *, MVertexPtrLessThan>::iterator it = boundaries.begin();
      it != boundaries.end(); ++it) {
    MVertex *v = *it;
    std::vector<cross2d *> bnd;
    int countCutGraph = 0;
    for(std::multimap<MVertex *, cross2d *>::iterator it2 = conn.lower_bound(v);
        it2 != conn.upper_bound(v); ++it2) {
      if(it2->second->inBoundary) { bnd.push_back(it2->second); }
      if(it2->second->inCutGraph) { countCutGraph++; }
    }
    if(bnd.size() == 2) {
      //      printf("%lu %12.5E\n",v->getNum(),gaussianCurvatures[*it]);
      double KURV = gaussianCurvatures[*it];
      if (v->onWhat()->dim() == 0){
           corners.insert(v);
      }
      if (KURV > 5*M_PI/4 || cutgraph.find(v) != cutgraph.end())
        corners.insert(v);
      if(KURV < 3*M_PI/4 || KURV > 5*M_PI/4) {
        cutgraph.insert(v);
      }
      if(countCutGraph == 1) {
        singularities.insert(v);
      }
    }
    if(bnd.size() > 2) { cutgraph.insert(v); }
  }

  std::string ss = gm->getName();
  std::string fn = cutGraph ? ss + "_groups_cg.pos" : ss + "_groups_bnd.pos";

  FILE *f = fopen(fn.c_str(), "w");

  fprintf(f, "View \" \"{\n");

  std::set<MVertex *, MVertexPtrLessThan> endPoints = singularities;
  {
    for(std::multimap<MVertex *, cross2d *>::iterator it = conn.begin();
        it != conn.end(); ++it) {
      int count = 0;
      for(std::multimap<MVertex *, cross2d *>::iterator it2 =
            conn.lower_bound(it->first);
          it2 != conn.upper_bound(it->first); ++it2) {
        if(it2->second->inCutGraph) { count++; }
      }
      if(count > 2) endPoints.insert(it->first);
    }
  }

  for(int AA = 0; AA < 4; AA++) {
    if(cutGraph) {
      for(std::set<MVertex *, MVertexPtrLessThan>::iterator it =
            endPoints.begin();
          it != endPoints.end(); ++it) {
        MVertex *v = *it;
        std::vector<cross2d *> group;
        do {
          MVertex *vnew = NULL;
          for(std::multimap<MVertex *, cross2d *>::iterator it2 =
                conn.lower_bound(v);
              it2 != conn.upper_bound(v); ++it2) {
            if((_all.find(it2->second) == _all.end()) &&
               (group.empty() || group[group.size() - 1] != it2->second) &&
               it2->second->inCutGraph) {
              group.push_back(it2->second);
              vnew = (it2->second->_e.getVertex(0) == v) ?
                       it2->second->_e.getVertex(1) :
                       it2->second->_e.getVertex(0);
              fprintf(f, "SL(%g,%g,%g,%g,%g,%g){%lu,%lu};\n",
                      it2->second->_e.getVertex(0)->x(),
                      it2->second->_e.getVertex(0)->y(),
                      it2->second->_e.getVertex(0)->z(),
                      it2->second->_e.getVertex(1)->x(),
                      it2->second->_e.getVertex(1)->y(),
                      it2->second->_e.getVertex(1)->z(), groups.size(),
                      groups.size());
              break;
            }
          }
          if(vnew == NULL) break;
          v = vnew;
        } while((boundaries.find(v) == boundaries.end()) &&
                (endPoints.find(v) == endPoints.end()));
        if(group.size()) {
          groups.push_back(group);
          _all.insert(group.begin(), group.end());
        }
      }
    }
    else {
      for(std::set<MVertex *, MVertexPtrLessThan>::iterator it =
            boundaries.begin();
          it != boundaries.end(); ++it) {
        MVertex *v = *it;
        if(cutgraph.find(v) != cutgraph.end() ||
           singularities.find(v) != singularities.end()) {
          //          printf("START POINT %lu %d %d\n",v->getNum(),cutgraph.find(v)
          //!= cutgraph.end() ,                   singularities.find(v) != singularities.end());
          std::vector<cross2d *> group;
          do {
            MVertex *vnew = NULL;
            for(std::multimap<MVertex *, cross2d *>::iterator it2 =
                  conn.lower_bound(v);
                it2 != conn.upper_bound(v); ++it2) {
              if((_all.find(it2->second) == _all.end()) &&
                 (group.empty() || group[group.size() - 1] != it2->second) &&
                 (it2->second->inBoundary)) {
                group.push_back(it2->second);
                vnew = (it2->second->_e.getVertex(0) == v) ?
                         it2->second->_e.getVertex(1) :
                         it2->second->_e.getVertex(0);
                //                printf("EDGE %lu %lu (%d)\n",
                //                       it2->second->_e.getVertex(0)->getNum(),it2->second->_e.getVertex(1)->getNum(),
                //                       singularities.find(v) == singularities.end());
                //            printf("v %lu EDGE %lu
                //%lu\n",v->getNum(),it2->second->_e.getVertex(0)->getNum(),it2->second->_e.getVertex(1)->getNum());
                fprintf(f, "SL(%g,%g,%g,%g,%g,%g){%lu,%lu};\n",
                        it2->second->_e.getVertex(0)->x(),
                        it2->second->_e.getVertex(0)->y(),
                        it2->second->_e.getVertex(0)->z(),
                        it2->second->_e.getVertex(1)->x(),
                        it2->second->_e.getVertex(1)->y(),
                        it2->second->_e.getVertex(1)->z(), groups.size(),
                        groups.size());
                break;
              }
            }
            if(vnew == NULL) break;
            v = vnew;
            //            printf("NEXT POINT %lu %d
            //%lu\n",v->getNum(),singularities.find(v) == singularities.end(),
            //                   singularities.size());
          } while(cutgraph.find(v) == cutgraph.end() &&
                  singularities.find(v) == singularities.end());
          if(group.size() && _all.find(group[0]) == _all.end()) {
            groups.push_back(group);
            _all.insert(group.begin(), group.end());
          }
        }
      }
    }
  }
  fprintf(f, "};\n");
  fclose(f);
}

static void
fastImplementationExtrinsic(std::map<MEdge, cross2d, MEdgeLessThan> &C,
                            double tol = 1.e-10)
{
  double *data = new double[C.size() * 6];
  size_t *graph = new size_t[C.size() * 4];
  std::map<MEdge, cross2d, MEdgeLessThan>::iterator it = C.begin();
  int counter = 0;

  for(; it != C.end(); ++it) {
    data[6 * counter + 0] = it->second.o_i.x();
    data[6 * counter + 1] = it->second.o_i.y();
    data[6 * counter + 2] = it->second.o_i.z();
    data[6 * counter + 3] = it->second._nrml.x();
    data[6 * counter + 4] = it->second._nrml.y();
    data[6 * counter + 5] = it->second._nrml.z();
    it->second.counter = counter;
    ++counter;
  }

  it = C.begin();
  counter = 0;
  for(; it != C.end(); ++it) {
    graph[4 * counter + 0] = graph[4 * counter + 1] = graph[4 * counter + 2] =
      graph[4 * counter + 3] = it->second.counter;
    for(size_t i = 0; i < it->second._cneighbors.size(); i++) {
      graph[4 * counter + i] = it->second._cneighbors[i]->counter;
    }
    if(it->second.inBoundary || it->second.inInternalBoundary) {
      graph[4 * counter + 2] = graph[4 * counter + 3] = it->second.counter;
    }

    counter++;
  }

  size_t N = C.size();
  int MAXITER = 10000;
  int ITER = -1;
  while(ITER++ < MAXITER) {
    double x[3], y[3];
    double RES = 0;
    for(size_t i = 0; i < N; i++) {
      double *r = &data[6 * i + 0];
      double *n = &data[6 * i + 3];
      SVector3 ro(r[0], r[1], r[2]);
      size_t *neigh = &graph[4 * i];
      double weight = 0;
      if(neigh[2] != neigh[3]) {
        for(int j = 0; j < 4; j++) {
          size_t k = neigh[j];
          const double *r2 = &data[6 * k + 0];
          const double *n2 = &data[6 * k + 3];
          compat_orientation_extrinsic(r, n, r2, n2, x, y);
          r[0] = x[0] * weight + y[0];
          r[1] = x[1] * weight + y[1];
          r[2] = x[2] * weight + y[2];
          const double dd = r[0] * n[0] + r[1] * n[1] + r[2] * n[2];
          r[0] -= n[0] * dd;
          r[1] -= n[1] * dd;
          r[2] -= n[2] * dd;
          double NRM = sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
          if(NRM != 0.0) {
            r[0] /= NRM;
            r[1] /= NRM;
            r[2] /= NRM;
          }
          weight += 1;
        }
        double dp = r[0] * ro[0] + r[1] * ro[1] + r[2] * ro[2];
        RES += std::min(1. - fabs(dp), fabs(dp));
      }
      //      data[6*i+0]=r[0];
      //      data[6*i+1]=r[1];
      //      data[6*i+2]=r[2];
    }
    if(ITER % 1000 == 0)
      Msg::Info("NL smooth : iter %6d RES = %12.5E", ITER, RES);
    if(RES < tol) break;
  }

  it = C.begin();
  for(; it != C.end(); ++it) {
    counter = it->second.counter;
    it->second.o_i[0] = data[6 * counter + 0];
    it->second.o_i[1] = data[6 * counter + 1];
    it->second.o_i[2] = data[6 * counter + 2];
  }
  delete[] data;
  delete[] graph;
}

// static dofManager<double> *computeH(GModel *gm, std::vector<GFace *> &f,
//                                     std::set<MVertex *, MVertexPtrLessThan> &vs,
// 				    std::map<MVertex *, double> &source)
// {
// #if defined(HAVE_PETSC)
//   linearSystemPETSc<double> *_lsys = new linearSystemPETSc<double>;
// #elif defined(HAVE_MUMPS)
//   linearSystemMUMPS<double> *_lsys = new linearSystemMUMPS<double>;
// #else
//   linearSystemFull<double> *_lsys = new linearSystemFull<double>;
// #endif
// 
//   dofManager<double> *myAssembler = new dofManager<double>(_lsys);
// 
//   //  myAssembler.fixVertex(*vs.begin(), 0, 1, 0);
//   for(std::set<MVertex *, MVertexPtrLessThan>::iterator it = vs.begin();
//       it != vs.end(); ++it)
//     myAssembler->numberVertex(*it, 0, 1);
// 
// 
//   simpleFunction<double> ONE(1.0);
//   laplaceTerm l(0, 1, &ONE);
// 
//   for(size_t i = 0; i < f.size(); i++) {
//     for(size_t j = 0; j < f[i]->triangles.size(); j++) {
//       MTriangle *t = f[i]->triangles[j];
// 
//       SElement se(t);
//       l.addToMatrix(*myAssembler, &se);
//     }
//   }
//   // to do !!!
//   return myAssembler;
// }

static dofManager<double> *computeH(GModel *gm, std::vector<GFace *> &f,
                                    std::set<MVertex *, MVertexPtrLessThan> &vs,
                                    std::map<MEdge, cross2d, MEdgeLessThan> &C)
{
#if defined(HAVE_PETSC)
  linearSystemPETSc<double> *_lsys = new linearSystemPETSc<double>;
#elif defined(HAVE_MUMPS)
  linearSystemMUMPS<double> *_lsys = new linearSystemMUMPS<double>;
#else
  linearSystemFull<double> *_lsys = new linearSystemFull<double>;
#endif

  dofManager<double> *myAssembler = new dofManager<double>(_lsys);

  //  myAssembler.fixVertex(*vs.begin(), 0, 1, 0);
  for(std::set<MVertex *, MVertexPtrLessThan>::iterator it = vs.begin();
      it != vs.end(); ++it)
    myAssembler->numberVertex(*it, 0, 1);

  simpleFunction<double> ONE(1.0);
  laplaceTerm l(0, 1, &ONE);

  std::map<MTriangle *, SVector3> gradients_of_theta;

  for(size_t i = 0; i < f.size(); i++) {
    for(size_t j = 0; j < f[i]->triangles.size(); j++) {
      MTriangle *t = f[i]->triangles[j];

      SVector3 v10(t->getVertex(1)->x() - t->getVertex(0)->x(),
                   t->getVertex(1)->y() - t->getVertex(0)->y(),
                   t->getVertex(1)->z() - t->getVertex(0)->z());
      SVector3 v20(t->getVertex(2)->x() - t->getVertex(0)->x(),
                   t->getVertex(2)->y() - t->getVertex(0)->y(),
                   t->getVertex(2)->z() - t->getVertex(0)->z());
      SVector3 normal_to_triangle = crossprod(v20, v10);
      normal_to_triangle.normalize();

      SElement se(t);
      l.addToMatrix(*myAssembler, &se);
      MEdge e0 = t->getEdge(0);
      MEdge e1 = t->getEdge(1);
      MEdge e2 = t->getEdge(2);
      std::map<MEdge, cross2d, MEdgeLessThan>::iterator it0 = C.find(e0);
      std::map<MEdge, cross2d, MEdgeLessThan>::iterator it1 = C.find(e1);
      std::map<MEdge, cross2d, MEdgeLessThan>::iterator it2 = C.find(e2);

      SVector3 x0, x1, x2, x3;
      SVector3 t_i = crossprod(normal_to_triangle, it0->second._tgt);
      t_i -= normal_to_triangle * dot(normal_to_triangle, t_i);
      t_i.normalize();

      SVector3 o_i = it0->second.o_i;
      o_i -= normal_to_triangle * dot(normal_to_triangle, o_i);
      o_i.normalize();
      SVector3 o_1 = it1->second.o_i;
      o_1 -= normal_to_triangle * dot(normal_to_triangle, o_1);
      o_1.normalize();
      SVector3 o_2 = it2->second.o_i;
      o_2 -= normal_to_triangle * dot(normal_to_triangle, o_2);
      o_2.normalize();

      compat_orientation_extrinsic(o_i, normal_to_triangle, o_1, normal_to_triangle, x0, x1);
      compat_orientation_extrinsic(o_i, normal_to_triangle, o_2, normal_to_triangle, x2, x3);
      
      double a0 = atan2(dot(t_i, o_i), dot(it0->second._tgt, o_i));

      x0 -= normal_to_triangle * dot(normal_to_triangle, x0);
      x0.normalize();
      x1 -= normal_to_triangle * dot(normal_to_triangle, x1);
      x1.normalize();
      x2 -= normal_to_triangle * dot(normal_to_triangle, x2);
      x2.normalize();
      x3 -= normal_to_triangle * dot(normal_to_triangle, x3);
      x3.normalize();

      it0->second.normalize(a0);
      it0->second._a = a0;
      double A1 = atan2(dot(t_i, x1), dot(it0->second._tgt, x1));
      double A2 = atan2(dot(t_i, x3), dot(it0->second._tgt, x3));
      it0->second.normalize(A1);
      double a1 = it0->second.lifting(A1);
      it0->second.normalize(A2);
      double a2 = it0->second.lifting(A2);

      double a[3] = {a0 + a2 - a1, a0 + a1 - a2, a1 + a2 - a0};
      double g[3] = {0, 0, 0};
      t->interpolateGrad(a, 0, 0, 0, g);
      gradients_of_theta[t] = SVector3(g[0], g[1], g[2]);
      SPoint3 pp = t->barycenter();

      SVector3 G(g[0], g[1], g[2]);
      SVector3 GT = crossprod(G, normal_to_triangle);

      double g1[3];
      a[0] = 1;
      a[1] = 0;
      a[2] = 0;
      t->interpolateGrad(a, 0, 0, 0, g1);
      double RHS1 = g1[0] * GT.x() + g1[1] * GT.y() + g1[2] * GT.z();
      a[0] = 0;
      a[1] = 1;
      a[2] = 0;
      t->interpolateGrad(a, 0, 0, 0, g1);
      double RHS2 = g1[0] * GT.x() + g1[1] * GT.y() + g1[2] * GT.z();
      a[0] = 0;
      a[1] = 0;
      a[2] = 1;
      t->interpolateGrad(a, 0, 0, 0, g1);
      double RHS3 = g1[0] * GT.x() + g1[1] * GT.y() + g1[2] * GT.z();
      int num1 = myAssembler->getDofNumber(l.getLocalDofR(&se, 0));
      int num2 = myAssembler->getDofNumber(l.getLocalDofR(&se, 1));
      int num3 = myAssembler->getDofNumber(l.getLocalDofR(&se, 2));
      double V = -t->getVolume();
      _lsys->addToRightHandSide(num1, RHS1 * V);
      _lsys->addToRightHandSide(num2, RHS2 * V);
      _lsys->addToRightHandSide(num3, RHS3 * V);
    }
  }
  _lsys->systemSolve();
  Msg::Info("Conformal Factor Computed (%d unknowns)",
            myAssembler->sizeOfR());
  
  return myAssembler;
}

static double coord1d(double a0, double a1, double a)
{
  if(a1 == a0) return 0.0;
  return (a - a0) / (a1 - a0);
}

struct edgeCuts {
  std::vector<SPoint3> ps;
  std::vector<MVertex *> vs;
  std::vector<int> indexOfCuts;
  std::vector<int> idsOfCuts;
  std::vector<int> p_occur;
  int n_occur;
  bool add(const SPoint3 &p, int ind, int id, double eps)
  {
    for(size_t i = 0; i < ps.size(); i++) {
      SVector3 v(ps[i], p);
      if(v.norm() < eps) {
        p_occur [i] ++;
        return false;
      }
    }
    ps.push_back(p);
    indexOfCuts.push_back(ind);
    idsOfCuts.push_back(id);
    p_occur.push_back(1);
    return true;
  }
  void finish(GModel *gm, GEdge *mother, MVertex *v0, MVertex *v1, FILE *f)
  {
    double x0=0,x1=0;
    if (mother){

      reparamMeshVertexOnEdge (v0,mother,x0);
      reparamMeshVertexOnEdge (v1,mother,x1);

      if(mother->periodic(0) && mother->getEndVertex() &&
         mother->getEndVertex()->getNumMeshVertices() > 0 &&
         v0 == mother->getEndVertex()->mesh_vertices[0]){
        double u0 = mother->parBounds(0).low();
        double u1 = mother->parBounds(0).high();
        if (fabs (u0 - x1) > fabs (u1 - x1))x0 = u1;
        else x0 = u0;
      }
      if(mother->periodic(0) && mother->getEndVertex() &&
         mother->getEndVertex()->getNumMeshVertices() > 0 &&
         v1 == mother->getEndVertex()->mesh_vertices[0]){
        double u0 = mother->parBounds(0).low();
        double u1 = mother->parBounds(0).high();
        if (fabs (u0 - x0) > fabs (u1 - x0))x1 = u1;
        else x1 = u0;
      }
    }

    if (v0->getNum() == 120 ||v0->getNum() == 121){
      printf("%lu\n",ps.size());
    }
    
    for(size_t i = 0; i < ps.size(); i++) {
      GEdge *ge = gm->getEdgeByTag(indexOfCuts[i]);
      if(!ge) {
        ge = new discreteEdge(gm, indexOfCuts[i]);
        gm->add(ge);
      }
      double xi = 0.0;
      if (mother){// FIXME
        //        SPoint3 p0 (v0->x(),v0->y(),v0->z());
        //        SPoint3 p1 (v1->x(),v1->y(),v1->z());
        //        double XI = p1.distance(ps[i])/p0.distance(p1);
        //        xi = x0*(1.-XI)+x1*XI;
        //        Msg::Info("CUT MESH INFO: a new point is created on model edge %d at coordinate xi = %12.5E (%12.5E  %12.5E  %12.5E)",mother->tag(),xi, xis[i], x0, x1);
        //        GPoint gp = mother->closestPoint(ps[i], xi);
        //        GPoint gp = mother->point (xi);
        //        ps[i] = SPoint3(gp.x(),gp.y(),gp.z());
      }  
      MEdgeVertex *v =
        new MEdgeVertex(ps[i].x(), ps[i].y(), ps[i].z(), ge, xi);
      if(f)
        fprintf(f, "SP(%g,%g,%g){%d};\n", ps[i].x(), ps[i].y(), ps[i].z(),
                ge->tag());
      vs.push_back(v);
      ge->mesh_vertices.push_back(v);
    }
  }
  edgeCuts() : n_occur (0){}
};

static bool addCut(const SPoint3 &p, const MEdge &e, int COUNT, int ID,
                   std::map<MEdge, edgeCuts, MEdgeLessThan> &cuts, double eps)
{
  std::map<MEdge, edgeCuts, MEdgeLessThan>::iterator itc = cuts.find(e);
  if(itc != cuts.end()) {
    if(!itc->second.add(p, COUNT, ID, eps)) return false;
    return true;
  }
  else {
    edgeCuts cc;
    if(!cc.add(p, COUNT, ID, eps)) return false;
    cuts[e] = cc;
    return true;
  }
}

/*static MVertex* inSingularZone (std::set<MVertex *, MVertexPtrLessThan> &singularities,SPoint3 &p,
                                MEdge &e, v2t_cont &adj, double &d ){
  MVertex vvv(p.x(), p.y(), p.z());
  std::set<MVertex *, MVertexPtrLessThan>::iterator it = singularities.begin();
  for ( ; it != singularities.end(); ++it){
    std::vector<MElement *> lst = adj[*it];
    for(size_t i = 0; i < lst.size(); i++) {
      for(size_t j = 0; j < 3; j++) {
        if (e == lst[i]->getEdge (j)){
          d = vvv.distance (*it);
          return *it;
        }
      }
    }
  }
  return NULL;
}
*/
static MVertex* inSingularZone (std::set<MVertex *, MVertexPtrLessThan> &singularities,
                                SPoint3 &p, double &d){
  MVertex vvv(p.x(), p.y(), p.z());
  std::set<MVertex *, MVertexPtrLessThan>::iterator it = singularities.begin();
  for ( ; it != singularities.end(); ++it){
    d = vvv.distance (*it);
    if (d < 1.e-8){
      return *it;
    }
  }
  return NULL;
}

static void computeOneIsoTillNextCutGraph(
  v2t_cont &adj,
  double VAL,
  MVertex *v0,
  MVertex *v1,
  SPoint3 &p,
  SPoint3 &pprec,
  std::map<MVertex *, double> &pot,
  std::vector< std::pair<MEdge, std::pair<std::map<MVertex *, double> *, double> > > &cutGraphEnds,
  std::map<MEdge, MEdge, MEdgeLessThan> &d1,
  FILE *f,
  int COUNT,
  std::map<MEdge, edgeCuts, MEdgeLessThan> &cuts,
  int &NB,
  cutGraphPassage &passage,
  std::vector<cutGraphPassage> &passages,
  std::set<MVertex *, MVertexPtrLessThan> &singularities,
  double xi)
{
  bool start = true;

  SBoundingBox3d bbox = GModel::current()->bounds();
  double TOLERANCE = 1.e-08*bbox.diag();

  while (1){
    MEdge e(v0, v1);  

    //// -------------- W O R K    I N    P R O G R E S S ----------------------------
    //// -----------------------------------------------------------------------------
    if (!start){    
      double d=1.e12;
      MVertex *close = inSingularZone (singularities, p, d);
      //MVertex *close = inSingularZone (singularities, p, e, adj, d);
      if (d < TOLERANCE){
        passage._type = cutGraphPassage::SING_TO_SING;
        passage.close = true;
        passage.sing_conn = close;
        passage.d=d;          
        return;
      }

      if (!passage.close){
        if (close){
          passage.pts.push_back(p);
          passage.vals.push_back (VAL);  
          passage.sing_conn = close;
          passage.close = true;
          passage.d=d;          
        }
      }
    }
    //// -----------------------------------------------------------------------------
      
    bool added = addCut(p, e, COUNT, NB, cuts, TOLERANCE);
    if(!added) {
      if (passage._type != cutGraphPassage::SING_TO_SING &&
          passage._type != cutGraphPassage::SING_TO_BDRY)          
        passage._type = cutGraphPassage::REDUNDANT;
      return;
    }    
    
    if (!passage.close){
      passage.pts.push_back(p);
      passage.vals.push_back (VAL);  
    }
    
    NB++;
    
    if(d1.find(e) != d1.end()) {
      std::pair<std::map<MVertex *, double> *, double> aa =
        std::make_pair(&pot, VAL);
      cutGraphEnds.push_back(std::make_pair(e, aa));
      if (!start) return;
    }

    start = false;
    
    std::vector<MElement *> lst = adj[v0];
    
    MVertex *vs[2] = {NULL, NULL};
    int count = 0;
    // MElement *next = NULL;
    for(size_t i = 0; i < lst.size(); i++) {
      if((lst[i]->getVertex(0) == v0 && lst[i]->getVertex(1) == v1) ||
         (lst[i]->getVertex(0) == v1 && lst[i]->getVertex(1) == v0)) {
        // next = lst[i];
        vs[count++] = lst[i]->getVertex(2);
      }
      else if((lst[i]->getVertex(0) == v0 && lst[i]->getVertex(2) == v1) ||
              (lst[i]->getVertex(0) == v1 && lst[i]->getVertex(2) == v0)) {
        // next = lst[i];
        vs[count++] = lst[i]->getVertex(1);
      }
      else if((lst[i]->getVertex(1) == v0 && lst[i]->getVertex(2) == v1) ||
              (lst[i]->getVertex(1) == v1 && lst[i]->getVertex(2) == v0)) {
        // next = lst[i];
        vs[count++] = lst[i]->getVertex(0);
      }
    }
    double U[2] = {pot[v0], pot[v1]};
    SPoint3 p0(v0->x(), v0->y(), v0->z());
    SPoint3 p1(v1->x(), v1->y(), v1->z());
    SVector3 dprec = p - pprec;

    int TOTAL = 0;
    
    for(int i = 0; i < 2; i++) {
      if(vs[i]) {
        double U2 = pot[vs[i]];
        SPoint3 ppp(vs[i]->x(), vs[i]->y(), vs[i]->z());
        if((U[0] - VAL) * (U2 - VAL) <= 0) {
          double XI = coord1d(U[0], U2, VAL);
          SPoint3 pp = p0 * (1. - XI) + ppp * XI;
          SVector3 d = pp - p;
          if (dot(dprec,d) >0){
            TOTAL++;
            fprintf(f, "SL(%g,%g,%g,%g,%g,%g){%d,%d};\n", p.x(), p.y(), p.z(),
                  pp.x(), pp.y(), pp.z(), COUNT, COUNT);
            v1 = vs[i];
            pprec = p;
            p = pp;
            xi = XI;
          }
        }
        else if((U[1] - VAL) * (U2 - VAL) <= 0) {
          double XI = coord1d(U[1], U2, VAL);
          SPoint3 pp = p1 * (1. - XI) + ppp * XI;
          SVector3 d = pp - p;
          if (dot(dprec,d) >0){
            TOTAL++;
            fprintf(f, "SL(%g,%g,%g,%g,%g,%g){%d,%d};\n", p.x(), p.y(), p.z(),
                    pp.x(), pp.y(), pp.z(), COUNT, COUNT);
            v0= v1;
            v1 = vs[i];
            pprec = p;
            p = pp;
            xi = XI;
          }
        }
        else {
          printf("strange\n");
        }
      }
    }
    if (TOTAL == 0){
      passage._type = cutGraphPassage::SING_TO_BDRY;
    }
    if (TOTAL > 1 )printf("ERRROOR %d\n",TOTAL);
  }  
  return;
}



/*
static void computeOneIsoRecur(
  MVertex *vsing,
  v2t_cont &adj,
  double VAL,
  MVertex *v0,
  MVertex *v1,
  SPoint3 &p,
  SPoint3 &pprec,
  std::map<MVertex *, double> &pot,
  std::vector< std::pair<MEdge, std::pair<std::map<MVertex *, double> *, double> > > &cutGraphEnds,
  std::map<MEdge, MEdge, MEdgeLessThan> &d1,
  std::vector<groupOfCross2d> &G,
  FILE *f,
  int COUNT,
  std::map<MEdge, edgeCuts, MEdgeLessThan> &cuts,
  int &NB,
  cutGraphPassage &passage,
  std::vector<cutGraphPassage> &passages,
  std::set<MVertex *, MVertexPtrLessThan> &singularities,
  MElement **before)
{
  MEdge e(v0, v1);
  
  MVertex vvv(p.x(), p.y(), p.z());
  //// -------------- W O R K    I N    P R O G R E S S ----------------------------
  //// -----------------------------------------------------------------------------
  {    
    std::set<MVertex *, MVertexPtrLessThan>::iterator it = singularities.begin();
    for ( ; it != singularities.end(); ++it){
      double d = vvv.distance (*it);
      if (d < 5.e-2){
        if (!passage.close && passage.length() > .2){
          passage.pts.push_back(p);
          passage.vals.push_back (VAL);  
          passage.sing_conn = *it;
          passage.close = true;
          passage.d=d;
          if (d < 1.e-10){
            printf("PERFECT CONNEXION %d\n",COUNT);
            return;
          }
        }
      }
    }
  }
  //// -----------------------------------------------------------------------------

  
  bool added = addCut(p, e, COUNT, NB, cuts);
  if(!added) {
    return;
  }

  if (!passage.close){
    passage.pts.push_back(p);
    passage.vals.push_back (VAL);  
  }
  
  NB++;

  if(d1.find(e) != d1.end()) {
    std::pair<std::map<MVertex *, double> *, double> aa =
      std::make_pair(&pot, VAL);
    cutGraphEnds.push_back(std::make_pair(e, aa));
  }
  std::vector<MElement *> lst = adj[v0];

  MVertex *vs[2] = {NULL, NULL};
  int count = 0;
  MElement *next = NULL;
  for(size_t i = 0; i < lst.size(); i++) {
    if (lst[i] != *before){
      if((lst[i]->getVertex(0) == v0 && lst[i]->getVertex(1) == v1) ||
         (lst[i]->getVertex(0) == v1 && lst[i]->getVertex(1) == v0)) {
        next = lst[i];
        vs[count++] = lst[i]->getVertex(2);
      }
      else if((lst[i]->getVertex(0) == v0 && lst[i]->getVertex(2) == v1) ||
              (lst[i]->getVertex(0) == v1 && lst[i]->getVertex(2) == v0)) {
        next = lst[i];
        vs[count++] = lst[i]->getVertex(1);
      }
      else if((lst[i]->getVertex(1) == v0 && lst[i]->getVertex(2) == v1) ||
              (lst[i]->getVertex(1) == v1 && lst[i]->getVertex(2) == v0)) {
        next = lst[i];
        vs[count++] = lst[i]->getVertex(0);
      }
          }
  }

  before = &next;
  
  double U[2] = {pot[v0], pot[v1]};
  SPoint3 p0(v0->x(), v0->y(), v0->z());
  SPoint3 p1(v1->x(), v1->y(), v1->z());
  //  SVector3 dprec = p - pprec;
  for(int i = 0; i < 2; i++) {
    if(vs[i]) {
      double U2 = pot[vs[i]];
      SPoint3 ppp(vs[i]->x(), vs[i]->y(), vs[i]->z());
      if((U[0] - VAL) * (U2 - VAL) <= 0) {
        double xi = coord1d(U[0], U2, VAL);
        SPoint3 pp = p0 * (1. - xi) + ppp * xi;
        //        SVector3 d = pp - p;
        //        if (dot(dprec,d) >0){
          fprintf(f, "SL(%g,%g,%g,%g,%g,%g){%d,%d};\n", p.x(), p.y(), p.z(),
                  pp.x(), pp.y(), pp.z(), COUNT, COUNT);
          computeOneIsoRecur(vsing, adj, VAL, v0, vs[i], pp, p, pot,
                             cutGraphEnds, d1, G, f, COUNT, cuts, NB, passage,passages,
                             singularities, before);
      }
      else if((U[1] - VAL) * (U2 - VAL) <= 0) {
        double xi = coord1d(U[1], U2, VAL);
        SPoint3 pp = p1 * (1. - xi) + ppp * xi;
        //        SVector3 d = pp - p;
        //        if (dot(dprec,d) >0){
        fprintf(f, "SL(%g,%g,%g,%g,%g,%g){%d,%d};\n", p.x(), p.y(), p.z(),
                pp.x(), pp.y(), pp.z(), COUNT, COUNT);
        computeOneIsoRecur(vsing, adj, VAL, v1, vs[i], pp, p, pot,
                           cutGraphEnds, d1, G, f, COUNT, cuts, NB, passage,passages,
                           singularities, before);
      }
      else {
        printf("strange\n");
      }
    }
  }
  return;
}
*/

static void computeOneIso(MVertex *vsing, v2t_cont &adj, double VAL,
                          MVertex *v0, MVertex *v1, SPoint3 &p,
                          std::map<MVertex *, double> *potU,
                          std::map<MVertex *, double> *potV,
                          std::map<MEdge, MEdge, MEdgeLessThan> &d1,
                          std::vector<groupOfCross2d> &G, FILE *f, int COUNT, int DIR,
                          std::map<MEdge, edgeCuts, MEdgeLessThan> &cuts,
                          std::vector<cutGraphPassage> & passages,
                          std::set<MVertex *, MVertexPtrLessThan> &singularities,
                          double xi)
{
  
  std::vector<std::pair<MEdge, std::pair<std::map<MVertex *, double> *, double> > >
    cutGraphEnds;
  int NB = 0;

  cutGraphPassage passage (COUNT , DIR, vsing);
  SPoint3 psing  (vsing->x(),vsing->y(),vsing->z());
  passage.pts.push_back (psing);  
  passage.vals.push_back (VAL);  

  fprintf(f, "SL(%g,%g,%g,%g,%g,%g){%d,%d};\n",vsing->x(),vsing->y(),vsing->z(), p.x(), p.y(), p.z(),
          COUNT, COUNT);

  
  computeOneIsoTillNextCutGraph(adj, VAL, v0, v1, p, psing, *potU, cutGraphEnds,
                                d1, f, COUNT, cuts, NB, passage, passages, singularities, xi);
  
  size_t XX = 1;

  while(!cutGraphEnds.empty()) {
    MEdge e = (*cutGraphEnds.begin()).first;

    std::map<MVertex *, double> *POT = (*cutGraphEnds.begin()).second.first;
    VAL = (*cutGraphEnds.begin()).second.second;
    xi = coord1d((*POT)[e.getVertex(0)], (*POT)[e.getVertex(1)], VAL);
    MEdge o = d1[e];
    p[0] = (1. - xi) * e.getVertex(0)->x() + xi * e.getVertex(1)->x();
    p[1] = (1. - xi) * e.getVertex(0)->y() + xi * e.getVertex(1)->y();
    p[2] = (1. - xi) * e.getVertex(0)->z() + xi * e.getVertex(1)->z();
    cutGraphEnds.erase(cutGraphEnds.begin());
    
    int ROT = 0;
    int maxCount = 0;
    int cutGraphId = -1;
    for(size_t i = 0; i < G.size(); i++) {
      int count = 0;
      count += (std::find(G[i].left.begin(), G[i].left.end(), o.getVertex(0)) !=
                    G[i].left.end() ?
                  1 :
                  0);
      count += (std::find(G[i].left.begin(), G[i].left.end(), o.getVertex(1)) !=
                    G[i].left.end() ?
                  1 :
                  0);
      count += (std::find(G[i].right.begin(), G[i].right.end(),
                          e.getVertex(0)) != G[i].right.end() ?
                  1 :
                  0);
      count += (std::find(G[i].right.begin(), G[i].right.end(),
                          e.getVertex(1)) != G[i].right.end() ?
                  1 :
                  0);
      count += (std::find(G[i].left.begin(), G[i].left.end(), e.getVertex(0)) !=
                    G[i].left.end() ?
                  1 :
                  0);
      count += (std::find(G[i].left.begin(), G[i].left.end(), e.getVertex(1)) !=
                    G[i].left.end() ?
                  1 :
                  0);
      count += (std::find(G[i].right.begin(), G[i].right.end(),
                          o.getVertex(0)) != G[i].right.end() ?
                  1 :
                  0);
      count += (std::find(G[i].right.begin(), G[i].right.end(),
                          o.getVertex(1)) != G[i].right.end() ?
                  1 :
                  0);
      if(count > maxCount) {
        maxCount = count;
        ROT = fabs(G[i].mat[0][0]) > .6 ? 0 : 1;
        cutGraphId = i;
      }
    }
    if(maxCount == 0) printf("IMPOSSIBLE\n");

    //    Msg::Info("ISO %d is reaching cut graph %d (internal %d)",COUNT,cutGraphId, G[cutGraphId].crosses[0]->inInternalBoundary); 
    
    if (G[cutGraphId].crosses[0]->inInternalBoundary){
      //      Msg::Info("ISO %d is reaching internal boundary %d",COUNT,cutGraphId); 
      break;
    }
    
    if (!passage.close) {
      passage.addPassage (POT == potU ? 0 : 1, cutGraphId);
      passage.eds.push_back(e);
      passage.pts_on_eds.push_back(p);
    }

    if(ROT) { POT = (POT == potU ? potV : potU); }
    if(distance(e.getVertex(0), o.getVertex(0)) < 1.e-10)
      VAL = (1. - xi) * (*POT)[o.getVertex(0)] + xi * (*POT)[o.getVertex(1)];
    else
      VAL = (1. - xi) * (*POT)[o.getVertex(1)] + xi * (*POT)[o.getVertex(0)];
    computeOneIsoTillNextCutGraph(adj, VAL, o.getVertex(0), o.getVertex(1), p, psing, *POT,
                                  cutGraphEnds, d1, f, COUNT, cuts, NB, passage, passages,
                                  singularities, xi);
    if(XX++ > MAX_PASSAGES) {
      passage._type = cutGraphPassage::SING_TO_NOTHING;
      break;
    }
  }

  passages.push_back(passage);

}
/*
void computeTwoVectorsOfCorner (MVertex *vsing, std::vector<MElement *> &faces,
                                SVector3 &v1, SVector3 &v2) {
  std::set<MEdge, MEdgeLessThan> eds;
  for(size_t i = 0; i < faces.size(); i++) {
    for(size_t j = 0; j < 3; j++) {
      MEdge e = faces[i]->getEdge(j);
      if (e.getVertex(0) == vsing || e.getVertex(1) == vsing){
        std::set<MEdge, MEdgeLessThan>::iterator it = eds.find(e);
        if (it == eds.end())eds.insert(e);
        else eds.erase(it);
      }
    }
  }
  if (eds.size() == 2){
    std::set<MEdge, MEdgeLessThan>::iterator it = eds.begin();
    v1 = *it; ++it;
    v2 = *it;     
  }
  else {
    Msg::Error("Not a corner %lu",eds.size());
  }
}
*/
static void computeIso(MVertex *vsing, v2t_cont &adj, double u,
                       std::map<MVertex *, double> &potU,
                       std::map<MVertex *, double> &potV, FILE *f,
                       std::map<MEdge, MEdge, MEdgeLessThan> &d1,
                       std::vector<groupOfCross2d> &G, int DIR, int &COUNT,
                       std::map<MEdge, edgeCuts, MEdgeLessThan> &cuts,
                       std::vector<cutGraphPassage> &passages,
                       std::set<MVertex *, MVertexPtrLessThan> &singularities,
                       bool corner, bool fake)
{
  
  std::vector<MElement *> faces = adj[vsing];

  /*  SVector3 v1, v2;
  if (corner)
    computeTwoVectorsOfCorner (vsing, faces,v1,v2);
  */
  for(size_t i = 0; i < faces.size(); i++) {
    MVertex *v0 = faces[i]->getVertex(0);
    MVertex *v1 = faces[i]->getVertex(1);
    MVertex *v2 = faces[i]->getVertex(2);
    double U0 = potU[v0];
    double U1 = potU[v1];
    double U2 = potU[v2];
    SPoint3 p0(v0->x(), v0->y(), v0->z());
    SPoint3 p1(v1->x(), v1->y(), v1->z());
    SPoint3 p2(v2->x(), v2->y(), v2->z());

    //    printf("%lu (%lu %lu %lu) %12.5E %12.5E %12.5E  %12.5E\n",vsing->getNum(),
    //           v0->getNum(),v1->getNum(),v2->getNum(),u,U0, U1,U2);

    
    double EPS = 1.e-8;
    if(v2 == vsing && (U0 - u) * (U1 - u) <= 0) {
      double xi = coord1d(U0, U1, u);
      if (!corner || (xi > EPS && xi < 1-EPS)){
        //        printf("%lu %12.5E %12.5E %12.5E  %12.5E\n",vsing->getNum(),xi,U0, U1,u);
        if (fake){
          COUNT++;
        }
        else {
          SPoint3 pp = p0 * (1 - xi) + p1 * xi;
          computeOneIso(vsing, adj, u, v0, v1, pp, &potU, &potV, d1, G, f, COUNT++,DIR,
                        cuts, passages, singularities, xi);
        }
      }
    }
    else if(v1 == vsing && (U0 - u) * (U2 - u) <= 0) {
      double xi = coord1d(U0, U2, u);
      if (!corner || (xi > EPS && xi < 1-EPS)){
        //        printf("%lu %12.5E %12.5E %12.5E  %12.5E\n",vsing->getNum(),xi,U0, U2, u);
        if (fake){
          COUNT++;
        }
        else {
          SPoint3 pp = p0 * (1 - xi) + p2 * xi;
          computeOneIso(vsing, adj, u, v0, v2, pp, &potU, &potV, d1, G, f, COUNT++,DIR,
                        cuts, passages, singularities, xi);
        }
      }
    }
    else if(v0 == vsing && (U1 - u) * (U2 - u) <= 0) {
      double xi = coord1d(U1, U2, u);
      if (!corner || (xi > EPS && xi < 1-EPS)){
        //        printf("%lu %12.5E %12.5E %12.5E %12.5E\n",vsing->getNum(),xi,U1, U2, u);
        if (fake){
          COUNT++;
        }
        else {
          SPoint3 pp = p1 * (1 - xi) + p2 * xi;
          computeOneIso(vsing, adj, u, v1, v2, pp, &potU, &potV, d1, G, f, COUNT++,DIR,
                        cuts, passages, singularities, xi);
        }
      }
    }
  }
}

static void computeDuplicateEdgesOnCutGraph(std::vector<groupOfCross2d> &G,
                                            std::map<MEdge, MEdge, MEdgeLessThan> &d1){
  d1.clear();
  for(size_t i = 0; i < G.size(); i++) {
    for(size_t j = 1; j < G[i].left.size(); j++) {
      MEdge l(G[i].left[j-1], G[i].left[j]);
      MEdge r(G[i].right[j-1], G[i].right[j]);
      d1[l] = r;
      d1[r] = l;
    }
  }
}

static bool computeIsos(
  GModel *gm, std::vector<GFace *> &faces,
  std::set<MVertex *, MVertexPtrLessThan> singularities,
  std::map<MEdge, cross2d, MEdgeLessThan> &C,
  std::map<MVertex *, MVertex *, MVertexPtrLessThan> &new2old,
  std::map<MEdge, MEdge, MEdgeLessThan> &duplicateEdges,
  std::vector<std::vector<cross2d *> > &groups,
  std::vector<std::vector<cross2d *> > &groups_cg,
  std::map<MVertex *, double> &potU, std::map<MVertex *, double> &potV,
  std::set<MEdge, MEdgeLessThan> &cutG, std::vector<groupOfCross2d> &G,
  std::map<MEdge, edgeCuts, MEdgeLessThan> &cuts,
  std::vector<cutGraphPassage> &passages,
  std::set<MVertex *, MVertexPtrLessThan> &corners,
  std::map<size_t,int> &COUNTS,
  std::map<MEdge, MEdge, MEdgeLessThan> &d1)                          
{
  passages.clear();
  v2t_cont adj;
  for(size_t i = 0; i < faces.size(); i++) {
    buildVertexToElement(faces[i]->triangles, adj);
  }

  {
    std::map<MVertex *, MVertex *, MVertexPtrLessThan>::iterator it =
      new2old.begin();
    for(; it != new2old.end(); ++it) {
      if(corners.find(it->second) != corners.end()) {
        corners.insert(it->first);
      }
    }

    singularities.insert(corners.begin(), corners.end());
    it = new2old.begin();
    for(; it != new2old.end(); ++it) {
      if(singularities.find(it->second) != singularities.end()) {
        singularities.insert(it->first);
      }
    }
  }

  std::set<MVertex *, MVertexPtrLessThan> boundaries;
  for(std::map<MEdge, cross2d, MEdgeLessThan>::iterator it = C.begin();
      it != C.end(); ++it) {
    MVertex *v0 = it->first.getVertex(0);
    MVertex *v1 = it->first.getVertex(1);
    if(it->second.inInternalBoundary) {
      boundaries.insert(v0);
      boundaries.insert(v1);
    }
  }
  

  std::string fn = gm->getName() + "_QLayoutResults.pos";
  FILE *f = fopen(fn.c_str(), "w");
  fprintf(f, "View\"Big Cut\"{\n");

  bool success = true;

  std::map<size_t,int> COUNTS_FAKE;
  std::set<MVertex *, MVertexPtrLessThan>::iterator it = singularities.begin();
  for(; it != singularities.end(); ++it) {
    GEntity *ge = (*it)->onWhat();
    if(ge->dim() == 2 || ge->edges().size() == 0) {
      MVertex *vvv = new2old.find(*it) == new2old.end() ?  *it : new2old[*it];
      if (COUNTS_FAKE.find(vvv->getNum()) == COUNTS_FAKE.end())COUNTS_FAKE [vvv->getNum()] = 1000 * vvv->getNum();
      int COUNT = COUNTS_FAKE [vvv->getNum()];      
      bool corner = corners.find(*it) != corners.end();
      computeIso(*it, adj, potU[*it], potU, potV, f, d1, G, 0, COUNT, cuts, passages, singularities, corner, true);
      computeIso(*it, adj, potV[*it], potV, potU, f, d1, G, 1, COUNT, cuts, passages, singularities, corner, true);
      COUNTS_FAKE [vvv->getNum()] = COUNT;      
    }
  }

  it = singularities.begin();
  for(; it != singularities.end(); ++it) {
    GEntity *ge = (*it)->onWhat();
    if(ge->dim() == 2 || ge->edges().size() == 0) {
      MVertex *vvv = new2old.find(*it) == new2old.end() ?  *it : new2old[*it];
      int COUNT_FAKE = COUNTS_FAKE[vvv->getNum()];
      bool corner = corners.find(*it) != corners.end();

      if (corner || COUNT_FAKE-vvv->getNum()*1000 != 4){      
        if (COUNTS.find(vvv->getNum()) == COUNTS.end())COUNTS [vvv->getNum()] = 1000 * vvv->getNum();
        int COUNT = COUNTS [vvv->getNum()];      
        computeIso(*it, adj, potU[*it], potU, potV, f, d1, G, 0, COUNT, cuts, passages, singularities, corner, false);
        computeIso(*it, adj, potV[*it], potV, potU, f, d1, G, 1, COUNT, cuts, passages, singularities, corner, false);
        COUNTS [vvv->getNum()] = COUNT;
      }
    }
  }

  fprintf(f, "};\n");
  fclose(f);

  return success;
}

void getAllConnectedTriangles(
  cross2d *start, std::vector<cross2d *> &group,
  std::set<MVertex *, MVertexPtrLessThan> &isolated_singularities,
  std::set<MVertex *, MVertexPtrLessThan> &all, std::set<MTriangle *> &t,
  std::set<MTriangle *> &allTrianglesConsidered)
{
  std::set<cross2d *> touched;

  //  printf("group %lu isolated singularities\n",
  //  isolated_singularities.size());

  for(size_t i = 0; i < group.size(); i++) {
    if(isolated_singularities.find(group[i]->_e.getVertex(0)) ==
       isolated_singularities.end())
      all.insert(group[i]->_e.getVertex(0));
    if(isolated_singularities.find(group[i]->_e.getVertex(1)) ==
       isolated_singularities.end())
      all.insert(group[i]->_e.getVertex(1));
  }

  if(allTrianglesConsidered.find(start->_t[0]) !=
     allTrianglesConsidered.end()) {
    if(!start->_cneighbors[0]->inCutGraph)
      start = start->_cneighbors[0];
    else if(!start->_cneighbors[1]->inCutGraph)
      start = start->_cneighbors[1];
    else
      printf("error\n");
  }
  else if(start->_cneighbors.size() == 4 &&
          allTrianglesConsidered.find(start->_t[1]) !=
            allTrianglesConsidered.end()) {
    if(start->_cneighbors.size() == 4 && !start->_cneighbors[2]->inCutGraph)
      start = start->_cneighbors[2];
    else if(start->_cneighbors.size() == 4 &&
            !start->_cneighbors[3]->inCutGraph)
      start = start->_cneighbors[3];
    else
      printf("error\n");
  }
  else {
    if(!start->_cneighbors[0]->inCutGraph)
      start = start->_cneighbors[0];
    else if(!start->_cneighbors[1]->inCutGraph)
      start = start->_cneighbors[1];
    else if(start->_cneighbors.size() == 4 &&
            !start->_cneighbors[2]->inCutGraph)
      start = start->_cneighbors[2];
    else if(start->_cneighbors.size() == 4 &&
            !start->_cneighbors[3]->inCutGraph)
      start = start->_cneighbors[3];
    else
      printf("error\n");
  }

  std::stack<cross2d *> _s;
  _s.push(start);

  while(!_s.empty()) {
    start = _s.top();
    touched.insert(start);
    _s.pop();
    for(size_t i = 0; i < start->_t.size(); i++) {
      t.insert(start->_t[i]);
      allTrianglesConsidered.insert(start->_t[i]);
    }

    for(size_t i = 0; i < start->_cneighbors.size(); i++) {
      cross2d *c = start->_cneighbors[i];
      if(!c->inCutGraph && touched.find(c) == touched.end()) {
        if(all.find(c->_e.getVertex(0)) != all.end() ||
           all.find(c->_e.getVertex(1)) != all.end()) {
          _s.push(c);
        }
      }
    }
  }
}

static bool computeLeftRight(groupOfCross2d &g, MVertex **left, MVertex **right)
{
  for(size_t i = 0; i < g.side.size(); i++) {
    if(g.side[i]->getVertex(0) == *right || g.side[i]->getVertex(1) == *right ||
       g.side[i]->getVertex(2) == *right) {
      MVertex *temp = *left;
      *left = *right;
      *right = temp;
      return true;
    }
    if(g.side[i]->getVertex(0) == *left || g.side[i]->getVertex(1) == *left ||
       g.side[i]->getVertex(2) == *left) {
      return true;
    }
  }
  return false;
}

static void createJumpyPairs(
  groupOfCross2d &g, std::set<MVertex *, MVertexPtrLessThan> &singularities,
  std::set<MVertex *, MVertexPtrLessThan> &boundaries,
  std::multimap<MVertex *, MVertex *, MVertexPtrLessThan> &old2new)
{

  std::vector<std::vector<MVertex *> > vsorted;
  std::vector<MEdge> edges;
  for(size_t i = 0; i < g.crosses.size(); ++i) {
    cross2d *c = g.crosses[i];
    edges.push_back(c->_e);
  }
  SortEdgeConsecutive(edges, vsorted);

  // PERIODIC !!!
  if (vsorted[0][0] == vsorted[0][vsorted[0].size()-1]) {
    for(size_t START = 0; START < vsorted[0].size(); ++START) {
      if (old2new.count(vsorted[0][START]) > 1){
        std::vector<MVertex *> temp;
        for(size_t i = START; i < vsorted[0].size() + START; ++i) {
          int index = i %  vsorted[0].size();
          if (index) temp.push_back( vsorted[0][index]);
        }
        temp.push_back( vsorted[0][START]);
        vsorted[0] = temp;
        break;
      }
    }
  }
  std::vector<cross2d *> ccc;
  //  printf("group %d\n",g.groupId);
  for(size_t j = 0; j < vsorted[0].size(); ++j) {
    MVertex *v0a = vsorted[0][j ? j-1 : j+1 ];
    MVertex *vv = vsorted[0][j];
    for(size_t i = 0; i < g.crosses.size(); ++i) {
      cross2d *c = g.crosses[i];
      if ( (c->_e.getVertex(0) == v0a && c->_e.getVertex(1) == vv ) ||
           (c->_e.getVertex(0) == vv && c->_e.getVertex(1) == v0a )){
        MTriangle *t1 = c->_t[0];
        MTriangle *t2 = c->_t[1];
        MVertex *v0 = NULL;
        MVertex *v1 = NULL;
        if(t1->getVertex(0) == vv || t1->getVertex(1) == vv ||
           t1->getVertex(2) == vv) {
          if(v0 == NULL)
            v0 = vv;
          else if(v1 == NULL)
            v1 = vv;
          else
            Msg::Error("error in JumpyPairs 1");
        }
        if(t2->getVertex(0) == vv || t2->getVertex(1) == vv ||
           t2->getVertex(2) == vv) {
          if(v0 == NULL)
            v0 = vv;
          else if(v1 == NULL)
            v1 = vv;
          else
            Msg::Error("error in JumpyPairs 1");
        }
        for(std::multimap<MVertex *, MVertex *>::iterator it =
              old2new.lower_bound(vv);
            it != old2new.upper_bound(vv); ++it) {
          MVertex *vvv = it->second;
          if(t1->getVertex(0) == vvv || t1->getVertex(1) == vvv ||
             t1->getVertex(2) == vvv) {
            if(v0 == NULL)
              v0 = vvv;
            else if(v1 == NULL)
              v1 = vvv;
            else
              Msg::Error("error in JumpyPairs 1");
          }
          if(t2->getVertex(0) == vvv || t2->getVertex(1) == vvv ||
             t2->getVertex(2) == vvv) {
            if(v0 == NULL)
              v0 = vvv;
            else if(v1 == NULL)
              v1 = vvv;
            else
              Msg::Error("error in JumpyPairs 2");
          }
        }
        if(!v1 || !v0) Msg::Error("error in JumpyPairs 3");
        if(computeLeftRight(g, &v0, &v1)) {
          g.left.push_back(v0);
          g.right.push_back(v1);
        }
        else
          Msg::Error("error in jumpy pairs %lu \n", vv->getNum());
      }
      else if(singularities.find(vv) != singularities.end()) {
        g.singularities.push_back(vv);
      }
    }
  }
  if (g.left[0] == g.right[0]){
    std::reverse(std::begin(g.left), std::end(g.left));
    std::reverse(std::begin(g.right), std::end(g.right));
  }
}

static void
analyzeGroup(std::vector<cross2d *> &group, groupOfCross2d &g,
             std::map<MTriangle *, SVector3> &d,
             std::map<MTriangle *, SVector3> &d2, v2t_cont &adj,
             std::set<MVertex *, MVertexPtrLessThan> &isolated_singularities,
             std::set<MVertex *, MVertexPtrLessThan> &boundaries,
             std::set<MTriangle *> &allTrianglesConsidered)
{
  g.crosses = group;
  double MAX = 0.0;
  for(size_t i = 0; i < g.crosses.size(); ++i) {
    cross2d *c = g.crosses[i];
    if(c->_t.size() == 2) {
      SVector3 t1 = d[c->_t[0]];
      SVector3 t2 = d[c->_t[1]];
      MAX = std::max(dot(t1, t2), MAX);
    }
  }
  if(MAX > .8)
    g.rot = false;
  else
    g.rot = true;
  for(size_t i = 0; i < g.crosses.size(); ++i) {
    cross2d *c = g.crosses[i];
    c->rotation = g.rot;
  }

  std::set<MTriangle *> t;
  std::set<MVertex *, MVertexPtrLessThan> all;
  getAllConnectedTriangles(group[0], group, isolated_singularities, all, t,
                           allTrianglesConsidered);
  g.side.insert(g.side.begin(), t.begin(), t.end());
  g.vertices.insert(g.vertices.begin(), all.begin(), all.end());

  // compute which rotation ...
  g.mat[0][0] = g.mat[0][1] = g.mat[1][0] = g.mat[1][1] = 0.0; 
  double div = 0.0;
  for(size_t i = 0; i < g.crosses.size(); ++i) {
    cross2d *c = g.crosses[i];
    if(c->_t.size() == 2) {
      if(t.find(c->_t[0]) != t.end()) {
        div += 1.0;
        g.mat[0][0] += dot(d[c->_t[0]], d[c->_t[1]]);
        g.mat[0][1] += dot(d[c->_t[0]], d2[c->_t[1]]);
        g.mat[1][0] += dot(d2[c->_t[0]], d[c->_t[1]]);
        g.mat[1][1] += dot(d2[c->_t[0]], d2[c->_t[1]]);
      }
      else if(t.find(c->_t[1]) != t.end()) {
        div += 1.0;
        g.mat[0][0] += dot(d[c->_t[0]], d[c->_t[1]]);
        g.mat[1][0] += dot(d[c->_t[0]], d2[c->_t[1]]);
        g.mat[0][1] += dot(d2[c->_t[0]], d[c->_t[1]]);
        g.mat[1][1] += dot(d2[c->_t[0]], d2[c->_t[1]]);
      }
    }
  }
  g.mat[0][0] /= div;
  g.mat[0][1] /= div;
  g.mat[1][0] /= div;
  g.mat[1][1] /= div;


  for(int j = 0; j < 2; j++) {
    for(int k = 0; k < 2; k++) {
      if(g.mat[j][k] > .7)
        g.mat[j][k] = 1;
      else if(g.mat[j][k] < -.7)
        g.mat[j][k] = -1;
      else
        g.mat[j][k] = 0;
    }
  }
  if (g.groupId == 62){
  }
}

///--- class containing the data
class quadLayoutData {
public:
  GModel *gm;
  std::vector<GFace *> f;
  std::map<MEdge, cross2d, MEdgeLessThan> C;
  dofManager<double> *myAssembler;
  std::set<MVertex *, MVertexPtrLessThan> vs;
  std::set<MEdge, MEdgeLessThan> cutG;
  std::set<MVertex *, MVertexPtrLessThan> singularities;
  std::map<MVertex *, int> indices;
  std::map<MVertex *, double> gaussianCurvatures;
  std::set<MVertex *, MVertexPtrLessThan> boundaries;
  std::set<MVertex *, MVertexPtrLessThan> corners;
  std::vector<std::vector<cross2d *> > groups;
  std::vector<std::vector<cross2d *> > groups_cg;
  std::map<MVertex *, MVertex *, MVertexPtrLessThan> new2old;
  std::string modelName;
  std::map<MTriangle *, SVector3> d0, d1;
  std::vector<groupOfCross2d> G;

  int loadThetaFromView(int viewTag) {
    PView* view = PView::getViewByTag(viewTag);
    if (view == NULL) {
      Msg::Error("loadThetaFromView: view %i not found", viewTag);
      return -1;
    }
    PViewDataGModel *d = dynamic_cast<PViewDataGModel *>(view->getData());
    if(!d) {
      Msg::Error("View with tag %d does not contain model data", viewTag);
      return -1;
    }
    int ntri = d->getNumTriangles();
    if (ntri == 0) {
      Msg::Error("View with tag %d does not contain triangles");
      return -1;
    }
    stepData<double> *s = d->getStepData(0);
    if(!s) {
      Msg::Error("View with tag %d does not contain model data for step %d", viewTag, 0);
      return -1;
    }
    std::vector<std::size_t> tags;
    std::vector<double> data;
    int numComponents = s->getNumComponents();
    if (numComponents != 3) {
      Msg::Error("View with tag %d does not contain model data with 3 components", viewTag);
      return -1;
    }
    int numEnt = 0;
    for(std::size_t i = 0; i < s->getNumData(); i++) {
      if(s->getData(i)) numEnt++;
    }
    if(!numEnt) {
      Msg::Error("View with tag %d does not contain entities");
      return -1;
    }
    /* Mapping from num to MTriangle*, from the model */
    std::map<std::size_t,MTriangle*> t2mt;
    for(size_t i = 0; i < f.size(); i++) {
      for(size_t j = 0; j < f[i]->triangles.size(); j++) {
        MTriangle *t = f[i]->triangles[j];
        t2mt[t->getNum()] = t;
      }
    }
    /* Assign theta value for each internal edge */
    std::map<std::pair<size_t,size_t>,double> edgeToTheta;
    for(std::size_t i = 0; i < s->getNumData(); i++) {
      double *dd = s->getData(i);
      if(dd) {
        auto it = t2mt.find(i);
        if (it == t2mt.end()) {
          Msg::Error("triangle %i not found (view has numData=%li)", i, s->getNumData());
          return -1;
        }
        MTriangle* t = it->second;
        for (size_t k = 0; k < 3; ++k) {
          MEdge edge = t->getEdge(k);
          size_t v1 = edge.getVertex(0)->getNum();
          size_t v2 = edge.getVertex(1)->getNum();
          std::pair<size_t,size_t> se = (v1 < v2) ? std::make_pair(v1,v2) : std::make_pair(v2,v1);
          if (edgeToTheta.find(se) == edgeToTheta.end()) {
            edgeToTheta[se] = (v1 < v2) ? dd[k] : -dd[k];
          }
        }
      }
    }
    /* Fill the info in quadLayoutData */
    std::map<MEdge, cross2d, MEdgeLessThan>::iterator it;
    for(it = C.begin(); it != C.end(); ++it) {
      size_t v1 = it->first.getVertex(0)->getNum();
      size_t v2 = it->first.getVertex(1)->getNum();
      std::pair<size_t,size_t> edge = std::make_pair(v1,v2);
      std::pair<size_t,size_t> sedge = (v1 < v2) ? edge : std::make_pair(v2,v1);
      std::map<std::pair<size_t,size_t>,double>::iterator itr = edgeToTheta.find(sedge);
      double A = 0;
      if (itr == edgeToTheta.end()) {
        Msg::Error("Edge (%i,%i) not found in result", edge.first, edge.second);
        return -1;
      } else{
        if (edge == sedge) {
          A = itr->second;
        } else {
          A = -itr->second;
        }
      }
      it->second._a = it->second._atemp = A;
      it->second.o_i = it->second._tgt * cos(it->second._atemp) + it->second._tgt2 * sin(it->second._atemp);
      it->second.o_i.normalize();
      it->second._btemp = itr->second;
    }
    Msg::Info("cross field loaded from view 'theta'");

    return 0;
  }

  int loadHFromViewViaProbing(int viewTag) {
    Msg::Error("impossible: cannot set value in myAssembler");
    return -1;

    // PView* view = PView::getViewByTag(viewTag);
    // if (view == NULL) {
    //   Msg::Error("loadHFromViewViaProbing: view %i not found", viewTag);
    //   return -1;
    // }
    // PViewData *vhd = view->getData();
    // if (view == NULL) {
    //   Msg::Info("loadHFromViewViaProbing: view has no data");
    //   return -1;
    // }

    // for(std::set<MVertex *, MVertexPtrLessThan>::iterator it = vs.begin(); it != vs.end(); ++it){
    //   SVector3 pt = (*it)->point();
    //   double val = 0.;
    //   double *qx = 0, *qy = 0, *qz = 0;
    //   int qn = 0;
    //   bool gradient = false;
    //   double tolerance = 0.;
    //   bool found = vhd->searchScalarWithTol(pt.x(), pt.y(), pt.z(), &val, 0, 0, tolerance, qn,
    //       qx, qy, qz, gradient);
    //   if (found) {
    //     // now what ?
    //   }
    // }

    return 0;
  }

  void printTheta(const char *name)
  {
    std::string fn = modelName + "_" + name + ".pos";
    FILE *of = fopen(fn.c_str(), "w");
    fprintf(of, "View \"Theta\"{\n");
    for(size_t i = 0; i < f.size(); i++) {
      for(size_t j = 0; j < f[i]->triangles.size(); j++) {
        MTriangle *t = f[i]->triangles[j];
        std::map<MEdge, cross2d, MEdgeLessThan>::iterator it0 =
          C.find(t->getEdge(0));
        std::map<MEdge, cross2d, MEdgeLessThan>::iterator it1 =
          C.find(t->getEdge(1));
        std::map<MEdge, cross2d, MEdgeLessThan>::iterator it2 =
          C.find(t->getEdge(2));

        SVector3 d0 = it0->second.o_i;
        SVector3 d1 = it1->second.o_i;
        SVector3 d2 = it2->second.o_i;
        double a = atan2(d0.y(), d0.x());
        double b = atan2(d1.y(), d1.x());
        double c = atan2(d2.y(), d2.x());
        it0->second.normalize(a);
        it0->second.normalize(b);
        it0->second.normalize(c);
        double A = c + a - b;
        double B = a + b - c;
        double C = b + c - a;
        it0->second.normalize(A);
        it0->second.normalize(B);
        it0->second.normalize(C);
        fprintf(of, "ST(%g,%g,%g,%g,%g,%g,%g,%g,%g){%g,%g,%g};\n",
                t->getVertex(0)->x(), t->getVertex(0)->y(),
                t->getVertex(0)->z(), t->getVertex(1)->x(),
                t->getVertex(1)->y(), t->getVertex(1)->z(),
                t->getVertex(2)->x(), t->getVertex(2)->y(),
                t->getVertex(2)->z(), A, B, C);
      }
    }
    fprintf(of, "};\n");
    fclose(of);
  }

  void printCross(const char *name)
  {
    std::string fn = modelName + "_" + name + ".pos";
    FILE *of = fopen(fn.c_str(), "w");
    fprintf(of, "View \"Direction fields\"{\n");
    std::map<MEdge, cross2d, MEdgeLessThan>::iterator it = C.begin();
    for(it = C.begin(); it != C.end(); ++it) {
      double a0 = it->second._a;
      MEdge e0 = it->second._e;
      SVector3 d1 = (it->second._tgt * cos(a0) + it->second._tgt2 * sin(a0));
      SVector3 d2 = (it->second._tgt * (-sin(a0)) + it->second._tgt2 * cos(a0));
      SVector3 d3 = (it->second._tgt * (-cos(a0)) - it->second._tgt2 * sin(a0));
      SVector3 d4 = (it->second._tgt * sin(a0) - it->second._tgt2 * cos(a0));

      for(size_t I = 0; I < it->second._t.size(); I++) {
        fprintf(of, "VP(%g,%g,%g){%g,%g,%g};\n",
                0.5 * (e0.getVertex(0)->x() + e0.getVertex(1)->x()),
                0.5 * (e0.getVertex(0)->y() + e0.getVertex(1)->y()),
                0.5 * (e0.getVertex(0)->z() + e0.getVertex(1)->z()), d1.x(),
                d1.y(), d1.z());
        fprintf(of, "VP(%g,%g,%g){%g,%g,%g};\n",
                0.5 * (e0.getVertex(0)->x() + e0.getVertex(1)->x()),
                0.5 * (e0.getVertex(0)->y() + e0.getVertex(1)->y()),
                0.5 * (e0.getVertex(0)->z() + e0.getVertex(1)->z()), d2.x(),
                d2.y(), d2.z());
        fprintf(of, "VP(%g,%g,%g){%g,%g,%g};\n",
                0.5 * (e0.getVertex(0)->x() + e0.getVertex(1)->x()),
                0.5 * (e0.getVertex(0)->y() + e0.getVertex(1)->y()),
                0.5 * (e0.getVertex(0)->z() + e0.getVertex(1)->z()), d3.x(),
                d3.y(), d3.z());
        fprintf(of, "VP(%g,%g,%g){%g,%g,%g};\n",
                0.5 * (e0.getVertex(0)->x() + e0.getVertex(1)->x()),
                0.5 * (e0.getVertex(0)->y() + e0.getVertex(1)->y()),
                0.5 * (e0.getVertex(0)->z() + e0.getVertex(1)->z()), d4.x(),
                d4.y(), d4.z());
      }
    }
    fprintf(of, "};\n");
    fclose(of);
  }

  int computeCrossFieldExtrinsic(double tol, size_t nIterLaplace = 2000)
  {
    std::map<MEdge, cross2d, MEdgeLessThan>::iterator it;
    std::vector<cross2d *> pc;
    for(it = C.begin(); it != C.end(); ++it) pc.push_back(&(it->second));

    size_t ITER = 0;
    while(ITER++ < nIterLaplace) {
      if(ITER % 200 == 0) std::random_shuffle(pc.begin(), pc.end());
      for(size_t i = 0; i < pc.size(); i++) pc[i]->average_init();
      if(ITER % 1000 == 0) Msg::Info("Linear smooth : iter %6lu", ITER);
    }

    for(size_t i = 0; i < pc.size(); i++) pc[i]->computeVector();

    fastImplementationExtrinsic(C, tol);

    for(size_t i = 0; i < pc.size(); i++) pc[i]->computeAngle();

    return 0;
  }

  void printScalar(dofManager<double> *dof, char c)
  {
    std::string fn = modelName + "_" + c + ".pos";

    FILE *_f = fopen(fn.c_str(), "w");
    fprintf(_f, "View \"H\"{\n");

    for(size_t i = 0; i < f.size(); i++) {
      for(size_t j = 0; j < f[i]->triangles.size(); j++) {
        MTriangle *t = f[i]->triangles[j];
        double a, b, c;
        dof->getDofValue(t->getVertex(0), 0, 1, a);
        dof->getDofValue(t->getVertex(1), 0, 1, b);
        dof->getDofValue(t->getVertex(2), 0, 1, c);
        fprintf(_f, "ST(%g,%g,%g,%g,%g,%g,%g,%g,%g){%g,%g,%g};\n",
                t->getVertex(0)->x(), t->getVertex(0)->y(),
                t->getVertex(0)->z(), t->getVertex(1)->x(),
                t->getVertex(1)->y(), t->getVertex(1)->z(),
                t->getVertex(2)->x(), t->getVertex(2)->y(),
                t->getVertex(2)->z(), a, b, c);
      }
    }
    fprintf(_f, "};\n");
    fclose(_f);
  }


  int computeCrossFieldAndH()
  {
#if defined(HAVE_QUADMESHINGTOOLS)
    /* Cross field parameters */
    int nb_diffusion_levels = 10;
    int bc_expansion_layers = 1;

    int cf_tag = -1;
    PView* theta = PView::getViewByName("theta");
    if (theta) cf_tag = theta->getTag();


    std::map<MEdge, cross2d, MEdgeLessThan>::iterator it;
    std::vector<cross2d *> pc;
    for(it = C.begin(); it != C.end(); ++it) pc.push_back(&(it->second));
    bool OLD = false;
    if (OLD) {
      std::map<std::pair<size_t,size_t>,double> edge_to_angle;
      bool okcf = QMT::compute_cross_field_with_heat(gm->getName(),cf_tag,nb_diffusion_levels,&edge_to_angle, bc_expansion_layers);
      if (!okcf) {
        Msg::Error("Failed to compute cross field");
        return -1;
      }

      for(it = C.begin(); it != C.end(); ++it) {
        std::pair<size_t,size_t> edge = std::make_pair(it->first.getMinVertex()->getNum(),
            it->first.getMaxVertex()->getNum());
        if (edge.first == edge.second) continue;
        std::map<std::pair<size_t,size_t>,double>::iterator itr = edge_to_angle.find(edge);
        double A = 0;
        if (itr == edge_to_angle.end()) {
          Msg::Error("Edge (%i,%i) not found in result", edge.first, edge.second);
          abort();
          // TODO debug this error when starting from .geo input
          return -1;
        }
        else{
          A = itr->second;
        }
        it->second._a = it->second._atemp = A;
        it->second.o_i = it->second._tgt * cos(it->second._atemp) + it->second._tgt2 * sin(it->second._atemp);
        it->second.o_i.normalize();
        it->second._btemp = itr->second;
      }
      printCross("test.pos");
    } else {
      std::vector<GFace *> faces;
      getFacesOfTheModel(gm, faces);
      std::vector<std::array<double,3> > points;
      std::vector<std::array<size_t,2> > lines;
      std::vector<std::array<size_t,3> > triangles;
      std::vector<MVertex*> origin;
      extractTriangularMeshFromFaces(f, points, origin, lines, triangles);

      std::map<std::array<size_t,2>,double> edgeThetaLocal;
      double thresholdNormConvergence = 1.e-2;
      bool okcf = QMT::compute_cross_field_with_multilevel_diffusion(
          points,lines,triangles,edgeThetaLocal,nb_diffusion_levels,
          thresholdNormConvergence, bc_expansion_layers);
      if (!okcf) {
        Msg::Error("Failed to compute cross field");
        return -1;
      }
      std::map<std::array<size_t,2>,double> edgeTheta;
      for (const auto& kv: edgeThetaLocal) {
        std::array<size_t,2> vPairGlobal = {origin[kv.first[0]]->getNum(),origin[kv.first[1]]->getNum()};
        if (vPairGlobal[1] < vPairGlobal[0]) {
          std::sort(vPairGlobal.begin(),vPairGlobal.end());
        }
        edgeTheta[vPairGlobal] = kv.second;
      }

      for(it = C.begin(); it != C.end(); ++it) {
        std::array<size_t,2> edge = {it->first.getMinVertex()->getNum(),
            it->first.getMaxVertex()->getNum()};
        if (edge[0] == edge[1]) continue;
        std::map<std::array<size_t,2>,double>::iterator itr = edgeTheta.find(edge);
        double A = 0;
        if (itr == edgeTheta.end()) {
          Msg::Error("Edge (%i,%i) not found in result", edge[0], edge[1]);
          return -1;
        } else{
          A = itr->second;
        }
        it->second._a = it->second._atemp = A;
        it->second.o_i = it->second._tgt * cos(it->second._atemp) + it->second._tgt2 * sin(it->second._atemp);
        it->second.o_i.normalize();
        it->second._btemp = itr->second;
      }
    }

#else
    computeCrossFieldExtrinsic(1.e-9);
#endif
    std::map<MVertex*, double> source;	     
    computeSingularities(C, singularities, indices,f,gaussianCurvatures, source);
    computeUniqueVectorPerTriangle(gm, f, C, d0, d1);
    computeSingularities(f,d0, d1, singularities, indices,gaussianCurvatures);
    d0.clear();
    myAssembler = computeH(gm, f, vs, C);
#if 0
    myAssembler = computeHFromSingularities(indices,  4);
#endif

    
#if defined(HAVE_QUADMESHINGTOOLS)
    bool SHOW_H = false;
    if (SHOW_H) {
      std::string name = gm->getName() + "_H";
      PViewDataGModel *d = new PViewDataGModel;
      d->setName(name);
      d->setFileName(name + ".msh");
      std::map<int, std::vector<double> > dataH;
      for(std::set<MVertex *, MVertexPtrLessThan>::iterator it = vs.begin(); it != vs.end(); ++it) {
        double h;
        myAssembler->getDofValue(*it, 0, 1, h);
        std::vector<double> jj;
        jj.push_back(h);
        dataH[(*it)->getNum()] = jj;
      }
      d->addData(gm, dataH, 0, 0.0, 1, 1);
      d->finalize();
      std::string posout = "/tmp/H.pos";
      d->writePOS(posout, false, true, false);
      GmshMergeFile(posout);
      return -1;
    }
#endif

    return 1;
  }

  dofManager<double> *computeHFromSingularities(std::map<MVertex *, int> &sing,
                                                int nbTurns)
  {
#if defined(HAVE_SOLVER)
#if defined(HAVE_PETSC)
    linearSystemPETSc<double> *_lsys = new linearSystemPETSc<double>;
#elif defined(HAVE_MUMPS)
  linearSystemMUMPS<double> *_lsys = new linearSystemMUMPS<double>;
#else
    linearSystemFull<double> *_lsys = new linearSystemFull<double>;
#endif
#endif

    dofManager<double> *dof = new dofManager<double>(_lsys);

    std::map<MEdge, cross2d, MEdgeLessThan>::iterator it = C.begin();
    std::vector<MEdge> edges;
    std::set<MVertex*> boundaries;
    for(; it != C.end(); ++it) {
      if(it->second.inBoundary) {
        edges.push_back(it->first);
        boundaries.insert(it->first.getVertex(0));
        boundaries.insert(it->first.getVertex(1));
      }
    }
    std::vector<std::vector<MVertex *> > vsorted;
    SortEdgeConsecutive(edges, vsorted);

    // AVERAGE
    dof->numberVertex(*vs.begin(), 1, 1);

    for(std::set<MVertex *, MVertexPtrLessThan>::iterator it = vs.begin();
        it != vs.end(); ++it){
      dof->numberVertex(*it, 0, 1);
    }

    simpleFunction<double> ONE(1.0);
    laplaceTerm l(0, 1, &ONE);

    std::set<GEntity *> firsts;
    for(size_t i = 0; i < f.size(); i++) {
      std::vector<GEdge *> e = f[i]->edges();
      if(e.size()) firsts.insert(e[0]);
      //      printf("--> %lu\n",e[0]->tag());
      for(size_t j = 0; j < f[i]->triangles.size(); j++) {
        MTriangle *t = f[i]->triangles[j];
        SElement se(t);
        l.addToMatrix(*dof, &se);
      }
    }

    for(size_t j = 0; j < vsorted.size(); ++j) {
      if(vsorted[j][0] == vsorted[j][vsorted[j].size() - 1]) {
        vsorted[j].erase(vsorted[j].begin());
      }

      std::vector<double> CURVATURE;
      CURVATURE.resize(vsorted[j].size());
      for(size_t i = 0; i < vsorted[j].size(); ++i) { CURVATURE[i] = 0.0; }
      double SUM = 0.0;
      for(size_t i = 0; i < vsorted[j].size(); ++i) {
        MVertex *vi = vsorted[j][i];
        MVertex *vip = vsorted[j][(i + 1) % vsorted[j].size()];
        MVertex *vim =
          vsorted[j][(i + vsorted[j].size() - 1) % vsorted[j].size()];
        SVector3 vv(vip->x() - vi->x(), vip->y() - vi->y(), vip->z() - vi->z());
        SVector3 ww(vi->x() - vim->x(), vi->y() - vim->y(), vi->z() - vim->z());
        vv.normalize();
        ww.normalize();
        SVector3 xx = crossprod(vv, ww);
        double ccos = dot(vv, ww);
        double ANGLE = atan2(xx.norm(), ccos);
        xx.normalize();

        MEdge edze(vi, vim);
        std::map<MEdge, cross2d, MEdgeLessThan>::iterator itip = C.find(edze);
        double sign = 1;
        if(itip != C.end()) {
          MTriangle *tt = itip->second._t[0];
          MVertex *vrv;
          if(tt->getVertex(0) != vi && tt->getVertex(0) != vim) {
            vrv = tt->getVertex(0);
          }
          else if(tt->getVertex(1) != vi && tt->getVertex(1) != vim) {
            vrv = tt->getVertex(1);
          }
          else
            vrv = tt->getVertex(2);
          SVector3 aa(vrv->x() - vim->x(), vrv->y() - vim->y(),
                      vrv->z() - vim->z());
          SVector3 zz = crossprod(aa, ww);
          zz.normalize();
          sign = -dot(zz, xx);// > 0 ? -1 : 1;
	  // sign = dot(zz, xx);// > 0 ? -1 : 1;//temp fix
          // sign = dot(zz, xx) > 0 ? -1 : 1;// was commented here
        }
        else
          printf("ARGH\n");
        //        if (vsorted.size() == 1)sign = -1;
        CURVATURE[i] += ANGLE * sign;
        //printf("%12.5E\n",sign);
      }

      for(size_t i = 0; i < vsorted[j].size(); ++i) { SUM += CURVATURE[i]; }

      printf("%22.15E \n",SUM);
      for(size_t i = 0; i < vsorted[j].size(); ++i) {
        Dof E(vsorted[j][i]->getNum(), Dof::createTypeWithTwoInts(0, 1));
	_lsys->addToRightHandSide(dof->getDofNumber(E),CURVATURE[i]); //was commented here
      }
    }

    double sum1 = 0;
    for(std::map<MVertex *, double>::iterator it = gaussianCurvatures.begin();it != gaussianCurvatures.end(); ++it){
      Dof E(it->first->getNum(), Dof::createTypeWithTwoInts(0, 1));
      //      printf("%12.5E\n",it->second);
      double XXX = boundaries.find(it->first) == boundaries.end() ? -2*M_PI+it->second : -M_PI+it->second;
      // _lsys->addToRightHandSide(dof->getDofNumber(E),XXX); //for manifold. not using it it's buggy
      sum1 += XXX;
    }

    double SSUM = 0;
    for(std::map<MVertex *, int>::iterator it = sing.begin(); it != sing.end();
        ++it) {
      Dof E(it->first->getNum(), Dof::createTypeWithTwoInts(0, 1));
      _lsys->addToRightHandSide(dof->getDofNumber(E),
                                2.0 * M_PI * (double)it->second / nbTurns);
      SSUM += 2.0 * M_PI * (double)it->second / nbTurns;
    }

    printf("%12.5E %12.5E\n",sum1,SSUM);
    
    // FIX DE LA MORT
    // AVERAGE
    Dof EAVG((*vs.begin())->getNum(), Dof::createTypeWithTwoInts(1, 1));

    for(std::set<MVertex *, MVertexPtrLessThan>::iterator it = vs.begin();
        it != vs.end(); ++it){
      Dof E((*it)->getNum(), Dof::createTypeWithTwoInts(0, 1));
      dof->assembleSym(EAVG, E, 1);
      //      dof->assemble(E, EAVG, 1);
    }
    dof->assemble(EAVG, EAVG, 0.0); //for petsc

    _lsys->systemSolve();
    return dof;
  }

  int computeHFromSingularities(std::map<MVertex *, int> &s)
  {
    myAssembler = computeHFromSingularities(s, 4);
    for(std::map<MVertex *, int>::iterator it = s.begin(); it != s.end();
        ++it) {
      singularities.insert(it->first);
    }
    //    printScalar(myAssembler, 'H');
    return 1;
  }

  //---------------------------------------------------------------------------

  void computeThetaUsingHCrouzeixRaviart(
    std::map<int, std::vector<double> > &dataTHETA)
  {
#if defined(HAVE_PETSC)
    linearSystemPETSc<double> *_lsys = new linearSystemPETSc<double>;
#elif defined(HAVE_MUMPS)
  linearSystemMUMPS<double> *_lsys = new linearSystemMUMPS<double>;
#else
    linearSystemFull<double> *_lsys = new linearSystemFull<double>;
#endif
    dofManager<double> *theta = new dofManager<double>(_lsys);

    std::map<MEdge, size_t, MEdgeLessThan> aaa;
    size_t count = 0;
    for(size_t i = 0; i < f.size(); i++) {
      for(size_t j = 0; j < f[i]->triangles.size(); j++) {
        for(size_t k = 0; k < 3; k++) {
          if(aaa.find(f[i]->triangles[j]->getEdge(k)) == aaa.end()) {
            Dof EdgeDof(count, Dof::createTypeWithTwoInts(0, 1));
            theta->numberDof(EdgeDof);
            aaa[f[i]->triangles[j]->getEdge(k)] = count++;
          }
        }
      }
    }

    for(size_t i = 0; i < f.size(); i++) {
      for(size_t j = 0; j < f[i]->triangles.size(); j++) {
        MTriangle *t = f[i]->triangles[j];
        double V = t->getVolume();
        double g1[3], g2[3], g3[3];
        double a[3];
        a[0] = 1;
        a[1] = 0;
        a[2] = 0;
        t->interpolateGrad(a, 0, 0, 0, g1);
        a[0] = 0;
        a[1] = 1;
        a[2] = 0;
        t->interpolateGrad(a, 0, 0, 0, g2);
        a[0] = 0;
        a[1] = 0;
        a[2] = 1;
        t->interpolateGrad(a, 0, 0, 0, g3);
        SVector3 G[3];
        G[0] = SVector3(g1[0] + g2[0] - g3[0], g1[1] + g2[1] - g3[1],
                        g1[2] + g2[2] - g3[2]);
        G[1] = SVector3(g2[0] + g3[0] - g1[0], g2[1] + g3[1] - g1[1],
                        g2[2] + g3[2] - g1[2]);
        G[2] = SVector3(g1[0] + g3[0] - g2[0], g1[1] + g3[1] - g2[1],
                        g1[2] + g3[2] - g2[2]);
        SVector3 v10(t->getVertex(1)->x() - t->getVertex(0)->x(),
                     t->getVertex(1)->y() - t->getVertex(0)->y(),
                     t->getVertex(1)->z() - t->getVertex(0)->z());
        SVector3 v20(t->getVertex(2)->x() - t->getVertex(0)->x(),
                     t->getVertex(2)->y() - t->getVertex(0)->y(),
                     t->getVertex(2)->z() - t->getVertex(0)->z());
        SVector3 xx = crossprod(v20, v10);
        xx.normalize();

        double H[3];
        for(int k = 0; k < 3; k++) {
          std::map<MVertex *, MVertex *, MVertexPtrLessThan>::iterator itk =
            new2old.find(t->getVertex(k));
          if(itk == new2old.end())
            myAssembler->getDofValue(t->getVertex(k), 0, 1, H[k]);
          else
            myAssembler->getDofValue(itk->second, 0, 1, H[k]);
        }
        double gradH[3];
        t->interpolateGrad(H, 0, 0, 0, gradH);

        SVector3 temp(gradH[0], gradH[1], gradH[2]);
        SVector3 gradHOrtho = crossprod(temp, xx);

        double RHS[3] = {dot(gradHOrtho, G[0]), dot(gradHOrtho, G[1]),
                         dot(gradHOrtho, G[2])};

        for(size_t k = 0; k < 3; k++) {
          Dof Ek(aaa[t->getEdge(k)], Dof::createTypeWithTwoInts(0, 1));
          theta->assemble(Ek, RHS[k] * V);
          for(size_t l = 0; l < 3; l++) {
            Dof El(aaa[t->getEdge(l)], Dof::createTypeWithTwoInts(0, 1));
            theta->assemble(Ek, El, -dot(G[k], G[l]) * V);
          }
        }
      }
    }

    double SUM = 0.0;
    for(size_t i = 0; i < aaa.size(); i++) {
      double a;
      _lsys->getFromRightHandSide(i, a);
      SUM += a;
    }
    SUM /= aaa.size();
    for(size_t i = 0; i < aaa.size(); i++) {
      //      _lsys->addToRightHandSide(i, -SUM);
    }

    _lsys->systemSolve();
    //    printScalar(theta, 'T');

    double sum = 0;
    int count_ = 0;

    std::map<MEdge, size_t, MEdgeLessThan>::iterator it = aaa.begin();
    for(; it != aaa.end(); ++it) {
      Dof d(it->second, Dof::createTypeWithTwoInts(0, 1));
      double t;
      theta->getDofValue(d, t);
      MVertex *v0, *v1;
      std::map<MVertex *, MVertex *, MVertexPtrLessThan>::iterator it0 =
        new2old.find(it->first.getVertex(0));
      if(it0 == new2old.end())
        v0 = it->first.getVertex(0);
      else
        v0 = it0->second;
      it0 = new2old.find(it->first.getVertex(1));
      if(it0 == new2old.end())
        v1 = it->first.getVertex(1);
      else
        v1 = it0->second;
      MEdge e(v0, v1);
      std::map<MEdge, cross2d, MEdgeLessThan>::iterator itc = C.find(e);
      // well... at first ...
      itc->second.o_i = SVector3(cos(t), sin(t), 0.0);
      // end well
      double aa = atan2(dot(itc->second._tgt2, itc->second.o_i),
                        dot(itc->second._tgt, itc->second.o_i));
      itc->second.normalize(aa);
      if(!itc->second.inBoundary) { itc->second._a = aa; }
      else {
        //        printf("%12.5E %lu %lu\n",aa,
        //               itc->second._e.getVertex(0)->getNum(),
        //               itc->second._e.getVertex(1)->getNum());
        itc->second._a = 0;
        count_++;
        sum += aa;
      }
    }

    sum /= count_;
    std::map<MEdge, cross2d, MEdgeLessThan>::iterator itc = C.begin();
    for(; itc != C.end(); ++itc) {
      if(!itc->second.inBoundary) {
        itc->second._a -= sum;
        itc->second._atemp = itc->second._a;
        itc->second.normalize(itc->second._a);
      }
    }

    {
      for(size_t i = 0; i < f.size(); i++) {
        for(size_t j = 0; j < f[i]->triangles.size(); j++) {
          MTriangle *t = f[i]->triangles[j];
          Dof d0(aaa[f[i]->triangles[j]->getEdge(0)],
                 Dof::createTypeWithTwoInts(0, 1));
          Dof d1(aaa[f[i]->triangles[j]->getEdge(1)],
                 Dof::createTypeWithTwoInts(0, 1));
          Dof d2(aaa[f[i]->triangles[j]->getEdge(2)],
                 Dof::createTypeWithTwoInts(0, 1));
          double a, b, c;
          theta->getDofValue(d0, a);
          theta->getDofValue(d1, b);
          theta->getDofValue(d2, c);
          double A = c + a - b;
          double B = a + b - c;
          double C = b + c - a;
          std::vector<double> ts;
          ts.push_back(A);
          ts.push_back(B);
          ts.push_back(C);
          dataTHETA[t->getNum()] = ts;
        }
      }
    }
  }
  //---------------------------------------------------------------------------

  quadLayoutData(GModel *_gm, std::vector<GFace *> &_f, const std::string &name,
                 bool includeFeatureEdges = true)
    : gm(_gm), f(_f), myAssembler(NULL)
  {
    modelName = name;
    for(size_t i = 0; i < f.size(); i++) {
      for(size_t j = 0; j < f[i]->triangles.size(); j++) {
        MTriangle *t = f[i]->triangles[j];
        for(size_t k = 0; k < 3; k++) {
          vs.insert(t->getVertex(k));
          MEdge e = t->getEdge(k);
          MEdge e1 = t->getEdge((k + 1) % 3);
          MEdge e2 = t->getEdge((k + 2) % 3);

          // Gaussian Curvatures
          MVertex *vk = t->getVertex(k);
          MVertex *vk1 = t->getVertex((k + 1) % 3);
          MVertex *vk2 = t->getVertex((k + 2) % 3);
          SVector3 v1 (vk1->x()-vk->x(),vk1->y()-vk->y(),vk1->z()-vk->z());
          SVector3 v2 (vk2->x()-vk->x(),vk2->y()-vk->y(),vk2->z()-vk->z());
          double CURV = angle(v1,v2);
          std::map<MVertex *, double>::iterator itg = gaussianCurvatures.find(vk);
          if (itg == gaussianCurvatures.end())  gaussianCurvatures[vk] = CURV;
          else itg->second += CURV;
          //---------------------------------------------------------------------

          cross2d c(e, t, e1, e2);
          std::map<MEdge, cross2d, MEdgeLessThan>::iterator it = C.find(e);
          if(it == C.end()) {
            C.insert(std::make_pair(e, c));
          } else {
            it->second._t.push_back(t);
            it->second._neighbors.push_back(e1);
            it->second._neighbors.push_back(e2);
          }
        }
      }
    }
    if(includeFeatureEdges) {
      for(size_t i = 0; i < f.size(); i++) {
        std::vector<GEdge *> e = f[i]->edges();
        for(size_t j = 0; j < e.size(); j++) {
          for(size_t k = 0; k < e[j]->lines.size(); k++) {
            MLine *l = e[j]->lines[k];
            MEdge e = l->getEdge(0);
            std::map<MEdge, cross2d, MEdgeLessThan>::iterator it = C.find(e);
            if(it != C.end()) { it->second.inBoundary = true; }
          }
        }
      }
    }
    std::map<MEdge, cross2d, MEdgeLessThan>::iterator it = C.begin();
    for(; it != C.end(); ++it) it->second.finish(C);
    it = C.begin();
    for(; it != C.end(); ++it) it->second.finish2();
    //    FILE *F = fopen("gc.pos","w");
    //    fprintf(F,"View\"\"{\n");
    //    double dd = 0;
    //    for (std::map<MVertex*,double>:: iterator it = gaussianCurvatures.begin(); it != gaussianCurvatures.end() ; ++it){
    //      fprintf(F,"SP(%g,%g,%g){%g};\n",it->first->x(),it->first->y(),it->first->z(),it->second);
    //      dd += it->second;      
    //    }
    //    printf("%22.15E %22.15E\n",dd,dd-4*M_PI);
    //    fprintf(F,"};\n");
    //    fclose(F);
  }

  void restoreInitialMesh()
  {
    unDuplicateNodesInCutGraph(f, new2old);
    G.clear();
    groups.clear();
    groups_cg.clear();
    cutG.clear();
    new2old.clear();
    //    boundaries.clear();
    std::map<MEdge, cross2d, MEdgeLessThan>::iterator it = C.begin();
    for(; it != C.end(); ++it) {
      it->second.inCutGraph = false;
      it->second._btemp = 0;
    }
  }

  int computeUniqueVectorsPerTriangle()
  {


    computeLifting(gm, f, C, d0, d1, cutG, singularities, groups);    
    return 0;
  }

  int computeUniqueVectorsPerTriangle_old()
  {
    // LIFTING
    std::map<MEdge, cross2d, MEdgeLessThan>::iterator it;
    std::set<cross2d *> visited;
    int ITER = 0;
    while(1) {
      bool allVisited = true;
      for(it = C.begin(); it != C.end(); ++it) {
        if (it->second._btemp < 1000)allVisited = false; 
        if(visited.find(&(it->second)) == visited.end() &&
           cutG.find(it->second._e) == cutG.end()) {
          computeLifting(&(it->second), 0, cutG, singularities, boundaries,
                         visited, ITER++);
          break;
        }
      }
      if (allVisited)break;
    }
    computeUniqueVectorPerTriangle(gm, f, C, d0, d1);
    return 0;
  }

  
  int computeCutGraph(std::map<MEdge, MEdge, MEdgeLessThan> &duplicateEdges)
  {
    std::map<MEdge, cross2d, MEdgeLessThan>::iterator it;
    // COMPUTING CUT GRAPH
    cutGraph(C, cutG, singularities, boundaries);
    for(it = C.begin(); it != C.end(); ++it) {
      MEdge e0 = it->second._e;
      if(cutG.find(e0) != cutG.end()) it->second.inCutGraph = true;
    }

    groupBoundaries(gm, C, groups, singularities, gaussianCurvatures,corners, false);
    groupBoundaries(gm, C, groups_cg, singularities, gaussianCurvatures,corners, true);

    v2t_cont adj;
    for(size_t i = 0; i < f.size(); i++) {
      buildVertexToElement(f[i]->triangles, adj);
    }

    std::string fn = modelName + "_groups_analyzed.pos";
    FILE *_f = fopen(fn.c_str(), "w");
    fprintf(_f, "View \"groups\"{\n");

    std::set<MVertex *, MVertexPtrLessThan> isolated_singularities;
    {
      for(std::set<MVertex *, MVertexPtrLessThan>::iterator it =
            singularities.begin();
          it != singularities.end(); ++it) {
        int count = 0;
        for(size_t i = 0; i < groups_cg.size(); i++) {
          for(size_t k = 0; k < groups_cg[i].size(); k++) {
            for(size_t j = 0; j < 2; j++) {
              MVertex *v = groups_cg[i][k]->_e.getVertex(j);
              if(v == *it) count++;
            }
          }
        }
        if(count == 1) { isolated_singularities.insert(*it); }
        else {
          isolated_singularities.insert(*it);
        }
      }
    }

    d0.clear();
    d1.clear();
    computeUniqueVectorsPerTriangle();

    // analyzing groups
    {
      std::set<MTriangle *> allTrianglesConsidered;
      for(size_t i = 0; i < groups_cg.size(); i++) {
        groupOfCross2d g(i);
        analyzeGroup(groups_cg[i], g, d0, d1, adj, isolated_singularities,
                     boundaries, allTrianglesConsidered);
        g.print(_f);
        G.push_back(g);
      }
    }
    fprintf(_f, "};\n");
    fclose(_f);

    std::multimap<MVertex *, MVertex *, MVertexPtrLessThan> old2new;
    duplicateNodesInCutGraph(f, C, new2old, old2new, duplicateEdges,
                             singularities, adj, G);

    for(size_t i = 0; i < groups_cg.size(); i++) {
      createJumpyPairs(G[i], singularities, boundaries, old2new);
    }
    return 0;
  }

  int computeCrossFieldAndH(std::map<MVertex *, int> *s,
                            std::map<int, std::vector<double> > &dataTHETA,
                            bool createViewTheta = false)
  {
    double a = Cpu();
    computeHFromSingularities(*s);
    double b = Cpu();
    Msg::Info("Real part H (nodal) has been computed in %4g sec", b - a);

    std::map<MEdge, MEdge, MEdgeLessThan> duplicateEdges;

    double c = Cpu();
    computeCutGraph(duplicateEdges);
    Msg::Info("Cut Graph has been computed in %4g sec", c - b);

    double d = Cpu();
    computeThetaUsingHCrouzeixRaviart(dataTHETA);
    Msg::Info("Imaginary part H (crouzeix raviart/multi-valued) has been "
              "computed in %4g sec",
              d - c);
    restoreInitialMesh();

    if (createViewTheta) { /* For interoperability with the quadMeshingTools API */
      std::vector<std::size_t> keys;
      std::vector<std::vector<double> > values;
      for(size_t i = 0; i < f.size(); i++) {
        for(size_t j = 0; j < f[i]->triangles.size(); j++) {
          MTriangle *t = f[i]->triangles[j];
          SVector3 tri_normal = crossprod(t->getVertex(2)->point()-t->getVertex(0)->point(),
              t->getVertex(1)->point()-t->getVertex(0)->point());
          tri_normal.normalize();
          size_t tnum = t->getNum();
          keys.push_back(tnum);
          double vals[3] = {0.,0.,0.};
          for(size_t k = 0; k < 3; k++) {
            size_t tv1 = t->getVertex(k)->getNum();
            size_t tv2 = t->getVertex((k+1)%3)->getNum();
            MEdge edge = t->getEdge(k);
            auto it = C.find(edge);
            if (it != C.end()) {
              MEdge edge_found = it->second._e;
              if (tv1 == edge_found.getVertex(0)->getNum() && tv2 == edge_found.getVertex(1)->getNum()) {
                vals[k] = it->second._a;
              } else if (tv1 == edge_found.getVertex(1)->getNum() && tv2 == edge_found.getVertex(0)->getNum()) {
                vals[k] = it->second._a;
              } else {
                Msg::Error("create view 'theta': tri %i, edge %i,%i not matching triangle ?", tnum, 
                    edge.getVertex(0)->getNum(),
                    edge.getVertex(1)->getNum());
              }
            } else {
              Msg::Error("create view 'theta': tri %i, edge %i,%i not found in C", tnum, 
                  edge.getVertex(0)->getNum(),
                  edge.getVertex(1)->getNum());
              return -1;
            }
          }
          values.push_back({vals[0],vals[1],vals[2]});
        }
      }
      PView* theta = PView::getViewByName("theta");
      if (theta) {delete theta; theta = NULL;}
      gmsh::initialize();
      std::string cname;
      gmsh::model::getCurrent(cname);
      int crossFieldTag = gmsh::view::add("theta");
      gmsh::view::addModelData(crossFieldTag, 0, cname, "ElementData", keys, values);
    }
    return 0;
  }

  MVertex *intersectEdgeEdge(MEdge &e, MVertex *v1, MVertex *v2, GFace *gf,
                             double model_size)
  {
    MVertex *e1 = e.getVertex(0);
    MVertex *e2 = e.getVertex(1);

    if(e1 == v2) return NULL;
    if(e2 == v2) return NULL;

    SVector3 e1e2(e2->x() - e1->x(), e2->y() - e1->y(), e2->z() - e1->z());
    SVector3 e1v1(v1->x() - e1->x(), v1->y() - e1->y(), v1->z() - e1->z());
    SVector3 e2v1(v1->x() - e2->x(), v1->y() - e2->y(), v1->z() - e2->z());

    SVector3 a = crossprod(e1e2, e1v1);
    double b = dot(e1v1, e2v1);
    if(a.norm() < model_size*1.e-12 && b < 0) return v1;

    if(!v2) return NULL;

    SVector3 e2v2(v2->x() - e2->x(), v2->y() - e2->y(), v2->z() - e2->z());
    SVector3 e1v2(v2->x() - e1->x(), v2->y() - e1->y(), v2->z() - e1->z());
    a = crossprod(e1e2, e1v2);
    b = dot(e1v2, e2v2);
    if(a.norm() < model_size*1.e-12 && b < 0) return v2;
    
    double x[2];
    
    bool inters = intersection_segments(
      SPoint3(e.getVertex(0)->x(), e.getVertex(0)->y(), e.getVertex(0)->z()),
      SPoint3(e.getVertex(1)->x(), e.getVertex(1)->y(), e.getVertex(1)->z()),
      SPoint3(v1->x(), v1->y(), v1->z()), SPoint3(v2->x(), v2->y(), v2->z()),
      x);
    if(!inters) return NULL;
    return new MEdgeVertex(v2->x() * x[1] + v1->x() * (1. - x[1]),
                           v2->y() * x[1] + v1->y() * (1. - x[1]),
                           v2->z() * x[1] + v1->z() * (1. - x[1]), NULL, 0);
  }

  void cut_edge(std::map<MEdge, int, MEdgeLessThan> &ecuts, MVertex *v0,
                MVertex *v1, MVertex *mid)
  {
    MEdge e(v0, v1);
    std::map<MEdge, int, MEdgeLessThan>::iterator it = ecuts.find(e);
    if(it != ecuts.end()) {
      int index = it->second;
      ecuts.erase(it);
      MEdge e1(v0, mid);
      MEdge e2(mid, v1);
      ecuts[e1] = index;
      ecuts[e2] = index;
    }
  }

  void splitEdge (MVertex *v1, MVertex *v2, MVertex* v12,
                  std::map<MEdge,GEdge*,MEdgeLessThan> &inverseClassificationEdges){
    MEdge e (v1,v2);
    std::map<MEdge,GEdge*,MEdgeLessThan>::iterator it = inverseClassificationEdges.find(e);
    if (it != inverseClassificationEdges.end()){
      MEdge e1 (v1,v12);
      MEdge e2 (v12,v2);
      inverseClassificationEdges[e1] = it->second;
      inverseClassificationEdges[e2] = it->second;
      inverseClassificationEdges.erase(it);
    }
  }
  
  void cutTriangles(std::vector<MTriangle *> &ts, GFace *gf, MVertex *v1,
                    MVertex *v2, GEdge *ge,
                    std::map<MEdge, int, MEdgeLessThan> &ecuts, int index,
                    FILE *f, std::map<MEdge,GEdge*,MEdgeLessThan> &inverseClassificationEdges,
                    double model_size, bool t_junction)
  {
    std::map<MEdge, MVertex *, MEdgeLessThan> e_cut;
    std::vector<MTriangle *> newt;

    for(size_t i = 0; i < ts.size(); ++i) {
      MVertex *vs[3] = {NULL, NULL, NULL};
      for(size_t j = 0; j < 3; ++j) {
        MEdge e = ts[i]->getEdge(j);
        if(e_cut.find(e) == e_cut.end()) {
          MVertex *v = intersectEdgeEdge(e, v1, v2, gf, model_size);
          if(v && v != v1 && v != v2) {
            gf->mesh_vertices.push_back(v);
            if(f)
              fprintf(f, "SP(%g,%g,%g){%d};\n", v->x(), v->y(), v->z(),
                      gf->tag());
          }
          e_cut[e] = v;
        }
        vs[j] = e_cut[e];
      }

      if (vs[0])splitEdge (ts[i]->getVertex(0), ts[i]->getVertex(1), vs[0],inverseClassificationEdges);
      if (vs[1])splitEdge (ts[i]->getVertex(1), ts[i]->getVertex(2), vs[1],inverseClassificationEdges);
      if (vs[2])splitEdge (ts[i]->getVertex(2), ts[i]->getVertex(0), vs[2],inverseClassificationEdges);      
      
      if(!vs[0] && !vs[1] && !vs[2])
        newt.push_back(ts[i]);
      else if(vs[0] && !vs[1] && !vs[2]) {
        newt.push_back(
          new MTriangle(ts[i]->getVertex(0), vs[0], ts[i]->getVertex(2)));
        newt.push_back(
          new MTriangle(vs[0], ts[i]->getVertex(1), ts[i]->getVertex(2)));
        cut_edge(ecuts, ts[i]->getVertex(0), ts[i]->getVertex(1), vs[0]);
        MEdge ed(ts[i]->getVertex(2), vs[0]);
        if (!t_junction)ecuts[ed] = index;
      }
      else if(!vs[0] && vs[1] && !vs[2]) {
        newt.push_back(
          new MTriangle(ts[i]->getVertex(0), ts[i]->getVertex(1), vs[1]));
        newt.push_back(
          new MTriangle(ts[i]->getVertex(0), vs[1], ts[i]->getVertex(2)));
        cut_edge(ecuts, ts[i]->getVertex(2), ts[i]->getVertex(1), vs[1]);
        MEdge ed(ts[i]->getVertex(0), vs[1]);
        if (!t_junction)  ecuts[ed] = index;
      }
      else if(!vs[0] && !vs[1] && vs[2]) {
        newt.push_back(
          new MTriangle(ts[i]->getVertex(0), ts[i]->getVertex(1), vs[2]));
        newt.push_back(
          new MTriangle(vs[2], ts[i]->getVertex(1), ts[i]->getVertex(2)));
        cut_edge(ecuts, ts[i]->getVertex(2), ts[i]->getVertex(0), vs[2]);
        MEdge ed(ts[i]->getVertex(1), vs[2]);
        if (!t_junction)ecuts[ed] = index;
      }
      else if(vs[0] && vs[1] && !vs[2]) { // OK
        newt.push_back(new MTriangle(ts[i]->getVertex(0), vs[0], vs[1]));
        newt.push_back(new MTriangle(ts[i]->getVertex(1), vs[1], vs[0]));
        newt.push_back(
          new MTriangle(ts[i]->getVertex(0), vs[1], ts[i]->getVertex(2)));
        cut_edge(ecuts, ts[i]->getVertex(0), ts[i]->getVertex(1), vs[0]);
        cut_edge(ecuts, ts[i]->getVertex(2), ts[i]->getVertex(1), vs[1]);
        MEdge ed(vs[0], vs[1]);
        if (!t_junction)ecuts[ed] = index;
      }
      else if(vs[0] && !vs[1] && vs[2]) { // OK
        newt.push_back(new MTriangle(ts[i]->getVertex(0), vs[0], vs[2]));
        newt.push_back(new MTriangle(ts[i]->getVertex(2), vs[2], vs[0]));
        newt.push_back(
          new MTriangle(ts[i]->getVertex(2), vs[0], ts[i]->getVertex(1)));
        cut_edge(ecuts, ts[i]->getVertex(0), ts[i]->getVertex(1), vs[0]);
        cut_edge(ecuts, ts[i]->getVertex(2), ts[i]->getVertex(0), vs[2]);
        MEdge ed(vs[0], vs[2]);
        if (!t_junction)ecuts[ed] = index;
      }
      else if(!vs[0] && vs[1] && vs[2]) {
        newt.push_back(new MTriangle(ts[i]->getVertex(2), vs[2], vs[1]));
        newt.push_back(new MTriangle(ts[i]->getVertex(0), vs[1], vs[2]));
        newt.push_back(
          new MTriangle(ts[i]->getVertex(1), vs[1], ts[i]->getVertex(0)));
        cut_edge(ecuts, ts[i]->getVertex(1), ts[i]->getVertex(2), vs[1]);
        cut_edge(ecuts, ts[i]->getVertex(2), ts[i]->getVertex(0), vs[2]);
        MEdge ed(vs[1], vs[2]);
        if (!t_junction)ecuts[ed] = index;
      }
      else if(vs[0] && vs[1] && vs[2]) {
        newt.push_back(new MTriangle(vs[0], vs[1], vs[2]));
        newt.push_back(new MTriangle(ts[i]->getVertex(0), vs[0], vs[2]));
        newt.push_back(new MTriangle(ts[i]->getVertex(1), vs[1], vs[0]));
        newt.push_back(new MTriangle(ts[i]->getVertex(2), vs[2], vs[1]));
      }
      else {
        newt.push_back(ts[i]);
      }
    }
    ts = newt;
  }

  static bool isTjunction (std::vector<std::pair<MTriangle*,int> > &t_junctions,
                           int index, MTriangle *t){


    bool _found = false;
    for (size_t i=0;i<t_junctions.size();i++){
      if (t_junctions[i].first == t && t_junctions[i].second == index)_found = true;
    }
    //    printf("triangle %lu is a t-junction for %d --> %d\n",t->getNum(),index,_found);
    return !_found ;
  }
  
  void cutMesh(std::map<MEdge, edgeCuts, MEdgeLessThan> &cuts,
               std::map<MEdge, GEdge*, MEdgeLessThan> &inverseClassificationEdges,
               std::vector<std::pair<MTriangle*,int> > &t_junctions)
  {
    SBoundingBox3d bnd = GModel::current()->bounds();
    double model_size = bnd.diag();

    // create an inverse classification for current mesh edges.

    std::set<GEdge*> modelEdges;
    for(size_t i = 0; i < f.size(); i++) {
      std::vector<GEdge*> ed_of_fi = f[i]->edges();
      modelEdges.insert(ed_of_fi.begin(),ed_of_fi.end());
      for (size_t j=0;j<ed_of_fi.size();j++){
        for (size_t k=0;k<ed_of_fi[j]->lines.size();k++){
          MEdge e (ed_of_fi[j]->lines[k]->getVertex(0),
              ed_of_fi[j]->lines[k]->getVertex(1));
          inverseClassificationEdges[e] = ed_of_fi[j];
        }
      }
    }
    
    std::map<MEdge, edgeCuts, MEdgeLessThan>::iterator it = cuts.begin();
    std::map<MEdge, int, MEdgeLessThan> ecuts;
    
    FILE *F = fopen("addedpoints.pos", "w");
    fprintf(F, "View \"\"{\n");
    for(; it != cuts.end(); ++it) {
      
      std::map<MEdge,GEdge*,MEdgeLessThan>::iterator ite =inverseClassificationEdges.find(it->first);
      if (ite != inverseClassificationEdges.end() && ite->second->haveParametrization()){
        it->second.finish(gm, ite->second, it->first.getVertex(0), it->first.getVertex(1), F);
      }
      else {
        it->second.finish(gm, NULL ,it->first.getVertex(0), it->first.getVertex(1), F);
      }
    }

    for(size_t i = 0; i < f.size(); i++) {
      std::vector<MTriangle *> newT;
      
      for(size_t j = 0; j < f[i]->triangles.size(); j++) {
        std::set<int> indices;
        std::multimap<int, std::pair<MVertex *, std::pair<int, int> > > tcuts;

        std::set<int> t_indices;
        for(size_t k = 0; k < 3; k++) {
          MEdge e = f[i]->triangles[j]->getEdge(k);
          std::map<MEdge, edgeCuts, MEdgeLessThan>::iterator it = cuts.find(e);
          if(it != cuts.end()) {
            for(size_t l = 0; l < it->second.vs.size(); ++l) {
              if (it->second.p_occur[l] != it->second.n_occur)t_indices.insert(it->second.indexOfCuts[l]);
              std::pair<int, std::pair<MVertex *, std::pair<int, int> > > pp =
                std::make_pair(
                  it->second.indexOfCuts[l],
                  std::make_pair(it->second.vs[l],
                                 std::make_pair(k , it->second.idsOfCuts[l])));
              tcuts.insert(pp);
              indices.insert(it->second.indexOfCuts[l]);
            }
          }
        }
        
        std::set<int>::iterator iti = indices.begin();
        std::vector<MTriangle *> ttt;
        ttt.push_back(f[i]->triangles[j]);
        for(; iti != indices.end(); ++iti) {
          //          if (*iti != 313310)continue;
          bool TJUNCTION = (t_indices.find(*iti) != t_indices.end()) &&
            isTjunction (t_junctions, *iti, (f[i]->triangles[j]));
          
          GEdge *ge = gm->getEdgeByTag(*iti);
          if(tcuts.count(*iti) == 1) {
            std::multimap<
              int, std::pair<MVertex *, std::pair<int, int> > >::iterator itt =
              tcuts.lower_bound(*iti);
            MVertex *v0 = itt->second.first;
            int k = itt->second.second.first;            
            //            if (itt->second.second.second < 0){
            //              printf("T JUNCTION %lu %lu \n",v0->getNum(), f[i]->triangles[j]->getVertex((k + 2) % 3)->getNum());
            //            }
            cutTriangles(ttt, f[i], v0,
                         f[i]->triangles[j]->getVertex((k + 2) % 3), ge, ecuts,
                         *iti, F, inverseClassificationEdges, model_size, TJUNCTION);
          }
          else if(tcuts.count(*iti) == 2) {
            std::multimap<
              int, std::pair<MVertex *, std::pair<int, int> > >::iterator itt =
              tcuts.lower_bound(*iti);
            MVertex *v0 = itt->second.first;
            ++itt;
            MVertex *v1 = itt->second.first;
            cutTriangles(ttt, f[i], v0, v1, ge, ecuts, *iti, F, inverseClassificationEdges, model_size, TJUNCTION);
          }
          else if(tcuts.count(*iti) == 3) {
            std::multimap<
              int, std::pair<MVertex *, std::pair<int, int> > >::iterator itt =
              tcuts.lower_bound(*iti);
            int k0 = itt->second.second.first;
            int id0 = abs(itt->second.second.second);
            MVertex *v0 = itt->second.first;
            ++itt;
            int k1 = itt->second.second.first;
            int id1 = abs(itt->second.second.second);
            MVertex *v1 = itt->second.first;
            ++itt;
            int k2 = itt->second.second.first;
            int id2 = abs(itt->second.second.second);
            MVertex *v2 = itt->second.first;
            ;

            if(abs(id0 - id1) <= 2) {
              cutTriangles(ttt, f[i], v2,
                           f[i]->triangles[j]->getVertex((k2 + 2) % 3), ge,
                           ecuts, *iti, F, inverseClassificationEdges, model_size, TJUNCTION);
              cutTriangles(ttt, f[i], v0, v1, ge, ecuts, *iti, F, inverseClassificationEdges, model_size, TJUNCTION);
            }
            else if(abs(id0 - id2) <= 2) {
              cutTriangles(ttt, f[i], v1,
                           f[i]->triangles[j]->getVertex((k1 + 2) % 3), ge,
                           ecuts, *iti, F, inverseClassificationEdges, model_size, TJUNCTION);
              cutTriangles(ttt, f[i], v0, v2, ge, ecuts, *iti, F, inverseClassificationEdges, model_size, TJUNCTION);
            }
            else if(abs(id1 - id2) <= 2) {
              cutTriangles(ttt, f[i], v0,
                           f[i]->triangles[j]->getVertex((k0 + 2) % 3), ge,
                           ecuts, *iti, F, inverseClassificationEdges, model_size, TJUNCTION);
              cutTriangles(ttt, f[i], v1, v2, ge, ecuts, *iti, F, inverseClassificationEdges, model_size, TJUNCTION);
            }
            else {
              printf("BAD BEHAVIOR 3\n");
              printf("%lu %lu %lu \n", v0->getNum(), v1->getNum(),
                     v2->getNum());
              printf("%d %d %d\n", id0, id1, id2);
            }
          }
          else if(tcuts.count(*iti) == 4) {
            std::multimap<
              int, std::pair<MVertex *, std::pair<int, int> > >::iterator itt =
              tcuts.lower_bound(*iti);
            int id0 = abs(itt->second.second.second);
            MVertex *v0 = itt->second.first;
            ++itt;
            int id1 = abs(itt->second.second.second);
            MVertex *v1 = itt->second.first;
            ++itt;
            int id2 = abs(itt->second.second.second);
            MVertex *v2 = itt->second.first;
            ++itt;
            int id3 = abs(itt->second.second.second);
            MVertex *v3 = itt->second.first;
            if(abs(id0 - id1) <= 2 || abs(id2 - id3) <= 2) {
              cutTriangles(ttt, f[i], v0, v1, ge, ecuts, *iti, F, inverseClassificationEdges, model_size,TJUNCTION);
              cutTriangles(ttt, f[i], v2, v3, ge, ecuts, *iti, F, inverseClassificationEdges, model_size,TJUNCTION);
            }
            else if(abs(id0 - id2) <= 2 || abs(id1 - id3) <= 2) {
              cutTriangles(ttt, f[i], v0, v2, ge, ecuts, *iti, F, inverseClassificationEdges, model_size,TJUNCTION);
              cutTriangles(ttt, f[i], v1, v3, ge, ecuts, *iti, F, inverseClassificationEdges, model_size,TJUNCTION);
            }
            else if(abs(id0 - id3) <= 2 || abs(id1 - id2) <= 2) {
              cutTriangles(ttt, f[i], v0, v3, ge, ecuts, *iti, F, inverseClassificationEdges, model_size,TJUNCTION);
              cutTriangles(ttt, f[i], v1, v2, ge, ecuts, *iti, F, inverseClassificationEdges, model_size,TJUNCTION);
            }
            else{
              printf("ERRROOOOR IN 4 CUTS %d %d %d %d\n",id0,id1,id2,id3);
            }
          }
          else if(tcuts.count(*iti) == 6) {            
            std::multimap<
              int, std::pair<MVertex *, std::pair<int, int> > >::iterator itt =
              tcuts.lower_bound(*iti);
            std::pair<int,MVertex*> id[10];
            for (std::size_t kk=0;kk< tcuts.count(*iti);kk++){
              id[kk] = std::make_pair(abs(itt->second.second.second), itt->second.first);
              ++itt;
            }
            std::sort(id,id+6);
            cutTriangles(ttt, f[i], id[0].second, id[1].second, ge, ecuts, *iti, F, inverseClassificationEdges, model_size,TJUNCTION);
            cutTriangles(ttt, f[i], id[2].second, id[3].second, ge, ecuts, *iti, F, inverseClassificationEdges, model_size,TJUNCTION);
            cutTriangles(ttt, f[i], id[4].second, id[5].second, ge, ecuts, *iti, F, inverseClassificationEdges, model_size,TJUNCTION);
            //            printf("%d %d %d %d %d %d\n",id[0].first,id[1].first,id[2].first,id[3].first,id[4].first,id[5].first);
          }
          else {
            Msg::Error("TODO %lu in CutMesh !!!!!!!", tcuts.count(*iti));
          }
        }
        newT.insert(newT.begin(), ttt.begin(), ttt.end());
      }
      f[i]->triangles = newT;
    }

    fprintf(F, "};\n");
    fclose(F);

    F = fopen("edges.pos", "w");
    fprintf(F, "View \"\"{\n");

    for(std::map<MEdge, int>::iterator it = ecuts.begin(); it != ecuts.end();
        ++it) {
      GEdge *ge = gm->getEdgeByTag(it->second);
      ge->lines.push_back(
        new MLine(it->first.getVertex(0), it->first.getVertex(1)));
      fprintf(F, "SL(%g,%g,%g,%g,%g,%g){1,1};\n", it->first.getVertex(0)->x(),
              it->first.getVertex(0)->y(), it->first.getVertex(0)->z(),
              it->first.getVertex(1)->x(), it->first.getVertex(1)->y(),
              it->first.getVertex(1)->z());
      for(int i = 0; i < 2; i++) {
        if(std::find(ge->mesh_vertices.begin(), ge->mesh_vertices.end(),
                     it->first.getVertex(i)) == ge->mesh_vertices.end()) {
          ge->mesh_vertices.push_back(it->first.getVertex(i));
          it->first.getVertex(i)->setEntity(ge);
        }
      }
    }
    fprintf(F, "};\n");
    fclose(F);
    CTX::instance()->mesh.changed = ENT_ALL;

    // re-make the mesh edges for boundaries
    {
      std::set<GEdge*>::iterator it = modelEdges.begin();    
      for (; it != modelEdges.end();++it){
        for (size_t i=0;i< (*it)->lines.size(); ++i)delete (*it)->lines[i];
        (*it)->lines.clear();
      }
    }
    {
      size_t nbl = 0;
      std::map<MEdge, GEdge*, MEdgeLessThan>::iterator it = inverseClassificationEdges.begin();
      for (; it != inverseClassificationEdges.end();++it){
        it->second->lines.push_back(new MLine (it->first.getVertex(0),it->first.getVertex(1)));
        nbl += 1;
      }      
      Msg::Info("created %i lines on the boundary", nbl);
    }
    
    GModel::current()->pruneMeshVertexAssociations();
  }


  // create edge cut points on the original edges of the uncut mesh
  // find out t-junctions
  int correctionOnCutGraph(std::map<MEdge, edgeCuts, MEdgeLessThan> &cuts,
                           std::map<MVertex *, MVertex *, MVertexPtrLessThan> &new2old,
                           std::vector<GFace*> &f,
                           std::vector<std::pair<MTriangle*,int> > &tj)
  {

    SBoundingBox3d bbox = GModel::current()->bounds();
    double TOLERANCE = 1.e-08*bbox.diag();
    
    std::map<MEdge, MEdge, MEdgeLessThan> duplicateEdges;

    std::multimap<MEdge, MEdge, MEdgeLessThan> t_junctions;
    std::set<MEdge, MEdgeLessThan> originals;

    // create empty edgecuts for t-junctions
    {
      edgeCuts empty;
      for(size_t i = 0; i < f.size(); i++) {
        for(size_t j = 0; j < f[i]->triangles.size(); j++) {
          for(size_t k = 0; k < 3; k++) {
            MEdge e =  f[i]->triangles[j]->getEdge(k);
            std::map<MEdge, edgeCuts, MEdgeLessThan>::iterator it_cg = cuts.find(e);
            if (it_cg == cuts.end())cuts[e] = empty;
          }
        }
      }
    }

    {
      for(std::map<MEdge, edgeCuts, MEdgeLessThan>::iterator it = cuts.begin();
          it != cuts.end(); ++it) {
        MVertex *v0 = it->first.getVertex(0);
        MVertex *v1 = it->first.getVertex(1);        
        MVertex *v2 = new2old.find(v0) == new2old.end() ? v0 : new2old[v0];
        MVertex *v3 = new2old.find(v1) == new2old.end() ? v1 : new2old[v1];                
        MEdge e(v2, v3);
        MEdge ex(v0, v1);
        duplicateEdges[ex] = e;
        std::pair<MEdge, MEdge> p = std::make_pair(e,ex);
        t_junctions.insert (p);
        originals.insert(e);
      }
    }
    FILE *ff = NULL;
    if(Msg::GetVerbosity() == 99) {      
      ff = fopen("t_junctions.pos","w");
      fprintf(ff,"View \"t_junctions\"{\n");
    }

    std::set<MEdge,MEdgeLessThan> toRemove;
    
    {
      MEdgeEqual op_equal;
      std::set<MEdge, MEdgeLessThan>::iterator it =  originals.begin();
      int t_counter = 0;
      for (; it != originals.end();++it){
        MEdge e = *it;        

        edgeCuts cut_original;
        
        std::map<MEdge, edgeCuts, MEdgeLessThan>::iterator it_original = cuts.find(e);
        if (it_original != cuts.end()) {
          cut_original.n_occur ++;
          for (size_t k = 0 ; k < it_original->second.ps.size() ; ++k){
            bool added = cut_original.add (it_original->second.ps[k],
                                           it_original->second.indexOfCuts[k],
                                           it_original->second.idsOfCuts[k],TOLERANCE);

            // !!! Fixme: error if !added
            (void)(added); // just suppress the warning so that it compiles
          }
        }
        
        for (std::multimap<MEdge, MEdge, MEdgeLessThan>::iterator ite = t_junctions.lower_bound(e);
             ite != t_junctions.upper_bound(e);++ite){
          std::map<MEdge, edgeCuts, MEdgeLessThan>::iterator it_copy = cuts.find(ite->second);
          
          if (!op_equal(ite->first,ite->second)){
            cut_original.n_occur ++;
            for (size_t k = 0 ; k < it_copy->second.ps.size() ; ++k){
              bool added = cut_original.add (it_copy->second.ps[k],
                                             it_copy->second.indexOfCuts[k],
                                             it_copy->second.idsOfCuts[k],TOLERANCE);
              // !!! Fixme: error if !added
              (void)(added); // just suppress the warning so that it compiles
            }
	    toRemove.insert(it_copy->first);
          }
        }

        bool t_j = false;
        for (size_t i=0;i<cut_original.p_occur.size();i++)
          if (cut_original.p_occur[i] != cut_original.n_occur) t_j = true;
        if (t_j){
          std::vector<MEdge> all_copies;
          all_copies.push_back(e);
          for (std::multimap<MEdge, MEdge, MEdgeLessThan>::iterator ite = t_junctions.lower_bound(e);
               ite != t_junctions.upper_bound(e);++ite){
            all_copies.push_back(ite->second);
          }
          for (size_t K=0;K<all_copies.size();K++){
            std::map<MEdge, edgeCuts, MEdgeLessThan>::iterator it_e = cuts.find(all_copies[K]);
            for(size_t i = 0; i < f.size(); i++) {
              for(size_t j = 0; j < f[i]->triangles.size(); j++) {
                for(size_t k = 0; k < 3; k++) {
                  MEdge e =  f[i]->triangles[j]->getEdge(k);
                  if (op_equal(e, it_e->first)){
                    for (size_t l=0;l<it_e->second.indexOfCuts.size();l++){
                      std::pair<MTriangle*,int> pp = std::make_pair(f[i]->triangles[j],it_e->second.indexOfCuts[l]);
                      //                      printf ("TRIANGLE %lu is associated to T-Junction for iso %d\n",f[i]->triangles[j]->getNum(),
                      //                              it_e->second.indexOfCuts[l]);
                      tj.push_back(pp);
                    }
                  }
                }
              }
            }
          }
          
          if (ff)fprintf(ff,"SL(%g,%g,%g,%g,%g,%g) {%d,%d};\n",e.getVertex(0)->x(),
			 e.getVertex(0)->y(),e.getVertex(0)->z(),
			 e.getVertex(1)->x(),e.getVertex(1)->y(),e.getVertex(1)->z(),t_counter,t_counter);
	  t_counter++;
    //          printf ("original %lu %lu --> %lu copies, exists %d %lu pts (%d)",
    //                  e.getVertex(0)->getNum(),e.getVertex(1)->getNum(),  t_junctions.count(e) , it_original != cuts.end(),
    //                  cut_original.ps.size(),cut_original.n_occur  );
    //          for (size_t i=0;i<cut_original.p_occur.size();i++)printf("%d ",cut_original.p_occur[i]);
    //          printf("\n");
        }

        if (!cut_original.indexOfCuts.empty())cuts[e] = cut_original;
	else toRemove.insert(*it);
      }
    }
    if (ff){
      fprintf(ff,"};\n");
      fclose(ff);
    }
    {
      std::set<MEdge,MEdgeLessThan>::iterator it =  toRemove.begin();
      for (; it != toRemove.end();++it){
	cuts.erase(*it);
      }
    }


    return 0;
  }

  bool computeQuadLayout(std::map<MVertex *, double> &potU,
                         std::map<MVertex *, double> &potV,
                         std::map<MEdge, MEdge, MEdgeLessThan> &duplicateEdges,
                         std::map<MEdge, edgeCuts, MEdgeLessThan> &cuts,
                         std::vector<cutGraphPassage> &passages,
                         std::vector<std::pair<MTriangle*,int> > &tj)
  {
    potU.clear();
    potV.clear();
    cuts.clear();
    
    if (!computePotential(gm, f, *myAssembler, C, new2old, groups,
                          d0, d1, G, potU, potV, passages))return false;
    
    std::map<MEdge, MEdge, MEdgeLessThan>duplicates;
    computeDuplicateEdgesOnCutGraph (G, duplicates);
    
    std::map<size_t,int> COUNTS;
    bool success = computeIsos(gm, f, singularities, C, new2old, duplicateEdges, groups,
                               groups_cg, potU, potV, cutG, G, cuts, passages, corners, COUNTS, duplicates);


    double chi_sing = 0.0;
    for (std::map<size_t,int>::iterator it = COUNTS.begin(); it != COUNTS.end(); ++it){
      MVertex *cor = gm->getMeshVertexByTag(it->first);
      if (corners.find(cor) == corners.end()){
        int diff = 4 - (it->second-it->first*1000);
        chi_sing += (double)diff/4.0;
      }
      else {
        int diff = 1 - (it->second-it->first*1000);
        chi_sing += (double)diff/4.0;
      }
    }

    //    printf("chi_sing ...%12.5E\n",chi_sing);
    
    double chi_curv = 0;
    for(std::map<MVertex *, double>::iterator it = gaussianCurvatures.begin();it != gaussianCurvatures.end(); ++it){
      bool bnd = boundaries.find(it->first) != boundaries.end();
      double XXX =  !bnd ? 2*M_PI-it->second : M_PI-it->second;
      chi_curv += XXX;
    }

    if (fabs (chi_sing-chi_curv/2/M_PI) < 1.e-8)
      Msg::Info("Compatibility test SUCCEDED : POINCARE CHARACTERISTIC %12.5E",chi_sing);  
    else{
      Msg::Info("Compatibility test FAILED %12.5E (singularities) vs %12.5E (curvature/exact)",chi_sing,chi_curv/2/M_PI);
    }
    
    correctionOnCutGraph(cuts, new2old, f, tj);
    
    double MAXX = 0.;
    //    double SUM_LEFT = 0 , SUM_RIGHT = 0;
    for(size_t i = 0; i < groups_cg.size(); i++) {
      if (G[i].crosses[0]->inInternalBoundary)continue;
      double MAXD1 = -1.e22, MIND1 = 1.e22, MAXD2 = -1.e22, MIND2 = 1.e22;
      for(size_t j = 0; j < G[i].left.size(); j++) {
        double Ul = potU[G[i].left[j]];
        double Ur = potU[G[i].right[j]];
        double Vl = potV[G[i].left[j]];
        double Vr = potV[G[i].right[j]];
        double D1 = Ul - G[i].mat[0][0] * Ur - G[i].mat[0][1] * Vr;
        double D2 = Vl - G[i].mat[1][0] * Ur - G[i].mat[1][1] * Vr;
        MAXD1 = std::max(D1, MAXD1);
        MAXD2 = std::max(D2, MAXD2);
        MIND1 = std::min(D1, MIND1);
        MIND2 = std::min(D2, MIND2);

      }
      double ROT = 0;
      if (G[i].mat[0][0] == 1) ROT = 0;
      else if (G[i].mat[0][0] == -1) ROT = M_PI;
      else if (G[i].mat[0][1] == -1) ROT = M_PI/2;
      else if (G[i].mat[1][0] == -1) ROT = -M_PI/2;

      Msg::Debug("group %3d DA(%12.5E %12.5E %12.5E) D2(%12.5E %12.5E %12.5E) ROT %12.5E",
                 G[i].groupId, MAXD1, MIND1, MAXD1 - MIND1, MAXD2, MIND2,
                 MAXD2 - MIND2, ROT);
      G[i].jump1 = MAXD1;
      G[i].jump2 = MAXD2;
      MAXX = std::max(MAXD2 - MIND2, MAXX);
    }
    if(MAXX < 1.e-09)
      Msg::Info("Success in computing potentials (all jumps are OK)");
    else
      Msg::Warning("Quad Layout Failure");
    return success;
  }
  void getH (std::map<int, std::vector<double> > & dataH){
    for(std::set<MVertex *, MVertexPtrLessThan>::iterator it = vs.begin();
        it != vs.end(); ++it) {
      double h;
      myAssembler->getDofValue(*it, 0, 1, h);
      std::vector<double> jj;
      jj.push_back(h);
      //    printf("adding data for %lu\n",(*it)->getNum());
      dataH[(*it)->getNum()] = jj;
    }
  }

  void getH (std::map<int, double > & dataH){
    for(std::set<MVertex *, MVertexPtrLessThan>::iterator it = vs.begin();
        it != vs.end(); ++it) {
      double h;
      myAssembler->getDofValue(*it, 0, 1, h);
      dataH[(*it)->getNum()] = h;
    }
  }

  
  void getDir (std::map<int, std::vector<double> > &dataDir,
               std::map<int, std::vector<double> > &dataDirOrtho){
    for(std::map<MTriangle *, SVector3>::iterator it = d0.begin();
        it != d0.end(); ++it) {
      std::vector<double> jj;
      jj.push_back(it->second.x());
      jj.push_back(it->second.y());
      jj.push_back(it->second.z());
      dataDir[it->first->getNum()] = jj;
    }
    for(std::map<MTriangle *, SVector3>::iterator it = d1.begin();
        it != d1.end(); ++it) {
      std::vector<double> jj;
      jj.push_back(it->second.x());
      jj.push_back(it->second.y());
      jj.push_back(it->second.z());
      dataDirOrtho[it->first->getNum()] = jj;
    }
  }
};

static void findPhysicalGroupsForSingularities(GModel *gm,
                                               std::vector<GFace *> &f,
                                               std::map<MVertex *, int> &temp)
{
  std::map<int, std::vector<GEntity *> > groups[4];
  gm->getPhysicalGroups(groups);
  for(std::map<int, std::vector<GEntity *> >::iterator it = groups[0].begin();
      it != groups[0].end(); ++it) {
    std::string name = gm->getPhysicalName(0, it->first);
    if(name == "SINGULARITY_OF_INDEX_THREE") {
      for(size_t j = 0; j < it->second.size(); j++) {
        if(!it->second[j]->mesh_vertices.empty())
          temp[it->second[j]->mesh_vertices[0]] = 1;
      }
    }
    else if(name == "SINGULARITY_OF_INDEX_FIVE") {
      for(size_t j = 0; j < it->second.size(); j++) {
        if(!it->second[j]->mesh_vertices.empty())
          temp[it->second[j]->mesh_vertices[0]] = -1;
      }
    }
    else if(name == "SINGULARITY_OF_INDEX_SIX") {
      for(size_t j = 0; j < it->second.size(); j++) {
        if(!it->second[j]->mesh_vertices.empty())
          temp[it->second[j]->mesh_vertices[0]] = -2;
      }
    }
    else if(name == "SINGULARITY_OF_INDEX_EIGHT") {
      for(size_t j = 0; j < it->second.size(); j++) {
        if(!it->second[j]->mesh_vertices.empty())
          temp[it->second[j]->mesh_vertices[0]] = -4;
      }
    }
    else if(name == "SINGULARITY_OF_INDEX_TWO") {
      for(size_t j = 0; j < it->second.size(); j++) {
        if(!it->second[j]->mesh_vertices.empty())
          temp[it->second[j]->mesh_vertices[0]] = 2;
      }
    }
  }
}

static void computeValidPassages ( std::vector<cutGraphPassage> &passages) {
  // sorted with respect to ids.
  std::vector<cutGraphPassage> todo;
    
  for (size_t i=0; i< passages.size(); ++i){
    if (passages[i].diff<1.e-12)todo.push_back(passages[i]);
    else {
      for (size_t j=i+1; j< passages.size(); ++j){
        if (passages[i] == passages [j]){
          todo.push_back(passages[i]);
        }
      }
    }
  }
  passages = todo;
}

  
static int computeCrossFieldAndH(GModel *gm, std::vector<GFace *> &f,
                                 std::vector<int> &tags, bool layout = true)
{
  quadLayoutData qLayout(gm, f, gm->getName());
  std::map<MVertex *, int> temp;
  std::map<int, std::vector<double> > dataH;
  std::map<int, std::vector<double> > dataTHETA;
  std::map<int, std::vector<double> > dataDir;
  std::map<int, std::vector<double> > dataDirOrtho;
  std::map<int, std::vector<double> > dataU;
  std::map<int, std::vector<double> > dataV;
  std::map<MVertex *, double> potU, potV;
  findPhysicalGroupsForSingularities(gm, f, temp);
  std::map<MEdge, MEdge, MEdgeLessThan> duplicateEdges;
  std::map<MEdge, edgeCuts, MEdgeLessThan> cuts;
  if(temp.size()) {
    Msg::Info("Computing cross field from %d prescribed singularities",
              temp.size());
    qLayout.computeCrossFieldAndH(&temp, dataTHETA);
    qLayout.computeCutGraph(duplicateEdges);
  }
  else {
    Msg::Info("Computing a cross field");
    qLayout.computeCrossFieldAndH();
    qLayout.computeCutGraph(duplicateEdges);
    qLayout.computeThetaUsingHCrouzeixRaviart(dataTHETA);
  }

  std::vector<std::pair<MTriangle*,int> > t_junctions;
  if(layout) {
    /// ARGHHHHHHHHHHHHHHHHH
    std::vector<cutGraphPassage> passages;
    int ITER = 0;
    while (1){
      qLayout.computeQuadLayout(potU, potV, duplicateEdges, cuts, passages, t_junctions);
      for (size_t i=0 ; i< passages.size() ; ++i){
        passages[i].analyze(potU,potV,qLayout.G,qLayout.new2old);
        //        passages[i].Print("All ");
        //        passages[i].PrintFile();
      }
      computeValidPassages( passages );
      if (ITER++ ==0)break;
    }
    //    for (size_t i=0 ; i< passages.size() ; ++i){
    //      passages[i].Print("All ");
    //      passages[i].PrintFile();
    //    }
  }

  PViewDataGModel *d = new PViewDataGModel;
  PViewDataGModel *dt = new PViewDataGModel(PViewDataGModel::ElementNodeData);
  PViewDataGModel *dd = new PViewDataGModel(PViewDataGModel::ElementData);
  std::string name = gm->getName() + "_H";
  d->setName(name);
  d->setFileName(name + ".msh");
  name = gm->getName() + "_Theta";
  dt->setName(name);
  dt->setFileName(name + ".msh");
  name = gm->getName() + "_Directions";
  dd->setName(name);
  dd->setFileName(name + ".msh");
  PViewDataGModel *U = NULL;
  PViewDataGModel *V = NULL;
  if(layout) {
    U = new PViewDataGModel(PViewDataGModel::ElementNodeData);
    V = new PViewDataGModel(PViewDataGModel::ElementNodeData);
    name = gm->getName() + "_U";
    U->setName(name);
    U->setFileName(name + ".msh");
    name = gm->getName() + "_V";
    V->setName(name);
    V->setFileName(name + ".msh");

    for(size_t i = 0; i < f.size(); i++) {
      for(size_t j = 0; j < f[i]->triangles.size(); j++) {
        MTriangle *t = f[i]->triangles[j];
        double a = potU[f[i]->triangles[j]->getVertex(0)];
        double b = potU[f[i]->triangles[j]->getVertex(1)];
        double c = potU[f[i]->triangles[j]->getVertex(2)];
        std::vector<double> ts;
        ts.push_back(a);
        ts.push_back(b);
        ts.push_back(c);
        dataU[t->getNum()] = ts;
        a = potV[f[i]->triangles[j]->getVertex(0)];
        b = potV[f[i]->triangles[j]->getVertex(1)];
        c = potV[f[i]->triangles[j]->getVertex(2)];
        ts.clear();
        ts.push_back(a);
        ts.push_back(b);
        ts.push_back(c);
        dataV[t->getNum()] = ts;
      }
    }

    U->addData(gm, dataU, 0, 0.0, 1, 1);
    U->finalize();
    V->addData(gm, dataV, 0, 0.0, 1, 1);
    V->finalize();
  }
  qLayout.getH (dataH);
  qLayout.getDir (dataDir,dataDirOrtho);
  
  d->addData(gm, dataH, 0, 0.0, 1, 1);
  d->finalize();
  dt->addData(gm, dataTHETA, 0, 0.0, 1, 1);
  dt->finalize();
  dd->addData(gm, dataDir, 0, 0.0, 1, 3);
  dd->addData(gm, dataDirOrtho, 1, 0.0, 1, 3);
  dd->finalize();

  std::string posout = gm->getName() + "_QLayoutResults.pos";
  std::string temp_  = gm->getName() + "_temp.pos";
  
  qLayout.restoreInitialMesh();
  dt->writePOS(posout, false, true, true);
  dd->writePOS(posout, false, true, true);
  d->writePOS(posout, false, true, true);
  // a temporary file
  d->writePOS(temp_, false, true, false);
  if(layout) {
    U->writePOS(posout, false, true, true);
    V->writePOS(posout, false, true, true);
  }
  //  return 0;

  GmshMergePostProcessingFile (temp_);
  tags.push_back(PView::list.size() - 1);
  PView::list[PView::list.size()-1]->getData()->setName("H");

  /* After the cut, the 'theta' view is no longer valid
   * deleting them for the moment ... */
  PView* viewTheta = PView::getViewByName("theta");
  if (viewTheta) delete viewTheta;
  
  Msg::Info("Cutting the mesh");
  std::map<MEdge,GEdge*,MEdgeLessThan> inverseClassificationEdges;
  qLayout.cutMesh(cuts,inverseClassificationEdges, t_junctions);
  if(Msg::GetVerbosity() == 99) 
    gm->writeMSH("cutmesh.msh", 4.0, false, true);

  constexpr bool do_reclassify = false; /* no longer needed, done by QMT */
  if (do_reclassify) {
    Msg::Info("Classifying the model");
    discreteEdge *de = new discreteEdge(
        GModel::current(), GModel::current()->getMaxElementaryNumber(1) + 1, 0, 0);
    GModel::current()->add(de);
    computeNonManifoldEdges(GModel::current(), de->lines, true);
    classifyFaces(GModel::current(), M_PI / 4, false);
    GModel::current()->remove(de);
    //  delete de;
    GModel::current()->pruneMeshVertexAssociations();
    /* Debug export */
    std::string mshout = gm->getName() + "_Cut.msh";
    if(Msg::GetVerbosity() == 99)
      gm->writeMSH(mshout, 4.0, false, true);
    /* Validity check */
    int countError = 0;
    for(GModel::fiter it = GModel::current()->firstFace();
        it != GModel::current()->lastFace(); it++) {
      if((*it)->edges().size() != 4) {
        Msg::Warning("quad layout failed : face %lu has %lu boundaries",
            (*it)->tag(), (*it)->edges().size());
        countError++;
      }
    }
    if(!countError) {
      Msg::Info(
          "Quad layout success : the model is partitioned in %d master quads",
          GModel::current()->getNumFaces());
      Msg::Info("Partitioned mesh has been saved in %s", mshout.c_str());
      Msg::Info("Result of computations have been saved in %s", posout.c_str());
    }
  }

  //

  delete d;
  delete dd;
  delete dt;
  if(layout) {
    delete U;
    delete V;
  }

  return 0;
}

#endif // from the beginning of the file



int computeCrossFieldAndH(GModel *gm,
                          std::map<int, std::vector<double> > &dataH,
                          std::map<int, std::vector<double> > &dataDir,
                          std::map<int, std::vector<double> > &dataDirOrtho)
{
#if defined(HAVE_SOLVER) && defined(HAVE_POST)
  std::vector<GFace *> f;
  getFacesOfTheModel(gm, f);
  quadLayoutData qLayout(gm, f, gm->getName());
  std::map<MVertex *, int> temp;
  findPhysicalGroupsForSingularities(gm, f, temp);
  if(temp.size()) {
    Msg::Info("Computing cross field from %d prescribed singularities",
              temp.size());
    std::map<int, std::vector<double> > dataTHETA;
    qLayout.computeCrossFieldAndH(&temp, dataTHETA);
  }
  else {
    Msg::Info("Computing a cross field");
    qLayout.computeCrossFieldAndH();
  }
  qLayout.computeUniqueVectorsPerTriangle_old();

  qLayout.getH (dataH);
  qLayout.getDir (dataDir,dataDirOrtho);

  {  
    std::map<MVertex*, double> source;	     
    computeSingularities(qLayout.C, qLayout.singularities, qLayout.indices, f,qLayout.gaussianCurvatures, source);
    computeUniqueVectorPerTriangle(gm, f, qLayout.C, qLayout.d0, qLayout.d1);
    computeSingularities(f,qLayout.d0,qLayout.d1, qLayout.singularities, qLayout.indices, qLayout.gaussianCurvatures, true);    
    
    std::string _ugly  = gm->getName()+"_singularities.txt";
    FILE *f__ = fopen (_ugly.c_str(), "w");
    fprintf(f__,"%lu\n",qLayout.singularities.size());
    for (std::set<MVertex *, MVertexPtrLessThan>::iterator it = qLayout.singularities.begin(); it != qLayout.singularities.end();++it){
      fprintf(f__,"%d %22.15E %22.15E %22.15E %d %d\n",qLayout.indices[*it],(*it)->x(),(*it)->y(),(*it)->z(),(*it)->onWhat()->dim(),
	      (*it)->onWhat()->tag());
    }
    fclose(f__);
  }
  
  return 0;
#else
  Msg::Error("Cross field computation requires solver and post-pro module");
  return -1;
#endif
}

int computeCrossFieldAndH(GModel *gm)
{

  std::vector<GFace *> f;
  getFacesOfTheModel(gm, f);

  std::vector<int> tags;
  
#if defined(HAVE_SOLVER) && defined(HAVE_POST)
  return computeCrossFieldAndH(gm, f, tags, false);
#else
  Msg::Error("Cross field computation requires solver and post-pro module");
  return -1;
#endif
}

int computeStructuredQuadMesh(GModel *gm, std::vector<int> &tags)
{
  
  if(gm->getMeshStatus(true) < 2){
    Msg::Error ("Computation of a structured quad mesh requires an initial mesh");
    return -1;
    //    gm->mesh(2);
  }
  
  const bool WRITE_MESHES = true;
  std::vector<GFace *> f;
  getFacesOfTheModel(gm, f);

#if defined(HAVE_QUADMESHINGTOOLS)
  QMT::TMesh boundary;
  bool oki = QMT::import_TMesh_from_gmsh(gm->getName(),boundary);
  if (!oki) {
    Msg::Error("Failed to import triangular mesh");
    return -1;
  }
  QMT::BoundaryProjector projector(boundary);
#endif

//   std::string initialGeometryName = "no_geometry";
// #if defined(HAVE_QUADMESHINGTOOLS)
//   std::string gname = gm->getName() + "_init_geo.brep";
//   GmshWriteFile(gname);
//   initialGeometryName = gname;
// #endif

#if defined(HAVE_SOLVER) && defined(HAVE_POST)
  int cf_status = computeCrossFieldAndH(gm, f, tags, true);
  if (cf_status == -1) return cf_status;
#if defined(HAVE_QUADMESHINGTOOLS)
  if (QMT_Utils::env_var("qstop") == "cf") {
    Msg::Warning("qstop=cf, stop after cross field computation");
    return -1;
  }

  std::string quad_layout_name = gm->getName();
  // int H_tag = tags.size() > 0 ? tags[0] : -1;
  double size_min = CTX::instance()->mesh.lcMin;
  double size_max = CTX::instance()->mesh.lcMax;
  if (CTX::instance()->mesh.lcMin != 0. && CTX::instance()->mesh.lcFactor) {
    size_min *= CTX::instance()->mesh.lcFactor;
  }
  if (CTX::instance()->mesh.lcMax != 1.e22 && CTX::instance()->mesh.lcFactor) {
    size_max *= CTX::instance()->mesh.lcFactor;
  }
  if (size_min == 0 && size_max == 1.e22) {
    SBoundingBox3d bbox = gm->bounds();
    size_min = 0.1 * bbox.diag();
    Msg::Warning("No size specified, using hmin = 0.1*bbox diagonal");
  }

  /* Generation */
  // QMT::QMesh Q;
  // bool okg = QMT::generate_quad_mesh_from_gmsh_colored_triangulation(
  //   quad_layout_name, H_tag, size_min, size_max, Q, &projector, &entityToInitialEntity);

  QuadMeshingOptions opt;
  QuadMeshingState state;
  opt.sizemap_nb_quads = 20000;

  int status_sizemap = computeQuadSizeMap(gm, opt, state);
  if (status_sizemap != 0) {
    Msg::Error("failed to compute size map");
    return -1;
  }
  int sizemapTag = -1;
  if (sizemapTag == -1) {
    PView* view_s = PView::getViewByName("s");
    if (view_s) {
      sizemapTag = view_s->getTag();
    }
  }
  if (sizemapTag == -1) {
    Msg::Error("Quad size map (view 's') not found but required");
    return -1;
  }

  bool repair_bad_decomposition = false;
  QMT::QMesh Q;
  bool okg = QMT::generate_quad_mesh_via_tmesh_quantization(
      quad_layout_name, sizemapTag, size_min, size_max, Q, &projector, repair_bad_decomposition);
  if (!okg) {
    Msg::Error("Failed to generate quad mesh");
    return -1;
  }

  bool oke1 = QMT::export_qmesh_to_gmsh_mesh(Q, "qmesh_init");
  if (!oke1) {
    Msg::Error("Failed to export quad mesh");
    return -1;
  }
  if (WRITE_MESHES) GmshWriteFile(gm->getName()+"_qmesh_init.msh");


  {
    /* Temporary solution because CAD not transfered */
    Msg::Warning("temporary solution: assigning closest CAD entities");
    bool oka = QMT::assignClosestEntities(Q, projector);
    if (!oka) {
      Msg::Error("Failed to assign quad vertices to closest entities in the BoundaryProjector");
      return -1;
    }
  }

  {
    /* Temporary solution because CAD not transfered */
    Msg::Warning("temporary solution: fill size values with probing");
    bool oksm = QMT::fill_vertex_sizes_from_sizemap(Q, sizemapTag);
    if (!oksm) {
      Msg::Error("Failed to evaluate size map on quad mesh");
      return -1;
    }
  }
  

  /* Simplification */
  double hc = 0.9;
  bool oks = QMT::simplify_quad_mesh(Q, hc, -1, &projector);
  if (!oks) {
    Msg::Error("Failed to simplify quad mesh");
    return -1;
  }
  bool oke2 = QMT::export_qmesh_to_gmsh_mesh(Q, "qmesh_simplified");
  if (!oke2) {
    Msg::Error("Failed to export quad mesh");
    return -1;
  }
  if (WRITE_MESHES) GmshWriteFile(gm->getName()+"_qmesh_simplified.msh");

  if (false) {
    Msg::Warning("Smoothing disabled for the moment");
  } else {
    /* Smoothing */
    size_t smoothing_iter = 100;
    bool oksm = QMT::smooth_quad_mesh(Q, smoothing_iter, &projector);
    if (!oksm) {
      Msg::Error("Failed to smooth quad mesh");
      return -1;
    }
    bool oke3 = QMT::export_qmesh_to_gmsh_mesh(Q, "qmesh_smoothed");
    if (!oke3) {
      Msg::Error("Failed to export quad mesh");
      return -1;
    }
    if (WRITE_MESHES) GmshWriteFile(gm->getName()+"_qmesh_smoothed.msh");
  }

  //  PView* hhh = PView::getViewByTag(69);
  //  if (hhh) delete hhh;

#endif
  return cf_status;
#else
  Msg::Error("Cross field computation requires solver and post-pro module");
  return -1;
#endif
}

int create_datalist_view_from_scalar_field(
    std::vector<GFace *> f,
    std::map<int, double>& dataH,
    PViewDataList* d) {
  std::vector<double> data;
  int numElements = 0;
  for(size_t i = 0; i < f.size(); i++) {
    for(size_t j = 0; j < f[i]->triangles.size(); j++) {
      MTriangle *t = f[i]->triangles[j];
      size_t vs[3] = {t->getVertex(0)->getNum(),
        t->getVertex(1)->getNum(), t->getVertex(2)->getNum()};
      SPoint3 ps[3] = {t->getVertex(0)->point(),
        t->getVertex(1)->point(), t->getVertex(2)->point()};
      double hs[3];
      for (size_t lv = 0; lv < 3; ++lv) {
        auto it = dataH.find(vs[lv]);
        if (it == dataH.end()) {
          Msg::Error("H value not found for vertex num = {}", vs[lv]);
          return -1;
        }
        hs[lv] = it->second;
      }
      for (size_t d = 0; d < 3; ++d) {
        for (size_t lv = 0; lv < 3; ++lv) {
          data.push_back(ps[lv][d]);
        }
      }
      for (size_t lv = 0; lv < 3; ++lv) {
        data.push_back(hs[lv]);
      }
      numElements += 1;
    }
  }
  const char *types[] = {"SP", "VP", "TP", "SL", "VL", "TL", "ST", "VT",
    "TT", "SQ", "VQ", "TQ", "SS", "VS", "TS", "SH",
    "VH", "TH", "SI", "VI", "TI", "SY", "VY", "TY"};
  for(int idxtype = 0; idxtype < 24; idxtype++) {
    if("ST" == std::string(types[idxtype])) {
      d->importList(idxtype, numElements, data, true);
    }
  }
  return 0;
}

int compute_H_from_cross_field_view(GModel * gm, 
    quadLayoutData& qLayout, 
    std::vector<GFace *> f,
    int cf_tag, int& h_tag) {
  int sload = qLayout.loadThetaFromView(cf_tag);
  if (sload != 0) {
    return -1;
  }
  dofManager<double> *myAssembler = computeH(gm, f, qLayout.vs, qLayout.C);
  if (myAssembler == NULL) {
    Msg::Error("Failed to compute H from cross field");
    return -1;
  }

  /* Create a view with 'H' */
  PView* vH = PView::getViewByName("H");
  if (vH) {delete vH; vH = NULL;}
  if (!vH) {
    Msg::Info("create a view 'H'");
    vH = new PView();
    vH->getData()->setName("H");
    h_tag = vH->getTag();
  }

  PViewDataList* d = dynamic_cast<PViewDataList*>(vH->getData());
  if(!d) { // change the view type
    delete vH->getData();
    d = new PViewDataList();
    d->setName("H");
    vH->setData(d);
  }

  std::map<int, double> dataH;
  for(std::set<MVertex *, MVertexPtrLessThan>::iterator it = qLayout.vs.begin(); it != qLayout.vs.end(); ++it) {
    double h;
    myAssembler->getDofValue(*it, 0, 1, h);
    dataH[(*it)->getNum()] = h;
  }

  return create_datalist_view_from_scalar_field(f, dataH, d);
}



/********************************************************/
/********************************************************/
/* Attempt at an API callable step-by-step from the GUI */
/********************************************************/
/********************************************************/

QuadMeshingState::~QuadMeshingState() {
  Msg::Debug("QuadMeshingState destructor call");
  if (this->data_uv_cuts != NULL) {
    std::map<MEdge, edgeCuts, MEdgeLessThan>* ptr = static_cast<std::map<MEdge, edgeCuts, MEdgeLessThan>*>(this->data_uv_cuts);
    delete ptr;
    this->data_uv_cuts = NULL;
  }
  if (this->data_boundary_projector != NULL) {
    QMT::BoundaryProjector* ptr = static_cast<QMT::BoundaryProjector*>(this->data_boundary_projector);
    delete ptr;
    this->data_boundary_projector = NULL;
  }
}

/* generate a view named 'theta' with 3 values per triangle */
int computeCrossField(GModel * gm, const QuadMeshingOptions& opt, QuadMeshingState& state) {
  bool use_prescribed = opt.cross_field_use_prescribed_if_available;
  std::vector<GFace *> f;
  getFacesOfTheModel(gm, f);
  quadLayoutData qLayout(gm, f, gm->getName());
  std::map<MVertex *, int> temp;

  if (opt.cross_field_use_prescribed_if_available) {
    std::vector<GFace *> f;
    getFacesOfTheModel(gm, f);
    findPhysicalGroupsForSingularities(gm, f, temp);
    if(!temp.size()) {
      use_prescribed = false;
      Msg::Warning("prescribed singularities not found, using heat-based cross field computation");
    }
  }
  if (use_prescribed) {
  // if (1) {//DBG
    Msg::Info("Computing cross field from %d prescribed singularities",
        temp.size());
    //ALEX
    std::map<MTriangle *, std::vector<std::vector<SVector3>>> crossEdgTri;
    std::map<MVertex *, double, MVertexPtrLessThan> H;
    ConformalMapping::computeScaledCrossesFromSingularities(gm,crossEdgTri,H);
    ConformalMapping::_viewCrossEdgTri(crossEdgTri, "CM::Crosses");;
    int cf_tag = -1;
    std::set<MTriangle *, MElementPtrLessThan> tri;
    std::vector<std::size_t> keys;
    std::vector<std::vector<double> > values;
    std::vector<std::vector<double> > valuesH;
    for(auto &kv: crossEdgTri){
      MTriangle* t=kv.first;
      tri.insert(kv.first);
      SVector3 v10(t->getVertex(1)->x() - t->getVertex(0)->x(),
    		   t->getVertex(1)->y() - t->getVertex(0)->y(),
    		   t->getVertex(1)->z() - t->getVertex(0)->z());
      SVector3 v20(t->getVertex(2)->x() - t->getVertex(0)->x(),
    		   t->getVertex(2)->y() - t->getVertex(0)->y(),
    		   t->getVertex(2)->z() - t->getVertex(0)->z());
      // SVector3 tNormal = crossprod(v10, v20); //for consistency with Max way of storing theta
      SVector3 tNormal = crossprod(v20, v10);
      tNormal.normalize();
      SVector3 thetaVect(0.0);
      std::vector<double> valTri;
      std::vector<double> valTriH;
      for(size_t k=0;k<3;k++){
    	MEdge eK=t->getEdge(k);
    	MVertex *v0=eK.getVertex(0);
    	MVertex *v1=eK.getVertex(1);
    	SVector3 vE(v1->x() - v0->x(),
    		    v1->y() - v0->y(),
    		    v1->z() - v0->z());
    	vE.normalize();
    	SVector3 V=crossprod(tNormal,vE);
    	V.normalize();
    	SVector3 branch=kv.second[k][0];
    	double thetaE=atan2(dot(branch,V),dot(branch,vE));
	double hE=H[t->getVertex(k)];
        valTri.push_back(thetaE);
	valTriH.push_back(hE);
      }
      keys.push_back(t->getNum());
      values.push_back(valTri);
      valuesH.push_back(valTriH);
    }
    PView* theta = PView::getViewByName("theta");
    if (theta) {delete theta; theta = NULL;}
    gmsh::initialize();
    std::string cname;
    gmsh::model::getCurrent(cname);
    int crossFieldTag = gmsh::view::add("theta");
    if(keys.size()>0)
      gmsh::view::addModelData(crossFieldTag, 0, cname, "ElementData", keys, values);

    PView* viewH = PView::getViewByName("H");
    if (viewH) {delete viewH; viewH = NULL;}
    int HTag = gmsh::view::add("H");
    if(keys.size()>0)
      gmsh::view::addModelData(HTag, 0, cname, "ElementData", keys, valuesH);
    ConformalMapping::_viewScalarTriangles(H,tri, "CM::H");
    // ALEX
    if(0){
      std::map<int, std::vector<double> > dataTHETA;

      const bool createViewTheta = true; /* - View 'theta' */
      qLayout.computeCrossFieldAndH(&temp, dataTHETA, createViewTheta);

    
      /* - View 'H' */
      // int h_tag = -1;
      std::map<int, std::vector<double> > dataH;
      qLayout.getH(dataH);
      std::map<int, double > dataH2;
      for (const auto& kv: dataH) {
	dataH2[kv.first] = kv.second[0];
      }
      PView* vH = PView::getViewByName("H");
      if (vH) {delete vH; vH = NULL;}
      if (!vH) {
	Msg::Info("create a view 'H'");
	vH = new PView();
	vH->getData()->setName("H");
	// h_tag = vH->getTag();
      }
      PViewDataList* d = dynamic_cast<PViewDataList*>(vH->getData());
      if(!d) { // change the view type
	delete vH->getData();
	d = new PViewDataList();
	d->setName("H");
	vH->setData(d);
      }
      int sview = create_datalist_view_from_scalar_field(f, dataH2, d);
      if (sview != 0) {
	Msg::Error("Failed to create view with H");
	return -1;
      }
    }
  } else {
#if defined(HAVE_QUADMESHINGTOOLS)
    int nb_iter = opt.cross_field_iter;
    int cf_tag = -1;
    PView* theta = PView::getViewByName("theta");
    if (theta) {delete theta; theta = NULL;}

    bool okcf = true;
    bool OLD = false;
    if (OLD) {
      okcf = QMT::compute_cross_field_with_heat(gm->getName(),cf_tag,nb_iter,NULL,opt.cross_field_bc_expansion);
    } else {
      std::vector<std::array<double,3> > points;
      std::vector<std::array<size_t,2> > lines;
      std::vector<std::array<size_t,3> > triangles;
      std::vector<MVertex*> origin;
      extractTriangularMeshFromFaces(f, points, origin, lines, triangles);
      std::map<std::array<size_t,2>,double> edgeThetaLocal;
      double thresholdNormConvergence = 1.e-2;
      okcf = QMT::compute_cross_field_with_multilevel_diffusion(
          points,lines,triangles,edgeThetaLocal,nb_iter,
          thresholdNormConvergence, opt.cross_field_bc_expansion);
      std::map<std::array<size_t,2>,double> edgeTheta;
      for (const auto& kv: edgeThetaLocal) {
        std::array<size_t,2> vPairGlobal = {origin[kv.first[0]]->getNum(),origin[kv.first[1]]->getNum()};
        if (vPairGlobal[1] < vPairGlobal[0]) {
          std::sort(vPairGlobal.begin(),vPairGlobal.end());
        } 
        edgeTheta[vPairGlobal] = kv.second;
      }
      QMT::create_cross_field_theta_view(gm->getName(),edgeTheta,cf_tag);
    }
    if (!okcf) {
      Msg::Error("Failed to compute cross field");
      return -1;
    }


    int h_tag = -1;
    int status_h = compute_H_from_cross_field_view(gm, qLayout, f, cf_tag, h_tag);
    if (status_h != 0) {
      Msg::Error("Failed to compute H from cross field view");
      return -1;
    }
#else
    Msg::Error("This Cross field computation requires the QuadMeshingTools module");
    return -1;
#endif
  }

  {
    PView* view = PView::getViewByName("theta");
    if (view) {
      view->getOptions()->visible = 0;
      state.theta_tag = view->getTag();
    }
  }

  {
    PView* view = PView::getViewByName("H");
    if (view) {
      view->getOptions()->visible = 0; /* view not shown by default */
      state.H_tag = view->getTag();
      state.H_min = view->getData()->getMin();
      state.H_max = view->getData()->getMax();
    }
  }

  return 0;
}

int computeQuadSizeMapUniform(GModel * gm, const QuadMeshingOptions& opt, QuadMeshingState& state) {
  PView* vS = PView::getViewByName("s");
  if (vS) {delete vS; vS = NULL;}

  double size_min = CTX::instance()->mesh.lcMin;
  double size_max = CTX::instance()->mesh.lcMax;
  double h = 0;
  if (CTX::instance()->mesh.lcMin != 0. && CTX::instance()->mesh.lcFactor) {
    size_min *= CTX::instance()->mesh.lcFactor;
    h = size_min;
  }
  if (CTX::instance()->mesh.lcMax != 1.e22 && CTX::instance()->mesh.lcFactor) {
    size_max *= CTX::instance()->mesh.lcFactor;
    h = size_max;
  }
  if (size_min == 0 && size_max == 1.e22) {
    SBoundingBox3d bbox = gm->bounds();
    size_min = 0.1 * bbox.diag() * CTX::instance()->mesh.lcFactor;
    h = size_min;
    Msg::Warning("No size specified, using hmin = 0.1*bbox diagonal*clscale");
  }

  double integral = 0.;
  double smin = DBL_MAX;
  double smax = -DBL_MAX;
  std::vector<GFace *> f;
  getFacesOfTheModel(gm, f);
  int numElements = 0;
  std::vector<double> data;
  for(size_t i = 0; i < f.size(); i++) {
    for(size_t j = 0; j < f[i]->triangles.size(); j++) {
      MTriangle *t = f[i]->triangles[j];
      SVector3 pts[3];
      for (size_t d = 0; d < 3; ++d) {
        for (size_t lv = 0; lv < 3; ++lv) {
          data.push_back(t->getVertex(lv)->point()[d]);
        }
      }
      for (size_t lv = 0; lv < 3; ++lv) {
        pts[lv] = t->getVertex(lv)->point();
        data.push_back(h);
      }
      numElements += 1;
      double area = 0.5 * crossprod(pts[2]-pts[0],pts[1]-pts[0]).norm();
      integral += area * 1. / std::pow(h,2);
    }
  }
  double FAC = 1.;
  if (opt.sizemap_nb_quads != 0) { /* target number of quads */
    FAC = double(opt.sizemap_nb_quads) / integral;
    double sf = 1./std::sqrt(FAC);
    smin = DBL_MAX;
    smax = -DBL_MAX;
    for(int ele = 0; ele < numElements; ele++) {
      for(size_t nod = 0; nod < 3; nod++) {
        double& val = data[12*ele+3*3+nod];
        val = sf * val;
        smin = std::min(smin,val);
        smax = std::max(smax,val);
      }
    }
  }

  if (numElements == 0)  {
    Msg::Error("no triangles, failed to build quad size map");
    return -1;
  }

  Msg::Info("create a view 's' with %d triangles", numElements);
  gmsh::initialize();
  int vi = gmsh::view::add("s");
  gmsh::view::addListData(vi, "ST", numElements, data);

  state.s_tag = vi;
  state.s_min = smin;
  state.s_max = smax;
  state.s_nb_quad_estimate = (size_t) (0.5 + FAC * integral);
  Msg::Info("size map: min=%.3f, max=%.3f, estimated number of quads: %li", state.s_min, state.s_max, state.s_nb_quad_estimate);

  return 0;
}

int computeQuadSizeMap(GModel * gm, const QuadMeshingOptions& opt, QuadMeshingState& state) {
  PView* vH = PView::getViewByName("H");
  if (vH == NULL) {
    Msg::Error("view 'H' not found");
    Msg::Warning("computing uniform size map ...");
    return computeQuadSizeMapUniform(gm, opt, state);
  }
  PViewData *vhd = vH->getData();
  if (vH == NULL) {
    Msg::Error("view 'H' has no data");
    return -1;
  }

  double Hmin = DBL_MAX;
  double Hmax = -DBL_MAX;
  {
    /* Restrict to boundary curves if possible (warning: ignore corners !) */
    const std::set<GEdge*, GEntityPtrLessThan>& edges =  gm->getEdges();
    for (const auto& edge : edges) {
      for (size_t i = 0; i < edge->getNumMeshVertices(); ++i) {
        SPoint3 pt = edge->getMeshVertex(i)->point();
        double val = 0.;
        double *qx = 0, *qy = 0, *qz = 0;
        int qn = 0;
        bool gradient = false;
        double tolerance = 0.;
        bool found = vhd->searchScalarWithTol(pt.x(), pt.y(), pt.z(), &val, 0, 0, tolerance, qn,
            qx, qy, qz, gradient);
        if (found) {
          Hmin = std::min(Hmin,val);
          Hmax = std::max(Hmax,val);
        }
      }
    }

    /* Global min / max if no curves */
    if (Hmin == DBL_MAX) Hmin = vhd->getMin();
    if (Hmax == -DBL_MAX) Hmax = vhd->getMax();
  }

  double size_min = CTX::instance()->mesh.lcMin;
  double size_max = CTX::instance()->mesh.lcMax;
  if (CTX::instance()->mesh.lcMin != 0. && CTX::instance()->mesh.lcFactor) {
    size_min *= CTX::instance()->mesh.lcFactor;
  }
  if (CTX::instance()->mesh.lcMax != 1.e22 && CTX::instance()->mesh.lcFactor) {
    size_max *= CTX::instance()->mesh.lcFactor;
  }
  if (size_min == 0 && size_max == 1.e22) {
    SBoundingBox3d bbox = gm->bounds();
    size_min = 0.1 * bbox.diag() * CTX::instance()->mesh.lcFactor;
    Msg::Warning("No size specified, using hmin = 0.1*bbox diagonal*clscale");
  }


  double integral = 0.;
  double smin = DBL_MAX;
  double smax = -DBL_MAX;

  constexpr bool CLAMP_WITH_IMPLIED_MIN_MAX = false;
  double implied_size_min = 0;
  double implied_size_max = 0;
  if (size_min != 0.) {
    implied_size_min = size_min * (exp(-Hmax)/exp(-Hmax));
    implied_size_max = size_min * (exp(-Hmin)/exp(-Hmax));
  } else if (size_max != 1.e22) {
    implied_size_min = size_max * (exp(-Hmax)/exp(-Hmin));
    implied_size_max = size_max * (exp(-Hmin)/exp(-Hmin));
  }
  
  std::vector<std::string> dataType;
  std::vector<int> numElements;
  std::vector<std::vector<double> > data;
  gmsh::view::getListData(vH->getTag(), dataType, numElements, data);
  size_t et_tri = 1e6;
  for(size_t et = 0; et < numElements.size(); ++et)  {
    if (numElements[et] == 0 || data[et].size() != (size_t) numElements[et]*12) continue;
    et_tri = et;
    for(int ele = 0; ele < numElements[et]; ele++) {
      double values[3] = {0,0,0};
      SVector3 pts[3];
      for(size_t nod = 0; nod < 3; nod++) {
        double& val = data[et][12*ele+3*3+nod];
        if (size_min != 0.) {
          val = size_min * (exp(-val)/exp(-Hmax));
        } else if (size_max != 1.e22) {
          val = size_max * (exp(-val)/exp(-Hmin));
        }
        if (CLAMP_WITH_IMPLIED_MIN_MAX) {
          val = std::max(val,implied_size_min);
          val = std::min(val,implied_size_max);
        }
        values[nod] = val;
        smin = std::min(smin,val);
        smax = std::max(smax,val);
        pts[nod] = SVector3(
            data[et][12*ele+nod],
            data[et][12*ele+3+nod],
            data[et][12*ele+6+nod]);
      }
      double area = 0.5 * crossprod(pts[2]-pts[0],pts[1]-pts[0]).norm();
      integral += area * 1. / std::pow(1./3. * (values[0] + values[1] + values[2]),2);
    }
  }
  if (et_tri == 1e6) {
    Msg::Error("failed to find scalar on triangles");
    return -1;
  }

  double FAC = 1.;
  if (opt.sizemap_nb_quads != 0) { /* target number of quads */
    FAC = double(opt.sizemap_nb_quads) / integral;
    double sf = 1./std::sqrt(FAC);
    smin = DBL_MAX;
    smax = -DBL_MAX;
    for(int ele = 0; ele < numElements[et_tri]; ele++) {
      for(size_t nod = 0; nod < 3; nod++) {
        double& val = data[et_tri][12*ele+3*3+nod];
        val = sf * val;
        smin = std::min(smin,val);
        smax = std::max(smax,val);
      }
    }
  }

  if (numElements[et_tri] == 0)  {
    Msg::Error("no triangles, failed to build quad size map");
    return -1;
  }


  PView* vS = PView::getViewByName("s");
  if (vS) {delete vS; vS = NULL;}

  Msg::Info("create a view 's' with %d triangles", numElements[et_tri]);
  int vi = gmsh::view::add("s");
  gmsh::view::addListData(vi, "ST", numElements[et_tri], data[et_tri]);

  state.s_tag = vi;
  state.s_min = smin;
  state.s_max = smax;
  state.s_nb_quad_estimate = (size_t) (0.5 + FAC * integral);
  Msg::Info("size map: min=%.3f, max=%.3f, estimated number of quads: %li", state.s_min, state.s_max, state.s_nb_quad_estimate);

  return 0;
}

int showScaledCrosses(GModel* gm, const QuadMeshingOptions& opt, QuadMeshingState& state) {
  /* Get view tags */
  PView* vH = PView::getViewByName("H");
  if (vH == NULL) {
    Msg::Info("view 'H' not found");
    return -1;
  }
  PView* theta = PView::getViewByName("theta");
  if (!theta) {
    Msg::Error("view 'theta' not found");
    return -1;
  }
  int h_tag = vH->getTag();
  int cf_tag = theta->getTag();

  std::string vname = "scaled_crosses";
  int view_tag = -1;
  PView* view_scaled_cf = PView::getViewByName(vname);
  if (view_scaled_cf != NULL) {
    delete view_scaled_cf;
    view_scaled_cf = NULL;
  }

  PViewDataGModel *_d = dynamic_cast<PViewDataGModel *>(vH->getData());
  bool isModelData = (_d != NULL);

  bool okv = QMT::create_scaled_cross_field_view(gm->getName(), cf_tag, h_tag, isModelData, "scaled_crosses", view_tag);
  if (!okv) {
    Msg::Error("Failed to create view with scaled crosses");
    return -1;
  }

  return 0;
}

/* generate two views, named 'U' and 'V', with 3 values per triangle */
int computeUV(GModel * gm, const QuadMeshingOptions& opt, QuadMeshingState& state) {
  /* load theta (angle per edge) */
  int cf_tag = -1;
  PView* theta = PView::getViewByName("theta");
  if (!theta) {
    Msg::Error("required view 'theta' not found");
    return -1;
  }
  cf_tag = theta->getTag();
  std::vector<GFace *> f;
  getFacesOfTheModel(gm, f);
  quadLayoutData qLayout(gm, f, gm->getName());
  qLayout.loadThetaFromView(cf_tag);

  PView* view_U = PView::getViewByName("U");
  if (view_U) {delete view_U; view_U = NULL;}
  PView* view_V = PView::getViewByName("V");
  if (view_V) {delete view_V; view_V = NULL;}

  /* recompute H (should load it but ...) */
  qLayout.myAssembler = computeH(gm, f, qLayout.vs, qLayout.C);
  {
    v2t_cont adj;
    for(size_t i = 0; i < f.size(); i++) {
      buildVertexToElement(f[i]->triangles, adj);
    }
    std::map<MVertex*, double> source;	     
    computeSingularities(qLayout.C, qLayout.singularities, qLayout.indices, f,qLayout.gaussianCurvatures, source);
    computeUniqueVectorPerTriangle(gm, f, qLayout.C, qLayout.d0, qLayout.d1);
    computeSingularities(f,qLayout.d0,qLayout.d1, qLayout.singularities, qLayout.indices, qLayout.gaussianCurvatures);
    qLayout.d0.clear();
    qLayout.d1.clear();

  }

  /* cut-graph and cross field projection */
  std::map<int, std::vector<double> > dataTHETA; /* per triangle */
  std::map<MEdge, MEdge, MEdgeLessThan> duplicateEdges;
  qLayout.computeCutGraph(duplicateEdges);
  qLayout.computeThetaUsingHCrouzeixRaviart(dataTHETA);


  std::map<MVertex *, double> potU, potV;
  std::map<MEdge, edgeCuts, MEdgeLessThan> cuts;
  bool layout = true;
  std::vector<std::pair<MTriangle*,int> > t_junctions;
  if(layout) {
    std::vector<cutGraphPassage> passages;
    int ITER = 0;
    while (1){
      qLayout.computeQuadLayout(potU, potV, duplicateEdges, cuts, passages, t_junctions);
      for (size_t i=0 ; i< passages.size() ; ++i){
        passages[i].analyze(potU,potV,qLayout.G,qLayout.new2old);
      }
      computeValidPassages( passages );
      if (ITER++ ==0)break;
    }

    PViewDataGModel *U = NULL;
    PViewDataGModel *V = NULL;
    U = new PViewDataGModel(PViewDataGModel::ElementNodeData);
    V = new PViewDataGModel(PViewDataGModel::ElementNodeData);
    U->setName("U");
    V->setName("V");
    std::map<int, std::vector<double> > dataU;
    std::map<int, std::vector<double> > dataV;

    for(size_t i = 0; i < f.size(); i++) {
      for(size_t j = 0; j < f[i]->triangles.size(); j++) {
        MTriangle *t = f[i]->triangles[j];
        double a = potU[f[i]->triangles[j]->getVertex(0)];
        double b = potU[f[i]->triangles[j]->getVertex(1)];
        double c = potU[f[i]->triangles[j]->getVertex(2)];
        std::vector<double> ts;
        ts.push_back(a);
        ts.push_back(b);
        ts.push_back(c);
        dataU[t->getNum()] = ts;
        a = potV[f[i]->triangles[j]->getVertex(0)];
        b = potV[f[i]->triangles[j]->getVertex(1)];
        c = potV[f[i]->triangles[j]->getVertex(2)];
        ts.clear();
        ts.push_back(a);
        ts.push_back(b);
        ts.push_back(c);
        dataV[t->getNum()] = ts;
      }
    }

    U->addData(gm, dataU, 0, 0.0, 1, 1);
    U->finalize();
    V->addData(gm, dataV, 0, 0.0, 1, 1);
    V->finalize();

    double minval = std::min(U->getMin(),V->getMin());
    double maxval = std::max(U->getMax(),V->getMax());

    view_U = new PView();
    view_U->setData(U);
    view_V = new PView();
    view_V->setData(V);

    view_U->getOptions()->intervalsType = PViewOptions::Iso;
    view_U->getOptions()->nbIso = 50;
    view_U->getOptions()->rangeType = PViewOptions::Custom;
    view_U->getOptions()->customMin = minval;
    view_U->getOptions()->customMax = maxval;
    view_U->getOptions()->lineWidth = 3.;
    view_V->getOptions()->intervalsType = PViewOptions::Iso;
    view_V->getOptions()->nbIso = 50;
    view_V->getOptions()->rangeType = PViewOptions::Custom;
    view_V->getOptions()->customMin = minval;
    view_V->getOptions()->customMax = maxval;
    view_V->getOptions()->lineWidth = 3.;

    /* Apply new2old to the cuts */
    std::map<MEdge, edgeCuts, MEdgeLessThan>* cutsPtr = new std::map<MEdge, edgeCuts, MEdgeLessThan>();
    for (auto kv: cuts) {
      MEdge edge = kv.first;
      edgeCuts cut = kv.second;
      MVertex* mv1 = edge.getMinVertex();
      MVertex* mv2 = edge.getMaxVertex();
      auto it1 = qLayout.new2old.find(mv1);
      auto it2 = qLayout.new2old.find(mv2);
      if (it1 != qLayout.new2old.end()) {
        mv1 = it1->second;
      }
      if (it2 != qLayout.new2old.end()) {
        mv2 = it2->second;
      }
      MEdge edge2(mv1,mv2);
      (*cutsPtr)[edge2] = cut;
    }

    qLayout.restoreInitialMesh(); 

    /* Save the cuts in the state */
    if (state.data_uv_cuts != NULL) {
      std::map<MEdge, edgeCuts, MEdgeLessThan>* ptr = static_cast<std::map<MEdge, edgeCuts, MEdgeLessThan>*>(state.data_uv_cuts);
      delete ptr;
      state.data_uv_cuts = NULL;
    }
    if (state.data_uv_cuts_tj != NULL) {
      std::vector<std::pair<MTriangle*,int> >* ptr = static_cast<std::vector<std::pair<MTriangle*,int> >* >(state.data_uv_cuts_tj);
      delete ptr;
      state.data_uv_cuts_tj = NULL;
    }
    std::vector<std::pair<MTriangle*,int> >* tjptr = new std::vector<std::pair<MTriangle*,int> >(t_junctions);
    state.data_uv_cuts = (void*) (cutsPtr);
    state.data_uv_cuts_tj = (void*) (tjptr);
    Msg::Debug("saved cuts map pointer in QuadMeshingState");
  }

  return 0;
}

/* generata a new model with cut triangles and classified triangles */
int computeQuadLayout(GModel * gm, const QuadMeshingOptions& opt, QuadMeshingState& state) {
  { /* Input verification */
    bool have_triangles = false;
    bool have_quads = false;
    std::vector<GFace *> f;
    getFacesOfTheModel(gm, f);
    for(size_t i = 0; i < f.size(); i++) {
      if (f[i]->triangles.size() > 0) have_triangles = true;
      if (f[i]->quadrangles.size() > 0) have_quads = true;
    }
    if (!have_triangles || have_quads) {
      Msg::Error("Input model '%s' is not a triangulation", gm->getName().c_str());
      return -1;
    }
  }
  PView* view_U = PView::getViewByName("U");
  PView* view_V = PView::getViewByName("V");
  if (view_U == NULL || view_V == NULL) {
    Msg::Error("View 'U' and 'V' not found");
    return -1;
  }
  if ( view_U->getData()->getModel(0) != gm
      || view_V->getData()->getModel(0) != gm) {
    Msg::Error("View 'U' and 'V' are not defined on current model '%s'", gm->getName().c_str());
    return -1;
  }
  if (state.data_uv_cuts == NULL || state.data_uv_cuts_tj == NULL) {
    Msg::Error("Cuts not found in QuadMeshingState");
    return -1;
  }

  /* create a boundary projector for futur use,
   * not clean but for the moment it is hard to transfer
   * CAD information from model to model */
  {
#if defined(HAVE_QUADMESHINGTOOLS)
    if (state.data_boundary_projector == NULL) {
      QMT::TMesh boundary;
      bool oki = QMT::import_TMesh_from_gmsh(gm->getName(),boundary);
      if (!oki) {
        Msg::Error("Failed to import triangular mesh");
        return -1;
      }
      state.data_boundary_projector = (void*) new QMT::BoundaryProjector(boundary);
      Msg::Debug("saved QMT::BoundaryProjector* in QuadMeshingState");
    }
#endif
  }

  /* change to a new model (created via disk write/read, not good but ...) */
  Msg::Info("create a new model '%s'", opt.model_cut.c_str());
  std::string tmp_path = "tmp_mesh.msh";
  gm->writeMSH(tmp_path, 4.1, false, true);
  GModel* gcc = GModel::findByName(opt.model_cut);
  if (gcc) {
    Msg::Warning("already a model with the same name, deleting it");
    delete gcc;
  }
  gcc = new GModel(opt.model_cut);
  GModel::setCurrent(gcc);
  GmshMergeFile(tmp_path);

  /* vertex pointers in the new model */
  std::vector<GFace *> of;
  getFacesOfTheModel(gm, of);
  std::vector<GFace *> f;
  getFacesOfTheModel(gcc, f);
  std::map<MVertex*,MVertex*,MVertexPtrLessThan> omv2mv;
  std::map<MTriangle*,MTriangle*> omt2mt;
  if (f.size() != of.size()) {Msg::Error("bad!");return -1;}
  for(size_t i = 0; i < f.size(); i++) {
    if (f[i]->triangles.size() != of[i]->triangles.size()) {Msg::Error("bad!");return -1;}
    for(size_t j = 0; j < f[i]->triangles.size(); j++) {
      MTriangle *t = f[i]->triangles[j];
      MTriangle *ot = of[i]->triangles[j];
      omt2mt[ot] = t;
      for(size_t k = 0; k < 3; k++) { 
        MVertex* omv = ot->getVertex(k);
        MVertex* mv = t->getVertex(k);
        omv2mv[omv] = mv;
      }
    }
  }

  /* Update cuts */
  std::map<MEdge, edgeCuts, MEdgeLessThan>* cutsPtr = static_cast<std::map<MEdge, edgeCuts, MEdgeLessThan>*>(state.data_uv_cuts);
  std::vector<std::pair<MTriangle*,int> >* cutsTjPtr = static_cast<std::vector<std::pair<MTriangle*,int> >* >(state.data_uv_cuts_tj);

  std::map<MEdge, edgeCuts, MEdgeLessThan>& cuts = *cutsPtr;
  std::map<MEdge, edgeCuts, MEdgeLessThan> gccCuts;
  for (auto& edge_cut : cuts) {
    MVertex* mv1 = edge_cut.first.getMinVertex();
    MVertex* mv2 = edge_cut.first.getMaxVertex();
    MVertex* nv1 = omv2mv[mv1];
    MVertex* nv2 = omv2mv[mv2];
    if (!nv1 || !nv2) {
      DBG("---------------bad---------------");
      DBG(mv1,mv2,nv1,nv2);
      // continue;
      return -1;
    }
    /* check colocate */
    if (true) {
      if (nv1->point().distance(mv1->point()) > 1.e-10) {
        printf("error in point mapping !?");
      }
      if (nv2->point().distance(mv2->point()) > 1.e-10) {
        printf("error in point mapping !?");
      }
    }
    MEdge gccEdge(nv1, nv2);
    edgeCuts gccCut = edge_cut.second;
    for (size_t k = 0; k < gccCut.vs.size(); ++k) {
      if (gccCut.vs[k] != NULL) {
        gccCut.vs[k] = omv2mv[gccCut.vs[k]];
      }
    }
    gccCuts[gccEdge] = gccCut;
  }
  for (auto& tj: *cutsTjPtr) {
    tj.first = omt2mt[tj.first];
  }

  /* Cutting the gcc mesh */
  quadLayoutData qLayout(gcc, f, gcc->getName());
  Msg::Info("Cutting the mesh");
  std::map<MEdge,GEdge*,MEdgeLessThan> inverseClassificationEdges;

  /// FIXME MAXENCE --> need for t_junctions 
  // std::vector<std::pair<MTriangle*,int> > t_junctions;
  qLayout.cutMesh(cuts, inverseClassificationEdges, *cutsTjPtr);

  constexpr bool do_reclassify = false; /* no longer needed, done by QMT */
  if (do_reclassify) {
    /* Classify the cut mesh */
    Msg::Info("Classifying the model");
    discreteEdge *de = new discreteEdge(
        GModel::current(), GModel::current()->getMaxElementaryNumber(1) + 1, 0, 0);
    GModel::current()->add(de);
    computeNonManifoldEdges(GModel::current(), de->lines, true);
    classifyFaces(GModel::current(), M_PI / 4, false);
    GModel::current()->remove(de);
    //  delete de;
    GModel::current()->pruneMeshVertexAssociations();

    int countError = 0;
    for(GModel::fiter it = GModel::current()->firstFace();
	it != GModel::current()->lastFace(); it++) {
      if((*it)->edges().size() != 4) {
	Msg::Warning("quad layout failed : face %lu has %lu boundaries",
		     (*it)->tag(), (*it)->edges().size());
	countError++;
      }
    }
    if(!countError) {
      Msg::Info("Quad layout success : the model is partitioned in %d master quads",
		GModel::current()->getNumFaces());
    }
  }

  /* Remove temporary mesh file */
  if (remove(tmp_path.c_str()) != 0) {
    Msg::Error("failed to remove file '%s'", tmp_path.c_str());
  }

  CTX::instance()->mesh.changed = ENT_ALL;

  return 0;
}

int generateQuadMesh(GModel * gm, const QuadMeshingOptions& opt, QuadMeshingState& state) {
  { /* Input verification */
    bool have_triangles = false;
    bool have_quads = false;
    std::vector<GFace *> f;
    getFacesOfTheModel(gm, f);
    for(size_t i = 0; i < f.size(); i++) {
      if (f[i]->triangles.size() > 0) have_triangles = true;
      if (f[i]->quadrangles.size() > 0) have_quads = true;
    }
    if (!have_triangles || have_quads) {
      Msg::Error("Input model '%s' is not a triangulation", gm->getName().c_str());
      return -1;
    }
  }

#if defined(HAVE_QUADMESHINGTOOLS)
  QMT::BoundaryProjector* projector = NULL;
  if (state.data_boundary_projector == NULL) {
    Msg::Warning("BoundaryProjector* not found in QuadMeshingState (this one is created by the Cut step). Proceed to quantization without projection.");

    bool force_one_boundary_projector = true;
    if (force_one_boundary_projector) {
      Msg::Warning("wait ! creating one from the current model, may be a bad idea ...");
      QMT::TMesh boundary;
      bool oki = QMT::import_TMesh_from_gmsh(gm->getName(),boundary);
      if (!oki) {
        Msg::Error("Failed to import triangular mesh");
        return -1;
      }
      state.data_boundary_projector = (void*) new QMT::BoundaryProjector(boundary);
      Msg::Debug("saved QMT::BoundaryProjector* in QuadMeshingState");
    }
  } 
  if (state.data_boundary_projector != NULL) {
    projector = static_cast<QMT::BoundaryProjector*>(state.data_boundary_projector);
  }

  int sizemapTag = -1;
  if (sizemapTag == -1) {
    PView* view_s = PView::getViewByName("s");
    if (view_s) {
      sizemapTag = view_s->getTag();
    }
  }
  if (sizemapTag == -1) {
    Msg::Warning("Quad size map (view 's') not found, using uniform");
  }

  std::string quad_layout_name = gm->getName();
  double size_min = CTX::instance()->mesh.lcMin;
  double size_max = CTX::instance()->mesh.lcMax;
  // double size_uniform = 0.;
  if (CTX::instance()->mesh.lcMin != 0. && CTX::instance()->mesh.lcFactor) {
    size_min *= CTX::instance()->mesh.lcFactor;
    // size_uniform = size_min;
  }
  if (CTX::instance()->mesh.lcMax != 1.e22 && CTX::instance()->mesh.lcFactor) {
    size_max *= CTX::instance()->mesh.lcFactor;
    // size_uniform = size_max;
  }
  if (size_min == 0 && size_max == 1.e22) {
    SBoundingBox3d bbox = gm->bounds();
    size_min = 0.1 * bbox.diag() * CTX::instance()->mesh.lcFactor;
    // size_uniform = size_min;
    Msg::Warning("No size specified, using hmin = 0.1*bbox diagonal*clscale");
  }
  
  QMT::QMesh Q;
  bool okg = QMT::generate_quad_mesh_via_tmesh_quantization(
      quad_layout_name, sizemapTag, size_min, size_max, Q, projector, opt.fix_decomposition);
  if (!okg) {
    Msg::Error("Failed to generate quad mesh");
    return -1;
  }

  {
    Msg::Info("create a new model '%s'",opt.model_quad_init.c_str());
    GModel* gg = GModel::findByName(opt.model_quad_init);
    if (gg) {
      Msg::Warning("already a model with the same name, deleting it");
      delete gg;
    }
  }
  bool oke1 = QMT::export_qmesh_to_gmsh_mesh(Q, opt.model_quad_init);
  if (!oke1) {
    Msg::Error("Failed to export quad mesh");
    return -1;
  }

  CTX::instance()->mesh.changed = ENT_ALL;

#else
  Msg::Error("Quad mesh generation requires the QuadMeshingTools module");
  return -1;
#endif

  return 0;
}

/* simplify the current quad mesh connectivity */
int simplifyQuadMesh(GModel * gm, const QuadMeshingOptions& opt, QuadMeshingState& state) {
  { /* Input verification */
    bool have_triangles = false;
    bool have_quads = false;
    std::vector<GFace *> f;
    getFacesOfTheModel(gm, f);
    for(size_t i = 0; i < f.size(); i++) {
      if (f[i]->triangles.size() > 0) have_triangles = true;
      if (f[i]->quadrangles.size() > 0) have_quads = true;
    }
    if (have_triangles || !have_quads) {
      Msg::Error("Input model '%s' is not a quadrangulation", gm->getName().c_str());
      return -1;
    }
  }

#if defined(HAVE_QUADMESHINGTOOLS)
  /* Import current quad mesh */
  QMT::QMesh Q;
  bool oki = QMT::import_QMesh_from_gmsh(gm->getName(),Q);
  if (!oki) {
    Msg::Error("Failed to simplify quad mesh");
    return -1;
  }

  /* Fill size map values in Q */
  int sizemapTag = -1;
  if (sizemapTag == -1) {
    PView* view_s = PView::getViewByName("s");
    if (view_s) {
      sizemapTag = view_s->getTag();
    } else {
      Msg::Warning("Size map (view named 's') not found, simplification without it");
    }
  }

  /* BoundaryProjector */
  QMT::BoundaryProjector* projector = NULL;
  bool proj_to_del = false;
  if (state.data_boundary_projector != NULL) {
    projector = static_cast<QMT::BoundaryProjector*>(state.data_boundary_projector);
  }
  if (projector == NULL) {
    Msg::Info("building a discrete BoundaryProjector from split quad mesh");
    QMT::TMesh T;
    QMT::convert_quad_mesh_to_tri_mesh(Q,T);
    projector = new QMT::BoundaryProjector(T);
    bool keep_it_for_smoothing = true;
    if (keep_it_for_smoothing) {
      state.data_boundary_projector = (void*) projector;
      Msg::Debug("saved QMT::BoundaryProjector* in QuadMeshingState");
    } else {
      proj_to_del = true;
    }
    // projector->show_projector();
  }
  // if (projector == NULL) {
  //   Msg::Error("BoundaryProjector* not found in QuadMeshingState. This one is created by the Cut step.");
  //   return -1;
  // }

  /* Simplification sizes */
  double size_min = CTX::instance()->mesh.lcMin;
  double size_max = CTX::instance()->mesh.lcMax;
  if (CTX::instance()->mesh.lcMin != 0. && CTX::instance()->mesh.lcFactor) {
    size_min *= CTX::instance()->mesh.lcFactor;
  }
  if (CTX::instance()->mesh.lcMax != 1.e22 && CTX::instance()->mesh.lcFactor) {
    size_max *= CTX::instance()->mesh.lcFactor;
  }
  if (size_min == 0 && size_max == 1.e22) {
    SBoundingBox3d bbox = gm->bounds();
    size_min = 0.1 * bbox.diag();
    Msg::Warning("No size specified, using hmin = 0.1*bbox diagonal");
  }
  // double hc = opt.simplify_size_factor * size_min;
  // if (size_min == 0.) hc = opt.simplify_size_factor * size_max;

  /* Temporary solution because CAD not transfered */
  bool oka = QMT::assignClosestEntities(Q, *projector);
  if (!oka) {
    Msg::Error("Failed to assign quad vertices to closest entities in the BoundaryProjector");
    return -1;
  }
  // projector->show_projector();

  if (sizemapTag != -1) { /* Simplification based on sizes */
    bool oksm = QMT::fill_vertex_sizes_from_sizemap(Q, sizemapTag);
    if (!oksm) {
      Msg::Error("Failed to evaluate size map on quad mesh");
      return -1;
    }

    /* Apply simplification */
    bool oks = QMT::simplify_quad_mesh(Q, opt.simplify_size_factor, -1, projector);
    if (!oks) {
      Msg::Error("Failed to simplify quad mesh");
      return -1;
    }
  } else { 
    Msg::Error("Size map required for the simplification");
    return -1;

    // // Purely topological 3-5 chord collapse is not very effective in practice ... 
    // // commented for the moment
    // /* Apply simplification */
    // bool oks = QMT::simplify_quad_mesh_by_merging_irregular_vertices(Q, -1, projector);
    // if (!oks) {
    //   Msg::Error("Failed to simplify quad mesh");
    //   return -1;
    // }
  }

  /* Export simplified quad mesh to new gmsh model */
  {
    Msg::Info("create a new model '%s'",opt.model_quad.c_str());
    GModel* gg = GModel::findByName(opt.model_quad);
    if (gg) {
      Msg::Warning("already a model with the same name, deleting it");
      delete gg;
    }
  }
  bool oke2 = QMT::export_qmesh_to_gmsh_mesh(Q, opt.model_quad);
  if (!oke2) {
    Msg::Error("Failed to export quad mesh");
    return -1;
  }

  if (proj_to_del) { delete projector; projector = NULL; }

  CTX::instance()->mesh.changed = ENT_ALL;

  return 0;
#else
  Msg::Error("Quad mesh simplification requires the QuadMeshingTools module");
  return -1;
#endif
}

/* smooth the current quad mesh geometry */
int smoothQuadMesh(GModel * gm, const QuadMeshingOptions& opt, QuadMeshingState& state) {
  { /* Input verification */
    bool have_triangles = false;
    bool have_quads = false;
    std::vector<GFace *> f;
    getFacesOfTheModel(gm, f);
    for(size_t i = 0; i < f.size(); i++) {
      if (f[i]->triangles.size() > 0) have_triangles = true;
      if (f[i]->quadrangles.size() > 0) have_quads = true;
    }
    if (have_triangles || !have_quads) {
      Msg::Error("Input model '%s' is not a quadrangulation", gm->getName().c_str());
      return -1;
    }
  }

#if defined(HAVE_QUADMESHINGTOOLS)
  /* Import current quad mesh */
  QMT::QMesh Q;
  bool oki = QMT::import_QMesh_from_gmsh(gm->getName(),Q);
  if (!oki) {
    Msg::Error("Failed to simplify quad mesh");
    return -1;
  }

  QMT::BoundaryProjector* projector = NULL;
  if (state.data_boundary_projector != NULL) {
    projector = static_cast<QMT::BoundaryProjector*>(state.data_boundary_projector);
  }
  if (projector == NULL) {
    Msg::Error("BoundaryProjector* not found in QuadMeshingState. This one is created by the Cut step.");
    return -1;
  }
  // projector->show_projector(); /* only for debug */

  /* Temporary solution because CAD not transfered */
  bool oka = QMT::assignClosestEntities(Q, *projector);
  if (!oka) {
    Msg::Error("Failed to assign quad vertices to closest entities in the BoundaryProjector");
    return -1;
  }

  /* Smoothing */
  size_t smoothing_iter = opt.smoothing_explicit_iter;
  bool oksm = QMT::smooth_quad_mesh(Q, smoothing_iter, projector);
  if (!oksm) {
    Msg::Error("Failed to smooth quad mesh");
    return -1;
  }

  /* Update GModel vertex positions */
  {
    std::vector<GFace *> f;
    getFacesOfTheModel(gm, f);
    std::set<MVertex *, MVertexPtrLessThan> vs;
    for(size_t i = 0; i < f.size(); i++) {
      for(size_t j = 0; j < f[i]->quadrangles.size(); j++) {
        MQuadrangle *t = f[i]->quadrangles[j];
        for(size_t k = 0; k < 4; k++) { vs.insert(t->getVertex(k)); }
      }
    }
    for (auto& v: vs) {
      size_t num = v->getNum();
      v->setXYZ(Q.points[num][0],Q.points[num][1],Q.points[num][2]);
    }
  }

  CTX::instance()->mesh.changed = ENT_ALL;
  return 0;
#else
  Msg::Error("Quad mesh smoothing requires the QuadMeshingTools module");
  return -1;
#endif
}

int computePerTriangleScaledCrossField(
    GModel* gm,
    int& viewTag,
    int cross_field_iter,
    int cross_field_bc_expansion,
    size_t sizemap_nb_quads,
    bool delete_other_tmp_views) {
#if defined(HAVE_QUADMESHINGTOOLS)
  PView* theta = PView::getViewByName("theta");
  if (theta) {delete theta; theta = NULL;}

  std::vector<GFace *> f;
  getFacesOfTheModel(gm, f);
  quadLayoutData qLayout(gm, f, gm->getName());
  std::map<MVertex *, int> temp;
  findPhysicalGroupsForSingularities(gm, f, temp);
  
  int cf_tag = -1;
  if (temp.size()){
    std::map<int, double > dataH;
    std::map<int, std::vector<double> > dataTHETA;
    const bool createViewTheta = true; /* - View 'theta' */
    qLayout.computeCrossFieldAndH(&temp, dataTHETA, createViewTheta);
    qLayout.getH (dataH);
    PViewDataList *d = new PViewDataList;
    d->setName("H");
    int sview = create_datalist_view_from_scalar_field(f, dataH, d);
    d->finalize();
    PView *vv = new PView(d);

    vv = PView::getViewByName("theta");
    if (vv) {
      cf_tag = vv->getTag();
    }    

    std::string _ugly  = gm->getName()+"_singularities.txt";
    std::string _ugly2 = gm->getName()+"_singularities.pos";
    FILE *f__ = fopen (_ugly.c_str(), "w");
    FILE *f2__ = fopen (_ugly2.c_str(), "w");
    fprintf(f__,"%lu\n",temp.size());
    fprintf(f2__,"View \"singularities\"{\n");
    for (std::map<MVertex *, int>::iterator it = temp.begin(); it != temp.end();++it){
      MVertex *v = it->first;
      int index = it->second;
      fprintf(f__,"%d %22.15E %22.15E %22.15E %d %d\n",index,v->x(),v->y(),v->z(),2,1);
      fprintf(f2__,"SP(%22.15E, %22.15E, %22.15E){ %d};\n",v->x(),v->y(),v->z(),index);
    }
    fclose(f__);
    fprintf(f2__,"};\n");
    fclose(f2__);
  }
  else {
    return -1;
    /* Cross field */
    bool okcf = QMT::compute_cross_field_with_heat(gm->getName(),cf_tag,cross_field_iter,NULL,cross_field_bc_expansion);
    if (!okcf) {
      Msg::Error("Failed to compute cross field");
      return -1;
    }

    /* Conformal scaling factor */
    int h_tag = -1;
    int status_h = compute_H_from_cross_field_view(gm, qLayout, f, cf_tag, h_tag);
    if (status_h != 0) {
      Msg::Error("Failed to compute H from cross field view");
      return -1;
    }
    { // transfer to pack algo
      std::map<MVertex*, double> source;	     
      computeSingularities(qLayout.C, qLayout.singularities, qLayout.indices, f,qLayout.gaussianCurvatures, source);
      computeUniqueVectorPerTriangle(gm, f, qLayout.C, qLayout.d0, qLayout.d1);
      computeSingularities(f,qLayout.d0,qLayout.d1, qLayout.singularities, qLayout.indices, qLayout.gaussianCurvatures, true);    
      
      std::string _ugly  = gm->getName()+"_singularities.txt";
      std::string _ugly2 = gm->getName()+"_singularities.pos";
      FILE *f__ = fopen (_ugly.c_str(), "w");
      FILE *f2__ = fopen (_ugly2.c_str(), "w");
      fprintf(f__,"%lu\n",qLayout.singularities.size());
      fprintf(f2__,"View \"singularities\"{\n");
      for (std::set<MVertex *, MVertexPtrLessThan>::iterator it = qLayout.singularities.begin(); it != qLayout.singularities.end();++it){
	fprintf(f__,"%d %22.15E %22.15E %22.15E %d %d\n",qLayout.indices[*it],(*it)->x(),(*it)->y(),(*it)->z(),(*it)->onWhat()->dim(),
		(*it)->onWhat()->tag());
	fprintf(f2__,"SP(%22.15E, %22.15E, %22.15E){ %d};\n",(*it)->x(),(*it)->y(),(*it)->z(),qLayout.indices[*it]);
      }
      fclose(f__);
      fprintf(f2__,"};\n");
      fclose(f2__);
    }
  }

  /* Size map */
  QuadMeshingOptions opt;
  opt.sizemap_nb_quads = sizemap_nb_quads;
  QuadMeshingState state;
  int status_sizemap = computeQuadSizeMap(gm, opt, state);
  if (status_sizemap != 0) {
    Msg::Error("Failed to compute size map from H");
    return -1;
  }

  int sizemapTag = -1;
  if (sizemapTag == -1) {
    PView* view_s = PView::getViewByName("s");
    if (view_s) {
      sizemapTag = view_s->getTag();
    }
  }
  if (sizemapTag == -1) {
    Msg::Error("Quad size map (view 's') not found but required");
    return -1;
  }

  /* ElementData view */
  bool okpt = QMT::create_per_triangle_scaled_cross_field_view(gm->getName(), cf_tag, sizemapTag, "scaled_directions", viewTag);
  if (!okpt) {
    Msg::Error("Failed create scaled directions view from cross field and size map");
    return -1;
  }

  if (delete_other_tmp_views) {
    PView* view_t = PView::getViewByName("theta");
    if (view_t) {delete view_t; view_t = NULL;}
    PView* view_h = PView::getViewByName("H");
    if (view_h) {delete view_h; view_h = NULL;}
    PView* view_s = PView::getViewByName("s");
    if (view_s) {delete view_s; view_s = NULL;}
  }

  return 0;

#else
  Msg::Error("requires the QuadMeshingTools module");
  return -1;
#endif
}

int smoothQuadMesh(GModel* gm, int explicit_iter, void* data_boundary_projector) {
#if defined(HAVE_QUADMESHINGTOOLS)
  { /* Input verification */
    bool have_triangles = false;
    bool have_quads = false;
    std::vector<GFace *> f;
    getFacesOfTheModel(gm, f);
    for(size_t i = 0; i < f.size(); i++) {
      if (f[i]->triangles.size() > 0) have_triangles = true;
      if (f[i]->quadrangles.size() > 0) have_quads = true;
    }
    if (have_triangles || !have_quads) {
      Msg::Error("Input model '%s' is not a quadrangulation", gm->getName().c_str());
      return -1;
    }
  }


  /* Import current quad mesh */
  QMT::QMesh Q;
  bool oki = QMT::import_QMesh_from_gmsh(gm->getName(),Q);
  if (!oki) {
    Msg::Error("Failed to simplify quad mesh");
    return -1;
  }

  /* Create boundary projector if necessary */
  QMT::BoundaryProjector* projector = NULL;
  bool proj_to_del = false;
  if (data_boundary_projector != NULL) {
    projector = static_cast<QMT::BoundaryProjector*>(data_boundary_projector);
  }
  if (projector == NULL) {
    Msg::Info("building a discrete BoundaryProjector from split quad mesh");
    QMT::TMesh T;
    QMT::convert_quad_mesh_to_tri_mesh(Q,T);
    projector = new QMT::BoundaryProjector(T);
    proj_to_del = true;
    // projector->show_projector();
  }

  bool oka = QMT::assignClosestEntities(Q, *projector);
  if (!oka) {
    Msg::Error("Failed to assign quad vertices to closest entities in the BoundaryProjector");
    return -1;
  }

  /* Smoothing */
  bool oksm = QMT::smooth_quad_mesh(Q, explicit_iter, projector);
  if (!oksm) {
    Msg::Error("Failed to smooth quad mesh");
    return -1;
  }

  /* Update GModel vertex positions */
  {
    std::vector<GFace *> f;
    getFacesOfTheModel(gm, f);
    std::set<MVertex *, MVertexPtrLessThan> vs;
    for(size_t i = 0; i < f.size(); i++) {
      for(size_t j = 0; j < f[i]->quadrangles.size(); j++) {
        MQuadrangle *t = f[i]->quadrangles[j];
        for(size_t k = 0; k < 4; k++) { vs.insert(t->getVertex(k)); }
      }
    }
    for (auto& v: vs) {
      size_t num = v->getNum();
      v->setXYZ(Q.points[num][0],Q.points[num][1],Q.points[num][2]);
    }
  }

  if (proj_to_del) { delete projector; projector = NULL; }

  CTX::instance()->mesh.changed = ENT_ALL;
  return 0;
#else
  Msg::Error("Quad mesh smoothing requires the QuadMeshingTools module");
  return -1;
#endif
}

//using cf computed without prescribed sing
int splitMeshWithSeparatrices(GModel * gm, QuadMeshingState& state) {
  /* load theta (angle per edge) */
  int cf_tag = -1;
  PView* theta = PView::getViewByName("theta"); 
  if (!theta) {
    Msg::Error("required view 'theta' not found");
    return -1;
  }
  cf_tag = theta->getTag();

  HXTMesh *mesh;
  HXT_CHECK(hxtMeshCreate(&mesh));
  std::map<MVertex *, uint32_t> v2c;
  std::vector<MVertex *> c2v;
  HXT_CHECK(Gmsh2Hxt(gm, mesh, v2c, c2v));
  Msg::Info("MBD | Generating separatrices to obtain split mesh");
  HXTMesh *splitMesh;
  hxtQuadMultiBlockDBG(mesh, cf_tag, &splitMesh);
  Msg::Info("MBD | Split mesh generated");
  
  Msg::Info("MBD |  Exporting split mesh to gmsh");
  //projector
#if defined(HAVE_QUADMESHINGTOOLS)
  if (state.data_boundary_projector == NULL) {
    QMT::TMesh boundary;
    bool oki = QMT::import_TMesh_from_gmsh(gm->getName(),boundary);
    if (!oki) {
      Msg::Error("Failed to import triangular mesh");
      return -1;
    }
    state.data_boundary_projector = (void*) new QMT::BoundaryProjector(boundary);
    Msg::Debug("saved QMT::BoundaryProjector* in QuadMeshingState");
  }
#endif
  
  const std::string meshName=gm->getName() + "_Cut.msh";
  GModel* gg = GModel::findByName(meshName);
  if (gg) {
    Msg::Warning("Already a model with the same name, deleting it");
    delete gg;
  }
  gmsh::model::add(meshName);
  gmsh::model::setCurrent(meshName);
  //variables
  std::vector<double> coords; coords.reserve(3*(splitMesh->vertices.num)); 
  std::vector<size_t> nodeTags; nodeTags.reserve(splitMesh->vertices.num);
  std::vector<std::vector<std::size_t>> elementTags; elementTags.reserve(splitMesh->triangles.num); 
  std::vector<std::vector<std::size_t>> eltNodeTags; eltNodeTags.reserve(splitMesh->triangles.num);
  std::vector<std::size_t> triInd; triInd.reserve(3); triInd.push_back(0); triInd.push_back(0); triInd.push_back(0);
  std::vector<int> TRI_ID; TRI_ID.reserve(splitMesh->triangles.num);
 
  int dim=2;
  int tag=gmsh::model::addDiscreteEntity(2);
  //vertices
  for(uint64_t i=0; i<splitMesh->vertices.num; i++){
    for(int j=0; j<3; j++)
      coords.push_back(splitMesh->vertices.coord[4*i+j]);
    nodeTags.push_back(i+1);
  }
  gmsh::model::mesh::addNodes(dim, tag, nodeTags, coords);
  //triangles
  for(uint64_t i=0; i<splitMesh->triangles.num; i++){
    std::vector<size_t> val; val.reserve(1); val.push_back(static_cast<size_t>(i+1));
    elementTags.push_back({val});
    for(int j=0; j<3; j++)
      triInd[j]=static_cast<size_t>(splitMesh->triangles.node[3*i+j] + 1);
    eltNodeTags.push_back(triInd);
    TRI_ID.push_back(2);
  }
  gmsh::model::mesh::addElements(dim, tag, TRI_ID, elementTags, eltNodeTags);
  //lines
  uint16_t maxLineTag=0;
  for(uint64_t i=0; i<splitMesh->lines.num; i++)
    if(splitMesh->lines.color[i]>maxLineTag)
      maxLineTag=splitMesh->lines.color[i];
  std::vector<int> diffColors; diffColors.reserve(maxLineTag+1);
  std::vector<int> diffTags; diffTags.reserve(maxLineTag+1);
  for(uint64_t i=0; i<splitMesh->lines.num; i++){
    if(std::find(diffColors.begin(), diffColors.end(), static_cast<int>(splitMesh->lines.color[i])) == diffColors.end()){
      diffColors.push_back(static_cast<int>(splitMesh->lines.color[i]));
      int lTag=gmsh::model::addDiscreteEntity(1);
      diffTags.push_back(lTag);
    }
  } 
  std::vector<std::vector<int>> coloredLines; coloredLines.reserve(diffColors.size());
  for(uint64_t i=0; i<diffColors.size(); i++){
    std::vector<int> vecL; vecL.reserve(splitMesh->lines.num);
    for(uint64_t j=0; j<splitMesh->lines.num; j++){
      if(diffColors[i]==static_cast<int>(splitMesh->lines.color[j]))
	vecL.push_back(static_cast<int>(j));
    }
    coloredLines.push_back(vecL);
  }
  int num=0;
  for(uint64_t i=0; i<coloredLines.size();i++){
    std::vector<std::vector<std::size_t>> lineNodeTags; lineNodeTags.reserve(coloredLines[i].size());
    std::vector<std::size_t> lnInd; lnInd.reserve(2); lnInd.push_back(0); lnInd.push_back(0);
    std::vector<std::vector<std::size_t>> lineTags; lineTags.reserve(coloredLines[i].size());
    std::vector<int> L_ID; L_ID.reserve(coloredLines[i].size());
    for(uint64_t j=0; j<coloredLines[i].size(); j++){
      std::vector<size_t> val; val.reserve(1); val.push_back(static_cast<size_t>(num+j+1));
      lineTags.push_back({val});
      lnInd[0]=static_cast<size_t>(splitMesh->lines.node[2*coloredLines[i][j]+0] + 1);
      lnInd[1]=static_cast<size_t>(splitMesh->lines.node[2*coloredLines[i][j]+1] + 1);
      lineNodeTags.push_back(lnInd);
      L_ID.push_back(1); 
    }
    num+=coloredLines[i].size()+1;
    gmsh::model::mesh::addElements(1, diffTags[i], L_ID, lineTags, lineNodeTags);
  }
  std::cout<<"---------------FINIIIIIIIISHED!----------------"<<std::endl;
  std::cout<<std::endl;
  
  gm = GModel::current();
  CTX::instance()->mesh.changed = ENT_ALL;
  Msg::Info("MBD | Split mesh shown in  gmsh");
  hxtMeshDelete(&mesh);
  hxtMeshDelete(&splitMesh);
  return 0;
}

int findAndMarkSingularities(GModel * gm){
  /* load theta (angle per edge) */
  int cf_tag = -1;
  PView* theta = PView::getViewByName("theta"); 
  if (!theta) {
    Msg::Error("required view 'theta' not found");
    return -1;
  }
  cf_tag = theta->getTag();  
  HXTMesh *mesh;  
  HXT_CHECK(hxtMeshCreate(&mesh));
  std::map<MVertex *, uint32_t> v2c;
  std::vector<MVertex *> c2v;
  HXT_CHECK(Gmsh2Hxt(gm, mesh, v2c, c2v));
  Msg::Info("MBD | Finding singularities and creating new geometry file");
  
  std::string geoFileName = gm->getName() + "_sing.geo";
  bool printLabels = true; bool onlyPhysicals = false;
  gm->writeGEO(geoFileName, printLabels, onlyPhysicals);
  
  std::string modelWithSingName = gm->getName() + "_sing";
  GModel* gg = GModel::findByName(modelWithSingName);
  if (gg) {
    Msg::Warning("Already a model with the same name, deleting it");
    delete gg;
  }
 
  hxtQuadMultiBlockGetSingInfo(mesh, cf_tag, geoFileName);
  gg=GModel::findByName(modelWithSingName);
  // if (gg) {
  //   Msg::Warning("Model with singularities not found");
  // }
  // else
  //   gm=gg;
  Msg::Info("MBD | New geometry ready");
  // std::string geoFileName2 = gm->getName() + "_sing2.geo";
  // gm->writeGEO(geoFileName2, printLabels, onlyPhysicals);
  hxtMeshDelete(&mesh);
  return 0;
}

//using cf computed on prescribed sing
int splitMeshWithPrescribedSing(GModel * gm, QuadMeshingState& state) {
  std::cout << "splitMeshWithPrescribedSing" << std::endl;
  /* load theta (angle per edge) */
  int cf_tag = -1;
  PView* theta = PView::getViewByName("theta"); 
  if (!theta) {
    Msg::Error("required view 'theta' not found");
    return -1;
  }
  cf_tag = theta->getTag();
  int H_tag = -1;
  PView* viewH = PView::getViewByName("H"); 
  if (!viewH) {
    Msg::Error("required view 'H' not found");
    return -1;
  }
  H_tag = viewH->getTag();
  //DBG
  std::string tmp_path = "temp0.msh";
  gm->writeMSH(tmp_path, 4.1, false, true);
  std::cout << "mesh written" << std::endl;
  //DBG
  HXTMesh *mesh;
  HXT_CHECK(hxtMeshCreate(&mesh));
  std::cout << "mesh allocated" << std::endl;
  std::map<MVertex *, uint32_t> v2c;
  std::vector<MVertex *> c2v;
  HXT_CHECK(Gmsh2Hxt(gm, mesh, v2c, c2v));
  std::cout << "mesh converted" << std::endl;
  
  //DBG: Write geo/msh with imposed sing
  bool printLabels = true; bool onlyPhysicals = false;
  std::string tempName= "temp.geo";
  std::cout << "geo being written" << std::endl;  
  gm->writeGEO(tempName, printLabels, onlyPhysicals);
  std::cout << "geo written" << std::endl;
  std::string tmp_path1 = "temp1.msh";
  gm->writeMSH(tmp_path1, 4.1, false, true);
  std::cout << "mesh written" << std::endl;
  //------------------------------------------------
  
  Msg::Info("MBD | Generating separatrices to obtain split mesh");
  HXTMesh *splitMesh;
  hxtQuadMultiBlockSplitWithPrescribedSing(mesh, cf_tag, &splitMesh, H_tag);
  std::cout << "mesh splitted" << std::endl;
  if (theta) {delete theta; theta = NULL;}//DBG
  Msg::Info("MBD | Split mesh generated");
  
  Msg::Info("MBD |  Exporting split mesh to gmsh");
  //projector
#if defined(HAVE_QUADMESHINGTOOLS)
  std::cout << "projection" << std::endl;
  if (state.data_boundary_projector == NULL) {
    QMT::TMesh boundary;
    bool oki = QMT::import_TMesh_from_gmsh(gm->getName(),boundary);
    if (!oki) {
      Msg::Error("Failed to import triangular mesh");
      return -1;
    }
    state.data_boundary_projector = (void*) new QMT::BoundaryProjector(boundary);
    Msg::Debug("saved QMT::BoundaryProjector* in QuadMeshingState");
  }
#endif
  std::cout << "mesh in new model" << std::endl;
  const std::string meshName=gm->getName() + "_Cut.msh";
  GModel* gg = GModel::findByName(meshName);
  if (gg) {
    Msg::Warning("Already a model with the same name, deleting it");
    delete gg;
  }
  gmsh::model::add(meshName);
  gmsh::model::setCurrent(meshName);
  //variables
  std::vector<double> coords; coords.reserve(3*(splitMesh->vertices.num)); 
  std::vector<size_t> nodeTags; nodeTags.reserve(splitMesh->vertices.num);
  std::vector<std::vector<std::size_t>> elementTags; elementTags.reserve(splitMesh->triangles.num); 
  std::vector<std::vector<std::size_t>> eltNodeTags; eltNodeTags.reserve(splitMesh->triangles.num);
  std::vector<std::size_t> triInd; triInd.reserve(3); triInd.push_back(0); triInd.push_back(0); triInd.push_back(0);
  std::vector<int> TRI_ID; TRI_ID.reserve(splitMesh->triangles.num);
 
  int dim=2;
  int tag=gmsh::model::addDiscreteEntity(2);
  //vertices
  for(uint64_t i=0; i<splitMesh->vertices.num; i++){
    for(int j=0; j<3; j++)
      coords.push_back(splitMesh->vertices.coord[4*i+j]);
    nodeTags.push_back(i+1);
  }
  gmsh::model::mesh::addNodes(dim, tag, nodeTags, coords);
  //triangles
  for(uint64_t i=0; i<splitMesh->triangles.num; i++){
    std::vector<size_t> val; val.reserve(1); val.push_back(static_cast<size_t>(i+1));
    elementTags.push_back({val});
    for(int j=0; j<3; j++)
      triInd[j]=static_cast<size_t>(splitMesh->triangles.node[3*i+j] + 1);
    eltNodeTags.push_back(triInd);
    TRI_ID.push_back(2);
  }
  gmsh::model::mesh::addElements(dim, tag, TRI_ID, elementTags, eltNodeTags);
  //lines
  uint16_t maxLineTag=0;
  for(uint64_t i=0; i<splitMesh->lines.num; i++)
    if(splitMesh->lines.color[i]>maxLineTag)
      maxLineTag=splitMesh->lines.color[i];
  std::vector<int> diffColors; diffColors.reserve(maxLineTag+1);
  std::vector<int> diffTags; diffTags.reserve(maxLineTag+1);
  for(uint64_t i=0; i<splitMesh->lines.num; i++){
    if(std::find(diffColors.begin(), diffColors.end(), static_cast<int>(splitMesh->lines.color[i])) == diffColors.end()){
      diffColors.push_back(static_cast<int>(splitMesh->lines.color[i]));
      int lTag=gmsh::model::addDiscreteEntity(1);
      diffTags.push_back(lTag);
    }
  } 
  std::vector<std::vector<int>> coloredLines; coloredLines.reserve(diffColors.size());
  for(uint64_t i=0; i<diffColors.size(); i++){
    std::vector<int> vecL; vecL.reserve(splitMesh->lines.num);
    for(uint64_t j=0; j<splitMesh->lines.num; j++){
      if(diffColors[i]==static_cast<int>(splitMesh->lines.color[j]))
	vecL.push_back(static_cast<int>(j));
    }
    coloredLines.push_back(vecL);
  }
  int num=0;
  for(uint64_t i=0; i<coloredLines.size();i++){
    std::vector<std::vector<std::size_t>> lineNodeTags; lineNodeTags.reserve(coloredLines[i].size());
    std::vector<std::size_t> lnInd; lnInd.reserve(2); lnInd.push_back(0); lnInd.push_back(0);
    std::vector<std::vector<std::size_t>> lineTags; lineTags.reserve(coloredLines[i].size());
    std::vector<int> L_ID; L_ID.reserve(coloredLines[i].size());
    for(uint64_t j=0; j<coloredLines[i].size(); j++){
      std::vector<size_t> val; val.reserve(1); val.push_back(static_cast<size_t>(num+j+1));
      lineTags.push_back({val});
      lnInd[0]=static_cast<size_t>(splitMesh->lines.node[2*coloredLines[i][j]+0] + 1);
      lnInd[1]=static_cast<size_t>(splitMesh->lines.node[2*coloredLines[i][j]+1] + 1);
      lineNodeTags.push_back(lnInd);
      L_ID.push_back(1); 
    }
    num+=coloredLines[i].size()+1;
    gmsh::model::mesh::addElements(1, diffTags[i], L_ID, lineTags, lineNodeTags);
  }
  std::cout<<"---------------FINIIIIIIIISHED!----------------"<<std::endl;
  std::cout<<std::endl;
  
  gm = GModel::current();
  CTX::instance()->mesh.changed = ENT_ALL;
  Msg::Info("MBD | Split mesh shown in  gmsh");
  // hxtMeshDelete(&mesh);//CRITICAL
  // hxtMeshDelete(&splitMesh);//CRITICAL
  return 0;
}
