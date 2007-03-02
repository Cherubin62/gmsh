// $Id: SecondOrder.cpp,v 1.54 2007-02-27 17:15:47 remacle Exp $
//
// Copyright (C) 1997-2007 C. Geuzaine, J.-F. Remacle
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
// USA.
// 
// Please report all bugs and problems to <gmsh@geuz.org>.

#include "SecondOrder.h"
#include "MElement.h"
#include "MRep.h"
#include "Message.h"
#include "OS.h"


/*
  consider 2 points with their tangent

  x(t) = x(0) * H1(t) + dx(0) * H2(t) + x(1) * H3(t) + dx(1) * H4(t) 

*/

void Hermite2D_C1 ( SPoint3  &p1 , 
		    SPoint3  &p2 , 
		    SPoint3  &t1 , 
		    SPoint3  &t2 ,
		    SPoint3  &one_third, 
		    SPoint3  &two_third )
{

//   double L = sqrt((p1.x()-p2.x())*(p1.x()-p2.x()) + (p1.y()-p2.y()) * (p1.y()-p2.y()));

//   SVector3 p1p2 (p2,p1);
//   SVector3 tt1  (t1.x(),t1.y(),0);
//   SVector3 tt2  (t2.x(),t2.y(),0);
//   const double cost1 = p1p2 * tt1;
//   const double cost2 = p1p2 * tt2;
  

//   const double ts[2] = { 1./3.,2./3.};
//   for (int i=0 ; i < 2; i++)
//     {
//       const double t = ts[i];
//       const double H1 = (2*t+1)*(t-1)*(t-1);
//       const double H2 = L*t*(t-1)*(t-1);
//       const double H3 = t*t*(-2*t+3);
//       const double H4 = -L*(1-t)*t*t;
//       if (i == 0)one_third = p1*H1 + t1*H2 + p2*H3 + t2*H4;
//       else two_third = p1*H1 + t1*H2 + p2*H3 + t2*H4;
//     }  
}


extern GModel *GMODEL;

// for each pair of vertices (an edge), we build a list of vertices that
// are the high order representation of the edge
typedef std::map<std::pair<MVertex*,MVertex*>, std::vector<MVertex*> > edgeContainer;
// for each face (a list of vertices) we build a list of vertices that are
// the high order representation of the face
typedef std::map<std::vector<MVertex*>, std::vector<MVertex*> > faceContainer;

bool reparamOnFace(MVertex *v, GFace *gf, SPoint2 &param)
{
  if(v->onWhat()->dim() == 0){
    GVertex *gv = (GVertex*)v->onWhat();

    // abort if we could be on a seam
    std::list<GEdge*> ed = gv->edges();
    for(std::list<GEdge*>::iterator it = ed.begin(); it != ed.end(); it++)
      if((*it)->isSeam(gf)) return false;

    param = gv->reparamOnFace(gf, 1);
  }
  else if(v->onWhat()->dim() == 1){
    GEdge *ge = (GEdge*)v->onWhat();

    // abort if we are on a seam (todo: try dir=-1 and compare)
    if(ge->isSeam(gf)) return false;

    double UU;
    v->getParameter(0, UU);
    param = ge->reparamOnFace(gf, UU, 1);
  }
  else{
    double UU, VV;
    if(v->onWhat() == gf && v->getParameter(0, UU) && v->getParameter(1, VV))
      param = SPoint2(UU, VV);
    else
      param = gf->parFromPoint(SPoint3(v->x(), v->y(), v->z()));
  }
  return true;
}

