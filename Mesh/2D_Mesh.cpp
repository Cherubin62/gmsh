/*
   Maillage Delaunay d'une surface (Point insertion Technique)

   3 types de maillages sont proposes
   - Center of circum circle point insertion
   - Voronoi Point Insertion (la meilleure en general)
   - G Point insertion (intermediaire)

   Le maillage surfacique passe par la determination d'un plan
   dont on minimise l'ecart au sens des moindes carres par rap-
   port a la surface :
     plan ax + bx + cz = 1
     tangeante = t = (a,b,c)
*/

#include "Gmsh.h"
#include "Const.h"
#include "Geo.h"
#include "CAD.h"
#include "Mesh.h"
#include "Create.h"
#include "2D_Mesh.h"
#include "Numeric.h"
#include "Context.h"

extern Mesh       *THEM;
extern Context_T   CTX;
extern int         CurrentNodeNumber, LocalNewPoint;
extern double      LC, FACTEUR_MULTIPLICATIF;

PointRecord   *gPointArray;
DocRecord     *BGMESH, *FGMESH;
double         qual, newqual, L;
int            is_3D = 0, UseBGMesh;

static Surface  *THESURFACE, *THESUPPORT;
static int       DEBUG = 0;

void ProjetteSurface (void *a, void *b){
  Vertex *v;
  v = *(Vertex **) a;

  if (!v->ListCurves)
    ProjectPointOnSurface (THESUPPORT, *v);
}

void Projette_Plan_Moyen (void *a, void *b){
  Vertex *v = *(Vertex **) a;
  Projette (v, THESURFACE->plan);
}

void Projette_Inverse (void *a, void *b){
  Vertex *v = *(Vertex **) a;
  Projette (v, THESURFACE->invplan);
}

