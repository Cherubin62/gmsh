
#include "Gmsh.h"
#include "Geo.h"
#include "CAD.h"
#include "Mesh.h"
#include "DataBase.h"

extern Mesh *THEM;

/* Contour extraction by a tree method */

static Tree_T *treelink;
static Tree_T *treeedges;
static Tree_T *treefaces;
static List_T *listlink;

typedef struct {
  int n,a,arbre;
}nxa;

typedef struct {
  int n,visited;
  List_T *l;
}lnk;

int complink(const void*a, const void*b){
  lnk *q,*w;
  q = (lnk*)a;
  w = (lnk*)b;
  return q->n-w->n;
}

static int POINT_FINAL;
static int CONTOUR_TROUVE;
static List_T *VisitedNodes ; //geuz

void recur_trouvecont(int ip , int ed , List_T *Liste, int gauche , List_T *old ){
  lnk lk;
  nxa a;
  int i,rev;

  lk.n = ip;
  Tree_Query(treelink,&lk);
  if(List_Nbr(lk.l) != 2 && !old)return;
  for(i=0;i<List_Nbr(lk.l);i++){
    List_Read(lk.l,i,&a);
    if(abs(a.a) != abs(ed)){
      if(!old || List_Search(old,&a.a,fcmp_absint) || List_Nbr(lk.l) == 2){
        if(!gauche){
          List_Add(Liste,&a.a);
          if(List_Search(VisitedNodes, &a.n, fcmp_absint)){//geuz
            CONTOUR_TROUVE =1;//end geuz
            return;//end geuz
          }//geuz
        }
        if(a.n == POINT_FINAL){
          CONTOUR_TROUVE = 1;
        }
        else{
          recur_trouvecont(a.n,abs(a.a),Liste,gauche,old);
        }
        if(gauche){
          rev = -a.a;
          List_Add(Liste,&rev);
          List_Add(VisitedNodes, &a.n); //geuz
        }
      }
    }
  }
}


void recur_trouvevol(int ifac , int iedge, List_T *Liste, List_T *old ,
                     Tree_T *treeedges, Tree_T *treefaces){

  lnk lk;
  nxa a;
  int i,is,rev,l;
  Curve *c;
  Surface *s = FindSurface(abs(ifac),THEM);

  for(l=0;l<List_Nbr(s->s.Generatrices);l++){
    List_Read(s->s.Generatrices,l,&c);
    lk.n = abs(c->Num);
    is = lk.n;
    if(!Tree_Search(treeedges,&is)){
      Tree_Add(treeedges,&is);
    }
    else{
      Tree_Suppress(treeedges,&is);
    }
    Tree_Query(treelink,&lk);
    if(List_Nbr(lk.l) == 2 || old){
      for(i=0;i<List_Nbr(lk.l);i++){
        List_Read(lk.l,i,&a);
        if(abs(a.a) != abs(ifac)){
          if(!Tree_Search(treefaces,&a.a)){
            Tree_Add(treefaces,&a.a);
            if(!old || List_Search(old,&a.a,fcmp_absint) || List_Nbr(lk.l) == 2){
              rev = abs(a.a);
              List_Add(Liste,&rev);
              recur_trouvevol(rev,is,Liste,old,treeedges,treefaces);
            }
          }
        }
      }
    }
  }
}


void BegEndCurve (Curve *c, int *ip1, int *ip2){
    *ip1 = c->beg->Num;
    *ip2 = c->end->Num;
}

void CreeLiens ( void ) {
  int i,is,ip1,ip2;
  lnk li,*pli;
  nxa  na1,na2;
  Curve *ic;

  treelink = Tree_Create(sizeof(lnk),complink);

  List_T *temp = Tree2List(THEM->Curves);
  for(i=0;i<List_Nbr(temp);i++){
    List_Read(temp,i,&ic);
    if(ic->Num > 0){
     is = ic->Num;
     BegEndCurve(ic,&ip1,&ip2);

     na1.a = -is;
     na2.a = is;
     na2.arbre = na1.arbre = li.visited =  0;
     na1.n = li.n = ip1;
     na2.n = ip2;
     if((pli = (lnk*)Tree_PQuery(treelink,&li))){
       List_Add(pli->l,&na2);
     }
     else{
       li.l = List_Create(20,1,sizeof(nxa));
       List_Add(li.l,&na2);
       Tree_Add(treelink,&li);
     }
     li.n = ip2;
     if((pli = (lnk*)Tree_PQuery(treelink,&li))){
       List_Add(pli->l,&na1);
     }
     else{
       li.l = List_Create(20,1,sizeof(nxa));
       List_Add(li.l,&na1);
       Tree_Add(treelink,&li);
     }
   }
  }
  listlink = Tree2List(treelink);
}


void CreeLiens2 ( void ) {
  int i,k;
  lnk li,*pli;
  nxa  na;
  Surface *s;
  Curve *c;

  treelink = Tree_Create(sizeof(lnk),complink);
  List_T *temp = Tree2List(THEM->Surfaces);

  for(i=0;i<List_Nbr(temp);i++){
    List_Read(temp,i,&s);
    if(s->Num > 0)
      na.a = s->Num;
    for(k=0;k<List_Nbr(s->s.Generatrices);k++){
      List_Read(s->s.Generatrices,k,&c);
      li.n = abs(c->Num);
      if((pli = (lnk*)Tree_PQuery(treelink,&li))){
        List_Add(pli->l,&na);
      }
      else{
        li.l = List_Create(20,1,sizeof(nxa));
        List_Add(li.l,&na);
        Tree_Add(treelink,&li);
      }
    }
  }
  List_Delete(temp);
  listlink = Tree2List(treelink);
}