void getEdgeVertices(GEdge *ge, MElement *ele, 
		     std::vector<MVertex*> &ve,
		     edgeContainer &edgeVertices,
		     bool linear, int nPts = 1)
{
  bool hermite = false;

  for(int i = 0; i < ele->getNumEdges(); i++){
    MEdge edge = ele->getEdge(i);
    std::pair<MVertex*, MVertex*> p(edge.getMinVertex(), edge.getMaxVertex());
    if(edgeVertices.count(p)){
      ve.insert(ve.end(),edgeVertices[p].begin(),edgeVertices[p].end());
    }
    else{
      MVertex *v0 = edge.getMinVertex(), *v1 = edge.getMaxVertex();            
      if (nPts == 2 && hermite){

	double u0 = 1e6, u1 = 1e6;
	Range<double> bounds = ge->parBounds(0);
	if(ge->getBeginVertex() && ge->getBeginVertex()->mesh_vertices[0] == v0) 
	  u0 = bounds.low();
	else if(ge->getEndVertex() && ge->getEndVertex()->mesh_vertices[0] == v0) 
	  u0 = bounds.high();
	else 
	  v0->getParameter(0, u0);
	if(ge->getBeginVertex() && ge->getBeginVertex()->mesh_vertices[0] == v1) 
	  u1 = bounds.low();
	else if(ge->getEndVertex() && ge->getEndVertex()->mesh_vertices[0] == v1) 
	  u1 = bounds.high();
	else 
	  v1->getParameter(0, u1);

	SVector3 tv1 = ge->firstDer(u0);
	SVector3 tv2 = ge->firstDer(u1);
	
	tv1.normalize();
	tv2.normalize();

	SPoint3 t1(tv1.x(),tv1.y(),0), t2(tv2.x(),tv2.y(),0), 
	  one_third, two_third,vv0(v0->x(),v0->y(),0),vv1(v1->x(),v1->y(),0);

	Hermite2D_C1 ( vv0,vv1,t1 ,t2 ,one_third,two_third );	

	printf("points (%g,%g) (%g,%g) tg (%g,%g) (%g,%g) int (%g,%g) and (%g,%g)\n",
	       v0->x(),v0->y(),v1->x(),v1->y(),tv1.x(),tv1.y(),tv2.x(),tv2.y(),
	       one_third.x(),one_third.y(),two_third.x(),two_third.y());

	

	MVertex *v1 = new MVertex(one_third.x(),one_third.y(),0);
	MVertex *v2 = new MVertex(two_third.x(),two_third.y(),0);
	edgeVertices[p].push_back(v1);
	ge->mesh_vertices.push_back(v1);
	ve.push_back(v1);
	edgeVertices[p].push_back(v2);
	ge->mesh_vertices.push_back(v2);
	ve.push_back(v2);	
      }
      else{
	for (int j=0;j<nPts;j++)
	  {
	    MVertex *v;
	    double t = (double)(j+1)/(nPts+1);
	    if(linear || ge->geomType() == GEntity::DiscreteCurve){
	      SPoint3 pc = edge.interpolate(t);
	      v = new MVertex(pc.x(), pc.y(), pc.z(), ge);
	    }
	    else {
	      double u0 = 1e6, u1 = 1e6;
	      Range<double> bounds = ge->parBounds(0);
	      if(ge->getBeginVertex() && ge->getBeginVertex()->mesh_vertices[0] == v0) 
		u0 = bounds.low();
	      else if(ge->getEndVertex() && ge->getEndVertex()->mesh_vertices[0] == v0) 
		u0 = bounds.high();
	      else 
		v0->getParameter(0, u0);
	      if(ge->getBeginVertex() && ge->getBeginVertex()->mesh_vertices[0] == v1) 
		u1 = bounds.low();
	      else if(ge->getEndVertex() && ge->getEndVertex()->mesh_vertices[0] == v1) 
		u1 = bounds.high();
	      else 
		v1->getParameter(0, u1);
	      double uc = (1.-t) * u0 + t * u1;
	      if(u0 < 1e6 && u1 < 1e6 && uc > u0 && uc < u1){
		GPoint pc = ge->point(uc);
		v = new MEdgeVertex(pc.x(), pc.y(), pc.z(), ge, uc);
	      }
	      else{
		// normally never here, but we don't treat periodic curves
		// properly, so we can get uc < u0 or uc > u1...
		SPoint3 pc = edge.interpolate(t);
		v = new MVertex(pc.x(), pc.y(), pc.z(), ge);
	      }
	    }
	    edgeVertices[p].push_back(v);
	    ge->mesh_vertices.push_back(v);
	    ve.push_back(v);
	}
      }
    }
  }
}