void Plan_Moyen (void *data, void *dum){
  int ix, iy, iz, i, j, N;
  static List_T *points;
  Curve *pC;
  static int deb = 1;
  double det, sys[3][3], b[3], res[3], mod, t1[3], t2[3], ex[3];
  double s2s[2][2], r2[2], X, Y, Z;
  Vertex *v;
  Surface **pS, *s;

  pS = (Surface **) data;
  s = *pS;

  if (deb){
    points = List_Create (10, 10, sizeof (Vertex *));
    deb = 0;
  }

  switch (s->Typ){
  case MSH_SURF_PLAN:
  case MSH_SURF_TRIC:
  case MSH_SURF_REGL:
  case MSH_SURF_NURBS:
  case MSH_SURF_TRIMMED:
    for (i = 0; i < List_Nbr (s->s.Generatrices); i++){
      List_Read (s->s.Generatrices, i, &pC);
      for (j = 0; j < List_Nbr (pC->Vertices); j++){
	List_Read (pC->Vertices, j, &v);
	List_Add (points, &v);
	Tree_Insert (s->Vertices, List_Pointer (pC->Vertices, j));
      }
    }
    break;
  }

  N = List_Nbr (points);

  for (i = 0; i < 3; i++){
    b[i] = 0.0;
    for (j = 0; j < 3; j++){
      sys[i][j] = 0.0;
    }
  }

  /* ax + by + cz = 1 */

  ix = iy = iz = 0;

  for (i = 0; i < N; i++){
    List_Read (points, i, &v);

    if (!i){
      X = v->Pos.X;
      Y = v->Pos.Y;
      Z = v->Pos.Z;
    }
    else{
      if (X != v->Pos.X)
	ix = 1;
      if (Y != v->Pos.Y)
	iy = 1;
      if (Z != v->Pos.Z)
	iz = 1;
    }
    
    sys[0][0] += v->Pos.X * v->Pos.X;
    sys[1][1] += v->Pos.Y * v->Pos.Y;
    sys[2][2] += v->Pos.Z * v->Pos.Z;
    sys[0][1] += v->Pos.X * v->Pos.Y;
    sys[0][2] += v->Pos.X * v->Pos.Z;
    sys[1][2] += v->Pos.Y * v->Pos.Z;
    sys[2][1] = sys[1][2];
    sys[1][0] = sys[0][1];
    sys[2][0] = sys[0][2];
    b[0] += v->Pos.X;
    b[1] += v->Pos.Y;
    b[2] += v->Pos.Z;
  }

  s->d = 1.0;

  /* x = X */

  if (!ix){
    s->d = X;
    res[0] = 1.;
    res[1] = res[2] = 0.0;
    if (DEBUG)
      Msg(INFO, "Plan de type x = c");
  }

  /* y = Y */

  else if (!iy){
    s->d = Y;
    res[1] = 1.;
    res[0] = res[2] = 0.0;
    if (DEBUG)
      Msg(INFO, "Plan de type y = c");
  }

  /* z = Z */

  else if (!iz){
    s->d = Z;
    res[2] = 1.;
    res[1] = res[0] = 0.0;
  }

  /* by + cz = -x */

  else if (!sys3x3 (sys, b, res, &det)){
    s->d = 0.0;
    s2s[0][0] = sys[1][1];
    s2s[0][1] = sys[1][2];
    s2s[1][0] = sys[1][2];
    s2s[1][1] = sys[2][2];
    b[0] = -sys[0][1];
    b[1] = -sys[0][2];
    if (sys2x2 (s2s, b, r2)){
      res[0] = 1.;
      res[1] = r2[0];
      res[2] = r2[1];
      if (DEBUG)
	Msg(INFO, "Plan de type by + cz = -x");
    }

    /* ax + cz = -y */
    
    else{
      s->d = 0.0;
      s2s[0][0] = sys[0][0];
      s2s[0][1] = sys[0][2];
      s2s[1][0] = sys[0][2];
      s2s[1][1] = sys[2][2];
      b[0] = -sys[0][1];
      b[1] = -sys[1][2];
      if (sys2x2 (s2s, b, r2)){
	res[0] = r2[0];
	res[1] = 1.;
	res[2] = r2[1];
	if (DEBUG)
	  Msg(INFO, "Plan de type ax + cz = -y");
      }
      
      /* ax + by = -z */
      
      else{
	s->d = 1.0;
	s2s[0][0] = sys[0][0];
	s2s[0][1] = sys[0][1];
	s2s[1][0] = sys[0][1];
	s2s[1][1] = sys[1][1];
	b[0] = -sys[0][2];
	b[1] = -sys[1][2];
	if (sys2x2 (s2s, b, r2)){
	  res[0] = r2[0];
	  res[1] = r2[1];
	  res[2] = 1.;
	  if (DEBUG)
	    Msg(INFO, "Plan de type ax + by = -z");
	}
	else{
	  Msg(ERROR, "Mean Plane");
	}
      }
    }
  }

  s->a = res[0];
  s->b = res[1];
  s->c = res[2];
  mod = sqrt (res[0] * res[0] + res[1] * res[1] + res[2] * res[2]);
  for (i = 0; i < 3; i++)
    res[i] /= mod;

  /* L'axe n'est pas l'axe des x */

  ex[0] = ex[1] = ex[2] = 0.0;
  if(res[0] == 0.0)
    ex[0] = 1.0;
  else if(res[1] == 0.0)
    ex[1] = 1.0;
  else
    ex[2] = 1.0;

  prodve (res, ex, t1);

  mod = sqrt (t1[0] * t1[0] + t1[1] * t1[1] + t1[2] * t1[2]);
  for (i = 0; i < 3; i++)
    t1[i] /= mod;

  prodve (t1, res, t2);

  mod = sqrt (t2[0] * t2[0] + t2[1] * t2[1] + t2[2] * t2[2]);
  for (i = 0; i < 3; i++)
    t2[i] /= mod;

  for (i = 0; i < 3; i++)
    s->plan[0][i] = t1[i];
  for (i = 0; i < 3; i++)
    s->plan[1][i] = t2[i];
  for (i = 0; i < 3; i++)
    s->plan[2][i] = res[i];

  if (DEBUG){
    Msg(INFO, "plan    : (%g x + %g y + %g z = %g)", s->a, s->b, s->c, s->d);
    Msg(INFO, "normale : (%g , %g , %g )", res[0], res[1], res[2]);
    Msg(INFO, "t1      : (%g , %g , %g )", t1[0], t1[1], t1[2]);
    Msg(INFO, "t2      : (%g , %g , %g )", t2[0], t2[1], t2[2]);
  }

  /* Matrice orthogonale */

  if (!iz){
    for (i = 0; i < 3; i++){
      for (j = 0; j < 3; j++){
	s->invplan[i][j] = (i == j) ? 1. : 0.;
	s->plan[i][j] = (i == j) ? 1. : 0.;
      }
    }
  }
  else{
    for (i = 0; i < 3; i++){
      for (j = 0; j < 3; j++){
	s->invplan[i][j] = s->plan[j][i];
      }
    }
  }
  List_Reset (points);
}


