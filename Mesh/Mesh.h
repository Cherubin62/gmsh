#ifndef _MESH_H_
#define _MESH_H_

#include "Vertex.h"
#include "Simplex.h"
#include "Edge.h"
#include "ExtrudeParams.h"

#define FORMAT_MSH  1
#define FORMAT_UNV  2
#define FORMAT_GREF 3

#define CONV_VALUE    0.8

#define GREEN_INTERPOLATION 1
#define FEM_INTERPOLATION   2

#define NOTTOLINK 1
#define TOLINK    2

#define BOF         1
#define A_TOUT_PRIX 2

#define LossMemory  1
#define BadLink2    2
#define DegTriangle 3
#define PtExterior  4

#define CENTER_CIRCCIRC 1
#define VORONOI_INSERT  2
#define BARYCENTER      3
#define SQUARE_TRI      4

#define EXTERN      1
#define INTERN      2
#define ACTIF       3
#define WAITING     4
#define ACCEPTED    5
#define NONACCEPTED 6

#define NONE        0
#define CONSTANT    1
#define ONFILE      2
#define WITHPOINTS  3
#define FUNCTION    4
#define LINEAR      5
#define QUADRATIC   6
#define CUBIC       7
#define LOGARITHMIC 8
#define EXPONENTIAL 9

#define DROITE 1
#define SPLINE 2

#define TRANSFINI 1
#define LIBRE     2
#define ELLIPTIC  3

#define REMOVE 1
#define ADD    2

#define BOULE 1
#define BOITE 2

#define HYPOTENU  1
#define SMALLFACE 2

typedef struct _POINT PointRecord, *PointPeek;
typedef struct _CONTOUR ContourRecord, *ContourPeek;
typedef struct _DOC DocRecord, *DocPeek;
typedef struct _CDLIST DListRecord, *DListPeek;
typedef struct _MAILLAGE maillage, *maipeek;
typedef struct _MAICON maillage_c;
typedef struct _DELAUNAY Delaunay, *delpeek;
typedef int PointNumero;

struct _DOC{
  PointRecord *points;	/* points a trianguler */
  List_T *hotpoints;		/* hotpoints */
  int numPoints;		/* nombre de points */
  int numTriangles;		/* nombre de triangles */
  Delaunay *delaunay;		/* resultats 2D */
};

typedef struct{
  double v;
  double h;
}MPoint;

struct _POINT{
  MPoint where;
  DListPeek adjacent;
  int initial, permu, info, info2, numcontour;
  double quality, qualityy, angle;
};

typedef struct{
  int Num;
  double t, lc, p;
}IntPoint;

struct _CDLIST{
  PointNumero point_num;	/* numero du point */
  DListPeek next, prev;
};

typedef struct{
  PointNumero search;
  PointNumero already;
}demi_triangle;

typedef struct{
  demi_triangle *info;
  PointNumero *t;
  int t_length, info_length;
}Striangle;

typedef struct {
  PointNumero from;
  PointNumero to;
  int num;
  int seg;
}edge;

typedef struct{
  PointNumero begin;
  PointNumero end;
}DT;

typedef struct{
  PointNumero from;
  PointNumero to;
}Segment;

typedef struct{
  PointNumero a, b, c;
  double xc, yc;
  double quality_value;
  int position, info, info2;
}Triangle;

typedef struct {
  Delaunay *voisin1, *voisin2, *voisin3;
}Voronoi;

struct _DELAUNAY{
  Triangle t;
  Voronoi v;
};

struct _CONTOUR{
  PointRecord *oriented_points;
  MPoint *perturbations;
  int numpoints, numerocontour, numerozon, info;
};

struct _MAICON{
  PointRecord *points;
  int numpoints, numcontour, numeroseg;
};

struct _MAILLAGE{
  PointRecord *points;
  delpeek *listdel;
  edge *listedges;
  int numedges, numtriangles, numpoints;
  int IncrAllocPoints, IncrAllocTri, IncrAllocEdges;
  int NumAllocPoints, NumAllocTri, NumAllocEdges;
  int zone;
};

typedef struct{
  Vertex *v;
  List_T *Liste;
}NXE;

typedef struct{
  int Num;		/* Numero                                       */
  int iEnt;		/* Entite geometrique                           */
  Vertex *V[8];		/* 8 noeuds                                     */
  Vertex **VSUP;	/* noeuds supplem pour les elts de degre eleves */
}Hexahedron;

typedef struct{
  int Num;		/* Numero                                       */
  int iEnt;		/* Entite geometrique                           */
  Vertex *V[6];		/* 6 noeuds                                     */
  Vertex **VSUP;	/* noeuds supplem pour les elts de degre eleves */
}Prism;