void getEdgeVertices(GFace *gf, MElement *ele, 
		     std::vector<MVertex*> &ve,
		     edgeContainer &edgeVertices,
		     bool linear, int nPts = 1)
{
  for(int i = 0; i < ele->getNumEdges(); i++){
    MEdge edge = ele->getEdge(i);    
    std::vector<MVertex*> temp;    
    std::pair<MVertex*, MVertex*> p(edge.getMinVertex(), edge.getMaxVertex());
    if(edgeVertices.count(p)){
      temp = edgeVertices[p];
    }
    else{
      MVertex *v0 = edge.getMinVertex(), *v1 = edge.getMaxVertex();
      for (int j=0;j<nPts;j++)
	{
	  const double t = (double)(j+1)/(nPts+1);
	  MVertex *v;
	  if(1 && (linear || gf->geomType() == GEntity::DiscreteSurface)){
	    SPoint3 pc = edge.interpolate(t);
	    v = new MVertex(pc.x(), pc.y(), pc.z(), gf);
	  }
	  else{
	    SPoint2 p0, p1;
	    if(reparamOnFace(v0, gf, p0) && reparamOnFace(v1, gf, p1)){
	      double uc = (1.-t) * p0[0] + t * p1[0];
	      double vc = (1.-t) * p0[1] + t * p1[1];
	      GPoint pc = gf->point(uc, vc);
	      v = new MFaceVertex(pc.x(), pc.y(), pc.z(), gf, uc, vc);
	    }
	    else{
	      // need to treat seams correctly!
	      SPoint3 pc = edge.interpolate(t);
	      v = new MVertex(pc.x(), pc.y(), pc.z(), gf);
	    }
	  }
	  edgeVertices[p].push_back(v);
	  gf->mesh_vertices.push_back(v);
	  temp.push_back(v);
	}
    }
    if (edge.getMinVertex() == edge.getVertex(0))
      ve.insert(ve.end(),temp.begin(),temp.end());
    else
      ve.insert(ve.end(),temp.rbegin(),temp.rend());
  }
}

void getEdgeVertices(GRegion *gr, MElement *ele, 
		     std::vector<MVertex*> &ve,
		     edgeContainer &edgeVertices,
		     bool linear, int nPts = 1)
{
  for(int i = 0; i < ele->getNumEdges(); i++){
    MEdge edge = ele->getEdge(i);
    std::pair<MVertex*, MVertex*> p(edge.getMinVertex(), edge.getMaxVertex());
    if(edgeVertices.count(p)){
      ve.insert (ve.end(),edgeVertices[p].begin(),edgeVertices[p].end());
    }
    else{
      for (int j=0;j<nPts;j++)
	{
	  double t = (double)(j+1)/(nPts+1);
	  SPoint3 pc = edge.interpolate(t);
	  MVertex *v = new MVertex(pc.x(), pc.y(), pc.z(), gr);
	  edgeVertices[p].push_back(v);
	  gr->mesh_vertices.push_back(v);
	  ve.push_back(v);
	}
    }
  }
}

