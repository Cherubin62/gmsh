// $Id: CommandLine.cpp,v 1.7 2003-02-12 20:27:12 geuzaine Exp $
//
// Copyright (C) 1997 - 2003 C. Geuzaine, J.-F. Remacle
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
// Please report all bugs and problems to "gmsh@geuz.org".

#include <unistd.h>
#include "Gmsh.h"
#include "GmshUI.h"
#include "GmshVersion.h"
#include "Numeric.h"
#include "Context.h"
#include "Options.h"
#include "Geo.h"
#include "Mesh.h"
#include "Views.h"
#include "OpenFile.h"
#include "Parser.h"

#if !defined(GMSH_MAJOR_VERSION)
#error 
#error Common/GmshVersion.h is not up-to-date. 
#error Please run 'make tag'.
#error
#endif

extern Context_T  CTX;

char  *TheFileNameTab[MAX_OPEN_FILES];
char  *TheBgmFileName=NULL, *TheOptString=NULL;

char gmsh_progname[]  = "This is Gmsh" ;
char gmsh_copyright[] = "Copyright (C) 1997-2003 Jean-Francois Remacle and Christophe Geuzaine";
char gmsh_version[]   = "Version        : " ;
char gmsh_os[]        = "Build OS       : " GMSH_OS ;
char gmsh_gui[]       = "GUI toolkit    : " ;
char gmsh_date[]      = "Build date     : " GMSH_DATE ;
char gmsh_host[]      = "Build host     : " GMSH_HOST ;
char gmsh_packager[]  = "Packager       : " GMSH_PACKAGER ;
char gmsh_url[]       = "Web site       : http://www.geuz.org/gmsh/" ;
char gmsh_email[]     = "Mailing list   : gmsh@geuz.org" ;

void Print_Usage(char *name){
  Msg(DIRECT, "Usage: %s [options] [files]", name);
  Msg(DIRECT, "Geometry options:");
  Msg(DIRECT, "  -0                    parse input files, output unrolled geometry, and exit");
  Msg(DIRECT, "Mesh options:");
  Msg(DIRECT, "  -1, -2, -3            perform batch 1D, 2D and 3D mesh generation");
  Msg(DIRECT, "  -saveall              save all elements (discard physical group definitions)");
  Msg(DIRECT, "  -o file               specify mesh output file name");
  Msg(DIRECT, "  -format msh|unv|gref  set output mesh format (default: msh)");
  Msg(DIRECT, "  -algo iso|tri|aniso   select 2D mesh algorithm (default: iso)");
  Msg(DIRECT, "  -smooth int           set mesh smoothing (default: 0)");
  //  Msg(DIRECT, "  -degree int           set mesh degree (default: 1)");
  Msg(DIRECT, "  -scale float          set global scaling factor (default: 1.0)");
  Msg(DIRECT, "  -meshscale float      set mesh scaling factor (default: 1.0)");
  Msg(DIRECT, "  -clscale float        set characteristic length scaling factor (default: 1.0)");
  Msg(DIRECT, "  -rand float           set random perturbation factor (default: 1.e-4)");
  Msg(DIRECT, "  -bgm file             load backround mesh from file");
  Msg(DIRECT, "  -constrain            constrain background mesh with characteristic lengths");
  Msg(DIRECT, "  -histogram            print mesh quality histogram");
  Msg(DIRECT, "  -extrude              use old extrusion mesh generator");
  Msg(DIRECT, "  -recombine            recombine meshes from old extrusion mesh generator");
#if defined(HAVE_FLTK)
  Msg(DIRECT, "  -interactive          display 2D mesh construction interactively");
  Msg(DIRECT, "Post-processing options:");
  Msg(DIRECT, "  -dl                   enable display lists");
  Msg(DIRECT, "  -noview               hide all views on startup");
  Msg(DIRECT, "  -link int             select link mode between views (default: 0)");
  Msg(DIRECT, "  -smoothview           smooth views");
  Msg(DIRECT, "  -convert file file    convert an ascii view into a binary one");
  Msg(DIRECT, "Display options:");    
  Msg(DIRECT, "  -nodb                 disable double buffering");
  Msg(DIRECT, "  -fontsize int         specify the font size for the GUI (default: 12)");
  Msg(DIRECT, "  -theme string         specify GUI theme");
  Msg(DIRECT, "  -alpha                enable alpha blending");
  Msg(DIRECT, "  -notrack              don't use trackball mode for rotations");
  Msg(DIRECT, "  -display string       specify display");
  Msg(DIRECT, "  -perspective          set projection mode to perspective");
#endif
  Msg(DIRECT, "Other options:");      
#if defined(HAVE_FLTK)
  Msg(DIRECT, "  -a, -g, -m, -s, -p    start in automatic, geometry, mesh, solver or");
  Msg(DIRECT, "                        post-processing mode (default: automatic)");
#endif
  Msg(DIRECT, "  -v int                set verbosity level (default: 2)");
  Msg(DIRECT, "  -string \"string\"      parse string before project file");
  Msg(DIRECT, "  -option file          parse option file before GUI creation");
  Msg(DIRECT, "  -version              show version number");
  Msg(DIRECT, "  -info                 show detailed version information");
  Msg(DIRECT, "  -help                 show this message");
}


