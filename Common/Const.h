#ifndef _CONSTS_H_
#define _CONSTS_H_

#define TEXT_BUFFER_SIZE       1024
#define SELECTION_BUFFER_SIZE  1024
#define LABEL_STR_L            16
#define NAME_STR_L             256
#define MAX_OPEN_FILES         256

#define RADTODEG      57.295779513082321
#define RacineDeDeux  1.4142135623730950
#define RacineDeTrois 1.7320508075688773
#define Pi            3.1415926535897932
#define Deux_Pi       6.2831853071795865

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)<(b))?(b):(a))
#define SQR(a)   ((a)*(a))

#define IMIN MIN
#define LMIN MIN
#define FMIN MIN
#define DMIN MIN

#define IMAX MAX
#define LMAX MAX
#define FMAX MAX
#define DMAX MAX

#define DSQR SQR
#define FSQR SQU

#define THRESHOLD(a,b,c) (((a)>(c))?(c):((a)<(b)?(b):(a)))

#define myhypot(a,b) (sqrt((a)*(a)+(b)*(b)))
#define sign(x)      (((x)>=0)?1:-1)
#define Pred(x)      ((x)->prev)
#define Succ(x)      ((x)->next)
#define square(x)    ((x)*(x))

#endif