void getFaceVertices(GFace *gf, MElement *ele, std::vector<MVertex*> &vf,
		     faceContainer &faceVertices,
		     bool linear, int nPts = 1)
{
  for(int i = 0; i < ele->getNumFaces(); i++){
    MFace face = ele->getFace(i);
    // triangles
    if(face.getNumVertices() == 3)
      {
	std::vector<MVertex*> p;
	face.getOrderedVertices(p);
	if(faceVertices.count(p)){
	  vf.insert(vf.begin(),faceVertices[p].begin(),faceVertices[p].end());
	}
	else{
	  for (int j = 0 ; j < nPts ; j++ )
	    {
	      for (int k = 0 ; k < nPts-j-1 ; k++ )
		{
		  MVertex *v;
		  double t1 = (double)(j+1)/(nPts+1);
		  double t2 = (double)(k+1)/(nPts+1);
		  if(linear || gf->geomType() == GEntity::DiscreteSurface){
		    SPoint3 pc = face.interpolate(t1,t2);
		    v = new MVertex(pc.x(), pc.y(), pc.z(), gf);
		  }
		  else{
		    SPoint2 p0, p1, p2;
		    if(reparamOnFace(p[0], gf, p0) && reparamOnFace(p[1], gf, p1) &&
		       reparamOnFace(p[2], gf, p2)){
		      double uc = (1.-t1-t2)*p0[0] + t1*p1[0] + t2*p2[0];
		      double vc = (1.-t1-t2)*p0[1] + t1*p1[1] + t2*p2[1];
		      GPoint pc = gf->point(uc, vc);
		      v = new MFaceVertex(pc.x(), pc.y(), pc.z(), gf, uc, vc);
		    }
		    else{
		      // need to treat seams correctly!
		      SPoint3 pc = face.interpolate(t1,t2);
		      v = new MVertex(pc.x(), pc.y(), pc.z(), gf);
		    }
		  }
		  faceVertices[p].push_back(v);
		  gf->mesh_vertices.push_back(v);
		  vf.push_back(v);
		}
	    }
	}
      }
    // quadrangles
    else if(face.getNumVertices() == 4)
      {
	std::vector<MVertex*> p;
	face.getOrderedVertices(p);
	if(faceVertices.count(p)){
	  vf.insert(vf.begin(),faceVertices[p].begin(),faceVertices[p].end());
	}
	else{
	  for (int j = 0 ; j < nPts ; j++ )
	    {
	      for (int k = 0 ; k < nPts ; k++ )
		{
		  MVertex *v;
		  double t1 = (double)(j+1)/(nPts+1);
		  double t2 = (double)(k+1)/(nPts+1);
		  if(linear || gf->geomType() == GEntity::DiscreteSurface){
		    SPoint3 pc = face.interpolate(t1,t2);
		    v = new MVertex(pc.x(), pc.y(), pc.z(), gf);
		  }
		  else{
		    SPoint2 p0, p1, p2, p3;
		    if(reparamOnFace(p[0], gf, p0) && reparamOnFace(p[1], gf, p1) &&
		       reparamOnFace(p[2], gf, p2) && reparamOnFace(p[3], gf, p3)){
		      double uc = 0.25*((1-t1)*(1-t2)* p0[0] + 
					(1-t1)*(1+t2)* p0[0] + 
					(1-t1)*(1+t2)* p0[0] + 
					(1+t1)*(1-t2)* p0[0] ); 
		      double vc = 0.25*((1-t1)*(1-t2)* p0[1] + 
					(1-t1)*(1+t2)* p0[1] + 
					(1-t1)*(1+t2)* p0[1] + 
					(1+t1)*(1-t2)* p0[1] ); 
		      GPoint pc = gf->point(uc, vc);
		      v = new MFaceVertex(pc.x(), pc.y(), pc.z(), gf, uc, vc);
		    }
		    else{
		      // need to treat seams correctly!
		      SPoint3 pc = face.interpolate(t1,t2);
		      v = new MVertex(pc.x(), pc.y(), pc.z(), gf);
		    }
		  }
		  faceVertices[p].push_back(v);
		  gf->mesh_vertices.push_back(v);
		  vf.push_back(v);
		}
	    }
	}
      }
    else throw;
  }
}

void getFaceVertices(GRegion *gr, MElement *ele, std::vector<MVertex*> &vf,
		     faceContainer &faceVertices,
		     bool linear, int nPts = 1)
{
  for(int i = 0; i < ele->getNumFaces(); i++){
    MFace face = ele->getFace(i);
    std::vector<MVertex*> p;
    face.getOrderedVertices(p);
    if(faceVertices.count(p)){
      vf.insert(vf.begin(),faceVertices[p].begin(),faceVertices[p].end());
    }
    else{      
	{
	  for (int j = 0 ; j < nPts ; j++ )
	    {
	      int st = nPts;
	      if(face.getNumVertices() == 3)st=j;
	      for (int k = 0 ; k < st ; k++ )
		{
		  double t1 = (double)(j+1)/(nPts+1);
		  double t2 = (double)(k+1)/(nPts+1);
		  SPoint3 pc = face.interpolate(t1,t2);
		  MVertex *v = new MVertex(pc.x(), pc.y(), pc.z(), gr);
		  faceVertices[p].push_back(v);
		  gr->mesh_vertices.push_back(v);
		  vf.push_back(v);
		}
	    }
	}
    }
  }
}