int alledgeslinked ( int ed , List_T *Liste , List_T *old){

  int ip1,ip2,i,rev;
  lnk lk;
  nxa a;

  VisitedNodes = List_Create(20,20,sizeof(int)); //geuz

  CreeLiens();

  Curve *c,C;
  c = &C;
  c->Num = ed;
  Tree_Query(THEM->Curves,&c);

  BegEndCurve(c,&ip1,&ip2);

  CONTOUR_TROUVE = 0;

  POINT_FINAL = ip2;
  recur_trouvecont(ip1,ed,Liste,1,old);

  if(old){
    List_Sort(old,fcmp_absint);
  }

  lk.n = ip2;
  Tree_Query(treelink,&lk);
  for(i=0;i<List_Nbr(lk.l);i++){
    List_Read(lk.l,i,&a);
    if(abs(a.a) == abs(ed)){
      rev = -a.a;
      List_Add(Liste,&rev);
    }
  }


  if(!CONTOUR_TROUVE){
    POINT_FINAL = ip1;
    recur_trouvecont(ip2,ed,Liste,0,old);
  }

  List_Delete(VisitedNodes); //geuz

  return(CONTOUR_TROUVE);
}


int allfaceslinked (int iz , List_T *Liste , List_T *old){

  CreeLiens2();
  treeedges = Tree_Create(sizeof(int),fcmp_absint);
  treefaces = Tree_Create(sizeof(int),fcmp_absint);

  Tree_Add(treefaces,&iz);
  List_Add(Liste,&iz);
  recur_trouvevol(iz,0,Liste,old,treeedges,treefaces);

  if(!Tree_Nbr(treeedges)){
    CONTOUR_TROUVE = 1;
  }
  else{
    CONTOUR_TROUVE = 0;
  }

  Tree_Delete(treeedges);
  Tree_Delete(treefaces);

  return(CONTOUR_TROUVE);
}

void PremierVolume(int iSurf, int *iVol){
  int i,j;
  Surface *sur;
  Volume *vol;

  *iVol = 0;

  List_T *temp = Tree2List(THEM->Volumes);
  for(i=0;i<List_Nbr(temp);i++){
        List_Read(temp,i,&vol);
    for(j=0;j<List_Nbr(vol->Surfaces);j++){
        List_Read(vol->Surfaces,j,&sur);
        if(abs(sur->Num) == iSurf){
                List_Delete(temp);
            *iVol = i+1;
            return;
        }
    }
  }
  List_Delete(temp);
}

/* Gestion des entites visibles */

extern Tree_T *EntitesVisibles;

typedef struct{
 int Entite;
 int Visible;
}EntiteVisible;

int compareEntiteVisible(const void *a, const void *b){
  EntiteVisible *q,*w;
  q = (EntiteVisible*)a;
  w = (EntiteVisible*)b;
  return(q->Entite-w->Entite);
}

int EntiteEstElleVisible(int iEnt){
  EntiteVisible e;
  e.Entite = iEnt;
  if(Tree_Query(EntitesVisibles,&e))
    return e.Visible;
  return 1;
}

void ToutesLesEntitesRelatives(int iEnt, Tree_T *Tree, int add_rem){
  int i;
  EntiteVisible e;

  Surface *s;
  Volume *v;
  Curve *c;

  if((c = FindCurve(iEnt,THEM))){
  }
  else if((s = FindSurface(iEnt,THEM))){
    for(i=0;i<List_Nbr(s->s.Generatrices);i++){
      List_Read(s->s.Generatrices,i,&c);
      e.Entite = abs(c->Num);
      e.Visible = add_rem;
      Tree_Replace(Tree,&e);
    }
  }
  else if((v = FindVolume(iEnt,THEM))){
    for(i=0;i<List_Nbr(v->Surfaces);i++){
      List_Read(v->Surfaces,i,&s);
      e.Entite = abs(s->Num);
      e.Visible = add_rem;
      Tree_Replace(Tree,&e);
    }
  }

  e.Entite = abs(iEnt);
  e.Visible = add_rem;
  Tree_Replace(Tree,&e);
}

void RemplirEntitesVisibles (int add_rem){
  int i;
  Volume *v;
  Surface *s;
  Curve *c;

  List_T *ListVolumes = Tree2List (THEM->Volumes);
  List_T *ListSurfaces = Tree2List (THEM->Surfaces);
  List_T *ListCurves = Tree2List (THEM->Curves);
  EntitesVisibles = Tree_Create(sizeof(EntiteVisible),compareEntiteVisible);
  for(i=0;i<List_Nbr(ListVolumes);i++){
    List_Read(ListVolumes,i,&v);
    ToutesLesEntitesRelatives(v->Num,EntitesVisibles,add_rem);
  }
  for(i=0;i<List_Nbr(ListSurfaces);i++){
    List_Read(ListSurfaces,i,&s);
    ToutesLesEntitesRelatives(s->Num,EntitesVisibles,add_rem);
  }
  for(i=0;i<List_Nbr(ListCurves);i++){
    List_Read(ListCurves,i,&c);
    ToutesLesEntitesRelatives(c->Num,EntitesVisibles,add_rem);
  }
  List_Delete(ListVolumes);
  List_Delete(ListSurfaces);
  List_Delete(ListCurves);
}