typedef struct{
  int N;
  List_T *pT;
}Brick;

typedef struct{
  int init;
  List_T *Bricks;
  Coord min, max;
  int Nx, Ny, Nz;
}Grid_T;

typedef struct{
  double zaxis[3];
  double radius1;
  double radius2;
  double xaxis[3];
  double center[3];
}CylParam;

class STL_Data{
 public:
  Tree_T * Vertices;
  Tree_T *Simplexes;
  List_T *LVertices;
  List_T *LSimplexes;
  void Add_Facet (double x1, double y1, double z1,
		  double x2, double y2, double z2,
		  double x3, double y3, double z3);
  int GetNbFacets (){
    return Tree_Nbr (Simplexes);
  }
  int GetNbVertices (){
    return Tree_Nbr (Vertices);
  }
  void GetFacet (int iFac, int &v1, int &v2, int &v3);
  void GetVertex (int iVertex, double &x, double &y, double &z);
  STL_Data ();
  ~STL_Data ();
};

struct _Surf{
  int Num;
  int Typ;
  int Mat;
  int Method;
  int Recombine;
  double RecombineAngle;
  int ipar[4];
  int Nu, Nv;
  union{
    List_T *Generatrices;	/* Surface reglee    */
  }s;
  List_T *Control_Points;	/* Patchs bicubiques */
  double plan[3][3];
  double invplan[3][3];
  double a, b, c, d;
  List_T *Orientations;
  List_T *Contours;
  Tree_T *Simplexes;
  List_T *TrsfSimplexes;
  Tree_T *Vertices;
  List_T *TrsfVertices;
  Tree_T *Edges;
  int OrderU, OrderV;
  float *ku, *kv, *cp;
  struct _Surf *Support;
  CylParam Cyl;
  Grid_T Grid;		/* Grille de recherches rapides          */
  ExtrudeParams *Extrude;
  STL_Data *STL;
};
typedef struct _Surf Surface;

typedef struct{
  int Num;
  List_T *Curves;
}EdgeLoop;

typedef struct{
  int Num;
  List_T *Surfaces;
}SurfaceLoop;

typedef struct{
  int Num;
  int Typ;
  List_T *Entities;
}PhysicalGroup;

typedef struct{
  Face F;
  Face Sorted;
  Simplex *S[2];
  int N;
}FxE;

typedef struct {
  int Num;
  int Mat;
  int Typ;
  int Method;
  int ipar[8];
  ExtrudeParams *Extrude;
  List_T *Surfaces;
  Tree_T *Vertices;
  Tree_T *Edges;
  Tree_T *Faces;
  Tree_T *Simplexes;
  Tree_T *Hexahedra;
  Tree_T *Prisms;
}Volume;

typedef struct {
  Edge e1, e2;
  int iFac;
}exf_T;

/* Structure intersection arete - Simplexe */

typedef struct{
  int NbIntersect;	/* nombre total d'intersections                   */
  Edge *e;		/* arete                                          */
  Simplex *s;		/* simplexe                                       */
  Face *f;		/* face                                           */
  int NbVertex;		/* nombre de noeuds du simplexe que coupe l'arete */
  Vertex *V[12];	/* noeuds du simplexe que coupe l'arete           */
  int iV[12];		/* noeuds du simplexe que coupe l'arete           */
  int NbEdge;		/* nombre d'intersections arete-arete             */
  int E[12];		/* aretes                                         */
  Vertex *VE[12];	/* noeuds d'intersection                          */
  int NbFace;		/* nombre d'intersections face-arete              */
  Face *F[12];		/* faces                                          */
  int iF[12];		/* faces                                          */
  Vertex *VF[12];	/* position des points d'intersections face-arete */
}Intersection;

typedef struct _Mesh Mesh;

typedef struct{
  int Typ;
  double lc;
  struct _Mesh *m;
  List_T *bgm;
}LcField;

typedef struct{
  int done;
  double t1, t2, f1, f2, incl;
  Vertex *v[4];
  double invmat[3][3];
  double n[3];
}CircParam;

typedef struct{
  int Num;
  int Typ;
  int Method;
  int ipar[4];
  double dpar[4];
  double l;
  double mat[4][4];
  Vertex *beg, *end;
  double ubeg, uend;
  List_T *Control_Points;
  List_T *Knots;
  List_T *Vertices;
  List_T *ConnectedSurfaces;
  Tree_T *Simplexes;
  List_T *TrsfSimplexes;
  ExtrudeParams *Extrude;
  float *k, *cp;
  int degre;
  CircParam Circle;
  char functu[256], functv[256], functw[256];
}Curve;