void setSecondOrder(GEdge *ge,
		    edgeContainer &edgeVertices,
		    bool linear, int nbPts = 1)
{
  std::vector<MLine*> lines2;
  for(unsigned int i = 0; i < ge->lines.size(); i++){
    MLine *l = ge->lines[i];
    std::vector<MVertex*> ve;
    getEdgeVertices(ge, l, ve, edgeVertices, linear,nbPts);
    if ( nbPts == 1)
      lines2.push_back(new MLine3(l->getVertex(0), l->getVertex(1), ve[0]));
    else
      lines2.push_back(new MLineN(l->getVertex(0), l->getVertex(1), ve));      
    delete l;
  }
  ge->lines = lines2;

  if(ge->meshRep) ge->meshRep->destroy();
}

void setSecondOrder(GFace *gf,
		    edgeContainer &edgeVertices,
		    faceContainer &faceVertices,
		    bool linear, bool incomplete, int nPts = 1)
{
  std::vector<MTriangle*> triangles2;
  for(unsigned int i = 0; i < gf->triangles.size(); i++){
    MTriangle *t = gf->triangles[i];
    std::vector<MVertex*> ve,vf;
    getEdgeVertices(gf, t, ve, edgeVertices, linear,nPts);
    if (nPts == 1)
      triangles2.push_back
	(new MTriangle6(t->getVertex(0), t->getVertex(1), t->getVertex(2),
			ve[0], ve[1], ve[2]));
    else
      if(incomplete){
	triangles2.push_back
	  (new MTriangleN(t->getVertex(0), t->getVertex(1), t->getVertex(2),
			  ve,nPts+1));
      }
      else
	{
	  getFaceVertices(gf, t, vf, faceVertices, linear,nPts);
	  ve.insert(ve.end(),vf.begin(),vf.end());
	  triangles2.push_back
	    (new MTriangleN(t->getVertex(0), t->getVertex(1), t->getVertex(2),
			    ve,nPts+1));
	}      
    delete t;
  }
  gf->triangles = triangles2;

  std::vector<MQuadrangle*> quadrangles2;
  for(unsigned int i = 0; i < gf->quadrangles.size(); i++){
    MQuadrangle *q = gf->quadrangles[i];
    std::vector<MVertex*> ve, vf;
    getEdgeVertices(gf, q, ve, edgeVertices, linear,nPts);
    if(incomplete){
      quadrangles2.push_back
	(new MQuadrangle8(q->getVertex(0), q->getVertex(1), q->getVertex(2),
			  q->getVertex(3), ve[0], ve[1], ve[2], ve[3]));
    }
    else{
      getFaceVertices(gf, q, vf, faceVertices, linear);
      quadrangles2.push_back
	(new MQuadrangle9(q->getVertex(0), q->getVertex(1), q->getVertex(2),
			  q->getVertex(3), ve[0], ve[1], ve[2], ve[3], vf[0]));
    }
    delete q;
  }
  gf->quadrangles = quadrangles2;
  
  if(gf->meshRep) gf->meshRep->destroy();
}

