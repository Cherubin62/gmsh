
#if defined(_OPENMP)
#include <omp.h>
#endif
#include <set>
#include <vector>
#include "GmshDefines.h"
#include "Context.h"
#include "GModel.h"
#include "MElement.h"
#include "MQuadrangle.h"
#include "GFace.h"
#include "discreteFace.h"
#include "MVertex.h"
#include "MEdge.h"
#include "MLine.h"
#include "meshWinslow2d.h"
#include "meshSurfaceProjection.h"
#include "meshGFaceOptimize.h"
#include "Field.h"
#include "geolog.h"

int BLOB = 1;

static GPoint CLOSESTPOINT (GFace *gf, const SPoint3 &p, double uv[2], GEntity::GeomType GT){
  //  if (GT == GEntity::DiscreteSurface){
  //    return GPoint(p.x(),p.y(),p.z(),gf,0,0);
  //  }
  return gf->closestPoint(p,uv);
}

static GPoint CLOSESTPOINT (GEdge *ge, const SPoint3 &p, double u){
  return ge->closestPoint(p,u);
}

// SHOULD BE FASTER
template <class ITERATOR> 
static std::vector<MVertex*> buildBoundary (ITERATOR beg, ITERATOR end){
  std::vector<MEdge> eds,veds;

  for (ITERATOR ite = beg; ite != end;++ite){
    for (size_t j=0;j<(size_t)(*ite)->getNumEdges();j++){
      eds.push_back((*ite)->getEdge(j));
    }
  }
  MEdgeLessThan melt;
  std::sort(eds.begin(),eds.end(), melt);
  for(size_t i=0;i<eds.size();i++){
    if (i != eds.size()-1 && eds[i] == eds[i+1])i++;
    else veds.push_back(eds[i]);
  }
  
  std::vector<std::vector<MVertex *> > vsorted;
  SortEdgeConsecutive(veds, vsorted);
  if (vsorted.size() != 1){
    std::vector<MVertex *> empty;
    return empty;
  }

  return vsorted[0];
}


// -------------------------------------------------------------------------------------------
//  BIG FUNCTION : COULD BE MADE SMALLER BUT EASY TO READ
//

static std::vector<MVertex*> reverseVector (const std::vector<MVertex*> &v){
  std::vector<MVertex*> r;
  for (size_t i=0;i<v.size();i++)r.push_back(v[v.size() - 1 - i]);
  return r;
}

static std::vector<MVertex*> createVertices (GFace* gf, MVertex *v1, MVertex *v2, int n) {
  std::vector<MVertex*> r;
  r.push_back(v1);
  for (int i=1;i<n;i++){
    double xi = (double)i/n;
    SPoint3 p ((1.-xi)*v1->x()+xi*v2->x(),(1.-xi)*v1->y()+xi*v2->y(),(1.-xi)*v1->z()+xi*v2->z());
    double uv[2] = {0,0};

    GPoint pp = CLOSESTPOINT(gf,p,uv, gf->geomType());

    MVertex *vNew = new MFaceVertex(pp.x(),pp.y(),pp.z(),gf,pp.u(),pp.v());
    gf->mesh_vertices.push_back(vNew);
    r.push_back(vNew);
  }
  r.push_back(v2);
  return r;
}

static void removeFromAdjacencyList (MElement*e,  v2t_cont &adj){

  for (size_t i=0;i<e->getNumVertices();i++){
    v2t_cont::iterator iti = adj.find(e->getVertex(i));
    if (iti != adj.end()){
      iti->second.erase(std::remove(iti->second.begin(),iti->second.end(),e),iti->second.end());
    }
  }
}

static void addToAdjacencyList (MElement*e,  v2t_cont &adj){
  for (size_t i=0;i<e->getNumVertices();i++){
    v2t_cont::iterator iti = adj.find(e->getVertex(i));
    if (iti != adj.end())iti->second.push_back(e);
    else {
      std::vector<MElement*> els;
      els.push_back(e);
      adj[e->getVertex(i)] =els;
    }
  }
}


static void createQuadPatch (GFace* gf,
			     const std::vector<MVertex*> &s0,
			     const std::vector<MVertex*> &s1,
			     const std::vector<MVertex*> &s2,
			     const std::vector<MVertex*> &s3,
			     std::vector<MQuadrangle*> &newQuads){
  std::vector< std::vector<MVertex*> > grid;
  grid.push_back(s0);
  std::vector<MVertex*> s3r = reverseVector(s3);
  for (size_t i=1;i<s3r.size()-1;i++){
    grid.push_back(createVertices(gf,s3r[i],s1[i],s0.size()-1));
  }
  grid.push_back(reverseVector(s2));

  
  for (size_t i=0;i<grid.size()-1;i++){
    for (size_t j=0;j<grid[i].size()-1;j++){
      MQuadrangle *q = new MQuadrangle (grid[i][j],grid[i+1][j],grid[i+1][j+1],grid[i][j+1]);
      newQuads.push_back(q);
      gf->quadrangles.push_back(q);
    }
  }    
}


