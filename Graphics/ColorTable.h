#ifndef _COLORTABLE_H_
#define _COLORTABLE_H_

#define COLORTABLE_NBMAX_PARAM 10
#define COLORTABLE_NBMAX_COLOR 255

typedef struct{
  unsigned int table[COLORTABLE_NBMAX_COLOR];
  int size;
  int ipar[COLORTABLE_NBMAX_PARAM];
  float fpar[COLORTABLE_NBMAX_PARAM];
}ColorTable;


/* COLORTABLE_MODE */

#define COLORTABLE_RGB  1
#define COLORTABLE_HSV  2


/* integrer parameters indices */

#define COLORTABLE_NUMBER    0	/* predefined curve index */
#define COLORTABLE_CHANGED   1	/* did the colortable change ? */
#define COLORTABLE_INVERT    2	/* invert (rbg<->255-rgb) */
#define COLORTABLE_SWAP      3	/* swap (min<->max) */
#define COLORTABLE_ROTATE    4	/* rotate */
#define COLORTABLE_MODE      5	/* mode (rgb, hsv) */

/* float parameters indices */

#define COLORTABLE_CURVE     0	/* curvature */
#define COLORTABLE_BIAS      1	/* offset */
#define COLORTABLE_ALPHAPOW  2	/* alpha channel power */
#define COLORTABLE_ALPHAVAL  3	/* alpha channel value */
#define COLORTABLE_BETA      4	/* beta coeff for brighten */

void color_table_init_param (int number, ColorTable * ct, int rgb_flag, int alpha_flag);
void color_table_recompute (ColorTable * ct, int rgb_flag, int alpha_flag);
void load_color_table(FILE *fp, ColorTable *ct);
void save_color_table(FILE *fp, ColorTable *ct) ;

#endif