int Calcule_Contours (Surface * s){
  int i, j, ori, ORI;
  Vertex *v, *first, *last;
  List_T *l, *linv;
  Curve *c;
  double n[] = {0., 0., 1.};

  l = List_Create (10, 10, sizeof (Vertex *));
  for (i = 0; i < List_Nbr (s->s.Generatrices); i++){
    List_Read (s->s.Generatrices, i, &c);
    if (!List_Nbr (l)){
      List_Read (c->Vertices, 0, &first);
    }
    for (j = 1; j < List_Nbr (c->Vertices); j++){
      List_Read (c->Vertices, j, &v);
      List_Add (l, &v);
      if (j == List_Nbr (c->Vertices) - 1){
	last = v;
      }
    }
    if (!compareVertex (&last, &first)){
      ori = Oriente (l, n);

      /* Le premier contour est oriente aire a droite
	 Les autes sont des trous orientes aire a gauche */
      
      if (!List_Nbr (s->Contours))
	ORI = ori;

      if ((!List_Nbr (s->Contours) && !ori) ||
	  (List_Nbr (s->Contours) && ori)){
	linv = List_Create (10, 10, sizeof (Vertex *));
	List_Invert (l, linv);
	List_Delete (l);
	l = linv;
      }
      List_Add (s->Contours, &l);
      l = List_Create (10, 10, sizeof (Vertex *));
    }
  }
  return ORI;
}


void Calcule_Z (void *data, void *dum){
  Vertex **pv, *v;
  double Z, U, V;

  pv = (Vertex **) data;
  v = *pv;
  if (v->Frozen || v->Num < 0)
    return;

  XYtoUV (THESUPPORT, &v->Pos.X, &v->Pos.Y, &U, &V, &Z);
  v->Pos.Z = Z;
}

void Calcule_Z_Plan (void *data, void *dum){
  Vertex **pv, *v;
  Vertex V;

  V.Pos.X = THESURFACE->a;
  V.Pos.Y = THESURFACE->b;
  V.Pos.Z = THESURFACE->c;

  Projette (&V, THESURFACE->plan);

  pv = (Vertex **) data;
  v = *pv;
  if (V.Pos.Z != 0.0)
    v->Pos.Z = (THESURFACE->d - V.Pos.X * v->Pos.X - V.Pos.Y * v->Pos.Y)
      / V.Pos.Z;
  else
    v->Pos.Z = 0.0;
}


void Add_In_Mesh (void *a, void *b){
  if (!Tree_Search (THEM->Vertices, a))
    Tree_Add (THEM->Vertices, a);
}

void Freeze_Vertex (void *a, void *b){
  Vertex *pv;
  pv = *(Vertex **) a;
  pv->Freeze.X = pv->Pos.X;
  pv->Freeze.Y = pv->Pos.Y;
  pv->Freeze.Z = pv->Pos.Z;
  pv->Frozen = 1;
}

void deFreeze_Vertex (void *a, void *b){
  Vertex *pv;
  pv = *(Vertex **) a;
  if (!pv->Frozen)
    return;
  pv->Pos.X = pv->Freeze.X;
  pv->Pos.Y = pv->Freeze.Y;
  pv->Pos.Z = pv->Freeze.Z;
  pv->Frozen = 0;
}

void PutVertex_OnSurf (void *a, void *b){
  Vertex *pv;
  pv = *(Vertex **) a;
  if (!pv->ListSurf)
    pv->ListSurf = List_Create (1, 1, sizeof (Surface *));
  List_Add (pv->ListSurf, &THESURFACE);
}


/* remplis la structure Delaunay d'un triangle cree */

Delaunay * testconv (avlptr * root, int *conv, DocRecord * ptr){

  avlptr *proot;
  double qual;
  Delaunay *pdel;

  proot = root;

  pdel = NULL;

  *conv = 0;
  if (*root == NULL)
    *conv = 1;
  else{
    pdel = findrightest (*proot, ptr);
    qual = pdel->t.quality_value;
    switch (LocalNewPoint){
    case CENTER_CIRCCIRC:
    case BARYCENTER:
      if (qual < CONV_VALUE)
	*conv = 1;
      break;
    case VORONOI_INSERT:
      if ((*root) == NULL)
	*conv = 1;
      break;
    }
  }
  return (pdel);
}