void setSecondOrder(GRegion *gr,
		    edgeContainer &edgeVertices,
		    faceContainer &faceVertices,
		    bool linear, bool incomplete, int nPts = 1)
{
  std::vector<MTetrahedron*> tetrahedra2;
  for(unsigned int i = 0; i < gr->tetrahedra.size(); i++){
    MTetrahedron *t = gr->tetrahedra[i];
    std::vector<MVertex*> ve;
    getEdgeVertices(gr, t, ve, edgeVertices, linear, nPts);
    tetrahedra2.push_back
      (new MTetrahedron10(t->getVertex(0), t->getVertex(1), t->getVertex(2), 
			  t->getVertex(3), ve[0], ve[1], ve[2], ve[3], ve[4], ve[5]));
    delete t;
  }
  gr->tetrahedra = tetrahedra2;

  std::vector<MHexahedron*> hexahedra2;
  for(unsigned int i = 0; i < gr->hexahedra.size(); i++){
    MHexahedron *h = gr->hexahedra[i];
    std::vector<MVertex*> ve, vf;
    getEdgeVertices(gr, h, ve, edgeVertices, linear, nPts);
    if(incomplete){
      hexahedra2.push_back
	(new MHexahedron20(h->getVertex(0), h->getVertex(1), h->getVertex(2), 
			   h->getVertex(3), h->getVertex(4), h->getVertex(5), 
			   h->getVertex(6), h->getVertex(7), ve[0], ve[1], ve[2], 
			   ve[3], ve[4], ve[5], ve[6], ve[7], ve[8], ve[9], ve[10], 
			   ve[11]));
    }
    else{
      getFaceVertices(gr, h, vf, faceVertices, linear);
      SPoint3 pc = h->barycenter();
      MVertex *v = new MVertex(pc.x(), pc.y(), pc.z(), gr);
      gr->mesh_vertices.push_back(v);
      hexahedra2.push_back
	(new MHexahedron27(h->getVertex(0), h->getVertex(1), h->getVertex(2), 
			   h->getVertex(3), h->getVertex(4), h->getVertex(5), 
			   h->getVertex(6), h->getVertex(7), ve[0], ve[1], ve[2], 
			   ve[3], ve[4], ve[5], ve[6], ve[7], ve[8], ve[9], ve[10], 
			   ve[11], vf[0], vf[1], vf[2], vf[3], vf[4], vf[5], v));
    }
    delete h;
  }
  gr->hexahedra = hexahedra2;

  std::vector<MPrism*> prisms2;
  for(unsigned int i = 0; i < gr->prisms.size(); i++){
    MPrism *p = gr->prisms[i];
    std::vector<MVertex*> ve, vf;
    getEdgeVertices(gr, p, ve, edgeVertices, linear, nPts);
    if(incomplete){
      prisms2.push_back
	(new MPrism15(p->getVertex(0), p->getVertex(1), p->getVertex(2), 
		      p->getVertex(3), p->getVertex(4), p->getVertex(5), 
		      ve[0], ve[1], ve[2], ve[3], ve[4], ve[5], ve[6], ve[7], ve[8]));
    }
    else{
      getFaceVertices(gr, p, vf, faceVertices, linear);
      prisms2.push_back
	(new MPrism18(p->getVertex(0), p->getVertex(1), p->getVertex(2), 
		      p->getVertex(3), p->getVertex(4), p->getVertex(5), 
		      ve[0], ve[1], ve[2], ve[3], ve[4], ve[5], ve[6], ve[7], ve[8],
		      vf[0], vf[1], vf[2]));
    }
    delete p;
  }
  gr->prisms = prisms2;

  std::vector<MPyramid*> pyramids2;
  for(unsigned int i = 0; i < gr->pyramids.size(); i++){
    MPyramid *p = gr->pyramids[i];
    std::vector<MVertex*> ve, vf;
    getEdgeVertices(gr, p, ve, edgeVertices, linear, nPts);
    if(incomplete){
      pyramids2.push_back
	(new MPyramid13(p->getVertex(0), p->getVertex(1), p->getVertex(2), 
			p->getVertex(3), p->getVertex(4), ve[0], ve[1], ve[2], 
			ve[3], ve[4], ve[5], ve[6], ve[7]));
    }
    else{
      getFaceVertices(gr, p, vf, faceVertices, linear);
      pyramids2.push_back
	(new MPyramid14(p->getVertex(0), p->getVertex(1), p->getVertex(2), 
			p->getVertex(3), p->getVertex(4), ve[0], ve[1], ve[2], 
			ve[3], ve[4], ve[5], ve[6], ve[7], vf[0]));
    }
    delete p;
  }
  gr->pyramids = pyramids2;

  if(gr->meshRep) gr->meshRep->destroy();
}

