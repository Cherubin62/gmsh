
#include "Gmsh.h"
#include "Const.h"
#include "Mesh.h"
#include "2D_Mesh.h"

extern int           LocalNewPoint, FindQualityMethod;
extern PointRecord  *gPointArray;
extern Mesh         *THEM;

int Comparekey(void * d1,void  * d2){

  double val ;
  PointNumero a,b,c,aa,bb,cc;

  a = ((Delaunay*)d1)->t.a;
  b = ((Delaunay*)d1)->t.b;
  c = ((Delaunay*)d1)->t.c;
  aa = ((Delaunay*)d2)->t.a;
  bb = ((Delaunay*)d2)->t.b;
  cc = ((Delaunay*)d2)->t.c;

  val = ((Delaunay*)d2)->t.quality_value - ((Delaunay*)d1)->t.quality_value;

  if ((aa==a)&&(bb==b)&&(cc==c)){
    return 0;
  }
  else if (val  > 1.e-21 )
    return 1;
  else if (val  < -1.e-21 )
    return -1;
  else{ 
    if (((Delaunay*)d1)->t.xc > ((Delaunay*)d2)->t.xc)
      return -1;
    else 
      return 1;    
  }
}

int Insert_Triangle (avlstruct **root, Delaunay * del){

  if ( !avltree_insert(root,del,Comparekey) ) return(0);

  return(1);
}

int Delete_Triangle ( avlstruct **root, Delaunay * del ){

  if (!avltree_delete(root,del,Comparekey)) return(0);  

  if (*root == NULL) return(0);
  return(1);
}

int Insert_Point (MPoint pt, int *numpoints, int *numalloc, 
		  DocRecord *doc, DocRecord *BGM, int is3d){
  Vertex *v,*dum;

  if ( *numpoints >= *numalloc ) {
    gPointArray = (PointRecord *) Realloc(gPointArray, 
					  (*numalloc + 1000) * sizeof(PointRecord));
    *numalloc += 1000;
    doc->points = gPointArray;
  }
  PushgPointArray(gPointArray);
  gPointArray[*numpoints].where.h = pt.h;
  gPointArray[*numpoints].where.v = pt.v;
  gPointArray[*numpoints].numcontour = -1;
  gPointArray[*numpoints].initial = -1;
  if(!is3d)
    gPointArray[*numpoints].quality = find_quality(pt,BGM);
  else{
    v = Create_Vertex (-1,pt.h,pt.v,0.0,0.0,0.0);
    Calcule_Z_Plan(&v, &dum);
    Projette_Inverse(&v, &dum);
    gPointArray[*numpoints].quality = Lc_XYZ ( v->Pos.X,v->Pos.Y,v->Pos.Z,THEM);
    Free(v);
  }
    
  (*numpoints)++;

  return 1;
}

void findtree(avlptr root, double *qualm, Delaunay **delf, DocRecord *MESH){

  /* 
     trouve le triangle possedant le facteur de qualite max 
     modif : le centre du cercle circonscrit de ce triangle
     doit etre dans le domaine   
  */

  MPoint pt;
  double q;
  Delaunay *del;

  if(root != NULL){
    findtree((root)->left,qualm,delf,MESH);
    del = (Delaunay*)root->treedata;
    q = del->t.quality_value;
    pt.h = del->t.xc;
    pt.v = del->t.yc;
    if ((q>*qualm) && (Find_Triangle (pt ,MESH,A_TOUT_PRIX) != NULL) ){
      *qualm = q;
      *delf = del;
    }
    findtree((root)->right,qualm,delf,MESH);
  }
}


Delaunay *findrightest(avlptr root, DocRecord *MESH){

  Delaunay *del,**dee;
  MPoint pt;
  avlptr exroot;
  double qualm;

  exroot = root;

  while((exroot)->left != NULL){
    exroot = (exroot)->left;
  }

  del = (Delaunay*)(exroot)->treedata;
  pt.h = del->t.xc;
  pt.v = del->t.yc;
  if( (LocalNewPoint == VORONOI_INSERT) ||(LocalNewPoint == SQUARE_TRI) ) 
    return del;

  if(Find_Triangle(pt,MESH,A_TOUT_PRIX) != NULL )return del;
  
  exroot = root;
  del = (Delaunay*)(root)->treedata;
  qualm = del->t.quality_value;
  dee = &del;
  findtree(exroot, &qualm, dee, MESH);
  del = *dee;
 
  return (del);    
}

