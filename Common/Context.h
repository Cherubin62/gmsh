#ifndef _CONTEXT_H_
#define _CONTEXT_H_

/* 
   Interface-independant context
 */

/* How RGBA values are packed and unpacked into/from a 4-byte integer */

#  ifdef _LITTLE
#    define PACK_COLOR(R,G,B,A)   ( (A)<<24 | (B)<<16 | (G)<<8 | (R) )
#    define UNPACK_RED(X)         ( (X) & 0xff )
#    define UNPACK_GREEN(X)       ( ( (X) >> 8 ) & 0xff )
#    define UNPACK_BLUE(X)        ( ( (X) >> 16 ) & 0xff )
#    define UNPACK_ALPHA(X)       ( ( (X) >> 24 ) & 0xff )
#  else
#    define PACK_COLOR(R,G,B,A)   ( (R)<<24 | (G)<<16 | (B)<<8 | (A) )
#    define UNPACK_RED(X)         ( ( (X) >> 24 ) & 0xff )
#    define UNPACK_GREEN(X)       ( ( (X) >> 16 ) & 0xff )
#    define UNPACK_BLUE(X)        ( ( (X) >> 8 ) & 0xff )
#    define UNPACK_ALPHA(X)       ( (X) & 0xff )
#  endif

typedef struct{
  int id;			/* the current rgbacolors id */
  
  /* general colors */
  unsigned int bg, fg, text, axes, little_axes;
  
  /* geometry colors */
  struct{
    unsigned int point, line, surface, volume;
    unsigned int point_sel, line_sel, surface_sel, volume_sel;
    unsigned int point_hlt, line_hlt, surface_hlt, volume_hlt;
    unsigned int tangents, normals;
  }
  geom;
  
  /* mesh colors */
  struct{
    unsigned int vertex, vertex_supp, line, triangle, quadrangle;
    unsigned int tetrahedron, hexahedron, prism, pyramid;
    unsigned int carousel[10];
    unsigned int tangents, normals;
  }
  mesh;
  
}rgbacolors;

typedef struct {

  int interactive;		/* 0=full gfx; -1=just parse; 1,2,3=batch mesh */

  double r[3], t[3], s[3];	/* current rotation, translation and scale */
  int rlock[3], tlock[3], slock[3];	/* locks for r, t and s */
  
  double min[3];		/* x, y and z min for the current geometry */
  double max[3];		/* x, y and z max for the current geometry */
  double range[3];		/* maximum range in the three directions */
  
  int db;			/* double buffer? */
  int overlay;	   	        /* overlay graphic window? */
  int stream;			/* output stream: TO_SCREEN or TO_FILE */
  int ortho;			/* orthogonal projection? */
  int fast;			/* inhibit mesh and postpro drawing when changing r,s,t */
  int command_win;		/* command window? */
  int display_lists;		/* use display lists? */
  int font_base;		/* display list indice for the font */
  int axes, little_axes;	/* draw axes? */
  int threads, threads_lock;	/* threads?, lock (should be a mutex...) */
  int alpha;                    /* enable alpha blending */
  int flash;                    /* authorize colormap flashing (beek) */
  int same_visual;              /* force same visual for GUI and Graphics */
  
  char *font_string;          /* main font */
  char *colorbar_font_string; /* font for colorbar */

  /* OpenGL stuff */
  int viewport[4];
  float light0[4];            /* light source position */
  float shine;                /* specular value */
  int render_mode;		/* RENDER, SELECT, FEEDBACK */

  double pixel_equiv_x, pixel_equiv_y ; /* approximative equivalent model lenght of a pixel */
  
  /* all colors except postpro colormaps */
  rgbacolors color;
  
  /* geometry options */
  struct{
    int vis_type;
    int points, lines, surfaces, volumes;
    int points_num, lines_num, surfaces_num, volumes_num;
    int hidden, shade;
    int highlight;
    int level;
    double normals, tangents;
  } geom;

  /* mesh options */
  struct {
    int vis_type;
    int draw;
    int points, lines, surfaces, volumes;
    int points_num, lines_num, surfaces_num, volumes_num;
    int is_limit_gamma, dual;
    double limit_gamma;
    int hidden, shade;
    int format, nb_smoothing, algo, degree;
    int reco_extrude;
    double normals, tangents, explode;
  } mesh;

  /* post processing options */
  struct{
    int draw, scales, link ;
    char *font;
    int  fontsize;
    int  initial_visibility, initial_nbiso, initial_intervals ;
    long anim_delay ;
  }post;

  /* print options */
  struct{
    int format, type;
  } print;

} Context_T;

typedef struct {
  char *string ; 
  int int1, int2, int3, int4 ;
} StringX4Int;

typedef struct {
  char *string ; 
  void *Pointer ;
} StringXPointer ;

void InitContext (Context_T * ctx);
void InitColors (rgbacolors * col, int num);

#endif