int mesh_domain (ContourPeek * ListContours, int numcontours,
		 maillage * mai, int *numpoints, DocRecord * BGMesh,
		 int OnlyTheInitialMesh){
  List_T *kill_L, *del_L ;
  MPoint pt;
  DocRecord docm, *doc;
  int conv,i,j,nump,numact,numlink,numkil,numaloc,numtri,numtrr,numtrwait;
  Delaunay *del, *del_P, *deladd, **listdel;
  PointNumero aa, bb, cc, a, b, c;
  DListPeek list, p, q;
  avlptr *root, *proot, *root_w, *root_acc;
  double volume_old, volume_new;

  root = (avlptr *) Malloc (sizeof (avlptr));
  root_w = (avlptr *) Malloc (sizeof (avlptr));
  root_acc = (avlptr *) Malloc (sizeof (avlptr));

  nump = 0;
  for (i = 0; i < numcontours; i++)
    nump += ListContours[i]->numpoints;

  /* creation du maillage initial grace au puissant algorithme "divide and conquer" */

  doc = &docm;
  FGMESH = &docm;
  doc->points = (PointRecord *) Malloc ((nump + 100) * sizeof(PointRecord));
  gPointArray = doc->points;

  numaloc = nump;
  doc->numPoints = nump;
  doc->numTriangles = 0;
  mai->numtriangles = 0;
  mai->numpoints = 0;

  if (!numcontours){
    Msg(ERROR, "No Contour");
    return 0;
  }

  numact = 0;
  for (i = 0; i < numcontours; i++){
    for (j = 0; j < ListContours[i]->numpoints; j++){
      doc->points[numact + j].where.h =
	ListContours[i]->oriented_points[j].where.h
	+ ListContours[i]->perturbations[j].h;
      doc->points[numact + j].where.v =
	ListContours[i]->oriented_points[j].where.v
	+ ListContours[i]->perturbations[j].v;
      doc->points[numact + j].quality =
	ListContours[i]->oriented_points[j].quality;
      doc->points[numact + j].initial =
	ListContours[i]->oriented_points[j].initial;
      ListContours[i]->oriented_points[j].permu = numact + j;
      doc->points[numact + j].permu = numact + j;
      doc->points[numact + j].numcontour = ListContours[i]->numerocontour;
      doc->points[numact + j].adjacent = NULL;
    }
    numact += ListContours[i]->numpoints;
  }

  DelaunayAndVoronoi (doc);
  Conversion (doc);
  remove_all_dlist (doc->numPoints, doc->points);

  if (!is_3D){
    if (UseBGMesh == 1)
      BGMESH = BGMesh;
    else{
      BGMESH = doc;
      InitBricks (BGMESH);
    }
  }

  /* elimination des triangles exterieurs + verification de l'existence
     des edges (coherence) */

  makepermut (nump);
  del_L = List_Create(doc->numTriangles, 1000, sizeof(Delaunay*));

  for(i= 0;i<doc->numTriangles;i++){
    del_P = &doc->delaunay[i] ;
    List_Add(del_L, &del_P);
  }
  
  verify_edges (del_L, ListContours, numcontours, doc->numTriangles);
  verify_inside (doc->delaunay, doc->numTriangles);

  /* creation des liens ( triangles voisins ) */

  CreateLinks (del_L, doc->numTriangles, ListContours, numcontours);

  /* initialisation de l'arbre  */

  (*root) = (*root_w) = (*root_acc) = NULL;

  if (doc->numTriangles == 1)
    doc->delaunay[0].t.position = INTERN;

  for (i = 0; i < doc->numTriangles; i++){
    if (doc->delaunay[i].t.position != EXTERN){
      del = &doc->delaunay[i];
      switch (LocalNewPoint){
      case CENTER_CIRCCIRC:
      case BARYCENTER:
	Insert_Triangle (root, del);
	break;
      case VORONOI_INSERT:
	switch (del->t.position){
	case ACTIF:
	case INTERN:
	  Insert_Triangle (root, del);
	  break;
	case WAITING:
	  Insert_Triangle (root_w, del);
	  break;
	case ACCEPTED:
	  Insert_Triangle (root_acc, del);
	  break;
	}
      }
    }
  }


  /* maillage proprement dit :
     1) Les triangles sont tries dans l'arbre suivant leur qualite
     2) on ajoute un noeud au c.g du plus mauvais
     3) on reconstruit le delaunay par Bowyer-Watson
     4) on ajoute les triangles dans l'arbre et on recommence
     jusque quand tous les triangles sont OK */

  del = testconv (root, &conv, doc);

  PushgPointArray (doc->points);

  List_Reset(del_L);
  kill_L = List_Create(1000, 1000, sizeof(Delaunay*));

  if (OnlyTheInitialMesh)
    conv = 1;

  while (conv != 1){

    pt = Localize (del, doc);

    numlink = 0;
    numkil = 0;
    list = NULL;

    if (!PE_Del_Triangle (del, pt, &list, kill_L, del_L, &numlink, &numkil)){
      Msg(WARNING, "Triangle Non Delete");
      Delete_Triangle (root, del);
      Delete_Triangle (root_w, del);
      del->t.quality_value /= 10.;
      switch (LocalNewPoint){
      case CENTER_CIRCCIRC:
      case BARYCENTER:
	Insert_Triangle (root, del);
	break;
      case VORONOI_INSERT:
	del->t.position = ACCEPTED;
	Insert_Triangle (root_acc, del);
	break;
      }
      
      numlink = numkil = 0;
      if (list != NULL){
	p = list;
	do{
	  q = p;
	  p = Pred (p);
	  Free (q);
	}
	while (p != list);
      }
      list = NULL;
      del = testconv (root, &conv, doc);
      continue;
    }

    /* Test du Volume */
    
    i = 0;
    p = list;
    volume_new = 0.0;
    do{
      q = p->next;
      bb = p->point_num;
      cc = q->point_num;
      volume_new += fabs ((doc->points[bb].where.h - pt.h) *
			  (doc->points[cc].where.v - doc->points[bb].where.v) -
			  (doc->points[cc].where.h - doc->points[bb].where.h) *
			  (doc->points[bb].where.v - pt.v));
      p = q;
    } while (p != list);

    volume_old = 0.0;
    for (i = 0; i < numkil; i++){
      del_P = *(Delaunay**)List_Pointer(kill_L, i);
      aa = del_P->t.a;
      bb = del_P->t.b;
      cc = del_P->t.c;
      volume_old += fabs ((doc->points[bb].where.h - doc->points[aa].where.h) *
			  (doc->points[cc].where.v - doc->points[bb].where.v) -
			  (doc->points[cc].where.h - doc->points[bb].where.h) *
			  (doc->points[bb].where.v - doc->points[aa].where.v));
    }
    
    if ((volume_old - volume_new) / (volume_old + volume_new) > 1.e-6){
      Msg(WARNING, "Volume has changed : %g -> %g", volume_old, volume_new);
      Delete_Triangle (root, del);
      Delete_Triangle (root_w, del);
      del->t.quality_value /= 10.;
      switch (LocalNewPoint){
      case CENTER_CIRCCIRC:
      case BARYCENTER:
	Insert_Triangle (root, del);
	break;
      case VORONOI_INSERT:
	del->t.position = ACCEPTED;
	Insert_Triangle (root_acc, del);
	break;
      }

      numlink = numkil = 0;
      if (list != NULL){
	p = list;
	do{
	  q = p;
	  p = Pred (p);
	  Free (q);
	} while (p != list);
      }
      list = NULL;
      del = testconv (root, &conv, doc);
      continue;
    }

    /* Fin test du volume */

    for (i = 0; i < numkil; i++){
      del_P = *(Delaunay**)List_Pointer(kill_L, i);

      switch (LocalNewPoint){
      case CENTER_CIRCCIRC:
      case BARYCENTER:
	Delete_Triangle (root, del_P);
	break;
      case VORONOI_INSERT:
	switch (del_P->t.position){
	case WAITING:
	  Delete_Triangle (root_w, del_P);
	  break;
	case ACTIF:
	case INTERN:
	  Delete_Triangle (root, del_P);
	  break;
	case ACCEPTED:
	  Delete_Triangle (root_acc, del_P);
	  break;
	}
      }
    }

    *numpoints = doc->numPoints;
    Insert_Point (pt, numpoints, &numaloc, doc, BGMESH, is_3D);
    doc->points = gPointArray;
    doc->numPoints = *numpoints;

    i = 0;
    p = list;

    do{
      q = p->next;
      
      aa = doc->numPoints - 1;
      bb = p->point_num;
      cc = q->point_num;
      
      deladd = (Delaunay *) Malloc (sizeof (Delaunay));
      
      filldel (deladd, aa, bb, cc, doc->points, BGMESH);
      
      List_Put(del_L, numlink+i, &deladd);
      i++;
      p = q;
      
    } while (p != list);

    p = list;
    
    do{
      q = p;
      p = Pred (p);
      Free (q);
    } while (p != list);

    CreateLinks (del_L, i + numlink, ListContours, numcontours);

    for (j = 0; j < i; j++){
      del_P = *(Delaunay**)List_Pointer(del_L, j+numlink) ;
      switch (LocalNewPoint) {
      case CENTER_CIRCCIRC:
      case BARYCENTER:
	Insert_Triangle (root, del_P);
	break;
      case VORONOI_INSERT:
	switch (del_P->t.position){
	case ACTIF:
	case INTERN:
	  Insert_Triangle (root, del_P);
	  break;
	case WAITING:
	  Insert_Triangle (root_w, del_P);
	  break;
	case ACCEPTED:
	  Insert_Triangle (root_acc, del_P);
	  break;
	}
      }
    }
    
    del = testconv (root, &conv, doc);
    
  }

  numtri = 0;
  numtrwait = 0;

  if (*root_w != NULL){
    proot = root_w;
    avltree_count (*proot, &numtrwait);
  }

  numtrr = 0;

  proot = root;
  switch (LocalNewPoint){
  case VORONOI_INSERT:
    proot = root_acc;
    break;
  }
  avltree_count (*proot, &numtrr);

  alloue_Mai_Del (mai, numtrr + numtrwait + 1, 100);

  listdel = mai->listdel;
  numtri = 0;
  avltree_print(*proot,listdel,&numtri);
  if(numtrwait != 0){
    numtri = 0;
    avltree_print(*root_w,(Delaunay**)del_L->array,&numtri);
    for(i=0;i<numtrwait;i++){
      mai->listdel[i+numtrr] = *(Delaunay**)List_Pointer(del_L, i);
    }
    avltree_remove(root_w);
  }
  avltree_remove(root);
  
  List_Delete(del_L);

  mai->numtriangles = numtrr + numtrwait;
  mai->numpoints = doc->numPoints;
  mai->points = doc->points;

  /* Tous Les Triangles doivent etre orientes */

  if (!mai->numtriangles)
    mai->numtriangles = 1;

  for (i = 0; i < mai->numtriangles; i++){
    a = mai->listdel[i]->t.a;
    b = mai->listdel[i]->t.b;
    c = mai->listdel[i]->t.c;

    mai->listdel[i]->v.voisin1 = NULL;
    if (((doc->points[b].where.h - doc->points[a].where.h) *
	 (doc->points[c].where.v - doc->points[b].where.v) -
	 (doc->points[c].where.h - doc->points[b].where.h) *
	 (doc->points[b].where.v - doc->points[a].where.v)) > 0.0){
      mai->listdel[i]->t.a = b;
      mai->listdel[i]->t.b = a;
    }
  }
  return 1;
}