double lengthseg (MPoint a, MPoint b){
  return (pow(DSQR(a.h-b.h)+DSQR(a.v-b.v),0.5));
}


MPoint Localize (Delaunay * del , DocRecord *MESH) {

  /*
    Routine de localisation du point a inserer.
    Variable globale LocalNewPoint :
      - CENTER_CIRCCIRC : au centre du cercle circonscrit
      - VORONOI_INSERT  : sur une branche de voronoi
      - BARYCENTER      : au centre de gravite 
      - SQUARE_TRI      : essaie de creer des triangles rectangles isoceles
                          dans le but de mailler avec des quadrangles
  */

  MPoint       pt,pta,ptb,ptc,ptm;
  PointNumero  a,b;
  double       p,q,val,vec[2],ro,rm;
  Delaunay    *v1,*v2,*v3,*del2 ;

  switch (LocalNewPoint) {

  case (CENTER_CIRCCIRC) :

    pt.h = del->t.xc;
    pt.v = del->t.yc;

    return(pt);

  case (BARYCENTER) :

    pt.h = ( gPointArray[del->t.a].where.h + gPointArray[del->t.b].where.h 
	    + gPointArray[del->t.c].where.h ) /3.;
    pt.v = ( gPointArray[del->t.a].where.v + gPointArray[del->t.b].where.v 
	    + gPointArray[del->t.c].where.v ) /3.;

    return(pt);

  case (VORONOI_INSERT) :
  case (SQUARE_TRI) :

    /* 
       si le triangle est pres d'un bord -> ce bord est l'arete choisie
    */
    if ((v1 = del->v.voisin1) == NULL) {      
      /* v1 == NULL; */
      v2 = del->v.voisin2;
      v3 = del->v.voisin3;
    }
    else if ((v2 = del->v.voisin2) == NULL) {      
      v1 = NULL;
      v2 = del->v.voisin1;
      v3 = del->v.voisin3;
    }
    else if ((v3 = del->v.voisin3) == NULL) {      
      v1 = NULL;
      v2 = del->v.voisin1;
      v3 = del->v.voisin2;
    }
    else {
      v1 = del->v.voisin1;
      v2 = del->v.voisin2;
      v3 = del->v.voisin3;
    }

    /* 
       Si l'arete est un bord -> 
    */   
    if (v1 == NULL){
      
      if((v2 != NULL) && (v3 != NULL) ) {
	
	if ( ((del->t.a == v2->t.a) || (del->t.a == v2->t.b) || (del->t.a == v2->t.c)) &&
	     ((del->t.a == v3->t.a) || (del->t.a == v3->t.b) || (del->t.a == v3->t.c))){
	  a = del->t.b;
	  b = del->t.c;
	}
	else if ( ((del->t.b == v2->t.a) || (del->t.b == v2->t.b) || (del->t.b == v2->t.c)) &&
		  ((del->t.b == v3->t.a) || (del->t.b == v3->t.b) || (del->t.b == v3->t.c))){  
	  a = del->t.a;
	  b = del->t.c;
	}
	else if ( ((del->t.c == v2->t.a) || (del->t.c == v2->t.b) || (del->t.c == v2->t.c)) &&
		  ((del->t.c == v3->t.a) || (del->t.c == v3->t.b) || (del->t.c == v3->t.c))){  
	  a = del->t.a;
	  b = del->t.b;
	}
	else{
	  Msg(ERROR, "vor insert 1"); 
	}
      }      
      else if(v2 != NULL) {	
	if((del->t.a != v2->t.c) && (del->t.a != v2->t.c) && (del->t.a != v2->t.c)){	  
	  a = del->t.a;
	  b = del->t.b;
	}
	else if((del->t.b != v2->t.c) && (del->t.b != v2->t.c) && (del->t.b != v2->t.c)){   
	  a = del->t.b;
	  b = del->t.c;
	}
	else if((del->t.c != v2->t.c) && (del->t.c != v2->t.c) && (del->t.c != v2->t.c)){   
	  a = del->t.a;
	  b = del->t.c;
	}
	else {
	  Msg(ERROR,"vor insert 2"); 
	}
      }      
      else if(v3 != NULL) {	
	if((del->t.a != v3->t.c) && (del->t.a != v3->t.c) && (del->t.a != v3->t.c)){ 
	  a = del->t.a;
	  b = del->t.b;
	}
	else if((del->t.b != v3->t.c) && (del->t.b != v3->t.c) && (del->t.b != v3->t.c)){   
	  a = del->t.b;
	  b = del->t.c;
	}
	else if((del->t.c != v3->t.c) && (del->t.c != v3->t.c) && (del->t.c != v3->t.c)){  
	  a = del->t.a;
	  b = del->t.c;
	}
	else {
	  Msg(ERROR, "vor insert 3"); 
	}
      }
    }    
    else {
      if( v1->t.position == ACCEPTED )del2 = v1;
      else if( v2->t.position == ACCEPTED )del2 = v2;
      else if( v3->t.position == ACCEPTED )del2 = v3;
      else {
	Msg(ERROR,"coherence"); 
      }
 
      if((del->t.a != del2->t.a) && (del->t.a != del2->t.b) && (del->t.a != del2->t.c)){
	a = del->t.b;
	b = del->t.c;
      }
      else if((del->t.b != del2->t.a) && (del->t.b != del2->t.b) && (del->t.b != del2->t.c)){
	a = del->t.a;
	b = del->t.c;
      }
      else if((del->t.c != del2->t.a) && (del->t.c != del2->t.b) && (del->t.c != del2->t.c)){
	a = del->t.a;
	b = del->t.b;
      }
      else{
	Msg(ERROR,"vor insert"); 
      }
    }

    /* 
       On sait que l'arete du nouveau triangle est a b 
    */

    pta.h = gPointArray[a].where.h;
    ptb.h = gPointArray[b].where.h;
    pta.v = gPointArray[a].where.v;
    ptb.v = gPointArray[b].where.v;

    /*
    pte.h = gPointArray[c].where.h;
    pte.v = gPointArray[c].where.v;
    */

    p = 0.5 * lengthseg(pta,ptb);

    ptc.h = del->t.xc;
    ptc.v = del->t.yc;
      
    ptm.h = 0.5*( pta.h + ptb.h );
    ptm.v = 0.5*( pta.v + ptb.v );
    
    q = lengthseg(ptm,ptc);

    vec[0] = (ptc.h - ptm.h)/q; 
    vec[1] = (ptc.v - ptm.v)/q;

    val = (p*p + q*q) / (2.*q); 
    
    ro = find_quality(ptm,MESH)/RacineDeTrois;
    
    rm = ((ro  > q )? ro : ro  );      
    rm = ((rm < val)? rm : val);

    // WARNING RANDOM
    
    pt.h = ptm.h + vec[0] * (rm + pow( rm*rm - p * p,0.5 )) ;
    //+ (double) (rand() % 1000) / 1.e8;
    pt.v = ptm.v + vec[1] * (rm + pow( rm*rm - p * p,0.5 )) ;
    //+ (double) (rand() % 1000) / 1.e8;

    return(pt);
  }

  return pt;

}
  
/********************************************************************/

void alloue_Mai_Pts(maillage *mai , int Nballoc , int incrAlloc){
  int i;

  mai->points = (PointRecord *)Malloc(Nballoc*sizeof(PointRecord));
  for(i=0;i<Nballoc;i++){
    mai->points[i].where.h=0.0;
    mai->points[i].where.v=0.0;
  }
  mai->IncrAllocPoints = incrAlloc;
  mai->NumAllocPoints = Nballoc;
}

void alloue_Mai_Del(maillage *mai , int Nballoc , int incrAlloc){
  mai->listdel = (delpeek *)Malloc(Nballoc * sizeof(delpeek));
  mai->IncrAllocTri = incrAlloc;
  mai->NumAllocTri = Nballoc;
}