int remeshCavity (GFace *gf,
			 int index,
			 std::set<MElement*> &cavity,
			 std::vector<MVertex*> &bnd,
			 v2t_cont &adj,
			 std::map<MVertex*,int, MVertexPtrLessThan> &newSings){

  int nb5=0, nb3=0;
  {
    std::set<MVertex*> internal;
    for (std::set<MElement*>::iterator it = cavity.begin(); it != cavity.end(); ++it){
      for (size_t i=0;i<(*it)->getNumVertices(); i++){
        MVertex *v = (*it)->getVertex(i);
        if (std::find(bnd.begin(), bnd.end(), v) == bnd.end())internal.insert(v);
      }
    }
    for (std::set<MVertex*>::iterator it = internal.begin(); it != internal.end(); ++it){
      v2t_cont::iterator itv = adj.find(*it);
      if      (itv != adj.end() && itv->second.size() == 3)nb3++;
      else if (itv != adj.end() && itv->second.size() == 5)nb5++;
    }
  }
  
  // look for corners
  
  std::vector<int> corners;
  for (size_t i=0;i<bnd.size();i++){
    v2t_cont::iterator it = adj.find(bnd[i]);
    int inside = 0;
    for (size_t j=0;j<it->second.size();j++){
      if (cavity.find(it->second[j]) != cavity.end()) inside ++;
    }
    if (inside == 1)corners.push_back(i);
  }

  //  printf(" ... index = %i, nb5=%i, nb3=%i, corners.size()=%li\n", index, nb5, nb3, corners.size());

  if (index == 5 && nb5 == 1 && nb3 == 0)return 0;
  if (index == 3 && nb3 == 1 && nb5 == 0)return 0;
  
  //  printf("%d\n",corners.size());
  
  //      a1   a0
  //    1----o--------2
  // a2 |    |      / a2
  //    o----x----o
  //    |        /
  // a0 |      /
  //    |    / a1
  //    |  /   
  //     0
  //    a0 + a2 = n0
  //    a0 + a1 = n1
  //    a1 + a2 = n2
  //    --> a0 = 1/2 (n0+n1-n2)
  //    --> a1 = 1/2 (n1+n2-n0)
  //    --> a2 = 1/2 (n0+n2-n1)

  if (corners.size() == index && index == 3){
    int n0 = corners[1]-corners[0];
    int n1 = corners[2]-corners[1];
    int n2 = bnd.size() + corners[0]-corners[2];
    int a0 = (n0+n1-n2)/2;
    int a1 = (n1+n2-n0)/2;
    int a2 = (n0+n2-n1)/2;

    if (a0 <= 0 || a1 <= 0 || a2 <= 0)return -1;
        
    if (a0+a2 != n0 || a0+a1 != n1 || a1+a2 != n2){
      return -1;
    }
    
    std::vector<MVertex*> vec_0_01; for (size_t i=0;i<=a0;i++)vec_0_01.push_back(bnd[(corners[0]+i)%bnd.size()]);
    std::vector<MVertex*> vec_01_1; for (size_t i=0;i<=a2;i++)vec_01_1.push_back(bnd[(corners[0]+a0+i)%bnd.size()]);
    std::vector<MVertex*> vec_1_12; for (size_t i=0;i<=a1;i++)vec_1_12.push_back(bnd[(corners[0]+a0+a2+i)%bnd.size()]);
    std::vector<MVertex*> vec_12_2; for (size_t i=0;i<=a0;i++)vec_12_2.push_back(bnd[(corners[0]+a0+a2+a1+i)%bnd.size()]);
    std::vector<MVertex*> vec_2_02; for (size_t i=0;i<=a2;i++)vec_2_02.push_back(bnd[(corners[0]+a0+a2+a1+a0+i)%bnd.size()]);
    std::vector<MVertex*> vec_02_0; for (size_t i=0;i<=a1;i++)vec_02_0.push_back(bnd[(corners[0]+a0+a2+a1+a0+a2+i)%bnd.size()]);


    double x=0,y=0,z=0;
    for (size_t i=0;i<bnd.size();i++){
      x += bnd[i]->x();
      y += bnd[i]->y();
      z += bnd[i]->z();
    }
    SPoint3 p (x/bnd.size(),y/bnd.size(),z/bnd.size());
    double uv[2] = {0,0};

    GPoint pp = CLOSESTPOINT(gf,p,uv, gf->geomType());
    MVertex *sing = new MFaceVertex(pp.x(),pp.y(),pp.z(),gf,pp.u(),pp.v());
    gf->mesh_vertices.push_back(sing);

    newSings[sing] = index;
    
    /*    printVector(vec_0_01);
    printVector(vec_01_1);
    printVector(vec_1_12);
    printVector(vec_12_2);
    printVector(vec_2_02);
    printVector(vec_02_0);*/
    
    std::vector<MVertex*> vec_01_sing = createVertices (gf, vec_01_1[0], sing, a1);
    std::vector<MVertex*> vec_12_sing = createVertices (gf, vec_12_2[0], sing, a2);
    std::vector<MVertex*> vec_02_sing = createVertices (gf, vec_02_0[0], sing, a0);
    /*
    printVector(vec_01_sing);
    printVector(vec_12_sing);
    printVector(vec_02_sing);
    */
    
    std::vector<MQuadrangle*> newQuads;
    createQuadPatch (gf, vec_0_01, vec_01_sing, reverseVector(vec_02_sing), vec_02_0, newQuads);
    createQuadPatch (gf, vec_01_1, vec_1_12, vec_12_sing, reverseVector(vec_01_sing), newQuads);
    createQuadPatch (gf, vec_12_2, vec_2_02, vec_02_sing, reverseVector(vec_12_sing), newQuads);

    Msg::Info("  Triangular patch (%lu --> %luquads) with (%d,%d) sing. (3,5) replaced by one singularity %lu",
	      cavity.size(), newQuads.size(), nb3, nb5, sing->getNum());

    for (std::set<MElement*>::iterator it = cavity.begin(); it != cavity.end() ; ++it){
      MQuadrangle *q = dynamic_cast<MQuadrangle*>(*it);	      
      if (!q)Msg::Error ("A non quad is present in the list of quad of face %lu",gf->tag());
      gf->quadrangles.erase (std::remove(gf->quadrangles.begin(),gf->quadrangles.end(),q),gf->quadrangles.end());
      removeFromAdjacencyList(*it,adj);
    }
    for (size_t i=0;i<newQuads.size();i++){
      addToAdjacencyList (newQuads[i],adj);
    }
    return 1;
  }
  // 5 patch --> one singularity
  else if (corners.size() == index && index == 5){
    int n0 = corners[1]-corners[0];
    int n1 = corners[2]-corners[1];
    int n2 = corners[3]-corners[2];
    int n3 = corners[4]-corners[3];
    int n4 = bnd.size() - n0 - n1 - n2 - n3;
    
    //a0 + a1 = n0
    //a2 + a3 = n1
    //a1 + a4 = n2  
    //a0 + a3 = n3
    //a2 + a4 = n4
    // 1 1 0 0 0 
    // 0 0 1 1 0 
    // 0 1 0 0 1 
    // 1 0 0 1 0 
    // 0 0 1 0 1 

    
    int a0 = (n0-n1-n2+n3+n4)/2;
    int a1 = (n0+n1+n2-n3-n4)/2;
    int a2 = (n0+n1-n2-n3+n4)/2;
    int a3 = (-n0+n1+n2+n3-n4)/2;
    int a4 = (-n0-n1+n2+n3+n4)/2;

    //if (a0 == a1 && a0 == a2 && a0 == a3 && a0 == a4 && cavity.size() == 5*a0*a0)return 1;

    if (a0 <=0 || a1 <=0 || a2 <=0 || a3 <=0 || a4 <=0){
      Msg::Info("  Non meshable blob with 5 corners found %d %d %d %d %d",n0,n1,n2,n3,n4);
      Msg::Info("                                         %d %d %d %d %d",a0,a1,a2,a3,a4);
      return 0;
    }
    
    if (a0+a1-n0 ||
	a2+a3-n1 ||
	a1+a4-n2 ||
	a0+a3-n3 ||
	a2+a4-n4)
      {
	//	printf("--> n's %d %d %d %d %d\n",n0,n1,n2,n3,n4);
	//	printf("--> a's %d %d %d %d %d\n",a0,a1,a2,a3,a4);
	//	printf("5 patch found but impossible to mesh\n");
	return -1;
      }  
    else
      {
      }


    std::vector<MVertex*> vec_0_01; for (size_t i=0;i<=a0;i++)vec_0_01.push_back(bnd[(corners[0]+i)%bnd.size()]);
    std::vector<MVertex*> vec_01_1; for (size_t i=0;i<=a1;i++)vec_01_1.push_back(bnd[(corners[0]+a0+i)%bnd.size()]);
    std::vector<MVertex*> vec_1_12; for (size_t i=0;i<=a2;i++)vec_1_12.push_back(bnd[(corners[0]+a0+a1+i)%bnd.size()]);
    std::vector<MVertex*> vec_12_2; for (size_t i=0;i<=a3;i++)vec_12_2.push_back(bnd[(corners[0]+a0+a1+a2+i)%bnd.size()]);
    std::vector<MVertex*> vec_2_23; for (size_t i=0;i<=a1;i++)vec_2_23.push_back(bnd[(corners[0]+a0+a1+a2+a3+i)%bnd.size()]);
    std::vector<MVertex*> vec_23_3; for (size_t i=0;i<=a4;i++)vec_23_3.push_back(bnd[(corners[0]+a0+a1+a2+a3+a1+i)%bnd.size()]);
    std::vector<MVertex*> vec_3_34; for (size_t i=0;i<=a3;i++)vec_3_34.push_back(bnd[(corners[0]+a0+a1+a2+a3+a1+a4+i)%bnd.size()]);
    std::vector<MVertex*> vec_34_4; for (size_t i=0;i<=a0;i++)vec_34_4.push_back(bnd[(corners[0]+a0+a1+a2+a3+a1+a4+a3+i)%bnd.size()]);
    std::vector<MVertex*> vec_4_04; for (size_t i=0;i<=a4;i++)vec_4_04.push_back(bnd[(corners[0]+a0+a1+a2+a3+a1+a4+a3+a0+i)%bnd.size()]);
    std::vector<MVertex*> vec_04_0; for (size_t i=0;i<=a2;i++)vec_04_0.push_back(bnd[(corners[0]+a0+a1+a2+a3+a1+a4+a3+a0+a4+i)%bnd.size()]);

    double x=0,y=0,z=0;
    for (size_t i=0;i<bnd.size();i++){
      x += bnd[i]->x();
      y += bnd[i]->y();
      z += bnd[i]->z();
    }
    SPoint3 p (x/bnd.size(),y/bnd.size(),z/bnd.size());
    double uv[2] = {0,0};
    GPoint pp = CLOSESTPOINT(gf,p,uv, gf->geomType());
    MVertex *sing = new MFaceVertex(pp.x(),pp.y(),pp.z(),gf,pp.u(),pp.v());
    gf->mesh_vertices.push_back(sing);

    
    newSings[sing] = index;

    std::vector<MVertex*> vec_01_sing = createVertices (gf, vec_01_1[0], sing, a2);
    std::vector<MVertex*> vec_12_sing = createVertices (gf, vec_12_2[0], sing, a1);
    std::vector<MVertex*> vec_23_sing = createVertices (gf, vec_23_3[0], sing, a3);
    std::vector<MVertex*> vec_34_sing = createVertices (gf, vec_34_4[0], sing, a4);
    std::vector<MVertex*> vec_04_sing = createVertices (gf, vec_04_0[0], sing, a0);
    /*
    printVector(vec_01_sing);
    printVector(vec_12_sing);
    printVector(vec_02_sing);
    */
    
    std::vector<MQuadrangle*> newQuads;
    createQuadPatch (gf, vec_0_01, vec_01_sing, reverseVector(vec_04_sing), vec_04_0, newQuads);
    createQuadPatch (gf, vec_01_1, vec_1_12, vec_12_sing, reverseVector(vec_01_sing), newQuads);
    createQuadPatch (gf, vec_12_2, vec_2_23, vec_23_sing, reverseVector(vec_12_sing), newQuads);
    createQuadPatch (gf, vec_23_3, vec_3_34, vec_34_sing, reverseVector(vec_23_sing), newQuads);
    createQuadPatch (gf, vec_34_4, vec_4_04, vec_04_sing, reverseVector(vec_34_sing), newQuads);
        
    Msg::Info("  Pentagonal patch (%lu --> %lu quads) with (%d,%d) sing. (3,5) replaced by one singularity %lu",
	      cavity.size(), newQuads.size(), nb3, nb5, sing->getNum());

    for (std::set<MElement*>::iterator it = cavity.begin(); it != cavity.end() ; ++it){
      MQuadrangle *q = dynamic_cast<MQuadrangle*>(*it);	      
      if (!q)Msg::Error ("A non quad is present in the list of quad of face %lu",gf->tag());
      gf->quadrangles.erase (std::remove(gf->quadrangles.begin(),gf->quadrangles.end(),q),gf->quadrangles.end());
      removeFromAdjacencyList(*it,adj);
    }
    for (size_t i=0;i<newQuads.size();i++){
      addToAdjacencyList (newQuads[i],adj);
    }
    return 1;
  }
  else if (corners.size() == index && index == 4){
    int n0 = corners[1]-corners[0];
    int n1 = corners[2]-corners[1];
    int n2 = corners[3]-corners[2];
    int n3 = bnd.size() - n0 - n1 - n2 ;

    if (n0 == n2 && n1 == n3 && (n0 !=2 || n1 !=2)){
      std::vector<MVertex*> vec_0_1; for (size_t i=0;i<=n0;i++)vec_0_1.push_back(bnd[(corners[0]+i)%bnd.size()]);
      std::vector<MVertex*> vec_1_2; for (size_t i=0;i<=n1;i++)vec_1_2.push_back(bnd[(corners[0]+n0+i)%bnd.size()]);
      std::vector<MVertex*> vec_2_3; for (size_t i=0;i<=n2;i++)vec_2_3.push_back(bnd[(corners[0]+n0+n1+i)%bnd.size()]);
      std::vector<MVertex*> vec_3_0; for (size_t i=0;i<=n3;i++)vec_3_0.push_back(bnd[(corners[0]+n0+n1+n2+i)%bnd.size()]);
      std::vector<MQuadrangle*> newQuads;

      createQuadPatch (gf, vec_0_1, vec_1_2, vec_2_3, vec_3_0, newQuads);

      Msg::Info("  Quadrilateral patch (%lu quads -> %lu quads) with (%d,%d) sing. (3,5) replaced by a regular grid",
		cavity.size(), newQuads.size(),nb3, nb5);

      for (std::set<MElement*>::iterator it = cavity.begin(); it != cavity.end() ; ++it){
	MQuadrangle *q = dynamic_cast<MQuadrangle*>(*it);	      
	if (!q)Msg::Error ("A non quad is present in the list of quad of face %lu",gf->tag());
	gf->quadrangles.erase (std::remove(gf->quadrangles.begin(),gf->quadrangles.end(),q),gf->quadrangles.end());
	removeFromAdjacencyList(*it,adj);
      }
      for (size_t i=0;i<newQuads.size();i++){
	addToAdjacencyList (newQuads[i],adj);
      }
      return 1;
    }
    else if ((n0>1 && n1>1 && n2>1 && n3>1) &&
	     ((n0 == n2 && abs(n1-n3) == 2) ||
	      (n1 == n3 && abs(n0-n2) == 2))){
      // make the right rotation to allow one single implementation
      // n0 == n2
      // n1 + 2 = n3

      int a0,a1,a2,a3,start;
      
      Msg::Info("found a cavity (bnd %lu) with 4 corners %d %d %d %d --> creation of a dipole",bnd.size(),n0,n1,n2,n3);
      if (n0 == n2 && n1+2 == n3){
	a0 = n0/2;
	a1 = n0 - a0 - 1;
	a2 = n1/2;
	a3 = n1 - a2;

	//	printf("%d %d %d %d\n",a0,a1,a2,a3);
	
	start = corners[0];
      }
      else if (n0 == n2 && n1-2 == n3){
	a0 = n0/2;
	a1 = n0 - a0 - 1;
	a2 = n3/2;
	a3 = n3 - a2;
	start = corners[2];	
      }
      else if (n1 == n3 && n0-2 == n2){
	a0 = n1/2;
	a1 = n1 - a0 - 1;
	a2 = n2/2;
	a3 = n2 - a2;
	start = corners[1];	
      }
      else if (n1 == n3 && n0+2 == n2){
	a0 = n3/2;
	a1 = n3 - a0 - 1;
	a2 = n0/2;
	a3 = n0 - a2;
	start = corners[3];	
      }
      else {
	printf("TO DO found a cavity (bnd %lu) with 4 corners %d %d %d %d\n",bnd.size(),n0,n1,n2,n3);
	return 0;
      }
      
      std::vector<MVertex*> vec_00; for (size_t i=0;i<=a0;i++) vec_00.push_back(bnd[(start+i)%bnd.size()]);
      std::vector<MVertex*> vec_01; for (size_t i=0;i<=1 ;i++) vec_01.push_back(bnd[(start+a0+i)%bnd.size()]);
      std::vector<MVertex*> vec_02;  for (size_t i=0;i<=a1;i++) vec_02.push_back(bnd[(start+a0+1+i)%bnd.size()]);

      std::vector<MVertex*> vec_10;  for (size_t i=0;i<=a2;i++) vec_10.push_back(bnd[(start+a0+1+a1+i)%bnd.size()]);
      std::vector<MVertex*> vec_11;  for (size_t i=0;i<=a3;i++) vec_11.push_back(bnd[(start+a0+1+a1+a2+i)%bnd.size()]);

      std::vector<MVertex*> vec_20 ;  for (size_t i=0;i<=a1;i++) vec_20.push_back(bnd[(start+a0+1+a1+a2+a3+i)%bnd.size()]);
      std::vector<MVertex*> vec_21;  for (size_t i=0;i<=1 ;i++)  vec_21.push_back(bnd[(start+a0+1+a1+a2+a3+a1+i)%bnd.size()]);
      std::vector<MVertex*> vec_22;   for (size_t i=0;i<=a0;i++) vec_22.push_back(bnd[(start+a0+1+a1+a2+a3+a1+1+i)%bnd.size()]);

      std::vector<MVertex*> vec_30;   for (size_t i=0;i<=a3;i++) vec_30.push_back(bnd[(start+a0+1+a1+a2+a3+a1+1+a0+i)%bnd.size()]);
      std::vector<MVertex*> vec_31;  for (size_t i=0;i<=1;i++) vec_31.push_back(bnd[(start+a0+1+a1+a2+a3+a1+1+a0+a3+i)%bnd.size()]);
      std::vector<MVertex*> vec_32;  for (size_t i=0;i<=1;i++) vec_32.push_back(bnd[(start+a0+1+a1+a2+a3+a1+1+a0+a3+1+i)%bnd.size()]);
      std::vector<MVertex*> vec_33;   for (size_t i=0;i<=a2;i++) vec_33.push_back(bnd[(start+a0+1+a1+a2+a3+a1+1+a0+a3+1+1+i)%bnd.size()]);

      MVertex *sing_5, *sing_3, *left, *right; 
      
      double x,y,z;
      {
	x = 0.5* (vec_02[0]->x()+vec_21[0]->x());
	y = 0.5* (vec_02[0]->y()+vec_21[0]->y());
	z = 0.5* (vec_02[0]->z()+vec_21[0]->z());
	SPoint3 p (x,y,z);
	double uv[2] = {0,0};
	GPoint pp = CLOSESTPOINT(gf,p,uv, gf->geomType());
	sing_5 = new MFaceVertex(pp.x(),pp.y(),pp.z(),gf,pp.u(),pp.v());
	gf->mesh_vertices.push_back(sing_5);
      }
      {
	x = 0.5* (vec_01[0]->x()+vec_22[0]->x());
	y = 0.5* (vec_01[0]->y()+vec_22[0]->y());
	z = 0.5* (vec_01[0]->z()+vec_22[0]->z());
	SPoint3 p (x,y,z);
	double uv[2] = {0,0};
	GPoint pp = CLOSESTPOINT(gf,p,uv, gf->geomType());
	sing_3 = new MFaceVertex(pp.x(),pp.y(),pp.z(),gf,pp.u(),pp.v());
	gf->mesh_vertices.push_back(sing_3);
      }
      {
	x = 0.25* vec_01[0]->x()+ 0.75*vec_22[0]->x();
	y = 0.25* vec_01[0]->y()+ 0.75*vec_22[0]->y();
	z = 0.25* vec_01[0]->z()+ 0.75*vec_22[0]->z();
	SPoint3 p (x,y,z);
	double uv[2] = {0,0};
	GPoint pp = CLOSESTPOINT(gf,p,uv, gf->geomType());
	left = new MFaceVertex(pp.x(),pp.y(),pp.z(),gf,pp.u(),pp.v());
	gf->mesh_vertices.push_back(left);
      }
      {
	x = 0.75* vec_01[0]->x()+ 0.25*vec_22[0]->x();
	y = 0.75* vec_01[0]->y()+ 0.25*vec_22[0]->y();
	z = 0.75* vec_01[0]->z()+ 0.25*vec_22[0]->z();
	SPoint3 p (x,y,z);
	double uv[2] = {0,0};
	GPoint pp = CLOSESTPOINT(gf,p,uv, gf->geomType());
	right = new MFaceVertex(pp.x(),pp.y(),pp.z(),gf,pp.u(),pp.v());
	gf->mesh_vertices.push_back(right);
      }

      std::vector<MVertex*> vec_02_sing_5 = createVertices (gf,  vec_01[0], sing_5, a2);
      std::vector<MVertex*> vec_11_sing_5 = createVertices (gf,  vec_11[0], sing_5, a1);
      std::vector<MVertex*> vec_21_sing_5 = createVertices (gf,  vec_21[0], sing_5, a3);

      std::vector<MVertex*> vec_sing_5_left  = createVertices (gf,  sing_5, left, 1);
      std::vector<MVertex*> vec_sing_5_right = createVertices (gf,  sing_5, right, 1);
      std::vector<MVertex*> vec_22_right = createVertices (gf,  vec_22[0], right, a3);
      std::vector<MVertex*> vec_01_left = createVertices (gf,  vec_01[0], left, a2);
      std::vector<MVertex*> vec_left_sing_3  = createVertices (gf,   left, sing_3, 1);
      std::vector<MVertex*> vec_right_sing_3 = createVertices (gf,   right, sing_3, 1);
      
      std::vector<MVertex*> vec_left_33   = createVertices (gf,   left, vec_33[0], a0);
      std::vector<MVertex*> vec_sing_3_32 = createVertices (gf,   sing_3, vec_32[0], a0);
      std::vector<MVertex*> vec_right_31 = createVertices (gf,   right, vec_31[0], a0);
      
      std::vector<MQuadrangle*> newQuads;
      createQuadPatch (gf, vec_02, vec_10, vec_11_sing_5,reverseVector(vec_02_sing_5), newQuads);
      createQuadPatch (gf, vec_11, vec_20, vec_21_sing_5,reverseVector(vec_11_sing_5), newQuads);

      createQuadPatch (gf, vec_01, vec_02_sing_5, vec_sing_5_left,reverseVector(vec_01_left), newQuads);
      createQuadPatch (gf, reverseVector(vec_sing_5_left), vec_sing_5_right,vec_right_sing_3,reverseVector(vec_left_sing_3), newQuads);
      createQuadPatch (gf, reverseVector(vec_sing_5_right), reverseVector(vec_21_sing_5), vec_21, vec_22_right,newQuads);
      
      createQuadPatch (gf, vec_00, vec_01_left, vec_left_33,vec_33, newQuads);
      createQuadPatch (gf, reverseVector(vec_left_33), vec_left_sing_3, vec_sing_3_32,vec_32, newQuads);
      createQuadPatch (gf, reverseVector(vec_sing_3_32), reverseVector(vec_right_sing_3), vec_right_31 ,vec_31, newQuads);
      createQuadPatch (gf, reverseVector(vec_right_31), reverseVector(vec_22_right), vec_22 ,vec_30, newQuads);


      Msg::Info("  Quadrilateral patch remeshed (%lu->%lu quads) with (%d,%d) sing (3,5) replaced by a 5/3 dipole (%lu %lu)",		
		cavity.size(), newQuads.size(), nb3, nb5, sing_3->getNum(), sing_5->getNum());

      
      for (std::set<MElement*>::iterator it = cavity.begin(); it != cavity.end() ; ++it){
	MQuadrangle *q = dynamic_cast<MQuadrangle*>(*it);	      
	if (!q)Msg::Error ("A non quad is present in the list of quad of face %lu",gf->tag());
	gf->quadrangles.erase (std::remove(gf->quadrangles.begin(),gf->quadrangles.end(),q),gf->quadrangles.end());
	removeFromAdjacencyList(*it,adj);
      }
      for (size_t i=0;i<newQuads.size();i++){
	addToAdjacencyList (newQuads[i],adj);
      }
      return 1;      
    }
  }
  return 0;
}