template<class T>
void setFirstOrder(GEntity *e, std::vector<T*> &elements)
{
  std::vector<T*> elements1;
  for(unsigned int i = 0; i < elements.size(); i++){
    T *ele = elements[i];
    int n = ele->getNumPrimaryVertices();
    std::vector<MVertex*> v1;
    for(int j = 0; j < n; j++)
      v1.push_back(ele->getVertex(j));
    for(int j = n; j < ele->getNumVertices(); j++)
      ele->getVertex(j)->setVisibility(-1);
    elements1.push_back(new T(v1));
    delete ele;
  }
  elements = elements1;
  
  if(e->meshRep) e->meshRep->destroy();
}

void removeSecondOrderVertices(GEntity *e)
{
  std::vector<MVertex*> v1;
  for(unsigned int i = 0; i < e->mesh_vertices.size(); i++){
    if(e->mesh_vertices[i]->getVisibility() < 0)
      delete e->mesh_vertices[i];
    else
      v1.push_back(e->mesh_vertices[i]);
  }
  e->mesh_vertices = v1;
}

void Degre1(GModel *m)
{
  // replace all elements with first order elements and mark all
  // unused vertices with a -1 visibility flag
  for(GModel::eiter it = m->firstEdge(); it != m->lastEdge(); ++it){
    setFirstOrder(*it, (*it)->lines);
  }
  for(GModel::fiter it = m->firstFace(); it != m->lastFace(); ++it){
    setFirstOrder(*it, (*it)->triangles);
    setFirstOrder(*it, (*it)->quadrangles);
  }
  for(GModel::riter it = m->firstRegion(); it != m->lastRegion(); ++it){
    setFirstOrder(*it, (*it)->tetrahedra);
    setFirstOrder(*it, (*it)->hexahedra);
    setFirstOrder(*it, (*it)->prisms);
    setFirstOrder(*it, (*it)->pyramids);
  }

  // remove all vertices with a -1 visibility flag
  for(GModel::eiter it = m->firstEdge(); it != m->lastEdge(); ++it)
    removeSecondOrderVertices(*it);
  for(GModel::fiter it = m->firstFace(); it != m->lastFace(); ++it)
    removeSecondOrderVertices(*it);
  for(GModel::riter it = m->firstRegion(); it != m->lastRegion(); ++it)
    removeSecondOrderVertices(*it);
}

void Degre2(GModel *m, bool linear, bool incomplete)
{
  // replace all the elements in the mesh with second order elements
  // by creating unique vertices on the edges/faces of the mesh:
  //
  // - if linear is set to true, new vertices are created by linear
  //   interpolation between existing ones. If not, new vertices are
  //   created on the exact geometry, provided that the geometrical
  //   edges/faces are discretized with 1D/2D elements. (I.e., if
  //   there are only 3D elements in the mesh then any new vertices on
  //   the boundary will always be created by linear interpolation,
  //   whether linear is set or not.)
  //
  // - if incomplete is set to true, we only create new vertices on 
  //   edges (creating 8-node quads, 20-node hexas, etc., instead of
  //   9-node quads, 27-node hexas, etc.)

  int nPts = CTX.mesh.order - 1;

  Msg(STATUS1, "Meshing second order...");
  double t1 = Cpu();

  // first, make sure to remove any existsing second order vertices/elements
  Degre1(m);

  // then create new second order vertices/elements
  edgeContainer edgeVertices;
  faceContainer faceVertices;
  for(GModel::eiter it = GMODEL->firstEdge(); it != GMODEL->lastEdge(); ++it)
    setSecondOrder(*it, edgeVertices, linear, nPts);
  for(GModel::fiter it = GMODEL->firstFace(); it != GMODEL->lastFace(); ++it)
    setSecondOrder(*it, edgeVertices, faceVertices, linear, incomplete,nPts);
  for(GModel::riter it = GMODEL->firstRegion(); it != GMODEL->lastRegion(); ++it)
    setSecondOrder(*it, edgeVertices, faceVertices, linear, incomplete,nPts);

  double t2 = Cpu();
  Msg(INFO, "Mesh second order complete (%g s)", t2 - t1);
  Msg(STATUS1, "Mesh");
}