void Maillage_Automatique_VieuxCode (Surface * pS, Mesh * m, int ori){
  ContourRecord *cp, **liste;
  List_T *c;
  Vertex *v, V[3], *ver[3], **pp[3];
  maillage M;
  int err, i, j, k, N, a, b, d;
  Simplex *s;

  if (m->BGM.Typ == WITHPOINTS){
    is_3D = 0;
    UseBGMesh = 0;
  }
  else{
    is_3D = 1;
  }

  liste = (ContourPeek *) Malloc (List_Nbr (pS->Contours) * sizeof (ContourPeek));

  k = 0;

  for (i = 0; i < List_Nbr (pS->Contours); i++){
    cp = (ContourRecord *) Malloc (sizeof (ContourRecord));
    List_Read (pS->Contours, i, &c);
    cp->oriented_points = (PointRecord *) Malloc (List_Nbr (c) * sizeof (PointRecord));
    cp->perturbations = (MPoint *) Malloc (List_Nbr (c) * sizeof (MPoint));
    cp->numerocontour = i;
    for (j = 0; j < List_Nbr (c); j++){
      List_Read (c, j, &v);
      cp->oriented_points[j].where.h = v->Pos.X;
      cp->oriented_points[j].where.v = v->Pos.Y;

      cp->perturbations[j].h = RAND_LONG;
      cp->perturbations[j].v = RAND_LONG;
      cp->oriented_points[j].numcontour = i;
      cp->oriented_points[j].quality = v->lc;
      cp->oriented_points[j].permu = k++;
      cp->oriented_points[j].initial = v->Num;
    }
    cp->numpoints = List_Nbr (c);
    liste[i] = cp;
  }

  if (pS->Method){
    /* lets force this, since it's the only one that works... */
    LocalNewPoint = CENTER_CIRCCIRC;
    mesh_domain (liste, List_Nbr (pS->Contours), &M, &N, NULL, 0);
  }

  for (i = 0; i < M.numpoints; i++){
    if (gPointArray[i].initial < 0){
      gPointArray[i].initial = ++CurrentNodeNumber;
      v = Create_Vertex (gPointArray[i].initial, gPointArray[i].where.h,
			 gPointArray[i].where.v, 0.0, gPointArray[i].quality, 0.0);
      if (!Tree_Search (pS->Vertices, &v))
	Tree_Add (pS->Vertices, &v);
    }
  }
  for (i = 0; i < 3; i++)
    ver[i] = &V[i];

  for (i = 0; i < M.numtriangles; i++){

    a = M.listdel[i]->t.a;
    b = M.listdel[i]->t.b;
    d = M.listdel[i]->t.c;
    
    ver[0]->Num = gPointArray[a].initial;
    ver[1]->Num = gPointArray[b].initial;
    ver[2]->Num = gPointArray[d].initial;
    err = 0;
    for (j = 0; j < 3; j++){
      if ((pp[j] = (Vertex **) Tree_PQuery (pS->Vertices, &ver[j]))){
      }
      else{
	err = 1;
	Msg(ERROR, "Unknown Vertex %d\n", ver[j]->Num);
      }
    }
    if (ori && !err)
      s = Create_Simplex (*pp[0], *pp[1], *pp[2], NULL);
    else if (!err)
      s = Create_Simplex (*pp[0], *pp[2], *pp[1], NULL);
    if (!err){
      s->iEnt = pS->Num;
      Tree_Insert (pS->Simplexes, &s);
    }
  }
  Free (gPointArray);
}