// -------------------------------------------------------------------------------------------

static std::vector<MVertex*> build_crown (MVertex *v, const std::vector<MElement*> &_e){
  //  printf("building crown %lu %lu\n",v->getNum(),_e.size());
  std::vector<MEdge> veds;
  std::set<MVertex*> connected;
  for (size_t i=0;i<(size_t)_e.size();i++){
    //    printf("%lu %lu %lu %lu \n",_e[i]->getVertex(0)->getNum(),_e[i]->getVertex(1)->getNum(),_e[i]->getVertex(2)->getNum(),_e[i]->getVertex(3)->getNum());
    for (size_t j=0;j<(size_t)_e[i]->getNumEdges();j++){
      MEdge ed = _e[i]->getEdge(j);
      if (ed.getVertex(0) != v && ed.getVertex(1) != v)	veds.push_back(ed);
      else if (ed.getVertex(0) == v)connected.insert(ed.getVertex(1));
      else if (ed.getVertex(1) == v)connected.insert(ed.getVertex(0));      
    }
  }

  std::vector<std::vector<MVertex *> > vsorted;
  SortEdgeConsecutive(veds, vsorted);

  if (vsorted.size() != 1) {
    Msg::Error("vertex with elements that are disconnected, %li lists", vsorted.size());
    for (size_t i = 0; i < vsorted.size(); ++i) {
      for (MVertex* vv: vsorted[i]) {
        GeoLog::add(SVector3(vv->point()),double(i),"!vertex_disco_elts");
      }
    }
    GeoLog::flush();
  }
  
  // start with a corner
  std::vector<MVertex *> result;
  if (vsorted.empty())return result;
  if (vsorted[0].size() > 1) vsorted[0].resize( vsorted[0].size()-1);
  if (connected.find(vsorted[0][0]) != connected.end()){
    for (size_t i=0;i<vsorted[0].size();i++)result.push_back(vsorted[0][(i+1)%vsorted[0].size()]);
  }
  else result = vsorted[0];

  //  for (size_t i=0;i<result.size();i++)
  //    printf("%lu ",result[i]->getNum());
  //  printf("\n");

  //  getchar();
  return result;
}

int classOfStencil(const std::vector<MElement *> &e) {
  size_t ntri = 0, nqua = 0, nelse = 0;
  for (size_t i=0;i<e.size();i++){
    if (e[i]->getTypeForMSH() == MSH_TRI_3)ntri++;
    else if (e[i]->getTypeForMSH() == MSH_QUA_4)nqua++;
    else nelse++;
  }
  if (nqua == e.size())return 1;
  if (ntri  == e.size())return 2;
  if (ntri + nqua == e.size())return 3;
  if (nelse != 0) return 4;
  return 5;
}

class winslowStencil{
public:
  int type;
  MVertex * center;
  std::vector<MVertex *> stencil;
  const std::vector<MElement*> elements;
  std::vector<SVector3> ptsStencil;
  ;
  winslowStencil (MVertex *v,
		  const std::vector<MElement*> &_e,
		  GFace *gf = NULL) : elements (_e)
  {
    type = classOfStencil(_e);
    if (type == 1 && _e.size() >= 3){
      stencil  = build_crown (v, _e);
      ptsStencil.resize(stencil.size());
      center = v;            
    }
    else{
      Msg::Error("weird class of stencil %d %lu",type,_e.size());
      GeoLog::add(SVector3(v->point()),double(0),"!weird_stencil");
      GeoLog::flush();
    }
  }  

  bool computeParameters (GFace *gf, std::vector<SPoint2> &ptsStencilParam){
    ptsStencilParam.resize(stencil.size());    
    for (size_t i=0; i< stencil.size();i++){
      if (stencil[i]->onWhat() != gf)return false;
      if (!reparamMeshVertexOnFace(stencil[i],gf,ptsStencilParam[i])){
	return false;
      }	  
    }
    return true;
  }

  SPoint3 new3dPositionCentroid (){
    double x=0,y=0,z=0;
    for (size_t i=0;i<stencil.size();i++){
      x += stencil[i]->x();
      y += stencil[i]->y();
      z += stencil[i]->z();
    }
    x /= stencil.size();
    y /= stencil.size();
    z /= stencil.size();
    return SPoint3(x,y,z);
  }

