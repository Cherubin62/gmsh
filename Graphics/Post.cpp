
#include "Gmsh.h"
#include "GmshUI.h"
#include "Geo.h"
#include "Mesh.h"
#include "Draw.h"
#include "Views.h"
#include "Context.h"

extern Context_T   CTX;
extern List_T     *Post_ViewList;

static double      Raise[3][5];
static double      RaiseFactor[3];

/* ------------------------------------------------------------------------
    Give Value from Index
   ------------------------------------------------------------------------ */

double GiveValueFromIndex_Lin(double ValMin, double ValMax, int NbIso, int Iso){
  if(NbIso==1) return (ValMax+ValMin)/2.;
  return ValMin + Iso*(ValMax-ValMin)/(NbIso-1.) ;
}

double GiveValueFromIndex_Log(double ValMin, double ValMax, int NbIso, int Iso){
  if(NbIso==1) return (ValMax+ValMin)/2.;
  if(ValMin <= 0.) return 0. ;
  return pow(10.,log10(ValMin)+Iso*(log10(ValMax)-log10(ValMin))/(NbIso-1.)) ;
}


/* ------------------------------------------------------------------------
    Give Index From Value
   ------------------------------------------------------------------------ */

int GiveIndexFromValue_Lin(double ValMin, double ValMax, int NbIso, double Val){
  if(ValMin==ValMax) return NbIso/2 ;
  return (int)((Val-ValMin)*(NbIso-1)/(ValMax-ValMin)) ;
}

int GiveIndexFromValue_Log(double ValMin, double ValMax, int NbIso, double Val){
  if(ValMin==ValMax) return NbIso/2 ;  
  if(ValMin <= 0.) return 0 ;
  return (int)((log10(Val)-log10(ValMin))*(NbIso-1)/(log10(ValMax)-log10(ValMin))) ;
}


/* ------------------------------------------------------------------------
    Color Palette
   ------------------------------------------------------------------------ */

void Palette(Post_View *v, int nbi, int i){ /* i in [0,nbi-1] */
  int index ;

  index = (nbi==1) ? 
    v->CT.size/2 :
    (int) (i/(double)(nbi-1)*(v->CT.size-1)) ;

  glColor4ubv( (GLubyte *) &v->CT.table[index] );
}

void Palette2(Post_View *v,double min, double max, double val){ /* val in [min,max] */
  int index;  

  index = (int)( (val-min)/(max-min)*(v->CT.size-1) );

  glColor4ubv((GLubyte *) &v->CT.table[index]);
}

void RaiseFill(int i, double Val, double ValMin, double Raise[3][5]){
  int j ;
  for(j=0 ; j<3 ; j++) Raise[j][i] = (Val-ValMin) * RaiseFactor[j] ;
}


int fcmp_Post_Triangle(const void *a, const void *b){
  return 0;

#if 0
  Post_Triangle *PT;
  double bary1[3], bary2[3]; 

  /* sorting % eye with barycenters */

  z1 = (PT->X[1]+View->Offset[0]+Raise[0][1]) + (PT->X[0]+View->Offset[0]+Raise[0][0]); 
  y1y0 = (PT->Y[1]+View->Offset[1]+Raise[1][1]) - (PT->Y[0]+View->Offset[1]+Raise[1][0]);
  z1z0 = (PT->Z[1]+View->Offset[2]+Raise[2][1]) - (PT->Z[0]+View->Offset[2]+Raise[2][0]); 

  x2x0 = (PT->X[2]+View->Offset[0]+Raise[0][2]) - (PT->X[0]+View->Offset[0]+Raise[0][0]);
  y2y0 = (PT->Y[2]+View->Offset[1]+Raise[1][2]) - (PT->Y[0]+View->Offset[1]+Raise[1][0]); 
  z2z0 = (PT->Z[2]+View->Offset[2]+Raise[2][2]) - (PT->Z[0]+View->Offset[2]+Raise[2][0]);

  q = *(GL2PSprimitive**)a;
  w = *(GL2PSprimitive**)b;
  diff = q->depth - w->depth;
  if(diff > 0.) 
    return 1;
  else if(diff < 0.)
    return -1;
  else
    return 0;
#endif
}

/* ------------------------------------------------------------------------ 
    D r a w _ P o s t                                                       
   ------------------------------------------------------------------------ */

void Draw_Post (void) {
  int            i,j,k,n;
  double         ValMin,ValMax,AbsMax;
  Post_View     *v;

  if(!Post_ViewList) return;
  
  for(i=0 ; i<List_Nbr(Post_ViewList) ; i++){

    v = (Post_View*)List_Pointer(Post_ViewList,i);

    if(v->Visible){ 

      if(CTX.display_lists && !v->Changed && glIsList(v->Num)){

	glCallList(v->Num);

      }
      else{

	if(CTX.display_lists){
	  if(glIsList(v->Num)) glDeleteLists(v->Num,1);
	  // printf("new dl\n");
	  glNewList(v->Num, GL_COMPILE_AND_EXECUTE);
	}

	if(v->Light && v->IntervalsType != DRAW_POST_ISO){
	  InitShading();
	}
	else{
	  InitNoShading();
	}
	
	/* force this */
	if(v->IntervalsType == DRAW_POST_CONTINUOUS)
	  glShadeModel(GL_SMOOTH); 
	
	switch(v->RangeType){
	case DRAW_POST_DEFAULT : ValMin = v->Min ; ValMax = v->Max ; break;
	case DRAW_POST_CUSTOM  : ValMin = v->CustomMin ; ValMax = v->CustomMax ; break;
	}
	
	switch(v->ScaleType){
	case DRAW_POST_LINEAR : 
	  v->GIFV = GiveIndexFromValue_Lin ;
	  v->GVFI = GiveValueFromIndex_Lin ;
	  break;
	case DRAW_POST_LOGARITHMIC : 
	  v->GIFV = GiveIndexFromValue_Log ;
	  v->GVFI = GiveValueFromIndex_Log ;
	  break;
	}
	
	AbsMax = DMAX(fabs(ValMin),fabs(ValMax));
	AbsMax = (AbsMax==0.) ? 1. : AbsMax;
	
	for(j=0;j<3;j++){
	  RaiseFactor[j] = v->Raise[j] / AbsMax ;
	  for(k=0;k<5;k++) Raise[j][k] = 0. ;
	}
	
	if((n = List_Nbr(v->Simplices)))
	  for(j=0 ; j<n ; j++)
	    Draw_Post_Simplex(v, (Post_Simplex*)List_Pointer(v->Simplices,j), 
			      ValMin, ValMax, Raise);
	
	if((n = List_Nbr(v->Triangles))){
	  //if(there is alpha)List_Sort(v->Triangles, fcmp_Post_Triangle);
	  for(j=0 ; j<n ; j++)
	    Draw_Post_Triangle(v, (Post_Triangle*)List_Pointer(v->Triangles,j), 
			       ValMin, ValMax, Raise);
	}
	
	if((n = List_Nbr(v->Lines)))
	  for(j=0 ; j<n ; j++)
	    Draw_Post_Line(v, (Post_Line*)List_Pointer(v->Lines,j), 
			   ValMin, ValMax, Raise);
	
	if((n = List_Nbr(v->Points)))
	  for(j=0 ; j<n ; j++)
	    Draw_Post_Point(v, (Post_Point*)List_Pointer(v->Points,j), 
			    ValMin, ValMax, Raise);
	
	if(CTX.display_lists){
	  glEndList();
	  v->Changed=0;
	}
	
      }
      
    }

  }

  /* revenir au shading par defaut, pour l'echelle */
  InitNoShading();

}