typedef struct{
  int Num;
  int Typ;
  Vertex *v;
  Curve *c;
  Surface *s;
  double lc1, lc2;
  double Radius;
}Attractor;

#include "Metric.h"

#define QUALITY_EDGES_BASED 1
#define QUALITY_SIMPLEX_BASED 2
#define INSERTION_CENTROID 1
#define INSERTION_EDGE 2
#define DELAUNAY_OLDALGO 1
#define DELAUNAY_NEWALGO 2
#define DELAUNAY_KERISO 1
#define DELAUNAY_KERANISO 2

class MeshParameters{
 public:
  int NbSmoothing;
  int DelaunayAlgorithm;
  int DelaunayInsertionMethod;
  int DelaunayQuality;
  int DelaunayKernel;
  bool InteractiveDelaunay;
  MeshParameters ();
};

struct _Mesh{
  char name[256];		/* Nom du probleme                       */
  int status;			/* Etat actuel du maillage               */
  Tree_T *Points;		/* Points de controle                    */
  Tree_T *Vertices;		/* Noeuds du maillage                    */
  Tree_T *Simplexes;		/* Simplexes                             */
  Tree_T *Curves;		/* Courbes                               */
  Tree_T *Surfaces;		/* Surfaces                              */
  Tree_T *Volumes;		/* Volumes                               */
  Tree_T *SurfaceLoops;	        /* Surface Loops                         */
  Tree_T *EdgeLoops;		/* Edge Loops                            */
  List_T *PhysicalGroups;	/*Physical Groups                      */
  Tree_T *VertexEdges;	        /* 2nd order Vertices on edges           */
  Grid_T Grid;		        /* Grille de recherches rapides          */
  LcField BGM;		        /* Background mesh                       */
  double Statistics[50];	/* Statistiques pour le maillage         */
  GMSHMetric *Metric;		/* Metrique */
  MeshParameters MeshParams;
};

typedef struct {
  Simplex *S;
  Face F;
  int NumFaceSimpl;
}SxF;

struct Map{
  int Num;
  List_T *List;
};


/* public functions */

double Correction_LC_Attractors (double X, double Y, double Z,
				 double *u, Mesh * m, double metr[3][3]);
int Extrude_Mesh (Curve * c);
int Extrude_Mesh (Surface * s);
int Extrude_Mesh (Volume * v);

int Calcule_Contours (Surface * s);

void Link_Simplexes (List_T * Sim, Tree_T * Tim);
void Maillage_Curve (void *data, void *dummy);

int AlgorithmeMaillage2DAnisotropeModeJF (Surface * s);
void Maillage_Automatique_VieuxCode (Surface * pS, Mesh * m, int ori);
void Maillage_Surface (void *data, void *dum);
void Maillage_Volume (void *data, void *dum);

void mai3d (Mesh * M, int Asked);
void Init_Mesh (Mesh * M, int all);
void GetStatistics (double s[50]);
void Maillage_Dimension_0 (Mesh * M);
void Maillage_Dimension_1 (Mesh * M);
void Maillage_Dimension_2 (Mesh * M);
void Maillage_Dimension_3 (Mesh * M);
void mai3d (Mesh * M, int Asked);
void Create_BgMesh (int i, double d, Mesh * m);
void Print_Mesh (Mesh * M, char *c, int Type);
void Read_Mesh (Mesh * M, FILE * File_GEO, int Type);
void Degre2 (Tree_T * AllNodes, Tree_T * TreeNodes, Tree_T * TreeElm,
	     Curve * c, Surface * s);
double Lc_XYZ (double X, double Y, double Z, Mesh * m);
int MeshTransfiniteSurface (Surface *sur);
int MeshTransfiniteVolume (Volume *vol);
int MeshCylindricalSurface (Surface * s);
int MeshParametricSurface (Surface * s);
int MeshEllipticSurface (Surface * sur);
void ActionLiss (void *data, void *dummy);
void ActionLissSurf (void *data, void *dummy);
void Recombine (Tree_T *TreeAllVert, Tree_T *TreeAllElg, double a);

void Calcule_Z (void *data, void *dum);
void Calcule_Z_Plan (void *data, void *dum);
void Projette_Plan_Moyen (void *a, void *b);
void Projette_Inverse (void *a, void *b);
void Freeze_Vertex (void *a, void *b);
void deFreeze_Vertex (void *a, void *b);
void crEdges (Tree_T * TreeElem, Tree_T * treeedges);


#endif