  // parameter space sm
  SPoint2 new3dPosition4quadsParam (GFace *gf, std::vector<SPoint2> &ptsStencilParam){
    /* simple unit stencil */
    double hx = 1.;
    double hy = 1.; 
    
    SPoint2 p;
    center->getParameter(0, p[0]);
    center->getParameter(1, p[1]);
    
    Pair<SVector3, SVector3> t = gf->firstDer(p);

    SVector3 dudu,dvdv,dudv;
    gf->secondDer(p, dudu, dvdv,dudv);

    const int order[8] = {7,1,3,5,0,6,2,4};
       
    /* Grid Generation §9 page 23 */

    const double dudxi  = 1./hx * (ptsStencilParam[order[0]].x() - ptsStencilParam[order[2]].x());
    const double dudeta = 1./hy * (ptsStencilParam[order[1]].x() - ptsStencilParam[order[3]].x());
    const double dvdxi  = 1./hx * (ptsStencilParam[order[0]].y() - ptsStencilParam[order[2]].y());
    const double dvdeta = 1./hy * (ptsStencilParam[order[1]].y() - ptsStencilParam[order[3]].y());
    const double J = dudxi*dvdeta-dudeta*dvdxi;

    const double gbar11 = dot(t.first(),t.first());
    const double gbar12 = dot(t.first(),t.second());
    const double gbar22 = dot(t.second(),t.second());
    const double Jbar = sqrt (gbar11*gbar22-gbar12*gbar12);    
    
    const double g11 = gbar11*dudxi*dudxi + 2*gbar12*dudxi*dvdxi+gbar22*dvdxi*dvdxi;
    const double g22 = gbar11*dudeta*dudeta + 2*gbar12*dudeta*dvdeta+gbar22*dvdeta*dvdeta;
    const double g12 = gbar11*dudxi*dudeta + gbar12*(dudxi*dvdeta+dudeta*dvdxi)+gbar22*dvdxi*dvdeta;
      
    
    const double gbar11u = 2*dot(t.first(),dudu);
    const double gbar11v = 2*dot(t.first(),dudv);    
    const double gbar22u = 2*dot(t.second(),dudv);
    const double gbar22v = 2*dot(t.second(),dvdv);
    const double gbar12u = dot(t.first(),dudv) + dot(t.second(),dudu);
    const double gbar12v = dot(t.first(),dvdv) + dot(t.second(),dudv);
    
    const double uip1j = ptsStencilParam[order[0]].x();  
    const double uim1j = ptsStencilParam[order[2]].x();  
    const double uijp1 = ptsStencilParam[order[1]].x();  
    const double uijm1 = ptsStencilParam[order[3]].x();  
    const double uip1jp1 = ptsStencilParam[order[4]].x();  
    const double uim1jp1 = ptsStencilParam[order[6]].x();  
    const double uim1jm1 = ptsStencilParam[order[7]].x();  
    const double uip1jm1 = ptsStencilParam[order[5]].x();  
    
    const double vip1j = ptsStencilParam[order[0]].y();  
    const double vim1j = ptsStencilParam[order[2]].y();  
    const double vijp1 = ptsStencilParam[order[1]].y();  
    const double vijm1 = ptsStencilParam[order[3]].y();  
    const double vip1jp1 = ptsStencilParam[order[4]].y();  
    const double vim1jp1 = ptsStencilParam[order[6]].y();  
    const double vim1jm1 = ptsStencilParam[order[7]].y();  
    const double vip1jm1 = ptsStencilParam[order[5]].y();  

    const double Jbaru = (1./(2*Jbar))*(gbar11*gbar22u+gbar22*gbar11u-2*gbar12*gbar12u);
    const double Jbarv = (1./(2*Jbar))*(gbar11*gbar22v+gbar22*gbar11v-2*gbar12*gbar12v);

    const double Delta2u = (1./(Jbar))*(Jbar*(gbar22u-gbar12v)-(gbar22*Jbaru-gbar12*Jbarv));
    const double Delta2v = (1./(Jbar))*(Jbar*(gbar11v-gbar12u)-(gbar11*Jbarv-gbar12*Jbaru));

    //    printf("G = %g %g %g  Gbar %g %g %g\n",g11,g22,g12,gbar11,gbar22,gbar12);
    
    // formulas 9.26a and 9.26b
    double uij = (1./(4*(g11+g22))) * (2*g22*(uip1j+uim1j) + 2*g11*(uijp1+uijm1)-2*g12*(uip1jp1-uim1jp1-uip1jm1+uim1jm1) - 2*J*J*Delta2u); 
    double vij = (1./(4*(g11+g22))) * (2*g22*(vip1j+vim1j) + 2*g11*(vijp1+vijm1)-2*g12*(vip1jp1-vim1jp1-vip1jm1+vim1jm1) - 2*J*J*Delta2v); 
    
    //    uij = 0.25*(uip1j+uim1j+uijp1+uijm1);
    //    vij = 0.25*(vip1j+vim1j+vijp1+vijm1);
    
    SPoint2 pp (uij,vij);
    
    return pp*1.1-p*.1;
    
  }
  
  SPoint3 new3dPosition4quads (){
    for (size_t i=0;i<stencil.size();i++)
      ptsStencil[i] = SVector3(stencil[i]->x(),stencil[i]->y(),stencil[i]->z());
    /* Stencil:
     *   6---1---4
     *   |   |   |
     *   2--- ---0
     *   |   |   |
     *   7---3---5
     */
    
    //      const int order[8] = {4,1,6,2,7,3,5,0};
    const int order[8] = {7,1,3,5,0,6,2,4};
   
    /* warning: 2D stencil but 3D coordinates */
    double hx = 1.;
    double hy = 1.; 
    
    /* 1. Compute the winslow coefficients (alpha_i, beta_i in the Karman paper) */
    /*    a. Compute first order derivatives of the position */
    SVector3 r_i[2];
    r_i[0] = 1./hx * (ptsStencil[order[0]] - ptsStencil[order[2]]);
    r_i[1] = 1./hy * (ptsStencil[order[1]] - ptsStencil[order[3]]);
    /*    b. Compute the alpha_i coefficients */
    double alpha_0 = dot(r_i[1],r_i[1]);
    double alpha_1 = dot(r_i[0],r_i[0]);
    /*    c. Compute the beta coefficient */
    double beta =  dot(r_i[0],r_i[1]);
    
    /* cross derivative */
    SVector3 u_xy = 1./(4.*hx*hy) * ((ptsStencil[order[4]]-ptsStencil[order[6]]) +
				     (ptsStencil[order[7]]-ptsStencil[order[5]]));
    
    /* 2. Compute the "winslow new position" */
    double denom = 2. * alpha_0 / (hx*hx) + 2. * alpha_1 / (hy*hy);
    SVector3 newPos = 1. / denom * (
				    alpha_0/(hx*hx) * (ptsStencil[order[0]] + ptsStencil[order[2]])
				    + alpha_1/(hy*hy) * (ptsStencil[order[1]] + ptsStencil[order[3]])
				    - 2. * beta * u_xy
				    );
    const double w = 1.7;
    SPoint3 p (w*newPos.x()+(1.-w)*center->x(),w*newPos.y()+(1.-w)*center->y(),w*newPos.z()+(1.-w)*center->z());
    return p;
  }
  
  double smooth (GEdge *ge){
    
    if (ge->geomType() == GEntity::DiscreteCurve){
      return 0;
    }
    
    if (stencil.empty())return 0;
    SPoint3 p;    
    if (type == 1 && stencil.size() == 8){
      p = new3dPosition4quads();
    }
    else {
      return 0;
    }
    double u ; center->getParameter(0,u);

    
    GPoint gp = CLOSESTPOINT(ge,p,u);
    if (!gp.succeeded()){
      return 0;
    }
    double dx = sqrt ((gp.x()-center->x())*(gp.x()-center->x())+
		      (gp.y()-center->y())*(gp.y()-center->y())+
		      (gp.z()-center->z())*(gp.z()-center->z()));			
    center->setXYZ(gp.x(),gp.y(),gp.z());
    center->setParameter(0,gp.u());
    center->setParameter(1,gp.v());
    return dx;
  }


  double smooth (GFace *gf, GEntity::GeomType GT, double radius, SPoint3 &c, SurfaceProjector* sp, size_t& cache, 
      bool spProjectOnCAD = false){
    if (stencil.empty())return 0;
    SPoint3 p;    
    //    printf("coucou1\n");
    if (type == 1 && stencil.size() == 8){      
      p = new3dPosition4quads();
    }
    else if (type == 1 && stencil.size() == 6){
      p = new3dPositionCentroid();
    }
    else if (type == 1 && stencil.size() == 10){
      p = new3dPositionCentroid();
    }
    else {
      return 0;
    }
    //    printf("coucou2\n");
    double uv[2] ; center->getParameter(0,uv[0]);center->getParameter(1,uv[1]);
    double dx;
    //    printf("coucou3\n");
    if (GT == GEntity::Plane){
      dx = sqrt ((p.x()-center->x())*(p.x()-center->x())+
          (p.y()-center->y())*(p.y()-center->y())+
          (p.z()-center->z())*(p.z()-center->z()));			
      center->setXYZ(p.x(),p.y(),p.z());
    }
    else if (GT == GEntity::Sphere){
      SVector3 vv = p - c;
      vv.normalize();
      vv *= radius;
      p= SPoint3(c.x() + vv.x(),c.y() + vv.y(),c.z() + vv.z());
      dx = sqrt ((p.x()-center->x())*(p.x()-center->x())+
          (p.y()-center->y())*(p.y()-center->y())+
          (p.z()-center->z())*(p.z()-center->z()));			
      center->setXYZ(p.x(),p.y(),p.z());
    }
    else {
      GPoint gp;
      if (sp) {
        gp = sp->closestPoint(p.data(), cache, true, true);
      } else {
        gp = CLOSESTPOINT(gf,p,uv, GT);
      }
      if (!gp.succeeded()){
        return 0;
      }
      dx = sqrt ((gp.x()-center->x())*(gp.x()-center->x())+
          (gp.y()-center->y())*(gp.y()-center->y())+
          (gp.z()-center->z())*(gp.z()-center->z()));			
      center->setXYZ(gp.x(),gp.y(),gp.z());
      center->setParameter(0,gp.u());
      center->setParameter(1,gp.v());
    }
    return dx;
  }
};


void meshWinslow1d (GEdge * ge, int nIter, Field *f) {
  return;
  nIter = 1000;
  std::vector<GFace*> faces = ge->faces();
  if (faces.size() != 2)return;
  
  v2t_cont adj;
  buildVertexToElement(faces[0]->triangles, adj);
  buildVertexToElement(faces[0]->quadrangles, adj);
  if (faces.size() == 2){
    buildVertexToElement(faces[1]->triangles, adj);
    buildVertexToElement(faces[1]->quadrangles, adj);
  }
  
  std::vector<winslowStencil> stencils;
  
  for (size_t i = 0; i<ge->mesh_vertices.size();i++){
    MEdgeVertex *v = dynamic_cast<MEdgeVertex*> (ge->mesh_vertices[i]);
    if (v){
      const std::vector<MElement *> &e = adj[v];
      winslowStencil st (v, e);
      stencils.push_back(st);      
    }
  }

  double dx0;
  for (int i=0;i<nIter;i++){
    double dx = 0;
    for (size_t j=0;j<stencils.size();j++){
      dx += stencils[j].smooth(ge);
    }
    if (i == 0)dx0 = dx;
    if (dx < .001*dx0) break;
  }     
}


/*
static int removeValence2Nodes(GFace * gf, v2t_cont &adj) {
  
  int nbDone=0;
  v2t_cont::iterator it = adj.begin();
  while(it != adj.end()) {
    MVertex *v = it->first;
    if (v->onWhat() == gf) {
      const std::vector<MElement *> e = it->second;
      int ntri = 0;
      int nqua = 0;
      int indextri[12];
      for (size_t i=0;i<e.size();i++){
	if (e[i]->getTypeForMSH() == MSH_TRI_3)indextri[ntri++] = i;
	else if (e[i]->getTypeForMSH() == MSH_QUA_4)indextri[nqua++] = i;
      }
      if (ntri == 2 && nqua == 3){
	winslowStencil st (v, e);	
      }
    }
    ++it;
  }
  return nbDone;
}
*/
/*
  -> Apply a unique pattern and modify the pattern to be a square
*/