void Make_Mesh_With_Points (DocRecord * ptr, PointRecord * Liste, int Numpoints){
  ptr->numTriangles = 0;
  ptr->points = Liste;
  ptr->numPoints = Numpoints;
  DelaunayAndVoronoi (ptr);
  Conversion (ptr);
  remove_all_dlist (ptr->numPoints, ptr->points);
}

void filldel (Delaunay * deladd, int aa, int bb, int cc,
	      PointRecord * points, DocRecord * mesh){

  double newqual, L;
  MPoint pt2, pt4;
  Vertex *v, *dum;

  deladd->t.a = aa;
  deladd->t.b = bb;
  deladd->t.c = cc;
  deladd->t.info = TOLINK;
  deladd->t.info2 = 0;
  deladd->v.voisin1 = NULL;
  deladd->v.voisin2 = NULL;
  deladd->v.voisin3 = NULL;

  CircumCircle (points[aa].where.h,
		points[aa].where.v,
		points[bb].where.h,
		points[bb].where.v,
		points[cc].where.h,
		points[cc].where.v,
		&deladd->t.xc,
		&deladd->t.yc);

  pt2.h = deladd->t.xc;
  pt2.v = deladd->t.yc;
  if (!is_3D){
    if (mesh){
      newqual = FACTEUR_MULTIPLICATIF * find_quality (pt2, mesh);
    }
    else{
      newqual = (points[aa].quality + points[bb].quality + points[cc].quality) / 3.;
    }
    v = Create_Vertex (-1, pt2.h, pt2.v, 0.0, 0.0, 0.0);
    Calcule_Z_Plan (&v, &dum);
    Projette_Inverse (&v, &dum);
    Free (v);
  }
  else{
    v = Create_Vertex (-1, pt2.h, pt2.v, 0.0, 0.0, 0.0);
    Calcule_Z_Plan (&v, &dum);
    Projette_Inverse (&v, &dum);
    newqual = Lc_XYZ (v->Pos.X, v->Pos.Y, v->Pos.Z, THEM);
    Free (v);
  }

  switch (LocalNewPoint){
  case CENTER_CIRCCIRC:
  case BARYCENTER:
    deladd->t.quality_value =
      sqrt ((deladd->t.xc - points[cc].where.h) * (deladd->t.xc - points[cc].where.h) +
	    (deladd->t.yc - points[cc].where.v) * (deladd->t.yc - points[cc].where.v)
	    ) / newqual;
    deladd->t.position = INTERN;
    break;
    
  case VORONOI_INSERT:
    pt4.h = points[bb].where.h;
    pt4.v = points[bb].where.v;
    //pt3.h = .5 * (points[bb].where.h + points[cc].where.h);
    //pt3.v = .5 * (points[bb].where.v + points[cc].where.v);
    deladd->t.quality_value = myhypot (pt2.h - pt4.h, pt2.v - pt4.v);
    L = newqual / deladd->t.quality_value;
    if (L > 1.5)
      deladd->t.position = ACCEPTED;
    else
      deladd->t.position = NONACCEPTED;
    break;
  }
}

