
#include "Gmsh.h"
#include "GmshUI.h"
#include "Geo.h"
#include "Verif.h"
#include "Mesh.h"
#include "Draw.h"
#include "Widgets.h"
#include "Pixmaps.h"
#include "Context.h"
#include "XContext.h"
#include "Register.h"

#include "CbGeneral.h"
#include "CbOptions.h"
#include "CbGeom.h"
#include "CbMesh.h"
#include "CbPost.h"

#include <sys/time.h>
#include <unistd.h>

extern Context_T  CTX;
extern XContext_T XCTX ;
extern Widgets_T  WID;
extern Pixmaps_T  PIX;
extern Mesh       M;
extern Tree_T    *EntitesVisibles;
extern double   LC;
extern int      ACTUAL_ENTITY, SHOW_ALL;

static int stop_anim ;
static long anim_time ;

/* ------------------------------------------------------------------------ 
    O p t i o n s C b                                                       
   ------------------------------------------------------------------------ */

long Get_AnimTime(){
  struct timeval tp;
  gettimeofday(&tp, (struct timezone *) 0);
  return (long)tp.tv_sec * 1000000 + (long)tp.tv_usec;
}

void OptionsCb (Widget w, XtPointer client_data, XtPointer call_data){
  int                i, e;
  char              *c, label[32];
  XWindowAttributes  xattrib;
  XEvent             event;
  
  switch((long int)client_data){

    /* globales */
    
  case OPTIONS_REPLOT        : Init(); Draw(); break;
  case OPTIONS_AXES          : CTX.axes = !CTX.axes; break;
  case OPTIONS_LITTLE_AXES   : CTX.little_axes = !CTX.little_axes; break;
  case OPTIONS_FAST_REDRAW   : CTX.fast = !CTX.fast ; break ;
  case OPTIONS_DISPLAY_LISTS : CTX.display_lists = !CTX.display_lists ; break ;
  case OPTIONS_ALPHA_BLENDING: CTX.alpha = !CTX.alpha; break;
  case OPTIONS_COLOR_SCHEME_SCALE: 
    XmScaleGetValue(WID.OD.miscColorSchemeScale, &e); InitColors(&CTX.color, e);
    Init(); Draw();
    break ;
  case OPTIONS_ORTHOGRAPHIC  : CTX.ortho = 1; break;
  case OPTIONS_PERSPECTIVE   : CTX.ortho = 0; break;
  case OPTIONS_LIGHT_X_SCALE : 
    XmScaleGetValue(WID.OD.miscLightScale[0], &e); CTX.light0[0] = 0.04*e ;
    MarkAllViewsChanged (0); break ;
  case OPTIONS_LIGHT_Y_SCALE : 
    XmScaleGetValue(WID.OD.miscLightScale[1], &e); CTX.light0[1] = 0.04*e ; 
    MarkAllViewsChanged (0); break ;
  case OPTIONS_LIGHT_Z_SCALE : 
    XmScaleGetValue(WID.OD.miscLightScale[2], &e); CTX.light0[2] = 0.04*e ; 
    MarkAllViewsChanged (0);break ;
  case OPTIONS_SHINE_SCALE   :
    XmScaleGetValue(WID.OD.miscShineScale, &e); CTX.shine = 0.04*e ; 
    MarkAllViewsChanged (0);break ;
  case OPTIONS_SCALEX        : CTX.s[0] = (GLdouble)atof(XmTextGetString(w)); break;
  case OPTIONS_SCALEY        : CTX.s[1] = (GLdouble)atof(XmTextGetString(w)); break;
  case OPTIONS_SCALEZ        : CTX.s[2] = (GLdouble)atof(XmTextGetString(w)); break;
  case OPTIONS_TRANX         : CTX.t[0] = (GLdouble)atof(XmTextGetString(w)); break;
  case OPTIONS_TRANY         : CTX.t[1] = (GLdouble)atof(XmTextGetString(w)); break;
  case OPTIONS_TRANZ         : CTX.t[2] = (GLdouble)atof(XmTextGetString(w)); break;
  case OPTIONS_ROTX          : CTX.r[0] = (GLdouble)atof(XmTextGetString(w)); break;
  case OPTIONS_ROTY          : CTX.r[1] = (GLdouble)atof(XmTextGetString(w)); break;
  case OPTIONS_ROTZ          : CTX.r[2] = (GLdouble)atof(XmTextGetString(w)); break;
  case OPTIONS_ROTX_LOCKED   : CTX.rlock[0] = !CTX.rlock[0];  break;
  case OPTIONS_ROTY_LOCKED   : CTX.rlock[1] = !CTX.rlock[1];  break;
  case OPTIONS_ROTZ_LOCKED   : CTX.rlock[2] = !CTX.rlock[2];  break;
  case OPTIONS_TRANX_LOCKED  : CTX.tlock[0] = !CTX.tlock[0];  break;
  case OPTIONS_TRANY_LOCKED  : CTX.tlock[1] = !CTX.tlock[1];  break;
  case OPTIONS_TRANZ_LOCKED  : CTX.tlock[2] = !CTX.tlock[2];  break;
  case OPTIONS_SCALEX_LOCKED : CTX.slock[0] = !CTX.slock[0];  break;
  case OPTIONS_SCALEY_LOCKED : CTX.slock[1] = !CTX.slock[1];  break;
  case OPTIONS_SCALEZ_LOCKED : CTX.slock[2] = !CTX.slock[2];  break;
  case OPTIONS_XVIEW : set_r(0,0.);  set_r(1,90.);set_r(2,0.); Init(); Draw(); break;
  case OPTIONS_YVIEW : set_r(0,-90.);set_r(1,0.); set_r(2,0.); Init(); Draw(); break;
  case OPTIONS_ZVIEW : set_r(0,0.);  set_r(1,0.); set_r(2,0.); Init(); Draw(); break;
  case OPTIONS_CVIEW : set_t(0,0.);  set_t(1,0.); set_t(2,0.); 
                       set_s(0,1.);  set_s(1,1.); set_s(2,1.); Init(); Draw(); break;
  case OPTIONS_PVIEW :
    XGetWindowAttributes(XtDisplay(WID.G.shell),XtWindow(WID.G.shell),&xattrib);
    fprintf(stderr, "-geometry %dx%d -viewport %g %g %g %g %g %g %g %g %g\n",
	    xattrib.width, xattrib.height,
	    CTX.r[0],CTX.r[1],CTX.r[2],
	    CTX.t[0],CTX.t[1],CTX.t[2],
	    CTX.s[0],CTX.s[1],CTX.s[2]);
    break ;

    /* print */

  case OPTIONS_PRINT_XDUMP        : CTX.print.type = XDUMP; CTX.print.format = FORMAT_XPM; break;
  case OPTIONS_PRINT_GIF          : CTX.print.type = GIF; CTX.print.format = FORMAT_GIF; break;
  case OPTIONS_PRINT_GLPPAINTER   : CTX.print.type = GLPPAINTER; CTX.print.format = FORMAT_EPS; break;
  case OPTIONS_PRINT_GLPRECURSIVE : CTX.print.type = GLPRECURSIVE; CTX.print.format = FORMAT_EPS; break;
  case OPTIONS_PRINT_GLPIMAGE     : CTX.print.type = GLPIMAGE; CTX.print.format = FORMAT_EPS; break;
  case OPTIONS_PRINT_GL2PS_SIMPLE : CTX.print.type = GLPRPAINTER; CTX.print.format = FORMAT_EPS;break;
  case OPTIONS_PRINT_GL2PS_COMPLEX: CTX.print.type = GLPRRECURSIVE; CTX.print.format = FORMAT_EPS;break;
  case OPTIONS_PRINT_GL2PS_IMAGE  : CTX.print.type = XDUMP; CTX.print.format = FORMAT_EPS;break;

    /* geometrie */

  case OPTIONS_GEOM_CHECK      : /* Print_Geo(&M,filename); */ break;    
  case OPTIONS_GEOM_VISIBILITY_ENTITY : 
    CTX.geom.vis_type = 0; 
    XtVaSetValues(WID.OD.geomVisibleButt[0], XmNset, CTX.geom.points?True:False, NULL);
    XtVaSetValues(WID.OD.geomVisibleButt[1], XmNset, CTX.geom.lines?True:False, NULL); 
    XtVaSetValues(WID.OD.geomVisibleButt[2], XmNset, CTX.geom.surfaces?True:False, NULL);
    XtVaSetValues(WID.OD.geomVisibleButt[3], XmNset, CTX.geom.volumes?True:False, NULL);
    for(i=0;i<4;i++)XmUpdateDisplay(WID.OD.geomVisibleButt[i]);
    break;
  case OPTIONS_GEOM_VISIBILITY_NUMBER : 
    CTX.geom.vis_type = 1; 
    XtVaSetValues(WID.OD.geomVisibleButt[0], XmNset, CTX.geom.points_num?True:False, NULL);
    XtVaSetValues(WID.OD.geomVisibleButt[1], XmNset, CTX.geom.lines_num?True:False, NULL); 
    XtVaSetValues(WID.OD.geomVisibleButt[2], XmNset, CTX.geom.surfaces_num?True:False, NULL);
    XtVaSetValues(WID.OD.geomVisibleButt[3], XmNset, CTX.geom.volumes_num?True:False, NULL);
    for(i=0;i<4;i++)XmUpdateDisplay(WID.OD.geomVisibleButt[i]);
    break;
  case OPTIONS_GEOM_POINTS     : 
    if(!CTX.geom.vis_type) CTX.geom.points = !CTX.geom.points; 
    else                   CTX.geom.points_num = !CTX.geom.points_num; break; 
  case OPTIONS_GEOM_LINES      :
    if(!CTX.geom.vis_type) CTX.geom.lines = !CTX.geom.lines;
    else                   CTX.geom.lines_num = !CTX.geom.lines_num; break; 
  case OPTIONS_GEOM_SURFACES   :
    if(!CTX.geom.vis_type) CTX.geom.surfaces = !CTX.geom.surfaces;
    else                   CTX.geom.surfaces_num = !CTX.geom.surfaces_num; break; 
  case OPTIONS_GEOM_VOLUMES    :
    if(!CTX.geom.vis_type) CTX.geom.volumes = !CTX.geom.volumes;
    else                   CTX.geom.volumes_num = !CTX.geom.volumes_num; break; 
  case OPTIONS_GEOM_NORMALS_SCALE : 
    XmScaleGetValue(WID.OD.geomNormalsScale, &e); CTX.geom.normals = e ;  
    sprintf(label,"%g",CTX.geom.normals);   
    XtVaSetValues(WID.OD.geomNormalsText, XmNvalue, label, NULL);
    XmUpdateDisplay(WID.OD.geomNormalsText); break;
  case OPTIONS_GEOM_NORMALS_TEXT : 
    CTX.geom.normals = atof(XmTextGetString(w));
    XtVaSetValues(WID.OD.geomNormalsScale, XmNvalue, 
		  THRESHOLD((int)CTX.geom.normals,0,100), NULL);
    XmUpdateDisplay(WID.OD.geomNormalsScale); break;
  case OPTIONS_GEOM_TANGENTS_SCALE : 
    XmScaleGetValue(WID.OD.geomTangentsScale, &e); CTX.geom.tangents = e ;  
    sprintf(label,"%g",CTX.geom.tangents);   
    XtVaSetValues(WID.OD.geomTangentsText, XmNvalue, label, NULL);
    XmUpdateDisplay(WID.OD.geomTangentsText); break;
  case OPTIONS_GEOM_TANGENTS_TEXT : 
    CTX.geom.tangents = atof(XmTextGetString(w));
    XtVaSetValues(WID.OD.geomTangentsScale, XmNvalue,
		  THRESHOLD((int)CTX.geom.tangents,0,100), NULL);
    XmUpdateDisplay(WID.OD.geomTangentsScale); break;
    

    /* mesh */

  case OPTIONS_MESH_SHADING      : CTX.mesh.hidden = 1; CTX.mesh.shade = 1; break; 
  case OPTIONS_MESH_HIDDEN_LINES : CTX.mesh.hidden = 1; CTX.mesh.shade = 0; break; 
  case OPTIONS_MESH_WIREFRAME    : CTX.mesh.hidden = 0; CTX.mesh.shade = 0; break; 
  case OPTIONS_MESH_VISIBILITY_ENTITY : 
    CTX.mesh.vis_type = 0; 
    XtVaSetValues(WID.OD.meshVisibleButt[0], XmNset, CTX.mesh.points?True:False, NULL);
    XtVaSetValues(WID.OD.meshVisibleButt[1], XmNset, CTX.mesh.lines?True:False, NULL); 
    XtVaSetValues(WID.OD.meshVisibleButt[2], XmNset, CTX.mesh.surfaces?True:False, NULL);
    XtVaSetValues(WID.OD.meshVisibleButt[3], XmNset, CTX.mesh.volumes?True:False, NULL);
    for(i=0;i<4;i++)XmUpdateDisplay(WID.OD.meshVisibleButt[i]);
    break;
  case OPTIONS_MESH_VISIBILITY_NUMBER : 
    CTX.mesh.vis_type = 1; 
    XtVaSetValues(WID.OD.meshVisibleButt[0], XmNset, CTX.mesh.points_num?True:False, NULL);
    XtVaSetValues(WID.OD.meshVisibleButt[1], XmNset, CTX.mesh.lines_num?True:False, NULL); 
    XtVaSetValues(WID.OD.meshVisibleButt[2], XmNset, CTX.mesh.surfaces_num?True:False, NULL);
    XtVaSetValues(WID.OD.meshVisibleButt[3], XmNset, CTX.mesh.volumes_num?True:False, NULL);
    for(i=0;i<4;i++)XmUpdateDisplay(WID.OD.meshVisibleButt[i]);
    break;
  case OPTIONS_MESH_POINTS     :
    if(!CTX.mesh.vis_type) CTX.mesh.points = !CTX.mesh.points; 
    else                   CTX.mesh.points_num = !CTX.mesh.points_num; break; 
  case OPTIONS_MESH_LINES      :
    if(!CTX.mesh.vis_type) CTX.mesh.lines = !CTX.mesh.lines;
    else                   CTX.mesh.lines_num = !CTX.mesh.lines_num; break; 
  case OPTIONS_MESH_SURFACES   :
    if(!CTX.mesh.vis_type) CTX.mesh.surfaces = !CTX.mesh.surfaces;
    else                   CTX.mesh.surfaces_num = !CTX.mesh.surfaces_num; break; 
  case OPTIONS_MESH_VOLUMES    :
    if(!CTX.mesh.vis_type) CTX.mesh.volumes = !CTX.mesh.volumes;
    else                   CTX.mesh.volumes_num = !CTX.mesh.volumes_num; break; 
  case OPTIONS_MESH_FORMAT_MSH   : CTX.mesh.format = FORMAT_MSH ; break ;
  case OPTIONS_MESH_FORMAT_UNV   : CTX.mesh.format = FORMAT_UNV ; break ;
  case OPTIONS_MESH_FORMAT_GREF  : CTX.mesh.format = FORMAT_GREF ; break ;
  case OPTIONS_MESH_DEGRE2       : 
    (CTX.mesh.degree==2) ? CTX.mesh.degree=1 : CTX.mesh.degree=2; break ;
  case OPTIONS_MESH_ANISOTROPIC  : 
    (CTX.mesh.algo==DELAUNAY_OLDALGO) ?
      CTX.mesh.algo=DELAUNAY_NEWALGO :
	CTX.mesh.algo=DELAUNAY_OLDALGO; break ;
  case OPTIONS_MESH_SMOOTHING_SCALE : 
    XmScaleGetValue(WID.OD.meshSmoothingScale, &e); CTX.mesh.nb_smoothing = e ;  
    sprintf(label,"%d",CTX.mesh.nb_smoothing);   
    XtVaSetValues(WID.OD.meshSmoothingText, XmNvalue, label, NULL);
    XmUpdateDisplay(WID.OD.meshSmoothingText); break;
  case OPTIONS_MESH_SMOOTHING_TEXT : 
    CTX.mesh.nb_smoothing = atoi(XmTextGetString(w));
    XtVaSetValues(WID.OD.meshSmoothingScale, XmNvalue, 
		  THRESHOLD(CTX.mesh.nb_smoothing,0,100), NULL);
    XmUpdateDisplay(WID.OD.meshSmoothingScale); break;
  case OPTIONS_MESH_EXPLODE_SCALE : 
    XmScaleGetValue(WID.OD.meshExplodeScale, &e); CTX.mesh.explode = 0.01*e ;  
    sprintf(label,"%g",CTX.mesh.explode);   
    XtVaSetValues(WID.OD.meshExplodeText, XmNvalue, label, NULL);
    XmUpdateDisplay(WID.OD.meshExplodeText); break;
  case OPTIONS_MESH_EXPLODE_TEXT : 
    CTX.mesh.explode = atof(XmTextGetString(w));
    XtVaSetValues(WID.OD.meshExplodeScale, XmNvalue,
		  THRESHOLD((int)(100*CTX.mesh.explode),0,100), NULL);
    XmUpdateDisplay(WID.OD.meshExplodeScale); break;
  case OPTIONS_MESH_NORMALS_SCALE : 
    XmScaleGetValue(WID.OD.meshNormalsScale, &e); CTX.mesh.normals = e ;  
    sprintf(label,"%g",CTX.mesh.normals);   
    XtVaSetValues(WID.OD.meshNormalsText, XmNvalue, label, NULL);
    XmUpdateDisplay(WID.OD.meshNormalsText); break;
  case OPTIONS_MESH_NORMALS_TEXT : 
    CTX.mesh.normals = atof(XmTextGetString(w));
    XtVaSetValues(WID.OD.meshNormalsScale, XmNvalue,
		  THRESHOLD((int)CTX.mesh.normals,0,100), NULL);
    XmUpdateDisplay(WID.OD.meshNormalsScale); break;
  case OPTIONS_MESH_ABORT : 
    CancelMeshThread();
    break;

    /* post */
    
  case OPTIONS_POST_LINK_NONE    : CTX.post.link = 0; break; 
  case OPTIONS_POST_LINK_VISIBLE : CTX.post.link = 1; break; 
  case OPTIONS_POST_LINK_ALL     : CTX.post.link = 2; break; 
  case OPTIONS_POST_ANIM_START: 
    stop_anim = 0 ;
    Set_AnimPixmap(&WID, &PIX, 0) ;
    Set_AnimCallback(&WID, 0) ;
    anim_time = Get_AnimTime();
    while(1){
      if(XtAppPending(XCTX.AppContext)){
	XtAppNextEvent(XCTX.AppContext,&event);
	XtDispatchEvent(&event);
	if(stop_anim) break ;
      }
      else{
	if(Get_AnimTime() - anim_time > CTX.post.anim_delay){
	  anim_time = Get_AnimTime();
	  MarkAllViewsChanged(2);
	  Init(); Draw();
	}
      }
    }
    break ;
  case OPTIONS_POST_ANIM_STOP: 
    stop_anim = 1;
    Set_AnimPixmap(&WID, &PIX, 1) ;
    Set_AnimCallback(&WID, 1) ;
    break ;
  case OPTIONS_POST_ANIM_DELAY: 
    XmScaleGetValue(WID.OD.postAnimScale, &e);
    CTX.post.anim_delay = (long)(1.e5*e) ; 
    break ;

    /* mesh + geom : a changer...*/
  case OPTIONS_GEOM_HIDE_SHOW :  
  case OPTIONS_MESH_HIDE_SHOW :  
    c = XmTextGetString(w); 
    if (!strcmp(c,"all") || !strcmp(c,"*")){
      if(SHOW_ALL){ 
	RemplirEntitesVisibles(0); SHOW_ALL = 0; 
      }
      else { 
	RemplirEntitesVisibles(1); SHOW_ALL = 1; 
      }
    }
    else{
      ACTUAL_ENTITY = atoi(c);
      if(EntiteEstElleVisible(ACTUAL_ENTITY))
	ToutesLesEntitesRelatives(ACTUAL_ENTITY,EntitesVisibles,0);
      else
	ToutesLesEntitesRelatives(ACTUAL_ENTITY,EntitesVisibles,1);
    }
    break;

  default :
    Msg(WARNING, "Unknown value in OptionsCb : %d", (long int)client_data); 
    break;
  }

}

