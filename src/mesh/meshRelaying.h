#ifndef _MESH_RELAYING_
#define _MESH_RELAYING_

#include <vector>
#include <functional>
#include <numeric>
#include <algorithm>
#include <set>
#include <map>
#include "SVector3.h"
#include "SBoundingBox3d.h"

class GModel;

class discreteFront {
  //  int64_t octree;
  std::vector<int> colors;
  std::vector<size_t> corners;
  std::vector<size_t> lines;
  std::vector<double> pos;
  double t;
  
  // Let us thus use a search structure based on edges  
  std::vector<std::vector<size_t> > sss;
  size_t NX, NY;
  SBoundingBox3d bbox;
  void getCoordinates(double x, double y, int &IX, int &IY);
  
 public :
  discreteFront (){}
  discreteFront (std::vector<double> &p, std::vector<size_t> &l, std::vector<int> &c, double _t0 = 0);
  void addLines (std::vector<double> &p, std::vector<size_t> &l, std::vector<int> &c){
    size_t n = colors.size();
    pos.insert (pos.end(), p.begin(), p.end());
    colors.insert (colors.end(), c.begin(), c.end());
    for (size_t i=0;i<l.size();i++)lines.push_back(l[i]+n);
  }
  // assume 2D x y here !!!!
  void intersectLine2d (const SVector3 &p0, const SVector3 &p1,
			std::vector<double> &d, std::vector<int> &c);
  void cornersInTriangle2d (const SVector3 &p0, const SVector3 &p1, const SVector3 &p2,
			    std::vector<SVector3> &c, std::vector<int> &col);
  SVector3 closestPoints2d (const SVector3 &P);
  bool empty() const {return pos.empty();}
  void move (double dt);
  virtual SVector3 velocity (double x, double y, double z, double t, int col);
  void print(FILE *f);
  int whatIsTheColorOf2d (const SVector3 &P);
  int getColor(int i){return colors[i/2];}
  void buildSpatialSearchStructure ();
  int dim() const {return 2;}
  void setBbox (SBoundingBox3d _bbox){bbox=_bbox;}
  // basic shapes
  void addEllipsis (int tag, double xc, double yc, double theta0, double r1, double r2, int n);
  void addRectangle (int tag, double xc, double yc, double r1, double r2, int n);
  void addPolygon (int tag, const std::vector<SVector3> &poly, int n);
  void boolOp ();
};



class meshRelaying {

  std::vector<std::vector<size_t> > v2v;

  std::vector<std::vector<size_t> > v2tet;
  std::vector<std::vector<size_t> > v2tri;
  std::vector<std::vector<size_t> > v2edg;

  std::vector<size_t> tets;
  std::vector<size_t> tris;
  std::vector<size_t> edgs;

  std::vector<size_t> front_nodes;
  std::vector<double> pos;
  std::vector<double> initial_pos;
  std::vector<std::pair<size_t,size_t> > bnd2d;
  std::vector<std::pair<size_t,size_t> > bnd1d;
  std::vector<size_t> corners;
  std::vector<size_t> dimVertex;

  
  //// levelset function that drives relaying
  double time;  
  std::function<double (double, double, double, double)> levelset;

  /// discrete front
  discreteFront df;
  
  /// functions for optimization
  double smallest_measure (const size_t n, 
			const SVector3 &target) ;  
  void computeFront (const std::function<double(size_t, size_t)> &fct,
		     std::vector<SVector3> &front,
		     const char*fn = nullptr);
 public:
  meshRelaying (GModel *gm = nullptr); // use GModel gm or Gmodel::current() if NULL  
  void doRelaying (const std::function<std::vector<std::pair<double, int> >(size_t, size_t)> &f); 
  void setLevelset (const std::function<double(double, double, double, double)> &_ls){
    levelset = _ls;
  }
  void setDiscreteFront (const discreteFront &_df){    
    SBoundingBox3d bbox;
    for (size_t i=0;i<pos.size();i+=3)
      bbox += SPoint3(pos[i],pos[i+1],pos[i+2]);    
    bbox *= 1.01;
    df = _df;
    df.setBbox (bbox);
  }
  void advanceInTime(double dt){
    df.move(dt);
  }
  void doRelaying (double t);
  void doRelax (double r);
  void doRelaxFrontNode (size_t i, const std::vector<size_t> &n, double r);
  void print4debug(const char *);
};


#endif
