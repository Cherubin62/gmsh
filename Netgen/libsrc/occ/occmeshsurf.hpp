#ifdef OCCGEOMETRY

#ifndef FILE_OCCMESHSURF
#define FILE_OCCMESHSURF

#include "occgeom.hpp"

class OCCGeometry;

class OCCSurface
{
public:
  TopoDS_Face topods_face;
  Handle(Geom_Surface) occface;
  TopAbs_Orientation orient;

protected:
  Point<3> p1;
  Point<3> p2;

  /// in plane, directed p1->p2
  Vec<3> ex;
  /// in plane
  Vec<3> ey;
  /// outer normal direction
  Vec<3> ez;

  /// normal vector in p2
  Vec<3> n2;

  /// average normal vector
  Vec<3> nmid;

  // for transformation to parameter space
  Point<2> psp1;
  Point<2> psp2;
  Vec<2> psex;
  Vec<2> psey;
  Mat<2,2> Amat, Amatinv;

public:
  OCCSurface (const TopoDS_Face & aface)
  {
    topods_face = aface;
    occface = BRep_Tool::Surface(topods_face);
    orient = topods_face.Orientation();
    /*
    TopExp_Explorer exp1;
    exp1.Init (topods_face, TopAbs_WIRE);
    orient = TopAbs::Compose (orient, exp1.Current().Orientation());
    */
  };
  
  ~OCCSurface()
  {};

  void Project (Point<3> & p, PointGeomInfo & gi);

  void GetNormalVector (const Point<3> & p,
			const PointGeomInfo & geominfo,
			Vec<3> & n) const;

  /**
    Defines tangential plane in ap1.
    The local x-coordinate axis point to the direction of ap2 */
  void DefineTangentialPlane (const Point<3> & ap1, 
			      const PointGeomInfo & geominfo1,
			      const Point<3> & ap2,
			      const PointGeomInfo & geominfo2);


  /// Transforms 3d point p3d to local coordinates pplane
  void ToPlane (const Point<3> & p3d, const PointGeomInfo & geominfo,
		Point<2> & pplane, double h, int & zone) const;
  
  /// Transforms point pplane in local coordinates to 3d point
  void FromPlane (const Point<2> & pplane, 
		  Point<3> & p3d,
		  PointGeomInfo & gi,
		  double h);
};



///
class Meshing2OCCSurfaces : public Meshing2
{
  ///
  OCCSurface surface;
   
public:
  ///
  Meshing2OCCSurfaces (const TopoDS_Shape & asurf, const Box<3> & aboundingbox);

protected:
  ///
  virtual void DefineTransformation (Point3d & p1, Point3d & p2,
				     const PointGeomInfo * geominfo1,
				     const PointGeomInfo * geominfo2);
  ///
  virtual void TransformToPlain (const Point3d & locpoint, 
				 const MultiPointGeomInfo & geominfo,
				 Point2d & plainpoint, 
				 double h, int & zone);
  ///
  virtual int TransformFromPlain (Point2d & plainpoint,
				  Point3d & locpoint,
				  PointGeomInfo & gi,
				  double h);
  ///
  virtual double CalcLocalH (const Point3d & p, double gh) const;
  
};



///
class MeshOptimize2dOCCSurfaces : public MeshOptimize2d
  {
  ///
  const OCCGeometry & geometry;

public:
    ///
    MeshOptimize2dOCCSurfaces (const OCCGeometry & ageometry); 
   
    ///
    virtual void ProjectPoint (INDEX surfind, Point3d & p) const;
    ///
    virtual void ProjectPoint2 (INDEX surfind, INDEX surfind2, Point3d & p) const;
    ///
    virtual void GetNormalVector(INDEX surfind, const Point3d & p, Vec3d & n) const;
    ///
    virtual void GetNormalVector(INDEX surfind, const Point3d & p, PointGeomInfo & gi, Vec3d & n) const;

    
  virtual int CalcPointGeomInfo(int surfind, PointGeomInfo& gi, const Point3d& p3) const;

};



class OCCGeometry;


class OCCRefinementSurfaces : public Refinement
{
  const OCCGeometry & geometry;

public:
  OCCRefinementSurfaces (const OCCGeometry & ageometry);
  virtual ~OCCRefinementSurfaces ();
  
  virtual void PointBetween (const Point3d & p1, const Point3d & p2, double secpoint,
			     int surfi, 
			     const PointGeomInfo & gi1, 
			     const PointGeomInfo & gi2,
			     Point3d & newp, PointGeomInfo & newgi);

  virtual void PointBetween (const Point3d & p1, const Point3d & p2, double secpoint,
			     int surfi1, int surfi2, 
			     const EdgePointGeomInfo & ap1, 
			     const EdgePointGeomInfo & ap2,
			     Point3d & newp, EdgePointGeomInfo & newgi);

  virtual void ProjectToSurface (Point<3> & p, int surfi);
};



#endif



#endif