void Get_Options (int argc, char *argv[], int *nbfiles) {
  int i=1;

  // Parse session and option files

  InitSymbols(); //this symbol context is local to option parsing (the
                 //symbols will not interfere with subsequent OpenFiles)

  ParseFile(CTX.sessionrc_filename,1);
  ParseFile(CTX.optionsrc_filename,1);

  // Get command line options

  TheFileNameTab[0] = CTX.default_filename ;
  *nbfiles = 0;
  
  while (i < argc) {
    
    if (argv[i][0] == '-') {
      
      if(!strcmp(argv[i]+1, "string")){
	i++;
        if(argv[i]!=NULL) TheOptString = argv[i++];
	else{
          fprintf(stderr, ERROR_STR "Missing string\n");
          exit(1);
	}
      }
      else if(!strcmp(argv[i]+1, "a")){ 
        CTX.initial_context = 0; i++;
      }
      else if(!strcmp(argv[i]+1, "g")){ 
        CTX.initial_context = 1; i++;
      }
      else if(!strcmp(argv[i]+1, "m")){ 
        CTX.initial_context = 2; i++;
      }
      else if(!strcmp(argv[i]+1, "s")){ 
        CTX.initial_context = 3; i++;
      }
      else if(!strcmp(argv[i]+1, "p")){ 
        CTX.initial_context = 4; i++;
      }
      else if(!strcmp(argv[i]+1, "0")){ 
        CTX.batch = -1; i++;
      }
      else if(!strcmp(argv[i]+1, "1")){ 
        CTX.batch = 1; i++;
      }
      else if(!strcmp(argv[i]+1, "2")){ 
        CTX.batch = 2; i++;
      }
      else if(!strcmp(argv[i]+1, "3")){ 
        CTX.batch = 3; i++;
      }
      else if(!strcmp(argv[i]+1, "saveall")){ 
        CTX.mesh.save_all = 1; i++;
      }
      else if(!strcmp(argv[i]+1, "extrude")){ //old extrusion mesh generator
        CTX.mesh.oldxtrude = 1; i++;
      }
      else if(!strcmp(argv[i]+1, "recombine")){ //old extrusion mesh generator
        CTX.mesh.oldxtrude_recombine = 1; i++;
      }
      else if(!strcmp(argv[i]+1, "dupli")){
        CTX.mesh.check_duplicates = 1; i++;
      }
      else if(!strcmp(argv[i]+1, "histogram")){ 
        CTX.mesh.histogram = 1; i++;
      }
      else if(!strcmp(argv[i]+1, "option")){ 
        i++;
        if(argv[i] != NULL) ParseFile(argv[i++],1);
        else {    
          fprintf(stderr, ERROR_STR "Missing file name\n");
          exit(1);
        }
      }
      else if(!strcmp(argv[i]+1, "o")){ 
        i++;
        if(argv[i] != NULL) CTX.output_filename = argv[i++];
        else {    
          fprintf(stderr, ERROR_STR "Missing file name\n");
          exit(1);
        }
      }
      else if(!strcmp(argv[i]+1, "bgm")){ 
        i++;
        if(argv[i] != NULL) TheBgmFileName = argv[i++];
        else {    
          fprintf(stderr, ERROR_STR "Missing file name\n");
          exit(1);
        }
      }
      else if(!strcmp(argv[i]+1, "constrain")){ 
	CTX.mesh.constrained_bgmesh = 1; i++;
      }
      else if(!strcmp(argv[i]+1, "convert")){ 
	i++;
	CTX.terminal = 1;
	if(argv[i] && argv[i+1]){
	  ParseFile(argv[i],0);
	  if(List_Nbr(CTX.post.list))
	    WriteView(1,(Post_View*)List_Pointer(CTX.post.list, 0),argv[i+1]);
	  else
	    fprintf(stderr, ERROR_STR "No view to convert\n");
	}
	else
	  fprintf(stderr, "Usage: %s -convert view.ascii view.binary\n", argv[0]);
	exit(1);
      }
      else if(!strcmp(argv[i]+1, "old")){ 
        CTX.geom.old_circle = 1; i++;
      }
      else if(!strcmp(argv[i]+1, "initial")){
        i++;
        if(argv[i]!=NULL) CTX.mesh.initial_only = atoi(argv[i++]);
        else {    
          fprintf(stderr, ERROR_STR "Missing number\n");
          exit(1);
        }
      }
      else if(!strcmp(argv[i]+1, "quality")){
        i++;
        if(argv[i]!=NULL) CTX.mesh.quality = atof(argv[i++]);
        else {    
          fprintf(stderr, ERROR_STR "Missing number\n");
          exit(1);
        }
      }
      else if(!strcmp(argv[i]+1, "scale")){
        i++;
        if(argv[i]!=NULL) CTX.geom.scaling_factor = atof(argv[i++]);
        else {    
          fprintf(stderr, ERROR_STR "Missing number\n");
          exit(1);
        }
      }
      else if(!strcmp(argv[i]+1, "meshscale")){
        i++;
        if(argv[i]!=NULL) CTX.mesh.scaling_factor = atof(argv[i++]);
        else {    
          fprintf(stderr, ERROR_STR "Missing number\n");
          exit(1);
        }
      }
      else if(!strcmp(argv[i]+1, "rand")){
        i++;
        if(argv[i]!=NULL) CTX.mesh.rand_factor = atof(argv[i++]);
        else {    
          fprintf(stderr, ERROR_STR "Missing number\n");
          exit(1);
        }
      }
      else if(!strcmp(argv[i]+1, "clscale")){
        i++;
        if(argv[i]!=NULL){
          CTX.mesh.lc_factor = atof(argv[i++]);
          if(CTX.mesh.lc_factor <= 0.0){
            fprintf(stderr, ERROR_STR 
                    "Characteristic length factor must be > 0\n");
            exit(1);
          }
        }
        else {    
          fprintf(stderr, ERROR_STR "Missing number\n");
          exit(1);
        }
      }
      else if(!strcmp(argv[i]+1, "smooth")){ 
        i++;
        if(argv[i]!=NULL) CTX.mesh.nb_smoothing = atoi(argv[i++]);
        else{
          fprintf(stderr, ERROR_STR "Missing number\n");
          exit(1);
        }
      }
      else if(!strcmp(argv[i]+1, "degree")){  
        i++;
        if(argv[i]!=NULL)
          opt_mesh_degree(0, GMSH_SET, atof(argv[i++]));
        else {    
          fprintf(stderr, ERROR_STR "Missing number\n");
          exit(1);
        }
      }
      else if(!strcmp(argv[i]+1, "format") ||  
              !strcmp(argv[i]+1, "f")){  
        i++;
        if(argv[i]!=NULL){
          if(!strcmp(argv[i],"msh") || 
             !strcmp(argv[i],"MSH") || 
             !strcmp(argv[i],"gmsh")){
            CTX.mesh.format = FORMAT_MSH ;
          }
          else if(!strcmp(argv[i],"unv") ||
                  !strcmp(argv[i],"UNV") || 
                  !strcmp(argv[i],"ideas")){
            CTX.mesh.format = FORMAT_UNV ;
          }
          else if(!strcmp(argv[i],"gref") ||
                  !strcmp(argv[i],"GREF") || 
                  !strcmp(argv[i],"Gref")){
            CTX.mesh.format = FORMAT_GREF ;
          }
          else{
            fprintf(stderr, ERROR_STR "Unknown mesh format\n");
            exit(1);
          }
          i++;
        }
        else {    
          fprintf(stderr, ERROR_STR "Missing format\n");
          exit(1);
        }
      }
      else if(!strcmp(argv[i]+1, "algo")){  
        i++;
        if(argv[i]!=NULL){
          if(!strncmp(argv[i],"iso",3))
            CTX.mesh.algo = DELAUNAY_ISO ;
          else if(!strncmp(argv[i],"tri",3))
            CTX.mesh.algo = DELAUNAY_SHEWCHUK ;
          else if(!strncmp(argv[i],"aniso",5))
            CTX.mesh.algo = DELAUNAY_ANISO ;
          else{
            fprintf(stderr, ERROR_STR "Unknown mesh algorithm\n");
            exit(1);
          }
          i++;
        }
        else {    
          fprintf(stderr, ERROR_STR "Missing algorithm\n");
          exit(1);
        }
      }
      else if(!strcmp(argv[i]+1, "version") || 
              !strcmp(argv[i]+1, "-version")){
        fprintf(stderr, "%d.%d.%d\n", GMSH_MAJOR_VERSION, GMSH_MINOR_VERSION, 
		GMSH_PATCH_VERSION);
        exit(1);
      }
      else if(!strcmp(argv[i]+1, "info") || 
              !strcmp(argv[i]+1, "-info")){
        fprintf(stderr, "%s%d.%d.%d\n", gmsh_version, GMSH_MAJOR_VERSION, 
	      GMSH_MINOR_VERSION, GMSH_PATCH_VERSION);
        fprintf(stderr, "%s\n", gmsh_os);
#if defined(HAVE_FLTK)
        fprintf(stderr, "%sFLTK %d.%d.%d\n", gmsh_gui, FL_MAJOR_VERSION, 
		FL_MINOR_VERSION, FL_PATCH_VERSION);
#else
        fprintf(stderr, "%snone\n", gmsh_gui);
#endif
        fprintf(stderr, "%s\n", gmsh_date);
        fprintf(stderr, "%s\n", gmsh_host);
        fprintf(stderr, "%s\n", gmsh_packager);
        fprintf(stderr, "%s\n", gmsh_url);
        fprintf(stderr, "%s\n", gmsh_email);
        exit(1) ; 
      }
      else if(!strcmp(argv[i]+1, "help") || 
              !strcmp(argv[i]+1, "-help")){
        fprintf(stderr, "%s\n", gmsh_progname);
        fprintf(stderr, "%s\n", gmsh_copyright);
	CTX.terminal = 1 ;
        Print_Usage(argv[0]);
        exit(1);
      }
      else if(!strcmp(argv[i]+1, "v")){  
        i++;
        if(argv[i]!=NULL) CTX.verbosity = atoi(argv[i++]);
        else {    
          fprintf(stderr, ERROR_STR "Missing number\n");
          exit(1);
        }
      }
#if defined(HAVE_FLTK)
      else if(!strcmp(argv[i]+1, "noterm")){ 
        CTX.terminal = 0; i++;
      }
      else if(!strcmp(argv[i]+1, "term")){ 
        CTX.terminal = 1; i++;
      }
      else if(!strcmp(argv[i]+1, "alpha")){ 
        CTX.alpha = 1; i++;
      }
      else if(!strcmp(argv[i]+1, "notrack")){ 
        CTX.useTrackball = 0; i++;
      }
      else if(!strcmp(argv[i]+1, "dual")){ 
        CTX.mesh.dual = 1; i++;
      }
      else if(!strcmp(argv[i]+1, "interactive")){ 
        CTX.mesh.interactive = 1; i++;
      }
      else if(!strcmp(argv[i]+1, "noview")){ 
        opt_view_visible(0, GMSH_SET, 0); i++;
      }
      else if(!strcmp(argv[i]+1, "plugin")){ 
	opt_general_default_plugins(0, GMSH_SET, 1); i++;
      }
      else if(!strcmp(argv[i]+1, "noplugin")){ 
	opt_general_default_plugins(0, GMSH_SET, 0); i++;
      }
      else if(!strcmp(argv[i]+1, "link")){ 
        i++ ;
        if(argv[i]!=NULL)
	  CTX.post.link = atoi(argv[i++]);
        else{
          fprintf(stderr, ERROR_STR "Missing number\n");
          exit(1);
        }
      }
      else if(!strcmp(argv[i]+1, "fill")){ 
        opt_view_intervals_type(0, GMSH_SET, DRAW_POST_CONTINUOUS) ; i++;
      }
      else if(!strcmp(argv[i]+1, "smoothview")){ 
	CTX.post.smooth=1; i++;
      }
      else if(!strcmp(argv[i]+1, "nbiso")){ 
        i++ ;
        if(argv[i]!=NULL)
	  opt_view_nb_iso(0, GMSH_SET, atoi(argv[i++]));
        else{
          fprintf(stderr, ERROR_STR "Missing number\n");
          exit(1);
        }
      }
      else if(!strcmp(argv[i]+1, "command") || 
              !strcmp(argv[i]+1, "c")){ 
        CTX.command_win = 1; i++;
      }
      else if(!strcmp(argv[i]+1, "nocommand") ||
              !strcmp(argv[i]+1, "noc")){ 
        CTX.command_win = 0; i++;
      }
      else if(!strcmp(argv[i]+1, "overlay") ||
              !strcmp(argv[i]+1, "ov")){ 
        CTX.overlay = 1; i++;
      }
      else if(!strcmp(argv[i]+1, "nooverlay") ||
              !strcmp(argv[i]+1, "noov")){ 
        CTX.overlay = CTX.geom.highlight = 0; i++;
      }
      else if(!strcmp(argv[i]+1, "hh")){ 
        CTX.overlay = 0 ; CTX.geom.highlight = 1; i++;
      }
      else if(!strcmp(argv[i]+1, "perspective") ||
              !strcmp(argv[i]+1, "p")){ 
        CTX.ortho = 0; i++;
      }
      else if(!strcmp(argv[i]+1, "ortho") ||
              !strcmp(argv[i]+1, "o")){ 
        CTX.ortho = 0; i++;
      }
      else if(!strcmp(argv[i]+1, "threads")){
        CTX.threads = 1; i++;
      }
      else if(!strcmp(argv[i]+1, "nothreads")){
        CTX.threads = 0; i++;
      }
      else if(!strcmp(argv[i]+1, "db")){ 
        CTX.db = 1; i++;
      }
      else if(!strcmp(argv[i]+1, "nodb")){ 
        CTX.db = 0; CTX.geom.highlight = 0; i++;
      }
      else if(!strcmp(argv[i]+1, "dl")){ 
        CTX.post.display_lists = 1; i++;
      }
      else if(!strcmp(argv[i]+1, "nodl")){ 
        CTX.post.display_lists = 0; i++;
      }
      else if(!strcmp(argv[i]+1, "fontsize")){
        i++;
        if(argv[i]!=NULL){
	  CTX.fontsize = atoi(argv[i]);
          i++;
	}
        else {    
          fprintf(stderr, ERROR_STR "Missing number\n");
          exit(1);
        }
      }
      else if(!strcmp(argv[i]+1, "theme")){
        i++;
        if(argv[i]!=NULL){
	  CTX.theme = argv[i];
          i++;
	}
        else {    
          fprintf(stderr, ERROR_STR "Missing argument\n");
          exit(1);
        }
      }
      else if(!strcmp(argv[i]+1, "display")){
        i++;
        if(argv[i]!=NULL){
	  CTX.display = argv[i];
          i++;
	}
        else {    
          fprintf(stderr, ERROR_STR "Missing argument\n");
          exit(1);
        }
      }
#endif


      else{
#if defined(__APPLE__)
	// The Mac Finder launches programs with special command line
	// arguments: just ignore them (and don't exit!)
        fprintf(stderr, "Unknown option '%s'\n", argv[i]);
	i++;
#else
        fprintf(stderr, "Unknown option '%s'\n", argv[i]);
	CTX.terminal = 1 ;
        Print_Usage(argv[0]);
        exit(1);
#endif
      }
    }

    else {
      if(*nbfiles < MAX_OPEN_FILES)
        TheFileNameTab[(*nbfiles)++] = argv[i++]; 
      else{
        fprintf(stderr, ERROR_STR "Too many input files\n");
        exit(1);
      }
    }

  }

  strncpy(CTX.filename, TheFileNameTab[0], 255);

}