void ActionEndTheCurve (void *a, void *b){
  Curve *c = *(Curve **) a;
  End_Curve (c);
}

int MeshParametricSurface (Surface * s);
int Extrude_Mesh (Surface * s);

void Maillage_Surface (void *data, void *dum){
  Surface  **pS, *s;
  Tree_T    *tnxe;
  int        ori;

  pS = (Surface **) data;
  s = *pS;

  if (!s->Support)
    return;

  THESUPPORT = s->Support;
  THESURFACE = s;

  if (Tree_Nbr (s->Simplexes))
    Tree_Delete (s->Simplexes);
  s->Simplexes = Tree_Create (sizeof (Simplex *), compareQuality);
  if (Tree_Nbr (s->Vertices))
    Tree_Delete (s->Vertices);
  s->Vertices = Tree_Create (sizeof (Vertex *), compareVertex);

  Msg(STATUS, "Meshing Surface %d", s->Num);

  if (MeshTransfiniteSurface (s) ||
      MeshEllipticSurface (s) ||
      MeshCylindricalSurface (s) ||
      MeshParametricSurface (s) ||
      Extrude_Mesh (s)){
    Tree_Action (THEM->Points, PutVertex_OnSurf);
    Tree_Action (s->Vertices, PutVertex_OnSurf);
    Tree_Action (s->Vertices, Add_In_Mesh);
    if (CTX.mesh.degree == 2)
      Degre2 (THEM->Vertices, s->Vertices, s->Simplexes, NULL, s);
    THEM->Statistics[5] += Tree_Nbr (THESURFACE->Vertices);
    THEM->Statistics[7] += Tree_Nbr (THESURFACE->Simplexes);

    /* void TRIE_MON_GARS(void *a, void *b);
       Tree_Action (THES->Simplexes, TRIE_MON_GARS);
       Link_Simplexes(NULL, THES->Simplexes);
       void  constraint_the_edge (int ,int ,int);
       constraint_the_edge (6, 45, 85);
    */
    return;
  }

  int TypSurface = s->Typ;
  s->Typ = MSH_SURF_PLAN;
  Plan_Moyen (pS, dum);

  Tree_Action (THEM->Points, Freeze_Vertex);
  Tree_Action (s->Vertices, Freeze_Vertex);
  Tree_Action (THEM->Points, Projette_Plan_Moyen);
  Tree_Action (s->Vertices, Projette_Plan_Moyen);
  Tree_Action (THEM->Curves, ActionEndTheCurve);

  End_Surface (s);

  ori = Calcule_Contours (s);

  if (CTX.mesh.algo == DELAUNAY_OLDALGO)
    Maillage_Automatique_VieuxCode (s, THEM, ori);
  else
    AlgorithmeMaillage2DAnisotropeModeJF (s);

  if(CTX.mesh.nb_smoothing){
    Msg(STATUS, "Mesh Smoothing");
    tnxe = Tree_Create (sizeof (NXE), compareNXE);
    create_NXE (s->Vertices, s->Simplexes, tnxe);
    for (int i = 0; i < CTX.mesh.nb_smoothing; i++)
      Tree_Action (tnxe, ActionLiss);
    Tree_Delete (tnxe);
  }

  if (s->Recombine)
    Recombine (s->Vertices, s->Simplexes, s->RecombineAngle);

  s->Typ = TypSurface;

  if (s->Typ != MSH_SURF_PLAN){
    if (s->Extrude)
      s->Extrude->Rotate (s->plan);
    Tree_Action (s->Vertices, Calcule_Z);
    if (s->Extrude)
      s->Extrude->Rotate (s->invplan);
  }
  else
    Tree_Action (s->Vertices, Calcule_Z_Plan);

  Tree_Action (s->Vertices, Projette_Inverse);
  Tree_Action (THEM->Points, Projette_Inverse);

  Tree_Action (THEM->Points, deFreeze_Vertex);
  Tree_Action (s->Vertices, deFreeze_Vertex);

  Tree_Action (THEM->Points, PutVertex_OnSurf);
  Tree_Action (s->Vertices, PutVertex_OnSurf);
  Tree_Action (s->Vertices, Add_In_Mesh);

  Tree_Action (THEM->Curves, ActionEndTheCurve);
  End_Surface (s->Support);
  End_Surface (s);

  if (DEBUG){
    Msg (INFO, "Nombre de triangles : %d", Tree_Nbr(s->Simplexes));
    Msg (INFO, "Nombre de points    : %d", Tree_Nbr(s->Vertices));
  }

  if (CTX.mesh.degree == 2)
    Degre2 (THEM->Vertices, THEM->VertexEdges, s->Simplexes, NULL, THESUPPORT);

  THEM->Statistics[5] += Tree_Nbr (THESURFACE->Vertices);
  THEM->Statistics[7] += Tree_Nbr (THESURFACE->Simplexes);

}