static int removeValence6Nodes(GFace * gf, v2t_cont &adj) {
  //  return 0;
  int nbDone=0;
  v2t_cont::iterator it = adj.begin();

  std::vector<MVertex*> SIX;
  while(it != adj.end()) {
    MVertex *v = it->first;
    if (v->onWhat() == gf) {
      const std::vector<MElement *> &e = it->second;     
      if (e.size() == 6){
	SIX.push_back(v);
      }
    }
    ++it;
  }
  for (size_t K=0;K<SIX.size();K++){
    MVertex *v = SIX[K];
    it = adj.find(v);
    const std::vector<MElement *> &e = it->second;     
    std::vector<MVertex *> bnd = buildBoundary (e.begin(),e.end());
    bnd.resize(bnd.size()-1);
    if (bnd.size() != 12){
      Msg::Error("Impossible topology %d",bnd.size());
      continue;
    }
    int valences[12];
    //    int n4 = 0;
    //    int n3 = 0;
    int exterior[12];
    
    // compute how many quads are exterior to the cavity
    for (int i=0;i<12;i++){
      exterior[i] = 0;
      v2t_cont::iterator iti = adj.find(bnd[i]);
      if (iti != adj.end()){
	for (size_t j=0; j< iti->second.size() ; j++){
	  if (std::find(e.begin(),e.end(),iti->second[j]) == e.end()){
	    exterior[i]++;
	  }
	}	    
	valences[i] = iti->second.size();
      }
      else {
	Msg::Error("Unknown vertex %d",bnd[i]->getNum());
      }
    }
    
    // the pattern adds alternatively 1 and 2 quads in the cavity
    int interior[12] = {1,2,1,2,2,2,1,2,1,2,2,2};
    int start = -1;
    int best  = 100000;
    int worst = 100000;
    for (int i=0;i<12;i++){
      int total = 0;
      int worsti = 0;
      for (int j=0;j<12;j++){
	int contribution_j = exterior[j] + interior[(j+i)%12];
	total += abs (contribution_j-4);
	worsti = std::max(worsti,abs (contribution_j-4));
      }
      //      printf("total[%d] = %d, ext = %d\n",i,total,exterior[i]);
      if (total < best){
	best = total;
	start = i;
	worst = worsti;
      }
    }
    //    printf("best = %d worst %d i %d\n ",best, worst,start);
    
    double uv[2] = {0,0};
    double x12 = 0.5*(v->x() + bnd[(start+1)%12]->x());
    double y12 = 0.5*(v->y() + bnd[(start+1)%12]->y());
    double z12 = 0.5*(v->z() + bnd[(start+1)%12]->z());
    double x13 = 0.5*(v->x() + bnd[(start+7)%12]->x());
    double y13 = 0.5*(v->y() + bnd[(start+7)%12]->y());
    double z13 = 0.5*(v->z() + bnd[(start+7)%12]->z());
    GPoint p0 = CLOSESTPOINT(gf,SPoint3(x12,y12,z12),uv, gf->geomType());
    GPoint p1 = CLOSESTPOINT(gf,SPoint3(x13,y13,z13),uv, gf->geomType());
    
    MFaceVertex *v12 = new MFaceVertex (p0.x(),p0.y(),p0.z(),gf,p0.u(),p0.v()); 
    MFaceVertex *v13 = new MFaceVertex (p1.x(),p1.y(),p1.z(),gf,p1.u(),p1.v()); 
    //    MFaceVertex *v14 = new MFaceVertex (v->x(),v->y(),v->z(),gf,p1.u(),p1.v()); 
    
    gf->mesh_vertices.push_back(v12);
    gf->mesh_vertices.push_back(v13);
    int t[8][4] = {
      {0,1,12,11},
      {1,2,3,12},
      {11,12,13,10},
      {12,3,4,13},
      {10,13,14,9},
      {13,4,5,14},
      {9,14,7,8},
      {14,5,6,7}
    };
    bnd.push_back(v12);
    bnd.push_back(v13);
    bnd.push_back(v);

    std::vector<MElement*> Es = e;
    
    for (int i=0;i<6;i++){
      MQuadrangle *q = dynamic_cast<MQuadrangle*>(Es[i]);	      
      if (!q)Msg::Error ("A non quad is present in the list of quad of face %lu",gf->tag());
      gf->quadrangles.erase (std::remove(gf->quadrangles.begin(),gf->quadrangles.end(),q),gf->quadrangles.end());
      removeFromAdjacencyList (Es[i],  adj);
    }	    	    
    for (int i=0;i<8;i++){
      int index0 = t[i][0] < 12 ? (start+ t[i][0])%12 : t[i][0];
      int index1 = t[i][1] < 12 ? (start+ t[i][1])%12 : t[i][1];
      int index2 = t[i][2] < 12 ? (start+ t[i][2])%12 : t[i][2];
      int index3 = t[i][3] < 12 ? (start+ t[i][3])%12 : t[i][3];
      //      printf("%lu %d %d %d %d\n",bnd.size(),index0,index1,index2,index3);
      MQuadrangle *q = new MQuadrangle (bnd[index0],bnd[index1],bnd[index2],bnd[index3]);
      gf->quadrangles.push_back(q);
      addToAdjacencyList (q,  adj);
    }
    
    nbDone++;
  }
  if (nbDone)Msg::Info ("Removing %d valence 6 nodes ", nbDone);
  return nbDone;
}

static void allowProjections (GFace * gf){
  GEntity::GeomType GT = gf->geomType();

  // allow to do projections on discrete surfaces --------------
  if (GT == GEntity::DiscreteSurface){
    discreteFace *ds = dynamic_cast<discreteFace *> (gf);
    if (ds && !ds->haveParametrization()){
      std::vector<MTriangle*> temp;
      for (size_t i=0; i< gf->quadrangles.size(); i++){
	temp.push_back(new MTriangle (gf->quadrangles[i]->getVertex(0),
				      gf->quadrangles[i]->getVertex(1),
				      gf->quadrangles[i]->getVertex(2)));
	temp.push_back(new MTriangle (gf->quadrangles[i]->getVertex(2),
				      gf->quadrangles[i]->getVertex(3),
				      gf->quadrangles[i]->getVertex(0)));
	
      }
      if (ds) ds->createGeometryFromTriangulation(temp);
      for (size_t i=0; i< temp.size(); i++){
	delete temp[i];
      }
    }
  }
  // -----------------------------------------------------------
}

void meshWinslow2d (GFace  * gf, const std::vector<MQuadrangle*>& quads, 
    const std::vector<MVertex*>& freeVertices, int nIter, Field *f, bool remove, SurfaceProjector* sp) {
  if (gf->triangles.size())return;
  Msg::Debug("winslow 2D on %li quads, %li free vertices (face %i), %i iterations ...", quads.size(), freeVertices.size(),
	     gf->tag(), nIter);
  GEntity::GeomType GT = gf->geomType();
  
  allowProjections (gf);
  
  v2t_cont adj;
  buildVertexToElement(quads, adj);

  
  std::vector<winslowStencil> stencils;

  double radius;
  SPoint3 c;
  if (GT == GEntity::Sphere){
    gf->isSphere(radius, c) ;
  }

  for (MVertex* v: freeVertices) {
    auto it = adj.find(v);
    if (it == adj.end()) continue;
    if (v->onWhat() == gf) { 
      const std::vector<MElement *> &e = it->second;
      winslowStencil st (v, e, gf);
      stencils.push_back(st);
    }
    ++it;
  }
  
  double dx0;
  std::vector<size_t> cache_tris(stencils.size(),(size_t)-1);
  for (int i=0;i<nIter;i++){
    bool spProjectOnCAD = (sp && (95 * i > 100 * nIter));
    double dx = 0;
    for (size_t j=0;j<stencils.size();j++){
      double xx = stencils[j].smooth(gf,GT,radius,c,sp,cache_tris[j], spProjectOnCAD);
      dx += xx ;
    }
    if (i == 0)dx0 = dx;
    if (dx < .002*dx0) break;
  }  
}

void meshWinslow2d (GFace * gf, int nIter, Field *f, bool remove, SurfaceProjector* sp) {
  if (gf->triangles.size())return;
  GEntity::GeomType GT = gf->geomType();

  // for (size_t i=0;i<gf->mesh_vertices.size();i++){
  //   double u,v;
  //   gf->mesh_vertices[i]->getParameter(0,u);
  //   gf->mesh_vertices[i]->getParameter(1,v);
  //   if (u == 0 && v == 0){
  //     double uv[2];
  //     SPoint3 xxx (gf->mesh_vertices[i]->x(),gf->mesh_vertices[i]->y(),gf->mesh_vertices[i]->z());
  //     GPoint gp = gf->closestPoint(xxx,uv);
  //     gf->mesh_vertices[i]->setParameter(0,gp.u());
  //     gf->mesh_vertices[i]->setParameter(1,gp.v());
  //   }
  // }    

  
  allowProjections (gf);
  
  v2t_cont adj;
  buildVertexToElement(gf->quadrangles, adj);
  v2t_cont::iterator it = adj.begin();

  std::vector<winslowStencil> stencils;

  double radius;
  SPoint3 c;
  if (GT == GEntity::Plane){
    nIter = 1000;
  }
  if (GT == GEntity::Sphere){
    nIter = 1000;
    gf->isSphere(radius, c) ;
  }
  
  while(it != adj.end()) {
    MVertex *v = it->first;
    if (v->onWhat() == gf) {      
      const std::vector<MElement *> &e = it->second;
      winslowStencil st (v, e, gf);
      stencils.push_back(st);
    }
    ++it;
  }
  
  std::vector<size_t> cache_tris(stencils.size(),(size_t)-1);
  double dx0;
  for (int i=0;i<nIter;i++){
    double dx = 0;
    bool spProjectOnCAD = (sp && (95 * i > 100 * nIter));
    for (size_t j=0;j<stencils.size();j++){
      double xx = stencils[j].smooth(gf,GT,radius,c,sp,cache_tris[j], spProjectOnCAD);
      dx += xx ;
      //      printf("%lu %12.5E\n",j, xx);
    }
    if (i == 0)dx0 = dx;
    if (dx < .002*dx0) break;
    //    printf("%12.5E %12.5E\n",dx,dx0);
  }  
}

static MVertex* next_vertex (GFace *gf, MVertex *prev, MVertex *current, v2t_cont &adj){
  v2t_cont::iterator it = adj.find(current);
  const std::vector<MElement *> e = it->second;
  if (current->onWhat() != gf || e.size() != 4)return NULL;
  winslowStencil st (current, e, gf);
  std::vector<MVertex *> &crown = st.stencil;
  for (size_t i=0;i<crown.size();i++){
    if (crown[i] == prev)return crown[(i+4)%8];    
  }
  return NULL;
}

static void printEdge (int num, MVertex *v1, MVertex *v2, FILE *f){
  fprintf(f,"SL(%g,%g,%g,%g,%g,%g) {%d,%d};\n",
	  v1->x(),v1->y(),v1->z(),
	  v2->x(),v2->y(),v2->z(),num,num);
}

static void printVertex (int num, MVertex *v1, FILE *f){
  fprintf(f,"SP(%g,%g,%g) {%d};\n",
	  v1->x(),v1->y(),v1->z(),num);
}


static void updateBoundary (std::set<MElement*> & _u,
			    std::set<MElement*> & _e,
			    std::vector<MVertex*>&bnd){

  //  return;
  std::vector<MEdge> eds, veds;
  eds.reserve(2*_u.size());

  bnd.resize(bnd.size()-1);
  
  for (size_t i=0; i<bnd.size();i++)
    eds.push_back(MEdge (bnd[i], bnd[(i+1)%bnd.size()]));
  
  for (std::set<MElement*>::iterator ite = _u.begin(); ite != _u.end();++ite){
    if (_e.find(*ite) == _e.end()){
      for (size_t j=0;j<(size_t)(*ite)->getNumEdges();j++){
	eds.push_back((*ite)->getEdge(j));
      }
    }
  }

  _e.insert(_u.begin(),_u.end());

  MEdgeLessThan melt;
  std::sort(eds.begin(),eds.end(), melt);
  for(size_t i=0;i<eds.size();i++){
    if (i != eds.size()-1 && eds[i] == eds[i+1])i++;
    else veds.push_back(eds[i]);
  }
  
  std::vector<std::vector<MVertex *> > vsorted;
  SortEdgeConsecutive(veds, vsorted);

  if (vsorted.size() != 1){
    std::vector<MVertex *> empty;
    bnd = empty;
  }
  else {
    bnd = vsorted[0];
  }
}


static MVertex* countSing (  std::set<MElement*> &cavity,
			     std::map<MVertex *, int, MVertexPtrLessThan> &sing,
			     int &nb){

  nb = 0;
  MVertex *theSing = NULL;
  for (std::set<MElement*>::iterator ite = cavity.begin(); ite != cavity.end(); ++ite){
    for (size_t i=0;i<(*ite)->getNumVertices();i++){
      std::map<MVertex *, int, MVertexPtrLessThan>::iterator it = sing.find((*ite)->getVertex(i));
      if (it != sing.end() && theSing != it->first){
	theSing = it->first;
	nb++;
      }
    }    
  }
  
  if (nb == 1) {
    //    printf("one single singularity %lu of index %d\n",theSing->getNum(),sing[theSing]);
    return theSing;
  }
  return NULL;

  for (std::map<MVertex *, int, MVertexPtrLessThan>::iterator it = sing.begin(); it != sing.end() ; ++it){
    double uvw[3];
    //    GVertex *gv = dynamic_cast<GVertex*> (it->first->onWhat());
    //    if (gv){
    //      if (gv->tag() == 24)printf("%12.5E %12.5E  vs %12.5E %12.5E \n",
    //				 it->first->x(),it->first->y(), gv->x(),gv->y());
    //    }
    double xyz[3] = {it->first->x(),it->first->y(),it->first->z()};
    for (std::set<MElement*>::iterator ite = cavity.begin(); ite != cavity.end(); ++ite){
      (*ite)->xyz2uvw(xyz,uvw);
      if ((*ite)->isInside(uvw[0],uvw[1],uvw[2])){
	theSing = it->first;
	nb++;
      }
    }
  }
  if (nb == 1) return theSing;
  return NULL;
}

