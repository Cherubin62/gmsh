
#include <unistd.h>

#include "Gmsh.h"
#include "GmshUI.h"
#include "Main.h"
#include "Mesh.h"
#include "Draw.h"
#include "Widgets.h"
#include "Context.h"
#include "ColorTable.h"
#include "XContext.h"

#include "CbFile.h"
#include "CbColorbar.h"

#include "XDump.h"
#include "gl2ps.h"
#include "gl2gif.h"

extern Context_T   CTX;
extern XContext_T  XCTX;
extern Widgets_T   WID;
extern Mesh        M;
extern int         WARNING_OVERRIDE;

/* ------------------------------------------------------------------------ */
/*  C r e a t e I m a g e                                                   */
/* ------------------------------------------------------------------------ */

static char KeepFileName[256];

void SaveToDisk (char *FileName, Widget warning, 
		 void (*function)(FILE *file)){
  FILE    *fp ;

  if(!WARNING_OVERRIDE){
    fp = fopen(FileName,"r");
    if(fp) {      
      XtManageChild(warning);
      WARNING_OVERRIDE = 1;
      strcpy(KeepFileName,FileName);
      fclose(fp);
      return;
    }
    else{
      strcpy(KeepFileName,FileName);
    }
  }

  if(!(fp = fopen(KeepFileName,"w"))) {
    Msg(WARNING, "Unable to open file '%s'", KeepFileName); 
    WARNING_OVERRIDE = 0;
    return;
  }

  function(fp);

  fclose(fp);

  WARNING_OVERRIDE = 0;
}

void CreateImage (FILE *fp) {
  FILE    *tmp;
  GLint    size3d;
  char     cmd[1000];
  char     *tmpFileName="tmp.xwd";
  int      res;

  switch(CTX.print.type){
    
  case XDUMP :    
    switch(CTX.print.format){
    case FORMAT_XPM :
      Window_Dump(XCTX.display, XCTX.scrnum, XtWindow(WID.G.glw), fp);    
      break;
    case FORMAT_PS :
    case FORMAT_EPS :
      tmp = fopen(tmpFileName,"w");
      Window_Dump(XCTX.display, XCTX.scrnum, XtWindow(WID.G.glw), tmp);
      fclose(tmp);
      sprintf(cmd, "xpr -device ps -gray 4 %s >%s", tmpFileName, KeepFileName);
      Msg(INFOS, "Executing: %s\n", cmd);
      system(cmd);
      unlink(tmpFileName);
      break;
    }
    Msg(INFOS, "X image dump complete: '%s'", KeepFileName);
    break ;

  case GIF :
    create_gif(fp, CTX.viewport[2]-CTX.viewport[0],
	       CTX.viewport[3]-CTX.viewport[1]);
    Msg(INFOS, "GIF dump complete: '%s'", KeepFileName);
    break;

  case GLPRPAINTER :
  case GLPRRECURSIVE :
    size3d = 0 ;
    res = GL2PS_OVERFLOW ;
    while(res == GL2PS_OVERFLOW){
      size3d += 1024*1024 ;
      gl2psBeginPage(TheBaseFileName, "Gmsh", 
		     (CTX.print.type == GLPRPAINTER ? 
		      GL2PS_SIMPLE_SORT : GL2PS_BSP_SORT),
		     GL2PS_SIMPLE_LINE_OFFSET | GL2PS_DRAW_BACKGROUND,
		     GL_RGBA, 0, NULL, size3d, fp);
      CTX.stream = TO_FILE ;
      Init();
      Draw();
      CTX.stream = TO_SCREEN ;
      res = gl2psEndPage();
    }
    Msg(INFOS, "GL2PS postscript output complete: '%s'", KeepFileName);
    break;

  default :
    Msg(WARNING, "Unknown print type");
    break;
  }

}

/* ------------------------------------------------------------------------ */
/*  file                                                                    */
/* ------------------------------------------------------------------------ */

void SaveColorTable(FILE *fp);

void FileCb(Widget w, XtPointer client_data, XtPointer call_data){
  char      *c;
  XmString  xms;
  
  if((long int)client_data == FILE_SAVE_MESH){
    Print_Mesh(&M, NULL, CTX.mesh.format); 
    return;
  }

  XtVaGetValues(w, XmNtextString, &xms, NULL);
  XmStringGetLtoR(xms, XmSTRING_DEFAULT_CHARSET, &c);
  XmStringFree(xms);
  
  switch ((long int)client_data) {
  case FILE_LOAD_GEOM          : OpenProblem(c); Init(); Draw(); break;
  case FILE_LOAD_POST          : MergeProblem(c); ColorBarRedraw(); Init(); Draw(); break;
  case FILE_SAVE_MESH_AS       : Print_Mesh(&M, c, CTX.mesh.format); break;
  case FILE_SAVE_COLORTABLE_AS : SaveToDisk(c, WID.ED.saveDialog, SaveColorTable); break;
  case FILE_CANCEL             : WARNING_OVERRIDE = 0; break;
  case FILE_PRINT              : SaveToDisk(c, WID.ED.printDialog, CreateImage); Init(); Draw(); break;
  default :
    Msg(WARNING, "Unknown event in FileCb : %d", (long int)client_data); 
    break;
  }

}