static bool PRINTBLOB (int B, std::set<MElement*> & cavity ,
		       std::vector<MVertex*> &bnd)
{
  std::string aaa = "BLOB" + std::to_string(B) + ".pos";
  FILE *f = fopen (aaa.c_str(),"w");
  fprintf(f,"View \"\"{\n");
  for (size_t i=0;i<bnd.size();i++){
    fprintf(f,"SL(%g,%g,%g,%g,%g,%g){%lu,%lu};\n",
	    bnd[i]->x(),bnd[i]->y(),bnd[i]->z(),	  
	    bnd[(i+1)%bnd.size()]->x(),bnd[(i+1)%bnd.size()]->y(),bnd[(i+1)%bnd.size()]->z(),B,B);
  }
  for ( std::set<MElement*>::iterator it = cavity.begin();it != cavity.end() ; ++it){
    fprintf(f,"SQ(%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g){%lu,%lu,%lu,%lu};\n",
	    (*it)->getVertex(0)->x(),
	    (*it)->getVertex(0)->y(),
	    (*it)->getVertex(0)->z(),
	    (*it)->getVertex(1)->x(),
	    (*it)->getVertex(1)->y(),
	    (*it)->getVertex(1)->z(),
	    (*it)->getVertex(2)->x(),
	    (*it)->getVertex(2)->y(),
	    (*it)->getVertex(2)->z(),
	    (*it)->getVertex(3)->x(),
	    (*it)->getVertex(3)->y(),
	    (*it)->getVertex(3)->z(),B,B,B,B);
  }
  fprintf(f,"};\n");
  fclose(f);
  return true;
}



// SHOULD BE FASTER ... NOW OK
static bool cavityMeshable (GFace *gf,
			    v2t_cont &adj,
			    std::vector<MVertex*> bnd,
			    std::set<MElement*> & cavity,
			    int index, bool debug_ = false){

  int nb5=0, nb3=0;
  for (std::set<MElement*>::iterator it = cavity.begin(); it != cavity.end(); ++it){
    for (size_t i=0;i<(*it)->getNumVertices(); i++){
      (*it)->getVertex(i)->setIndex(0);
    }
  }

  for (std::set<MElement*>::iterator it = cavity.begin(); it != cavity.end(); ++it){
    for (size_t i=0;i<(*it)->getNumVertices(); i++){
      MVertex *v = (*it)->getVertex(i);
      v->setIndex(v->getIndex() + 1);
    }
  }

  for (size_t i=0;i<bnd.size();i++)bnd[i]->setIndex(0);

  
  for (std::set<MElement*>::iterator it = cavity.begin(); it != cavity.end(); ++it){
    for (size_t i=0;i<(*it)->getNumVertices(); i++){
      MVertex *v = (*it)->getVertex(i);
      if      (v->getIndex() == 3)nb3++;
      else if (v->getIndex() == 5)nb5++;
      v->setIndex(0);
    }
  }


  // DEFINITION OF CORNER
  // REGULAR VERTEX --> ONE INSIDE AND 3 OUTSIDE

  std::vector<int> corners;
  if (bnd.size() > 0) bnd.resize(bnd.size() - 1);

  for (size_t i=0;i<bnd.size();i++){
    v2t_cont::iterator it = adj.find(bnd[i]);
    if (it == adj.end()){
      Msg::Warning("not in the adj list");
      return false;
    }
    int inside = 0;
    for (size_t j=0;j<it->second.size();j++){
      if (cavity.find(it->second[j]) != cavity.end()) inside ++;
    }
    if (inside == 1)corners.push_back(i);
  }

  
  if (corners.size() == index && index == 3){
    if (nb3 == 1 && nb5 == 0)return false;

    int n0 = corners[1]-corners[0];
    int n1 = corners[2]-corners[1];
    int n2 = bnd.size() - n0 - n1;
    int a0 = (n0+n1-n2)/2;
    int a1 = (n1+n2-n0)/2;
    int a2 = (n0+n2-n1)/2;

    
    if (a0 <= 0 || a1 <= 0 || a2 <= 0)return false;
    return true;
  }
  else if (corners.size() == index && index == 5){
    if (nb3 == 0 && nb5 == 1)return false;
    int n0 = corners[1]-corners[0];
    int n1 = corners[2]-corners[1];
    int n2 = corners[3]-corners[2];
    int n3 = corners[4]-corners[3];
    int n4 = bnd.size() - n0 - n1 - n2 - n3;
    int a0 = (n0-n1-n2+n3+n4)/2;
    int a1 = (n0+n1+n2-n3-n4)/2;
    int a2 = (n0+n1-n2-n3+n4)/2;
    int a3 = (-n0+n1+n2+n3-n4)/2;
    int a4 = (-n0-n1+n2+n3+n4)/2;

    if (a0 <=0 || a1 <=0 || a2 <=0 || a3 <=0 || a4 <=0){
      if (debug_){
	Msg::Info("  Non meshable blob with 5 corners found %d %d %d %d %d",n0,n1,n2,n3,n4);
	Msg::Info("                                         %d %d %d %d %d",a0,a1,a2,a3,a4);
      }
      return false;
    }
    return true;
  }
  else if (corners.size() == index && index == 4){
    int n0 = corners[1]-corners[0];
    int n1 = corners[2]-corners[1];
    int n2 = corners[3]-corners[2];
    int n3 = bnd.size() - n0 - n1 - n2;
    //      if (debug_){
	//      }


    if ((n0 == n1 && abs(n2-n3) == 2) ||
	(n1 == n3 && abs(n0-n2) == 2)){
      if (nb5 == nb3 && nb3 != 1)
	return true;
      return false;
    }

    if ((n0 == n2 && abs(n1-n3) == 2) ||
	(n1 == n3 && abs(n0-n2) == 2)){
      if (nb5 == nb3 && nb3 != 1)
	return true;
      return false;
    }
    if (n0 == n2 && n1 == n3 && (n0 != 2 || n1 != 2)){
      if (nb5 == 0 || nb3 == 0)return false;
      return true;
    }
    Msg::Info("  Currently non meshable Blob with 4 corners found %d %d %d %d with %d %d",n0,n1,n2,n3,nb3,nb5);
    PRINTBLOB (BLOB++,cavity, bnd);
  }
  return false;
}

static bool removeConcaveCorners (v2t_cont &adj,
				  std::vector<MVertex*> &bnd,
				  std::set<MElement*> & cavity){
  int iter = 0;
  while (1){
    std::vector<MElement*> toAdd;
    for (size_t i=0;i<bnd.size();i++){
      if (bnd[i]->onWhat()->dim() == 2){
	v2t_cont::iterator it = adj.find(bnd[i]);
	int n_outside = 0;
	MElement *e = NULL;
	for (size_t j=0;j<it->second.size();j++){
	  if (cavity.find(it->second[j]) == cavity.end()){
	    e = it->second[j];
	    n_outside ++;
	  }
	}
	if (n_outside == 1){
	  toAdd.push_back(e);
	}
      }
    }
    if (toAdd.empty())break;
    cavity.insert(toAdd.begin(), toAdd.end());
    bnd = buildBoundary (cavity.begin(),cavity.end());
    if (iter++ > 5) return false;
  }
  for (size_t i=0;i<bnd.size();i++){
    if (bnd[i]->onWhat()->dim() == 2){
      v2t_cont::iterator it = adj.find(bnd[i]);
      if (it->second.size() != 4)return false;
    }
  }
  return true;
}
				  

static bool buildCavity (GFace *gf,
			 MVertex *v, v2t_cont &adj,
			 std::map<MVertex *, int, MVertexPtrLessThan> &sing,
			 std::vector<MVertex*> &bnd,
			 std::set<MElement*> & cavity,
			 int &index,
			 MVertex **theSing,
			 int MAXITER,
			 bool singOnly = true) {

  bnd.clear();
  cavity.clear();
  v2t_cont::iterator it = adj.find(v);

  cavity.insert(it->second.begin(), it->second.end());

  bool debug_ = (v->getNum() == 1979);
  FILE *deb = NULL;
  if (debug_){
    Msg::Info("NODE %lu %lu",v->getNum(),cavity.size());
    deb = fopen ("debugMaxCavity.pos", "w");
    fprintf(deb,"View \"\"{\n");
  }

  

  int iter = 0;
  bnd = buildBoundary (cavity.begin(),cavity.end());
  while (iter++ < MAXITER){    
    if (bnd.empty())break;
    bool allCornersGood = true;

    for (size_t	i=0; i<bnd.size();i++){
      v2t_cont::iterator itvv = adj.find(bnd[i]);
      if ((bnd[i]->onWhat()->dim() == 2 && itvv->second.size() != 4) ||
	  (bnd[i]->onWhat()->dim() == 1 && itvv->second.size() != 2) ||
	  (bnd[i]->onWhat()->dim() == 0 && itvv->second.size() != 1)) allCornersGood = false;
    }
    allCornersGood = removeConcaveCorners (adj,bnd,cavity);
    if (bnd.empty()){
      return false;
    }

    if (debug_){
      Msg::Info("CAVITY %lu BND %lu ",cavity.size(),bnd.size());
      for (size_t i=0;i<bnd.size();i++){
	fprintf(deb,"SL(%g,%g,%g,%g,%g,%g){%lu,%lu};\n",
		bnd[i]->x(),bnd[i]->y(),bnd[i]->z(),	  
		bnd[(i+1)%bnd.size()]->x(),bnd[(i+1)%bnd.size()]->y(),bnd[(i+1)%bnd.size()]->z(),bnd.size(),bnd.size());
      }
    }

    
    int ns;
    *theSing = countSing (cavity, sing, ns);    
    index = *theSing ? sing [*theSing] : 4 ;
    //    printf(" --> iter %d bnd size %lu %d %d %d\n",iter,bnd.size(),allCornersGood,ns,index);
    if (ns == 0  && singOnly)index = -1;
    if (allCornersGood && index >= 0){
      //      printf(" --> checking\n");
      if (cavityMeshable(gf,adj,bnd,cavity, index, debug_)){
	//	printf("the cavity is meshable %lu\n",cavity.size());
	return true;
      }
    }
    std::set<MElement*> update_cavity;
    for (size_t	i=0; i<bnd.size();i++){
      v2t_cont::iterator itvv = adj.find(bnd[i]);
      //      cavity.insert(itvv->second.begin(), itvv->second.end());
      update_cavity.insert(itvv->second.begin(), itvv->second.end());
    }
    size_t sb = bnd.size();
    updateBoundary (update_cavity, cavity, bnd);    
    if (sb == bnd.size()){
      return false;
    }
	//bnd = buildBoundary (cavity.begin(),cavity.end());
  }

  if (debug_){
    fprintf(deb,"};\n");
    fclose(deb);
  }
  
  
  return false;
}

//static void printVector (const std::vector<MVertex*> &v){
//  for (size_t i=0;i<v.size();i++)printf("%lu ",v[i]->getNum());
//  printf("\n");
//}


// Create a maximal size cavity around a singularity
struct incidentSingularity {
  MVertex *sing;
  MElement *notInlayer;
  incidentSingularity (MVertex *s, MElement *n) : sing (s) , notInlayer(n) {}
};

// get the real maximal cavity-------------------------------------------------------------

static void computeCorners ( std::vector<MVertex*> &bnd ,
		      std::set<MElement*> &cavity,				
		      v2t_cont &adj,
		      std::vector<int>  &corners,
		      std::vector<std::vector<MElement*> >  &bnd_adj) {  
  for (size_t i=0;i<bnd.size();i++){
    v2t_cont::iterator it = adj.find(bnd[i]);
    bnd_adj.push_back(it->second);
    int inside = 0;
    for (size_t j=0;j<it->second.size();j++){
      if (cavity.find(it->second[j]) != cavity.end()) inside ++;
    }
    if (inside != 2){
      corners.push_back(i);
    }
  }
}

static void buildMaximalCavity (GFace *gf,
				MVertex *sing,
				std::vector<MVertex*> &_bnd,
				std::set<MElement*> &_cavity,
				v2t_cont &adj,
				std::map<MVertex*,int, MVertexPtrLessThan> &newSings){    
  v2t_cont::iterator it = adj.find(sing);
  int valence = newSings[sing];
  if (it == adj.end())return;
  if (it->second.empty())return;
  std::vector<MVertex*> bnd, copy;
  std::set<MElement*> cavity;
  std::set<MElement*> maximal_cavity;
  cavity.insert(it->second.begin(), it->second.end());
  maximal_cavity = cavity;
  
  //  printf("FACE %lu computing maximal cavity for vertex %lu (%d) cs %lu\n",gf->tag(),sing->getNum(),valence,cavity.size());
  
  bool debug_ = (sing->getNum() == 9519);

  FILE *deb = NULL;
  if (debug_){
    Msg::Info("NODE %lu",sing->getNum());
    deb = fopen ("debugMaxCavity.pos", "w");
    fprintf(deb,"View \"\"{\n");
  }
  
  while (1){
    bnd = buildBoundary (cavity.begin(),cavity.end());
    if (bnd.empty())break;
    bnd.resize(bnd.size() - 1);
    /*bool allCornersGood = */removeConcaveCorners (adj,bnd,cavity);
    bnd = buildBoundary (cavity.begin(),cavity.end());
    bnd.resize(bnd.size() - 1);
    std::vector<int>  corners;
    std::vector<std::vector<MElement*> >  bnd_adj;
    computeCorners ( bnd , cavity, adj, corners, bnd_adj);  
    if (debug_){
      Msg::Info("CAVITY %lu BND %lu COR %lu",cavity.size(),bnd.size(), corners.size());
      for (size_t i=0;i<bnd.size();i++){
	fprintf(deb,"SL(%g,%g,%g,%g,%g,%g){%lu,%lu};\n",
		bnd[i]->x(),bnd[i]->y(),bnd[i]->z(),	  
		bnd[(i+1)%bnd.size()]->x(),bnd[(i+1)%bnd.size()]->y(),bnd[(i+1)%bnd.size()]->z(),bnd.size(),bnd.size());
      }
    }


    
    if (corners.size() > valence+3)break;
    if (valence == corners.size()){
      maximal_cavity = cavity;
      copy = bnd;
      copy.push_back(bnd[0]);
      if(cavityMeshable (gf,adj,copy,cavity,valence, debug_))break;
    }
    _cavity = cavity;
    _bnd = bnd;
    //    printf("corners are nows %lu\n",corners.size());
    size_t cavSize = cavity.size();

    std::vector<int> extend = corners;
    for (size_t i=0;i<corners.size();i++){
      extend[i] = 1;
    }
    for (size_t i=0;i<corners.size();i++){
      int n;
      if (i == corners.size()-1) n  = bnd.size() + corners[0] - corners[i] ;
      else n = corners[i+1]-corners[i];
      for (size_t j=0; j<=n; j++){
	int index = (corners[i]+j)%bnd.size();
	MVertex *v = bnd [index];
	if (newSings.find(v) != newSings.end() ||
	    newSings.find(v) != newSings.end()){      
	  extend[i] = 0;
	  extend[(i+1)%corners.size()] = 0;
	}
      }      
    }
    
    for (size_t i=0;i<corners.size();i++){
      int n;
      if (i == corners.size()-1) n  = bnd.size() + corners[0] - corners[i] ;
      else n = corners[i+1]-corners[i];
      std::set<MElement*> toExtend;
      if (extend[i] || extend[(i+1)%corners.size()]){      
	int start = extend[i] ? 0:1;
	int end   = extend[(i+1)%corners.size()] ? n+1:n;
	for (size_t j=start; j<end ; j++){
	  int index = (corners[i]+j)%bnd.size();
	  std::vector<MElement*> &e = bnd_adj[index];
	  toExtend.insert(e.begin(),e.end());
	}
	cavity.insert(toExtend.begin(), toExtend.end());
      }
    }
    if (cavity.size() <= cavSize)break;
  }
  
  if (debug_){
    fprintf(deb,"};\n");
    fclose(deb);
  }


  _cavity = maximal_cavity;
  _bnd = buildBoundary (_cavity.begin(),_cavity.end());
  _bnd.resize(_bnd.size() - 1);

  //  printf("FINALLY boundary size %lu cavity %lu\n",_bnd.size(), _cavity.size());

}



int remeshMaximalCavities (GFace *gf, std::map<MVertex*,int, MVertexPtrLessThan> &newSings, bool singOnly){
  v2t_cont adj;
  buildVertexToElement(gf->quadrangles, adj);
  std::string aaa = gf->model()->getName() + "maximal_cavities_face_" + std::to_string(gf->tag()) + ".pos";
  FILE *f = fopen (aaa.c_str(),"w");
  fprintf(f,"View \"\"{\n");
  int counter  = 0;
  std::map<MVertex*,int, MVertexPtrLessThan> newSings_ = newSings;

  v2t_cont::iterator it = adj.begin();
  while(it != adj.end()) {
    MVertex *v = it->first;
    int sing_index; 
    std::map<MVertex*,int, MVertexPtrLessThan>::iterator it2 = newSings.find(it->first);
    
    if (it2 == newSings.end())sing_index = 4;
    else sing_index = it2->second;

    const std::vector<MElement *> e = it->second;
    if (v->onWhat() == gf && /*e.size()*/ sing_index != 4 && e.size() != 0) {
      //  for (std::map<MVertex*,int, MVertexPtrLessThan>::iterator it = newSings.begin() ; it != newSings.end(); ++it){
      std::vector<MVertex*> bnd;
      std::set<MElement*> cavity;

      buildMaximalCavity (gf,it->first,bnd,cavity,adj,newSings_);
      
      if (bnd.empty()){
	if (it->first->onWhat() == gf)newSings_.erase(it->first);
      }
      else {
	fprintf(f,"SP(%g,%g,%g){%lu};\n",it->first->x(),
		it->first->y(),it->first->z(), it->first->getNum());
	for (size_t i=0;i<bnd.size();i++){
	  fprintf(f,"SL(%g,%g,%g,%g,%g,%g){%lu,%lu};\n",
		  bnd[i]->x(),bnd[i]->y(),bnd[i]->z(),	  
		  bnd[(i+1)%bnd.size()]->x(),bnd[(i+1)%bnd.size()]->y(),bnd[(i+1)%bnd.size()]->z(),it->first->getNum(), it->first->getNum());
	}
	//	printf("MAX CAVITY for %lu (%lu)\n",v->getNum(),it->second.size());
	if (remeshCavity (gf, sing_index, cavity, bnd, adj, newSings_) == 1){	
	  //	printf("cavity %lu remeshed\n",it->first->getNum());
	  counter++;
	}
      }
    }
    ++it;
  }
  newSings = newSings_;

  fprintf(f,"};\n");
  fclose(f);
  return counter;
}

void computeSingularitesThroughCrossField (GFace * gf,  v2t_cont &adj, Field *f, 
					   std::map<MVertex *, int, MVertexPtrLessThan> &sing){  
  
  std::map<MVertex*, double, MVertexPtrLessThan> sizes;
  v2t_cont::iterator it = adj.begin();
  while(it != adj.end()) {
    MVertex *v = it->first;
    SVector3 t1;
    (*f)(v->x(), v->y(), v->z(), t1, gf);
    sizes[v] = t1.norm();
    ++it;
  }
  
  it = adj.begin();
  while(it != adj.end()) {
    MVertex *v = it->first;
    const std::vector<MElement *> &e = it->second;
    if (v->onWhat() == gf && e.size() != 0) {
      double L = sizes[v];
      std::set<MElement*> ee; ee.insert(e.begin(),e.end());
      std::vector<MVertex*> b = buildBoundary (ee.begin(),ee.end());
      bool MIN = true, MAX = true;
      for (size_t i = 0 ; i < b.size(); i++){
	double li = sizes[b[i]];
	if (li > L) MAX = false;
	if (li < L) MIN = false;	
      }
      if (MIN && !MAX)sing[v] = 3;
      if (MAX && !MIN)sing[v] = 5;
    }
    ++it;
  }
}



// -------------------------------------------
static void bunin (GFace * gf,
		   std::map<MVertex *, int, MVertexPtrLessThan> &sing,
		   std::map<MVertex*,int, MVertexPtrLessThan> &newSings,
		   Field *cross_field,
		   int niter,
		   bool singOnly) {

  if (singOnly && sing.empty())return;
  
  std::string aaa = gf->model()->getName() + "cavities_face_" + std::to_string(gf->tag()) + "_" + std::to_string(niter) +".pos"; 
  FILE *f = fopen (aaa.c_str(),"w");
  fprintf(f,"View \"\"{\n");
  v2t_cont adj;
  buildVertexToElement(gf->quadrangles, adj);

  //  if (sing.empty()) computeSingularitesThroughCrossField (gf,  adj, cross_field, sing);
  //  if(Msg::GetVerbosity() == 99)
    //    if (singOnly)gf->model()->writeMSH("before6.msh", 4.0, false, true);	  	  
  removeValence6Nodes(gf, adj);
  //    meshWinslow2d (gf, 10000, cross_field);
  //  if(Msg::GetVerbosity() == 99)
  //    if (singOnly)gf->model()->writeMSH("after6.msh", 4.0, false, true);	  	  
  
  //  exit(1);
  
  std::vector<MVertex*> sing_;
  for (std::map<MVertex *, int, MVertexPtrLessThan>::iterator it =  sing.begin() ; it != sing.end(); ++it)  
    sing_.push_back(it->first);

  int nbSing = 0;
  v2t_cont::iterator it = adj.begin();

  while(it != adj.end()) {
     MVertex *v = it->first;
     const std::vector<MElement *> e = it->second;
     if (v->onWhat() == gf && e.size() != 4 && e.size() != 0) {
       nbSing ++;
       sing_.push_back(v);
       //       for (size_t i=0;i< e.size() ; i++){
       //	 for (size_t j=0;j< e[i]->getNumVertices() ; j++){
       //	   MVertex *vv = e[i]->getVertex(j);
       //	   if (std::find(sing_.begin(), sing_.end(), vv)==sing_.end())sing_.push_back(vv);
       //	 }
       //       }
     }
     ++it;
  }

  Msg::Info("Non local topological repair on surface %lu (iter %3d) --> found %d internal mesh sngs vs. %lu true sngs",gf->tag(),niter,nbSing,
	    sing.size());
  
  int count = 0;
  for ( std::vector<MVertex*>::iterator itv = sing_.begin() ; itv != sing_.end() ; ++itv) {
    MVertex *v = *itv;
    //    printf("treating %lu\n",v->getNum());
    it = adj.find(v);
    
    if (it != adj.end() && v->onWhat() == gf && it->second.size() != 0) {
      // compute a cavity that contains ONE "true" singularity
      // but possibly more "mesh singularities" --> just keep one at
      // thet end and replace the singularity by the remaining mesh
      // one
      std::vector<MVertex*> bnd;
      std::set<MElement*> cavity ;
      int index;
      MVertex *theSing = NULL;
      bool success = buildCavity (gf,v,adj, sing,
				  bnd, cavity, index,&theSing, niter, singOnly);
      if (success){	
	// periodic
	bnd.resize(bnd.size() -1);
	bool ok = remeshCavity (gf, index, cavity, bnd, adj, newSings) == 1;
	for (size_t i=0;i<bnd.size();i++){
	  fprintf(f,"SL(%g,%g,%g,%g,%g,%g){%d,%d};\n",
		  bnd[i]->x(),bnd[i]->y(),bnd[i]->z(),	  
		  bnd[i+1]->x(),bnd[(i+1)%bnd.size()]->y(),bnd[(i+1)%bnd.size()]->z(),ok,ok);
	}
	if (ok){	
	  if (success && theSing){
	    sing.erase(theSing);
	  }
	  //	if (success){
	  //	  int tag = success ? count:-count;
	  std::string aaa = gf->model()->getName() + "cavities_face_" + std::to_string(gf->tag()) + "_" + std::to_string(singOnly) +"_" + std::to_string(count)+".msh"; 
	  //	  if(Msg::GetVerbosity() == 99)
	  //	    gf->model()->writeMSH(aaa, 4.0, false, true);	  	  
	}
	count ++;
      }
    }
  }
  {
    nbSing = 0;
    it = adj.begin();
    while(it != adj.end()) {
      MVertex *v = it->first;
      const std::vector<MElement *> e = it->second;
      if (v->onWhat() == gf && e.size() != 4 && e.size() != 0) {
	nbSing ++;
      }
      ++it;
    }    
  }
  Msg::Info("Non local topological repair done --> %d internal mesh sngs vs. %lu + %lu true sngs",nbSing,
	    sing.size(),newSings.size());
  sing.insert(newSings.begin(), newSings.end());
  newSings.clear();
  
  fprintf(f,"};\n");
  fclose(f);
}
// -------------------------------------------



void computeLayout (GFace * gf, FILE *f, std::map<MVertex*,int, MVertexPtrLessThan> &newSings) {

  v2t_cont adj;
  buildVertexToElement(gf->triangles, adj);
  buildVertexToElement(gf->quadrangles, adj);
  v2t_cont::iterator it = adj.begin();
  int numSep = 1;
  
  while(it != adj.end()) {
    MVertex *v = it->first;
    const std::vector<MElement *> e = it->second;
    if (v->onWhat() == gf && e.size() != 4 && newSings.find(v) != newSings.end()) {
      fprintf(f,"SP(%g,%g,%g){%d};\n",v->x(),v->y(),v->z(),newSings[v]);
      winslowStencil st (v, e, gf);
      std::vector<MVertex *> &crown = st.stencil;
      printVertex (e.size(), v,f);
      for (int i=1;i<crown.size();i+=2){
	MVertex *prec    = v;
	MVertex *current = crown[i];
	MVertex *next  = NULL;
	printEdge   (numSep, prec, current,f);
	while (1){
	  next = next_vertex (gf, prec, current, adj);
	  if (!next)break;
	  prec = current;
	  current = next;
	  printEdge (numSep, prec, current,f);
	}
	numSep++;
      }      
    }
    ++it;
  }
}


// static void findPhysicalGroupsForSingularities(GModel *gm,
//                                                std::map<MVertex *, int, MVertexPtrLessThan> &temp)
// {
 
//   std::map<int, std::vector<GEntity *> > groups[4];
//   gm->getPhysicalGroups(groups);
//   for(std::map<int, std::vector<GEntity *> >::iterator it = groups[0].begin();
//       it != groups[0].end(); ++it) {
//     std::string name = gm->getPhysicalName(0, it->first);
//     if(name == "SINGULARITY_OF_INDEX_THREE") {
//       for(size_t j = 0; j < it->second.size(); j++) {
// 	if(!it->second[j]->mesh_vertices.empty())
// 	  temp[it->second[j]->mesh_vertices[0]] = 3;
//       }
//     }
//     else if(name == "SINGULARITY_OF_INDEX_FIVE") {
//       for(size_t j = 0; j < it->second.size(); j++) {
// 	if(!it->second[j]->mesh_vertices.empty())
// 	  temp[it->second[j]->mesh_vertices[0]] = 5;
//       }
//     }
//   }
// }


bool  getSingularitiesFromFile (const std::string &fn,
				std::vector<GFace *> &temp,
				std::map<MVertex*,int, MVertexPtrLessThan> &newSings){
  
  std::vector<MVertex*> s_3;
  std::vector<MVertex*> s_5;
  for (size_t i=0;i<temp.size();++i){
    GFace *gf = temp[i];
    v2t_cont adj;
    buildVertexToElement(gf->quadrangles, adj);
    v2t_cont::iterator it = adj.begin();
    while (it !=adj.end()){
      if (it->first->onWhat() == gf && it->second.size() == 3)s_3.push_back(it->first);
      else if (it->first->onWhat() == gf && it->second.size() >= 5)s_5.push_back(it->first);
      ++it;
    }
  }

  FILE *f = fopen(fn.c_str(),"r");
  if (!f) {
    Msg::Error("file not found: %s", fn.c_str());
    return false;
  }
  std::string fn3 = fn+"_cross.pos";
  std::string fn2 = fn+".pos";
  FILE *ff = fopen (fn2.c_str(),"w");
  FILE *ff3 = fopen (fn3.c_str(),"w");
  fprintf(ff,"View\" sing from file\"{\n");
  fprintf(ff3,"View\" sing from file not moved\"{\n");

  int n;
  fscanf (f,"%d",&n);
  for (int i=0;i<n;i++){
    int dim, tag, index;
    double x,y,z;
    fscanf(f,"%d %lf %lf %lf %d %d",&index,&x,&y,&z,&dim,&tag);
    fprintf(ff3,"SP(%lf,%lf,%lf){%d};\n",x,y,z,index);
    MVertex s(x,y,z);
    double distMin = 1.e22;
    MVertex *found = NULL;
    if (index == -1) index = 5;
    else index = 3;
    std::vector<MVertex*> &s_ = index == 3 ? s_3 : s_5;
    for (size_t j=0;j<s_.size();j++){
      MVertex *v = s_[j];
      if (v->onWhat()->tag() == tag && newSings.find(v) == newSings.end()){
	double dist = s.distance(v);
	if (dist < distMin){
	  distMin = dist;
	  found = v;
	}
      }
    }    
    if (found){
      fprintf(ff,"SP(%g,%g,%g){%d};\n",found->x(),found->y(),found->z(),index);
      newSings[found] = index;
    }
  }
  fprintf(ff,"};\n");
  fclose(ff);
  fprintf(ff3,"};\n");
  fclose(ff3);
  fclose(f);
  return true;
}



void meshWinslow2d (GModel * gm, int nIter, Field *f) {

  std::vector<GFace *> temp;
  temp.insert(temp.begin(), gm->firstFace(), gm->lastFace());
  std::vector<GEdge *> tempe;
  tempe.insert(tempe.begin(), gm->firstEdge(), gm->lastEdge());

  for (size_t i=0;i<temp.size();i++){
    printf("face %lu --> %lu quads\n",temp[i]->tag(),temp[i]->quadrangles.size());
    allowProjections (temp[i]);
  }
  
  if (std::getenv("NO_SMOOTHING") != NULL) {
    Msg::Info("smoothing and simplification disabled in meshWinslow2d (env var NO_SMOOTHING)");
    return;
  }
  if (std::getenv("NO_SIMPLIFICATION") != NULL) {
    Msg::Info("simplification disabled in meshWinslow2d (env var NO_SIMPLIFICATION)");

    int sIter = nIter/4;
    sIter = 10;

    for (int NIT = 0;NIT<4;NIT++){  
      for (size_t i=0;i<tempe.size();i++)
        meshWinslow1d (tempe[i],sIter, f);    

#if defined(_OPENMP)
#pragma omp parallel for schedule(dynamic)
#endif
      for (size_t i=0;i<temp.size();i++){
        meshWinslow2d (temp[i],sIter, f, false);
      }
    }

    return;
  }
  
  std::map<MVertex*,int, MVertexPtrLessThan> newSings;
  std::map<MVertex *, int, MVertexPtrLessThan> s;
  bool singRead = getSingularitiesFromFile (gm->getName()+"_singularities.txt", temp, s);

  Field *cross_field = NULL;
  
  if (!singRead){
    FieldManager *fields = gm->getFields();
    if(fields->getBackgroundField() > 0) {        
      cross_field = fields->get(fields->getBackgroundField());
      if(cross_field->numComponents() != 3) {// we hae a true scaled cross fields !!
	Msg::Error ("Packing of Parallelograms require a scaled cross field");
	Msg::Error ("Do first gmsh yourmeshname.msh -crossfield to create yourmeshname_scaled_crossfield.pos");
	Msg::Error ("Then do yourmeshname.geo -bgm yourmeshname_scaled_crossfield.pos");
	return;
      }
    }
    else {
      Msg::Error ("Packing of Parallelograms require a scaled cross field");
      Msg::Error ("Do first gmsh yourmeshname.msh -crossfield to create yourmeshname_scaled_crossfield.pos");
      Msg::Error ("Then do yourmeshname.geo -bgm yourmeshname_scaled_crossfield.pos");
      return;
    }
  }
  

  // reclassify all seams on the face (all the rest is parameter space "free")
  if (1){
    for (size_t i=0;i<temp.size();i++){
      std::vector<GEdge*> ed = 	temp[i]->edges();
      for (size_t j=0;j<ed.size();j++){
	if (ed[j]->isSeam(temp[i]) && ed[j]->faces().size() == 1){
	  for (size_t k=0;k<ed[j]->lines.size();k++){
	    ed[j]->lines[k]->getVertex(0)->setEntity(temp[i]);
	    ed[j]->lines[k]->getVertex(1)->setEntity(temp[i]);
	  }
	}
      }
    }
  }

  
  Msg::Info ("Non local topological cleanup ");

  
  //  if(Msg::GetVerbosity() == 99)
    gm->writeMSH("before_bunin.msh", 4.0, false, true);
  
  {
    //    findPhysicalGroupsForSingularities(gm,s);
    for (size_t i=0;i<temp.size();i++){
      bunin (temp[i], s, newSings, cross_field, 5, true) ;
      bunin (temp[i], s, newSings, cross_field, 5, true) ;
      bunin (temp[i], s, newSings, cross_field, 15, true) ;
    }
#if 1
    if(Msg::GetVerbosity() == 99)
      gm->writeMSH("between_bunin.msh", 4.0, false, true);
    for (size_t i=0;i<temp.size();i++){
      bunin (temp[i], s, newSings, cross_field, 5, false) ;
      bunin (temp[i], s, newSings, cross_field, 6, false) ;
      bunin (temp[i], s, newSings, cross_field, 15, false) ;
    }
#endif
  }
  //  if(Msg::GetVerbosity() == 99)
    gm->writeMSH("after_bunin.msh", 4.0, false, true);

  Msg::Info ("Winslow Smoothing\n");

  
  for (int NIT = 0;NIT<2;NIT++){  
    for (size_t i=0;i<tempe.size();i++)
      meshWinslow1d (tempe[i],2, f);    
  }
  
#if defined(_OPENMP)
#pragma omp parallel for schedule(dynamic)
#endif
  for (size_t i=0;i<temp.size();i++){
    meshWinslow2d (temp[i],20, f, true);
  }
    
  s.clear();
  std::string sing_file = gm->getName()+"_singularities.txt";
  Msg::Info("load singularities from file: %s", sing_file.c_str());
  getSingularitiesFromFile (sing_file, temp, s);

  //  if(Msg::GetVerbosity() == 99)
    gm->writeMSH("after_smooth.msh", 4.0, false, true);
#if 1
  Msg::Info("Growing cavities for the %lu true isolated singularities found in the domain",s.size());
  for (size_t i=0;i<temp.size();i++){
    int ITER =0;
    while (1) {
      int C = remeshMaximalCavities (temp[i], s, true);
      //      break;
      if (!C || ITER++ > 20)break;
    }
  }
  
  //  if(Msg::GetVerbosity() == 99)
    gm->writeMSH("after_grow.msh", 4.0, false, true);  

  for (int NIT = 0;NIT<4;NIT++){  
    for (size_t i=0;i<tempe.size();i++)
      meshWinslow1d (tempe[i],nIter/4, f);    
  }

#if defined(_OPENMP)
#pragma omp parallel for schedule(dynamic)
#endif
  for (size_t i=0;i<temp.size();i++){
    meshWinslow2d (temp[i],100, f, true);
  }
    
  //  if(Msg::GetVerbosity() == 99)
    gm->writeMSH("after_merge.msh", 4.0, false, true);
#endif
  
  //  return;
  std::string name = gm->getName()+"_layout.pos";
  FILE *_f = fopen(name.c_str(),"w");
  fprintf(_f,"View\"Layout\"{\n");
  for (size_t i=0;i<temp.size();i++)
    computeLayout (temp[i], _f,s);
  fprintf(_f,"};\n");
  fclose(_f); 
}

