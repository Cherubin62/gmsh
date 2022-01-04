#include "hxt_surface_mesh_quad.h"

#include "hxt_quad_mesh.h"

#include "hxt_message.h"
#include "hxt_tools.h"
#include "hxt_edge.h"
#include "hxt_point_gen_utils.h"
#include "hxt_point_gen_realloc.h"

#include "hxt_point_gen_numerics.h"

#include "hxt_split_edge.h"

#include "hxt_surface_mesh_processing.h"


#include "hxt_rtree_wrapper.h"


#include "hxt_point_gen_options.h"


#include "hxt_post_debugging.h"


//*****************************************************************************************
//*****************************************************************************************
//
//*****************************************************************************************
//*****************************************************************************************
uint32_t hamming(uint32_t n1, uint32_t n2)
{
  uint32_t x = n1^n2;
  int setBits = 0;
  
  while (x>0){
    setBits += x&1;
    x >>= 1;
  }
  return setBits;
}

//*****************************************************************************************
//*****************************************************************************************
//
// FUNCTION to print 
//
//*****************************************************************************************
//*****************************************************************************************
HXTStatus hxtPointGenQuadPrintInfo(HXTMesh *mesh, 
                                   uint32_t *bin, 
                                   uint16_t *flagTris,
                                   const char *fmesh,
                                   const char *fpos)
{

  hxtMeshWriteGmsh(mesh,fmesh);
  FILE *test;
  hxtPosInit(fpos,"edges",&test);
  for (uint64_t i=0; i<mesh->triangles.num; i++){
    if (flagTris[i] == UINT16_MAX) continue;
    uint32_t *v = mesh->triangles.node + 3*i;
    hxtPosAddTriangle(test,&mesh->vertices.coord[4*v[0]],&mesh->vertices.coord[4*v[1]],&mesh->vertices.coord[4*v[2]],0);
  }
  hxtPosNewView(test,"bin");
  for (uint64_t i=0; i<mesh->triangles.num; i++){
    if (flagTris[i] == UINT16_MAX) continue;
    uint32_t *v = mesh->triangles.node + 3*i;
    //hxtPosAddTriangle(test,&mesh->vertices.coord[4*v[0]],&mesh->vertices.coord[4*v[1]],&mesh->vertices.coord[4*v[2]],0);
    hxtPosAddPoint(test,&mesh->vertices.coord[4*v[0]],bin[v[0]]);
    hxtPosAddPoint(test,&mesh->vertices.coord[4*v[1]],bin[v[1]]);
    hxtPosAddPoint(test,&mesh->vertices.coord[4*v[2]],bin[v[2]]);
  }
  hxtPosNewView(test,"binText");
  for (uint64_t i=0; i<mesh->triangles.num; i++){
    if (flagTris[i] == UINT16_MAX) continue;
    uint32_t *v = mesh->triangles.node + 3*i;
    //hxtPosAddTriangle(test,&mesh->vertices.coord[4*v[0]],&mesh->vertices.coord[4*v[1]],&mesh->vertices.coord[4*v[2]],0);
    hxtPosAddText(test,&mesh->vertices.coord[4*v[0]],"%d",bin[v[0]]);
    hxtPosAddText(test,&mesh->vertices.coord[4*v[1]],"%d",bin[v[1]]);
    hxtPosAddText(test,&mesh->vertices.coord[4*v[2]],"%d",bin[v[2]]);
  }
  hxtPosNewView(test,"binAll");
  for (uint32_t i=0; i<mesh->vertices.num; i++){
    hxtPosAddPoint(test,&mesh->vertices.coord[4*i],bin[i]);
  }
  hxtPosFinish(test);


  return HXT_STATUS_OK; 
}

//*****************************************************************************************
//*****************************************************************************************
//
// FUNCTION to print binary indices 
//
//*****************************************************************************************
//*****************************************************************************************
HXTStatus hxtPointGenQuadPrintBin(HXTMesh *mesh, 
                                  uint32_t *bin, 
                                  const char *fbin)
{

  FILE *test;
  hxtPosInit(fbin,"points",&test);
  for (uint32_t i=0; i<mesh->vertices.num;i++){
    hxtPosAddPoint(test,&mesh->vertices.coord[4*i],bin[i]);
  }
  hxtPosFinish(test);


  return HXT_STATUS_OK; 
}




//*****************************************************************************************
//*****************************************************************************************
//
// FUNCTION to output singularities of a quad mesh 
//
//*****************************************************************************************
//*****************************************************************************************
HXTStatus hxtSurfaceQuadSingularitiesOutput(HXTPointGenOptions *opt, HXTMesh *mesh)
{

  uint32_t *isBoundary;
  HXT_CHECK(hxtMalloc(&isBoundary,mesh->vertices.num*sizeof(uint32_t)));
  for (uint32_t i=0; i<mesh->vertices.num; i++) isBoundary[i] = UINT32_MAX;
  for (uint64_t i=0; i<mesh->lines.num; i++){
    isBoundary[mesh->lines.node[2*i+0]] = 1;
    isBoundary[mesh->lines.node[2*i+1]] = 1;
  }

  uint32_t *isFeature;
  HXT_CHECK(hxtMalloc(&isFeature,mesh->vertices.num*sizeof(uint32_t)));
  for (uint32_t i=0; i<mesh->vertices.num; i++) isFeature[i] = UINT32_MAX;
  for (uint64_t i=0; i<mesh->points.num; i++){
    isFeature[mesh->points.node[i]] = 1;
  }

  uint64_t *counter;
  HXT_CHECK(hxtMalloc(&counter,mesh->vertices.num*sizeof(uint64_t)));
  for (uint32_t i=0; i<mesh->vertices.num; i++) counter[i] = 0;

  for (uint64_t i=0; i<mesh->quads.num; i++){
    uint32_t *v = mesh->quads.node + 4*i;

    for (uint32_t j=0; j<4; j++){
      counter[v[j]]++;
    }
  }

  for (uint32_t i=0; i<mesh->vertices.num; i++){
    //if (counter[i] > 5) printf("PROBLEM - did not expect this\n");
    if (counter[i] == 1 && isBoundary[i] != 1){
    
      return HXT_ERROR_MSG(HXT_STATUS_ERROR," !!! Huge error - vertex with only one quad");
    }

  }

  uint32_t c1 = 0;
  uint32_t c2 = 0;
  uint32_t c3 = 0;
  uint32_t c5 = 0;
  uint32_t c6 = 0;

  uint32_t c1b = 0;
  uint32_t c2b = 0;
  uint32_t c3b = 0;
  uint32_t c5b = 0;
  uint32_t c6b = 0;


  if (opt->verbosity>=1){
    FILE *test;
    hxtPosInit("checkSing.pos","featurePoints",&test);
    for (uint32_t i=0; i<mesh->points.num; i++){
      hxtPosAddPoint(test,&mesh->vertices.coord[4*mesh->points.node[i]],0);
    }
    hxtPosNewView(test,"sing1");
    for (uint32_t i=0; i<mesh->vertices.num; i++){
      if (counter[i]==1){
        hxtPosAddPoint(test,&mesh->vertices.coord[4*i],0);
        c1++;
      }
    }
    hxtPosNewView(test,"sing2");
    for (uint32_t i=0; i<mesh->vertices.num; i++){
      if (counter[i]==2){
        hxtPosAddPoint(test,&mesh->vertices.coord[4*i],0);
        c2++;
      }
    }
    hxtPosNewView(test,"sing3");
    for (uint32_t i=0; i<mesh->vertices.num; i++){
      if (counter[i]==3){
        hxtPosAddPoint(test,&mesh->vertices.coord[4*i],0);
        c3++;
      }
    }
    hxtPosNewView(test,"sing5");
    for (uint32_t i=0; i<mesh->vertices.num; i++){
      if (counter[i]==5){
        hxtPosAddPoint(test,&mesh->vertices.coord[4*i],0);
        c5++;
      }
    }

    hxtPosNewView(test,"sing6");
    for (uint32_t i=0; i<mesh->vertices.num; i++){
      if (counter[i]>5){
        hxtPosAddPoint(test,&mesh->vertices.coord[4*i],0);
        c6++;
      }
    }


    hxtPosNewView(test,"sing1boundary");
    for (uint32_t i=0; i<mesh->vertices.num; i++){
      if (isBoundary[i] != 1) continue;
      if (isFeature[i] == 1) continue;
      if (counter[i]==1){
        hxtPosAddPoint(test,&mesh->vertices.coord[4*i],0);
        c1b++;
      }
    }
    hxtPosNewView(test,"sing2boundary");
    for (uint32_t i=0; i<mesh->vertices.num; i++){
      if (isBoundary[i] != 1) continue;
      if (isFeature[i] == 1) continue;
      if (counter[i]==2){
        hxtPosAddPoint(test,&mesh->vertices.coord[4*i],0);
        c2b++;
      }
    }
    hxtPosNewView(test,"sing3boundary");
    for (uint32_t i=0; i<mesh->vertices.num; i++){
      if (isBoundary[i] != 1) continue;
      if (isFeature[i] == 1) continue;
      if (counter[i]==3){
        hxtPosAddPoint(test,&mesh->vertices.coord[4*i],0);
        c3b++;
      }
    }
    hxtPosNewView(test,"sing5boundary");
    for (uint32_t i=0; i<mesh->vertices.num; i++){
      if (isBoundary[i] != 1) continue;
      if (isFeature[i] == 1) continue;
      if (counter[i]==5){
        hxtPosAddPoint(test,&mesh->vertices.coord[4*i],0);
        c5b++;
      }
    }

    hxtPosNewView(test,"sing6boundary");
    for (uint32_t i=0; i<mesh->vertices.num; i++){
      if (isBoundary[i] != 1) continue;
      if (isFeature[i] == 1) continue;
      if (counter[i]>5){
        hxtPosAddPoint(test,&mesh->vertices.coord[4*i],0);
        c6b++;
      }
    }
    hxtPosFinish(test);
  }


  HXT_INFO_COND(opt->verbosity>=1,"");
  HXT_INFO_COND(opt->verbosity>=1,"========= Singular points info =========");
  HXT_INFO_COND(opt->verbosity>=1,"");
  HXT_INFO_COND(opt->verbosity>=1,"     valence 1      : %d", c1);
  HXT_INFO_COND(opt->verbosity>=1,"     valence 2      : %d", c2);
  HXT_INFO_COND(opt->verbosity>=1,"     valence 3      : %d", c3);
  HXT_INFO_COND(opt->verbosity>=1,"     valence 5      : %d", c5);
  HXT_INFO_COND(opt->verbosity>=1,"     valence 6+     : %d", c6);
  HXT_INFO_COND(opt->verbosity>=1,"");
  HXT_INFO_COND(opt->verbosity>=1,"     valence 1  bnd : %d", c1b);
  HXT_INFO_COND(opt->verbosity>=1,"     valence 3  bnd : %d", c3b);
  HXT_INFO_COND(opt->verbosity>=1,"     valence 5  bnd : %d", c5b);
  HXT_INFO_COND(opt->verbosity>=1,"     valence 6+ bnd : %d", c6b);
  HXT_INFO_COND(opt->verbosity>=1,"");
  HXT_INFO_COND(opt->verbosity>=1,"--- Feature points      = %d ", mesh->points.num);
  HXT_INFO_COND(opt->verbosity>=1,"--- Singular bnd points = %d ", c1b+c3b+c5b+c6b);



  // PRINT OUT FILE 

  // Count interior singularities
  int countInterior=0;
  for (uint32_t i=0; i<mesh->vertices.num; i++){
    if (isBoundary[i] == 1) continue;

    if (counter[i] != 3 && counter[i] !=5) continue;

    countInterior++;

  }


  if(opt->verbosity>=1){
    FILE *f=fopen("singularities.txt","w");
    fprintf(f,"%d \n",countInterior);

    for (uint32_t i=0; i<mesh->vertices.num; i++){
      if (isBoundary[i] == 1) continue;

      if (counter[i] != 3 && counter[i] !=5) continue;

      fprintf(f,"%f %f %f \n", mesh->vertices.coord[4*i+0], mesh->vertices.coord[4*i+1], mesh->vertices.coord[4*i+2]);

    }
    fclose(f);
  }



  HXT_CHECK(hxtFree(&isBoundary));
  HXT_CHECK(hxtFree(&isFeature));
  HXT_CHECK(hxtFree(&counter));

  return HXT_STATUS_OK;
}




//**********************************************************************************************************
//**********************************************************************************************************
//
// SIMPLE SMOOTHING
//
//**********************************************************************************************************
//**********************************************************************************************************
HXTStatus hxtPointGenQuadSmoothing(HXTMesh *omesh,
                                   HXTMesh *mesh,
                                   void *dataTri,
                                   uint32_t *isBoundary,
                                   uint64_t *p2t,
                                   uint32_t *v2v,
                                   uint32_t nIter)
{

  for (uint32_t ii=0; ii<nIter; ii++){

    for (uint32_t i=0; i<mesh->vertices.num; i++){
      uint32_t cv = i;
      if (isBoundary[cv] == 1) continue;


      /*int DO_NOT_SMOOTH = 0;*/
      /*for (uint32_t kk =0 ;kk<5; kk++){*/
        /*uint32_t v0 = v2v[5*cv+kk];*/
        /*if (v0 == UINT32_MAX) continue;*/

        /*if (isBoundary[v0] == 1) DO_NOT_SMOOTH = 1;*/
      /*}*/
      /*if (DO_NOT_SMOOTH) continue;*/

  

      // ATTENTION 
      // TODO 
      // just for now for new points that were not correctly projected and assigned with a parent triangle
      if (p2t[i] == UINT64_MAX){
        printf("no parent for point %d \n", i);
        return HXT_STATUS_ERROR;
      }
  
      double x = 0;
      double y = 0;
      double z = 0;
      int vertNum = 0;
      for (uint32_t kk =0 ;kk<5; kk++){
        uint32_t v0 = v2v[5*cv+kk];
        if (v0 == UINT32_MAX) continue;
  
        x += mesh->vertices.coord[4*v0+0];
        y += mesh->vertices.coord[4*v0+1];
        z += mesh->vertices.coord[4*v0+2];
  
        vertNum++;
      }
      if (vertNum == 0) continue;
      mesh->vertices.coord[4*cv+0] = x/vertNum;
      mesh->vertices.coord[4*cv+1] = y/vertNum;
      mesh->vertices.coord[4*cv+2] = z/vertNum;



      // Project point back to initial triangulation 
      uint64_t nt = p2t[i];
      if (p2t[i] == UINT64_MAX) printf("ERROR %d %d \n", ii, i);
      if (p2t[i] == UINT64_MAX) return HXT_STATUS_ERROR;
      double np[3] = {mesh->vertices.coord[4*cv+0],mesh->vertices.coord[4*cv+1],mesh->vertices.coord[4*cv+2]};

      HXT_CHECK(hxtPointGenProjectCloseRTree(omesh,dataTri,p2t[i],&mesh->vertices.coord[4*cv],&nt,np));

      p2t[i] = nt;
      mesh->vertices.coord[4*cv+0] = np[0];
      mesh->vertices.coord[4*cv+1] = np[1];
      mesh->vertices.coord[4*cv+2] = np[2];
  
    }
  }




  return HXT_STATUS_OK; 
}

//**********************************************************************************************************
//**********************************************************************************************************
//
// FUNCTION ATTENTION assuming max valence of 5  
//
//**********************************************************************************************************
//**********************************************************************************************************
HXTStatus hxtPointGenQuadBuildV2E(HXTEdges *edges,
                                  uint32_t *bin,
                                  uint32_t *v2e)
{
  uint32_t maxVal = 15;

  for (uint32_t i=0; i<edges->numEdges; i++){
    uint32_t v0 = edges->node[2*i+0];
    uint32_t v1 = edges->node[2*i+1];

    int insert0 = 1;
    for (uint32_t j=0; j<maxVal; j++){
      if (v2e[maxVal*v0+j] == i) insert0 = 0;
    }

    if (insert0 == 1){
      for (uint32_t j=0; j<maxVal; j++){
        if (v2e[maxVal*v0+j] == UINT32_MAX){
          v2e[maxVal*v0+j] = i;
          break;
        }
      }
    }

    int insert1 = 1;
    for (uint32_t j=0; j<maxVal; j++){
      if (v2e[maxVal*v1+j] == i) insert1 = 0;
    }
    if(insert1 == 1){
      for (uint32_t j=0; j<maxVal; j++){
        if (v2e[maxVal*v1+j] == UINT32_MAX){
          v2e[maxVal*v1+j] = i;
          break;
        }
      }
    }
  }

  return HXT_STATUS_OK;
}

//**********************************************************************************************************
//**********************************************************************************************************
//
// FUNCTION 
//
//**********************************************************************************************************
//**********************************************************************************************************
HXTStatus hxtPointGenQuadBuildV2V(HXTEdges *edges,
                              uint32_t *bin,
                              uint32_t *v2v)
{

  for (uint32_t i=0; i<edges->numEdges; i++){
    uint32_t v0 = edges->node[2*i+0];
    uint32_t v1 = edges->node[2*i+1];

    if (bin[v0] == bin[v1]) continue;

    int insert0 = 1;
    for (uint32_t j=0; j<5; j++){
      if (v2v[5*v0+j] == v1) insert0 = 0;
    }

    /*int full = 1;*/
    /*for (uint32_t j=0; j<5; j++){*/
      /*if (v2v[5*v0+j] == UINT32_MAX) full = 0;*/
    /*}*/
    /*if (full == 1) return HXT_ERROR_MSG(HXT_STATUS_ERROR,"v2v is full for %d", v0);*/

    if (insert0 == 1){
      for (uint32_t j=0; j<5; j++){
        if (v2v[5*v0+j] == UINT32_MAX){
          v2v[5*v0+j] = v1;
          break;
        }
      }
    }

    int insert1 = 1;
    for (uint32_t j=0; j<5; j++){
      if (v2v[5*v1+j] == v0) insert1 = 0;
    }
    if(insert1 == 1){
      for (uint32_t j=0; j<5; j++){
        if (v2v[5*v1+j] == UINT32_MAX){
          v2v[5*v1+j] = v0;
          break;
        }
      }
    }
  }

  return HXT_STATUS_OK;
}

//**********************************************************************************************************
//**********************************************************************************************************
//
//**********************************************************************************************************
//**********************************************************************************************************
double hxtPointGenQuadQuality(double *p0, double *p1, double *p2, double *p3)
{

  double const a0 = 180 * hxtAngle3Vertices(p3,p0,p1) / M_PI;
  double const a1 = 180 * hxtAngle3Vertices(p2,p1,p0) / M_PI;
  double const a2 = 180 * hxtAngle3Vertices(p3,p2,p1) / M_PI;
  double const a3 = 180 * hxtAngle3Vertices(p0,p3,p2) / M_PI;

  double quality = fabs(90. - a0);
  quality = fmax(fabs(90. - a1), quality);
  quality = fmax(fabs(90. - a2), quality);
  quality = fmax(fabs(90. - a3), quality);

  quality = fmax( (1-2*quality/180),0);

  /*printf("%f %f %f %f \n", a0,a1,a2,a3);*/
  /*printf("%f %f %f \n", quality, 2/M_PI, 1-2*quality/180);*/
  /*printf("%f \n", quality);*/

  return quality;
}


//**********************************************************************************************************
//**********************************************************************************************************
//
// FUNCTION to remove diamond (rhombus) quads (two opposite vertices of valence 3)
//
//**********************************************************************************************************
//**********************************************************************************************************
HXTStatus hxtPointGenQuadRemoveDiamondQuads(HXTPointGenOptions *opt,
                                            HXTMesh *mesh,
                                            HXTEdges *edges,
                                            uint64_t *p2t,
                                            uint32_t *bin,
                                            uint32_t *isBoundary,
                                            uint64_t *edges2lines)
{
  HXT_INFO_COND(opt->verbosity>=1,"");
  HXT_INFO_COND(opt->verbosity>=1,"--- Removing diamonds");
  HXT_INFO_COND(opt->verbosity>=1,"");

  typedef struct vert{
    double v[3];
    uint64_t p2t;
    uint32_t bin;
    uint32_t isBoundary;
    uint32_t newInd;
  }Vert;

  // Filling structure of vertices
  Vert *vInfo;
  HXT_CHECK(hxtMalloc(&vInfo,sizeof(Vert)*mesh->vertices.num));
  
  for (uint32_t i=0; i<mesh->vertices.num; i++){
    vInfo[i].v[0] = mesh->vertices.coord[4*i+0];
    vInfo[i].v[1] = mesh->vertices.coord[4*i+1];
    vInfo[i].v[2] = mesh->vertices.coord[4*i+2];
    vInfo[i].p2t = p2t[i];
    vInfo[i].bin = bin[i];
    vInfo[i].isBoundary = isBoundary[i];
    vInfo[i].newInd = i;
  }


  // Create v2v array 
  // Supposing maximum valence of 5 !!! TODO  
  uint32_t *v2v;
  HXT_CHECK(hxtMalloc(&v2v,mesh->vertices.num*sizeof(uint32_t)*5));
  for (uint32_t i=0; i<5*mesh->vertices.num; i++) v2v[i] = UINT32_MAX;
  HXT_CHECK(hxtPointGenQuadBuildV2V(edges,bin,v2v));

  // Count adjacent opposite indexed points for each one 
  uint32_t *adj;
  HXT_CHECK(hxtMalloc(&adj,mesh->vertices.num*sizeof(uint32_t)));
  for (uint32_t i=0; i<mesh->vertices.num; i++) adj[i] = 0;

  for (uint32_t i=0; i<mesh->vertices.num;i++){
    int count = 0;
    for (uint32_t j=0; j<5; j++){
      if (v2v[5*i+j] != UINT32_MAX) count++;
    }
    adj[i] = count;
    if (adj[i] < 2 && isBoundary[i] != 1){ 
      hxtMeshWriteGmsh(mesh,"splitted_test.msh");
      return HXT_ERROR_MSG(HXT_STATUS_ERROR,"One quad only for vertex %d",i); 
    }
  }


  // Find and delete invalid quads
  uint64_t countDeletedTriangles = 0;
  for (uint32_t i=0; i<edges->numEdges; i++){
    uint32_t v0 = edges->node[2*i+0];
    uint32_t v1 = edges->node[2*i+1];
    uint64_t t0 = edges->edg2tri[2*i+0];
    uint64_t t1 = edges->edg2tri[2*i+1];

    if (bin[v0] != bin[v1]) continue;

    // Find other vertices
    uint32_t ov0 = UINT32_MAX;
    uint32_t ov1 = UINT32_MAX;
    for (uint32_t k=0; k<3; k++){
      uint32_t vt1 = mesh->triangles.node[3*t0+k];
      if (vt1 != v0 && vt1 != v1) ov0 = vt1;
      uint32_t vt2 = mesh->triangles.node[3*t1+k];
      if (vt2 != v0 && vt2 != v1) ov1 = vt2;
    }


    if ( (adj[v0] == 3 && isBoundary[v0] != 1) &&
         (adj[v1] == 3 && isBoundary[v1] != 1) ){

        mesh->triangles.color[t0] = UINT16_MAX;
        mesh->triangles.color[t1] = UINT16_MAX;

        adj[v0] = UINT32_MAX;
        adj[v1] = 100;

        vInfo[v0].newInd = v1;


        vInfo[v0].v[0] = (mesh->vertices.coord[4*v0+0]+mesh->vertices.coord[4*v1+0])/2.;
        vInfo[v0].v[1] = (mesh->vertices.coord[4*v0+1]+mesh->vertices.coord[4*v1+1])/2.;
        vInfo[v0].v[2] = (mesh->vertices.coord[4*v0+2]+mesh->vertices.coord[4*v1+2])/2.;

        vInfo[v1].v[0] = (mesh->vertices.coord[4*v0+0]+mesh->vertices.coord[4*v1+0])/2.;
        vInfo[v1].v[1] = (mesh->vertices.coord[4*v0+1]+mesh->vertices.coord[4*v1+1])/2.;
        vInfo[v1].v[2] = (mesh->vertices.coord[4*v0+2]+mesh->vertices.coord[4*v1+2])/2.;


        countDeletedTriangles+=2;
    }
    else if ( (adj[ov0] == 3 && isBoundary[ov0] != 1) &&
         (adj[ov1] == 3 && isBoundary[ov1] != 1) ){

        mesh->triangles.color[t0] = UINT16_MAX;
        mesh->triangles.color[t1] = UINT16_MAX;

        adj[ov0] = UINT32_MAX;
        adj[ov1] = 100;

        vInfo[ov0].newInd = ov1;


        vInfo[ov0].v[0] = (mesh->vertices.coord[4*ov0+0]+mesh->vertices.coord[4*ov1+0])/2.;
        vInfo[ov0].v[1] = (mesh->vertices.coord[4*ov0+1]+mesh->vertices.coord[4*ov1+1])/2.;
        vInfo[ov0].v[2] = (mesh->vertices.coord[4*ov0+2]+mesh->vertices.coord[4*ov1+2])/2.;

        vInfo[ov1].v[0] = (mesh->vertices.coord[4*ov0+0]+mesh->vertices.coord[4*ov1+0])/2.;
        vInfo[ov1].v[1] = (mesh->vertices.coord[4*ov0+1]+mesh->vertices.coord[4*ov1+1])/2.;
        vInfo[ov1].v[2] = (mesh->vertices.coord[4*ov0+2]+mesh->vertices.coord[4*ov1+2])/2.;


        countDeletedTriangles+=2;
    }
  }

  HXT_INFO_COND(opt->verbosity>=1,"    Deleting %lu triangles", countDeletedTriangles);

  uint64_t count=0;
  for (uint64_t i=0; i<mesh->triangles.num; i++){
    if (mesh->triangles.color[i] == UINT16_MAX) count++;
  }
  
  // Printing out 
  // TODO delete
  if(opt->verbosity>=2){
    hxtMeshWriteGmsh(mesh,"splitted9.msh");
    FILE *test;
    hxtPosInit("splitQuad9.pos","edges",&test);

    hxtPosNewView(test,"triangles");
    for (uint64_t i=0; i<mesh->triangles.num; i++){
      uint32_t *v = mesh->triangles.node + 3*i;
  
      if (mesh->triangles.color[i] == UINT16_MAX){
        hxtPosAddTriangle(test,&mesh->vertices.coord[4*v[0]],&mesh->vertices.coord[4*v[1]],&mesh->vertices.coord[4*v[2]],0);
      }

    }
    hxtPosNewView(test,"bin");
    for (uint32_t i=0; i<mesh->vertices.num; i++){
      hxtPosAddPoint(test,&mesh->vertices.coord[4*i],bin[i]);
    }
    hxtPosFinish(test);
  }


  // New index of vertices on triangles
  for (uint64_t i=0; i<mesh->triangles.num; i++){
    uint32_t *v = mesh->triangles.node + 3*i;
    v[0] = vInfo[v[0]].newInd;
    v[1] = vInfo[v[1]].newInd;
    v[2] = vInfo[v[2]].newInd;
  }




  //*************************************************************
  // Triangles
  //*************************************************************
  uint32_t *tris;
  uint16_t *col;
  HXT_CHECK(hxtAlignedMalloc(&tris,(3*mesh->triangles.size)*sizeof(uint32_t)));
  HXT_CHECK(hxtAlignedMalloc(&col,(mesh->triangles.size)*sizeof(uint16_t)));
  uint64_t cT = 0;
  for (uint64_t i=0; i<mesh->triangles.num; i++){
    if (mesh->triangles.color[i] == UINT16_MAX) continue;
    tris[3*cT+0] = mesh->triangles.node[3*i+0];
    tris[3*cT+1] = mesh->triangles.node[3*i+1];
    tris[3*cT+2] = mesh->triangles.node[3*i+2];
    col[cT] = mesh->triangles.color[i];
    cT++;
  }

  for (uint64_t i=0; i<cT; i++){
    mesh->triangles.node[3*i+0] = tris[3*i+0];
    mesh->triangles.node[3*i+1] = tris[3*i+1];
    mesh->triangles.node[3*i+2] = tris[3*i+2];
    mesh->triangles.color[i] = col[i];
  }
  mesh->triangles.num = mesh->triangles.num - countDeletedTriangles;
  HXT_CHECK(hxtAlignedFree(&tris));
  HXT_CHECK(hxtAlignedFree(&col));


  // New vertices
  uint32_t countVertices = 0;
  for (uint32_t i=0; i<mesh->vertices.num; i++){
    if (adj[i] == UINT32_MAX) continue;

    mesh->vertices.coord[4*countVertices+0] = vInfo[i].v[0];
    mesh->vertices.coord[4*countVertices+1] = vInfo[i].v[1];
    mesh->vertices.coord[4*countVertices+2] = vInfo[i].v[2];
    bin[countVertices] = vInfo[i].bin;
    p2t[countVertices] = vInfo[i].p2t;
    isBoundary[countVertices] = vInfo[i].isBoundary;
    vInfo[i].newInd = countVertices;

    countVertices++;

  }

  mesh->vertices.num = countVertices;


  // Relabel triangle vertices
  for (uint64_t i=0; i<mesh->triangles.num; i++){
    uint32_t *v = mesh->triangles.node + 3*i;
    v[0] = vInfo[v[0]].newInd;
    v[1] = vInfo[v[1]].newInd;
    v[2] = vInfo[v[2]].newInd;
  }


  // Relabel lines vertices
  for (uint64_t i=0; i<mesh->lines.num; i++){
    uint32_t *v = mesh->lines.node + 2*i;
    v[0] = vInfo[v[0]].newInd;
    v[1] = vInfo[v[1]].newInd;
  }

  // Relabel points vertices
  for (uint64_t i=0; i<mesh->points.num; i++){
    mesh->points.node[i] = vInfo[mesh->points.node[i]].newInd;
  }


  HXT_CHECK(hxtFree(&vInfo));
  HXT_CHECK(hxtFree(&v2v));
  HXT_CHECK(hxtFree(&adj));
  return HXT_STATUS_OK;
}

//**********************************************************************************************************
//**********************************************************************************************************
//
// FUNCTION to remove doublet quads (vertices with only two quad neighbours) 
//
//**********************************************************************************************************
//**********************************************************************************************************
HXTStatus hxtPointGenQuadRemoveDoubletQuads_OLD(HXTPointGenOptions *opt,
                                            HXTMesh *mesh,
                                            HXTEdges *edges,
                                            uint64_t *p2t,
                                            uint32_t *bin,
                                            uint32_t *isBoundary,
                                            uint64_t *edges2lines)
{
  HXT_INFO_COND(opt->verbosity>=1,"");
  HXT_INFO_COND(opt->verbosity>=1,"--- Removing doublets - interior vertices contained in only two (invalid) quads");
  HXT_INFO_COND(opt->verbosity>=1,"");

  typedef struct vert{
    double v[3];
    uint64_t p2t;
    uint32_t bin;
    uint32_t isBoundary;
    uint32_t newInd;
  }Vert;

  // Filling structure of vertices
  Vert *vInfo;
  HXT_CHECK(hxtMalloc(&vInfo,sizeof(Vert)*mesh->vertices.num));
  
  for (uint32_t i=0; i<mesh->vertices.num; i++){
    vInfo[i].v[0] = mesh->vertices.coord[4*i+0];
    vInfo[i].v[1] = mesh->vertices.coord[4*i+1];
    vInfo[i].v[2] = mesh->vertices.coord[4*i+2];
    vInfo[i].p2t = p2t[i];
    vInfo[i].bin = bin[i];
    vInfo[i].isBoundary = isBoundary[i];
    vInfo[i].newInd = i;
  }


  // Create v2v array 
  // Supposing maximum valence of 5 !!! TODO  
  uint32_t *v2v;
  HXT_CHECK(hxtMalloc(&v2v,mesh->vertices.num*sizeof(uint32_t)*5));
  for (uint32_t i=0; i<5*mesh->vertices.num; i++) v2v[i] = UINT32_MAX;
  HXT_CHECK(hxtPointGenQuadBuildV2V(edges,bin,v2v));


  // Count adjacent opposite indexed points for each one 
  uint32_t *adj;
  HXT_CHECK(hxtMalloc(&adj,mesh->vertices.num*sizeof(uint32_t)));
  for (uint32_t i=0; i<mesh->vertices.num; i++) adj[i] = 0;

  for (uint32_t i=0; i<mesh->vertices.num;i++){
    int count = 0;
    for (uint32_t j=0; j<5; j++){
      if (v2v[5*i+j] != UINT32_MAX) count++;
    }
    adj[i] = count;
    if (adj[i] < 2 && isBoundary[i] != 1) 
      return HXT_ERROR_MSG(HXT_STATUS_ERROR,"One quad only for vertex %d",i); 
  }



  // Create v2e array 
  // Supposing maximum valence of 15 !!! 
  int maxVal = 15;
  uint32_t *v2e;
  HXT_CHECK(hxtMalloc(&v2e,mesh->vertices.num*sizeof(uint32_t)*maxVal));
  for (uint32_t i=0; i<maxVal*mesh->vertices.num; i++) v2e[i] = UINT32_MAX;
  HXT_CHECK(hxtPointGenQuadBuildV2E(edges,bin,v2e));




  // Find and delete invalid quads
  uint64_t countDeletedTriangles = 0;
  for (uint32_t i=0; i<edges->numEdges; i++){
    uint32_t v0 = edges->node[2*i+0];
    uint32_t v1 = edges->node[2*i+1];
    uint64_t t0 = edges->edg2tri[2*i+0];
    uint64_t t1 = edges->edg2tri[2*i+1];

    if (bin[v0] != bin[v1]) continue;

    if ( (adj[v0] == 2 && isBoundary[v0] != 1) ||
         (adj[v1] == 2 && isBoundary[v1] != 1) ){

        mesh->triangles.color[t0] = UINT16_MAX;
        mesh->triangles.color[t1] = UINT16_MAX;

        if (adj[v0]==2 && isBoundary[v0]!=1){
          adj[v0]=UINT32_MAX;
          vInfo[v0].newInd = v1;
        }
        if (adj[v1]==2 && isBoundary[v1]!=1){
          adj[v1]=UINT32_MAX;
          vInfo[v1].newInd = v0;
        }
        countDeletedTriangles+=2;
    }
  }

  HXT_INFO_COND(opt->verbosity>=1,"    Deleting %lu triangles", countDeletedTriangles);

  uint64_t count=0;
  for (uint64_t i=0; i<mesh->triangles.num; i++){
    if (mesh->triangles.color[i] == UINT16_MAX) count++;
  }
  
  // Printing out 
  // TODO delete
  if(opt->verbosity>=2){
    hxtMeshWriteGmsh(mesh,"splitted8.msh");
    FILE *test;
    hxtPosInit("splitQuad8.pos","edges",&test);

    hxtPosNewView(test,"triangles");
    for (uint64_t i=0; i<mesh->triangles.num; i++){
      uint32_t *v = mesh->triangles.node + 3*i;
  
      if (mesh->triangles.color[i] == UINT16_MAX){
        hxtPosAddTriangle(test,&mesh->vertices.coord[4*v[0]],&mesh->vertices.coord[4*v[1]],&mesh->vertices.coord[4*v[2]],0);
      }

    }
    hxtPosNewView(test,"bin");
    for (uint32_t i=0; i<mesh->vertices.num; i++){
      hxtPosAddPoint(test,&mesh->vertices.coord[4*i],bin[i]);
    }
    hxtPosFinish(test);
  }


  // New index of vertices on triangles
  for (uint64_t i=0; i<mesh->triangles.num; i++){
    uint32_t *v = mesh->triangles.node + 3*i;
    v[0] = vInfo[v[0]].newInd;
    v[1] = vInfo[v[1]].newInd;
    v[2] = vInfo[v[2]].newInd;
  }




  //*************************************************************
  // Triangles
  //*************************************************************
  uint32_t *tris;
  uint16_t *col;
  HXT_CHECK(hxtAlignedMalloc(&tris,(3*mesh->triangles.size)*sizeof(uint32_t)));
  HXT_CHECK(hxtAlignedMalloc(&col,(mesh->triangles.size)*sizeof(uint16_t)));
  uint64_t cT = 0;
  for (uint64_t i=0; i<mesh->triangles.num; i++){
    if (mesh->triangles.color[i] == UINT16_MAX) continue;
    tris[3*cT+0] = mesh->triangles.node[3*i+0];
    tris[3*cT+1] = mesh->triangles.node[3*i+1];
    tris[3*cT+2] = mesh->triangles.node[3*i+2];
    col[cT] = mesh->triangles.color[i];
    cT++;
  }

  for (uint64_t i=0; i<cT; i++){
    mesh->triangles.node[3*i+0] = tris[3*i+0];
    mesh->triangles.node[3*i+1] = tris[3*i+1];
    mesh->triangles.node[3*i+2] = tris[3*i+2];
    mesh->triangles.color[i] = col[i];
  }
  mesh->triangles.num = mesh->triangles.num - countDeletedTriangles;
  HXT_CHECK(hxtAlignedFree(&tris));
  HXT_CHECK(hxtAlignedFree(&col));


  // New vertices
  uint32_t countVertices = 0;
  for (uint32_t i=0; i<mesh->vertices.num; i++){
    if (adj[i] == UINT32_MAX) continue;

    mesh->vertices.coord[4*countVertices+0] = vInfo[i].v[0];
    mesh->vertices.coord[4*countVertices+1] = vInfo[i].v[1];
    mesh->vertices.coord[4*countVertices+2] = vInfo[i].v[2];
    bin[countVertices] = vInfo[i].bin;
    p2t[countVertices] = vInfo[i].p2t;
    isBoundary[countVertices] = vInfo[i].isBoundary;
    vInfo[i].newInd = countVertices;

    countVertices++;

  }

  mesh->vertices.num = countVertices;


  // Relabel triangle vertices
  for (uint64_t i=0; i<mesh->triangles.num; i++){
    uint32_t *v = mesh->triangles.node + 3*i;
    v[0] = vInfo[v[0]].newInd;
    v[1] = vInfo[v[1]].newInd;
    v[2] = vInfo[v[2]].newInd;
  }


  // Relabel lines vertices
  for (uint64_t i=0; i<mesh->lines.num; i++){
    uint32_t *v = mesh->lines.node + 2*i;
    v[0] = vInfo[v[0]].newInd;
    v[1] = vInfo[v[1]].newInd;
  }

  // Relabel points vertices
  for (uint64_t i=0; i<mesh->points.num; i++){
    mesh->points.node[i] = vInfo[mesh->points.node[i]].newInd;
  }


  HXT_CHECK(hxtFree(&vInfo));
  HXT_CHECK(hxtFree(&v2v));
  HXT_CHECK(hxtFree(&v2e));
  HXT_CHECK(hxtFree(&adj));
  return HXT_STATUS_OK;
}


//**********************************************************************************************************
//**********************************************************************************************************
//
// FUNCTION to remove doublet quads (vertices with only two quad neighbours) 
//
//**********************************************************************************************************
//**********************************************************************************************************
HXTStatus hxtPointGenQuadRemoveDoubletQuads(HXTPointGenOptions *opt,
                                            HXTMesh *mesh,
                                            HXTEdges *edges,
                                            uint64_t *p2t,
                                            uint32_t *bin,
                                            uint32_t *isBoundary,
                                            uint64_t *edges2lines)
{
  HXT_INFO_COND(opt->verbosity>=1,"");
  HXT_INFO_COND(opt->verbosity>=1,"--- Removing doublets - interior vertices contained in only two (invalid) quads");
  HXT_INFO_COND(opt->verbosity>=1,"");

  typedef struct vert{
    double v[3];
    uint64_t p2t;
    uint32_t bin;
    uint32_t isBoundary;
    uint32_t newInd;
  }Vert;

  // Filling structure of vertices
  Vert *vInfo;
  HXT_CHECK(hxtMalloc(&vInfo,sizeof(Vert)*mesh->vertices.num));
  
  for (uint32_t i=0; i<mesh->vertices.num; i++){
    vInfo[i].v[0] = mesh->vertices.coord[4*i+0];
    vInfo[i].v[1] = mesh->vertices.coord[4*i+1];
    vInfo[i].v[2] = mesh->vertices.coord[4*i+2];
    vInfo[i].p2t = p2t[i];
    vInfo[i].bin = bin[i];
    vInfo[i].isBoundary = isBoundary[i];
    vInfo[i].newInd = i;
  }


  // Create v2v array 
  // Supposing maximum valence of 5 !!! TODO  
  uint32_t *v2v;
  HXT_CHECK(hxtMalloc(&v2v,mesh->vertices.num*sizeof(uint32_t)*5));
  for (uint32_t i=0; i<5*mesh->vertices.num; i++) v2v[i] = UINT32_MAX;
  HXT_CHECK(hxtPointGenQuadBuildV2V(edges,bin,v2v));


  // Count adjacent opposite indexed points for each one 
  uint32_t *adj;
  HXT_CHECK(hxtMalloc(&adj,mesh->vertices.num*sizeof(uint32_t)));
  for (uint32_t i=0; i<mesh->vertices.num; i++) adj[i] = 0;

  for (uint32_t i=0; i<mesh->vertices.num;i++){
    int count = 0;
    for (uint32_t j=0; j<5; j++){
      if (v2v[5*i+j] != UINT32_MAX) count++;
    }
    adj[i] = count;
    if (adj[i] < 2 && isBoundary[i] != 1) 
      return HXT_ERROR_MSG(HXT_STATUS_ERROR,"One quad only for vertex %d",i); 
  }



  // Create v2e array 
  // Supposing maximum valence of 15 !!! 
  int maxVal = 15;
  uint32_t *v2e;
  HXT_CHECK(hxtMalloc(&v2e,mesh->vertices.num*sizeof(uint32_t)*maxVal));
  for (uint32_t i=0; i<maxVal*mesh->vertices.num; i++) v2e[i] = UINT32_MAX;
  HXT_CHECK(hxtPointGenQuadBuildV2E(edges,bin,v2e));


  // Find and delete invalid quads
  uint64_t countDeletedTriangles = 0;
  for (uint32_t i=0; i<mesh->vertices.num; i++){

    if (adj[i] == 2 && isBoundary[i] != 1){ 

      for (uint32_t j=0; j<maxVal; j++){ 
        uint32_t ce = v2e[maxVal*i+j];
        if (ce == UINT32_MAX) continue;
        uint32_t ov = edges->node[2*ce+0] == i ? edges->node[2*ce+1] : edges->node[2*ce+0]; 

        if (isBoundary[ov] == 1) continue;

        uint32_t v0 = edges->node[2*ce+0];
        uint32_t v1 = edges->node[2*ce+1];
        uint64_t t0 = edges->edg2tri[2*ce+0];
        uint64_t t1 = edges->edg2tri[2*ce+1];

        mesh->triangles.color[t0] = UINT16_MAX;
        mesh->triangles.color[t1] = UINT16_MAX;

        adj[i]=UINT32_MAX;
        vInfo[i].newInd = ov;
        countDeletedTriangles+=2;
        break;
      }
    }
  }

  HXT_INFO_COND(opt->verbosity>=1,"    Deleting %lu triangles", countDeletedTriangles);

  uint64_t count=0;
  for (uint64_t i=0; i<mesh->triangles.num; i++){
    if (mesh->triangles.color[i] == UINT16_MAX) count++;
  }
  
  // Printing out 
  // TODO delete
  if(opt->verbosity>=2){
    hxtMeshWriteGmsh(mesh,"splitted9.msh");
    FILE *test;
    hxtPosInit("splitQuad9.pos","edges",&test);

    hxtPosNewView(test,"triangles");
    for (uint64_t i=0; i<mesh->triangles.num; i++){
      uint32_t *v = mesh->triangles.node + 3*i;
  
      if (mesh->triangles.color[i] == UINT16_MAX){
        hxtPosAddTriangle(test,&mesh->vertices.coord[4*v[0]],&mesh->vertices.coord[4*v[1]],&mesh->vertices.coord[4*v[2]],0);
      }

    }
    hxtPosNewView(test,"bin");
    for (uint32_t i=0; i<mesh->vertices.num; i++){
      hxtPosAddPoint(test,&mesh->vertices.coord[4*i],bin[i]);
    }
    hxtPosFinish(test);
  }


  // New index of vertices on triangles
  for (uint64_t i=0; i<mesh->triangles.num; i++){
    uint32_t *v = mesh->triangles.node + 3*i;
    v[0] = vInfo[v[0]].newInd;
    v[1] = vInfo[v[1]].newInd;
    v[2] = vInfo[v[2]].newInd;
  }




  //*************************************************************
  // Triangles
  //*************************************************************
  uint32_t *tris;
  uint16_t *col;
  HXT_CHECK(hxtAlignedMalloc(&tris,(3*mesh->triangles.size)*sizeof(uint32_t)));
  HXT_CHECK(hxtAlignedMalloc(&col,(mesh->triangles.size)*sizeof(uint16_t)));
  uint64_t cT = 0;
  for (uint64_t i=0; i<mesh->triangles.num; i++){
    if (mesh->triangles.color[i] == UINT16_MAX) continue;
    tris[3*cT+0] = mesh->triangles.node[3*i+0];
    tris[3*cT+1] = mesh->triangles.node[3*i+1];
    tris[3*cT+2] = mesh->triangles.node[3*i+2];
    col[cT] = mesh->triangles.color[i];
    cT++;
  }

  for (uint64_t i=0; i<cT; i++){
    mesh->triangles.node[3*i+0] = tris[3*i+0];
    mesh->triangles.node[3*i+1] = tris[3*i+1];
    mesh->triangles.node[3*i+2] = tris[3*i+2];
    mesh->triangles.color[i] = col[i];
  }
  mesh->triangles.num = mesh->triangles.num - countDeletedTriangles;
  HXT_CHECK(hxtAlignedFree(&tris));
  HXT_CHECK(hxtAlignedFree(&col));


  // New vertices
  uint32_t countVertices = 0;
  for (uint32_t i=0; i<mesh->vertices.num; i++){
    if (adj[i] == UINT32_MAX) continue;

    mesh->vertices.coord[4*countVertices+0] = vInfo[i].v[0];
    mesh->vertices.coord[4*countVertices+1] = vInfo[i].v[1];
    mesh->vertices.coord[4*countVertices+2] = vInfo[i].v[2];
    bin[countVertices] = vInfo[i].bin;
    p2t[countVertices] = vInfo[i].p2t;
    isBoundary[countVertices] = vInfo[i].isBoundary;
    vInfo[i].newInd = countVertices;

    countVertices++;

  }

  mesh->vertices.num = countVertices;


  // Relabel triangle vertices
  for (uint64_t i=0; i<mesh->triangles.num; i++){
    uint32_t *v = mesh->triangles.node + 3*i;
    v[0] = vInfo[v[0]].newInd;
    v[1] = vInfo[v[1]].newInd;
    v[2] = vInfo[v[2]].newInd;
  }


  // Relabel lines vertices
  for (uint64_t i=0; i<mesh->lines.num; i++){
    uint32_t *v = mesh->lines.node + 2*i;
    v[0] = vInfo[v[0]].newInd;
    v[1] = vInfo[v[1]].newInd;
  }

  // Relabel points vertices
  for (uint64_t i=0; i<mesh->points.num; i++){
    mesh->points.node[i] = vInfo[mesh->points.node[i]].newInd;
  }


  HXT_CHECK(hxtFree(&vInfo));
  HXT_CHECK(hxtFree(&v2v));
  HXT_CHECK(hxtFree(&v2e));
  HXT_CHECK(hxtFree(&adj));
  return HXT_STATUS_OK;
}

//**********************************************************************************************************
//**********************************************************************************************************
//
// FUNCTION to remove degenerate quads on the boundary
//
// After smoothing 'flat' triangles on the boundary can remain since we cannot move 
// the boundary vertices
// 
// In this functions only one point is added on the interior 
// so it may be necessary to do it recursively
//
// TODO delete
// 
//**********************************************************************************************************
//**********************************************************************************************************
HXTStatus hxtPointGenQuadSnapInvalidBoundary_Jacobian(HXTPointGenOptions *opt,
                                                        HXTMesh *omesh,
                                                        HXTMesh *mesh,
                                                        HXTEdges *edges,
                                                        void *dataTri,
                                                        uint32_t *isBoundary,
                                                        uint64_t *edges2lines,
                                                        uint64_t *p2t,
                                                        uint32_t *bin)
{

  HXT_INFO_COND(opt->verbosity>=1,"");
  HXT_INFO_COND(opt->verbosity>=1,"--- Snap invalid boundary quads to the boundary if possible");
  HXT_INFO_COND(opt->verbosity>=1,"");

  typedef struct vert{
    double v[3];
    uint64_t p2t;
    uint32_t bin;
    uint32_t isBoundary;
    uint32_t newInd;
  }Vert;

  // Filling structure of vertices
  Vert *vInfo;
  HXT_CHECK(hxtMalloc(&vInfo,sizeof(Vert)*mesh->vertices.num));
  
  for (uint32_t i=0; i<mesh->vertices.num; i++){
    vInfo[i].v[0] = mesh->vertices.coord[4*i+0];
    vInfo[i].v[1] = mesh->vertices.coord[4*i+1];
    vInfo[i].v[2] = mesh->vertices.coord[4*i+2];
    vInfo[i].p2t = p2t[i];
    vInfo[i].bin = bin[i];
    vInfo[i].isBoundary = isBoundary[i];
    vInfo[i].newInd = i;
  }


  // Create v2v array 
  // Supposing maximum valence of 5 !!! TODO  
  uint32_t *v2v;
  HXT_CHECK(hxtMalloc(&v2v,mesh->vertices.num*sizeof(uint32_t)*5));
  for (uint32_t i=0; i<5*mesh->vertices.num; i++) v2v[i] = UINT32_MAX;
  HXT_CHECK(hxtPointGenQuadBuildV2V(edges,bin,v2v));


  // Count adjacent opposite indexed points for each one 
  uint32_t *adj;
  HXT_CHECK(hxtMalloc(&adj,mesh->vertices.num*sizeof(uint32_t)));
  for (uint32_t i=0; i<mesh->vertices.num; i++) adj[i] = 0;

  for (uint32_t i=0; i<mesh->vertices.num;i++){
    int count = 0;
    for (uint32_t j=0; j<5; j++){
      if (v2v[5*i+j] != UINT32_MAX) count++;
    }
    adj[i] = count;
    if (adj[i] < 2 && isBoundary[i] != 1) 
      return HXT_ERROR_MSG(HXT_STATUS_ERROR,"One quad only for vertex %d",i); 
  }

  //============================================================
  // Flag edges to be splitted
  //============================================================
  uint16_t *flagEdg;
  HXT_CHECK(hxtMalloc(&flagEdg,2*edges->numEdges*sizeof(uint16_t)));
  for (uint64_t i=0; i<2*edges->numEdges; i++) flagEdg[i] = UINT16_MAX;

  uint16_t *flagTris;
  HXT_CHECK(hxtMalloc(&flagTris,2*mesh->triangles.num*sizeof(uint16_t)));
  for (uint64_t i=0; i<2*mesh->triangles.num; i++) flagTris[i] = UINT16_MAX;


  uint64_t countDeletedTriangles = 0;

  for (uint32_t i=0; i<edges->numEdges; i++){

    uint32_t v0 = edges->node[2*i+0];
    uint32_t v1 = edges->node[2*i+1];

    if (bin[v0] != bin[v1]) continue;
    if (isBoundary[v0] != 1 && isBoundary[v1] != 1) continue;

    uint32_t ov0 = UINT32_MAX;
    uint32_t ov1 = UINT32_MAX;

    uint64_t t0 = edges->edg2tri[2*i+0];
    uint64_t t1 = edges->edg2tri[2*i+1];
  
    for (uint32_t k=0; k<3; k++){
      uint32_t vt1 = mesh->triangles.node[3*t0+k];
      if (vt1 != v0 && vt1 != v1) ov0 = vt1;
      uint32_t vt2 = mesh->triangles.node[3*t1+k];
      if (vt2 != v0 && vt2 != v1) ov1 = vt2;
    }

    uint32_t quadNodes[4] = {v0,ov0,v1,ov1};

    double *p0 = mesh->vertices.coord + 4 *quadNodes[0];
    double *p1 = mesh->vertices.coord + 4 *quadNodes[1];
    double *p2 = mesh->vertices.coord + 4 *quadNodes[2];
    double *p3 = mesh->vertices.coord + 4 *quadNodes[3];

    int badNode = UINT32_MAX;
    double qual = hxtPointGenQuadScaledJacobian(p0,p1,p2,p3,&badNode);

    uint32_t newBin = bin[v0];

    // If quad is invalid (two boundary edges flat)
    if (qual <= 0.1 && isBoundary[quadNodes[badNode]]==1){

      uint32_t oe0 = UINT32_MAX;
      uint32_t oe1 = UINT32_MAX;

      flagEdg[i] = 1;

      // Find other edge for t0
      for (uint32_t j=0; j<3; j++){

        uint32_t ce = edges->tri2edg[3*t0+j];

        uint32_t *v = edges->node + 2*ce;

        uint64_t tt0 = edges->edg2tri[2*ce+0];
        uint64_t tt1 = edges->edg2tri[2*ce+1];

        if (tt1 == UINT64_MAX) continue;
        if (mesh->triangles.color[tt0] != mesh->triangles.color[tt1]) continue;
        if (edges2lines[ce] != UINT64_MAX) continue;
        if (ce == i) continue;

        oe0 = ce;

      }

      // Find other edge for t1
      for (uint32_t j=0; j<3; j++){

        uint32_t ce = edges->tri2edg[3*t1+j];

        uint32_t *v = edges->node + 2*ce;

        uint64_t tt0 = edges->edg2tri[2*ce+0];
        uint64_t tt1 = edges->edg2tri[2*ce+1];

        if (tt1 == UINT64_MAX) continue;
        if (mesh->triangles.color[tt0] != mesh->triangles.color[tt1]) continue;
        if (edges2lines[ce] != UINT64_MAX) continue;
        if (ce == i) continue;

        oe1 = ce;
      }

      if (oe0 == UINT32_MAX || oe1 == UINT32_MAX) continue; 

      flagEdg[oe0] = 1;
      flagEdg[oe1] = 1;



      // Find neighbour triangles of oe0 and oe1 
      uint64_t ot0 = edges->edg2tri[2*oe0+0] == t0 ? edges->edg2tri[2*oe0+1] : edges->edg2tri[2*oe0+0];
      uint64_t ot1 = edges->edg2tri[2*oe1+0] == t1 ? edges->edg2tri[2*oe1+1] : edges->edg2tri[2*oe1+0];


      // FInd the other triangle forming a quad 
      uint64_t ott0 = UINT64_MAX; 
      uint64_t ott1 = UINT64_MAX; 

      for (uint32_t j=0; j<3; j++){

        uint32_t ce = edges->tri2edg[3*ot0+j];

        uint32_t *v = edges->node + 2*ce;

        uint64_t tt0 = edges->edg2tri[2*ce+0];
        uint64_t tt1 = edges->edg2tri[2*ce+1];

        if (bin[v[0]] == bin[v[1]]){
          ott0 = tt0 == ot0 ? tt1 : tt0;
        }
      }


      for (uint32_t j=0; j<3; j++){

        uint32_t ce = edges->tri2edg[3*ot1+j];

        uint32_t *v = edges->node + 2*ce;

        uint64_t tt0 = edges->edg2tri[2*ce+0];
        uint64_t tt1 = edges->edg2tri[2*ce+1];

        if (bin[v[0]] == bin[v[1]]){
          ott1 = tt0 == ot1 ? tt1 : tt0;
        }
      }

      if (ott0 == UINT64_MAX || ott1 == UINT64_MAX) 
        return HXT_ERROR_MSG(HXT_STATUS_ERROR,"Could not find other triangle");

      if (flagTris[ot0] == 1 || flagTris[ot1] == 1) continue;
      if (flagTris[ott0] == 1 || flagTris[ott1] == 1) continue;


      int checkBoundary = 0;
      for (uint32_t j=0; j<3; j++){

        uint32_t e0 = edges->tri2edg[3*ot0+j];
        uint32_t e1 = edges->tri2edg[3*ot1+j];
        uint32_t e2 = edges->tri2edg[3*ott0+j];
        uint32_t e3 = edges->tri2edg[3*ott1+j];

        if (edges2lines[e0] != UINT64_MAX) checkBoundary = 1; 
        if (edges2lines[e1] != UINT64_MAX) checkBoundary = 1; 
        if (edges2lines[e2] != UINT64_MAX) checkBoundary = 1; 
        if (edges2lines[e3] != UINT64_MAX) checkBoundary = 1; 
      }

      if (checkBoundary == 0){
        flagTris[ot0] = 1;
        flagTris[ot1] = 1;
        flagTris[ott0] = 1;
        flagTris[ott1] = 1;


        mesh->triangles.color[t0] = UINT16_MAX;
        mesh->triangles.color[t1] = UINT16_MAX;

        uint32_t vertexToDelete = UINT32_MAX;
        uint32_t vertexToKeep = UINT32_MAX;
        if (isBoundary[v0] == 1){
          vertexToDelete = v1;
          vertexToKeep = v0;
        }
        else if (isBoundary[v1] == 1){
          vertexToDelete = v0;
          vertexToKeep = v1;
        }

        vInfo[vertexToDelete].newInd = vertexToKeep;
        countDeletedTriangles+=2;

      }

    }
  }


  // Print out edges to be splitted
  // TODO delete
  if(opt->verbosity>=2){
    FILE *test;
    hxtPosInit("checkEdges.pos","edges",&test);
    for (uint32_t i=0; i<edges->numEdges; i++){

      if (flagEdg[i] != UINT16_MAX){

        uint32_t *v = edges->node + 2*i;
        hxtPosAddLine(test,&mesh->vertices.coord[4*v[0]],&mesh->vertices.coord[4*v[1]],0);
      }
    }

    hxtPosNewView(test, "triangles");
    for (uint64_t i=0; i<mesh->triangles.num; i++){
      if (flagTris[i] != UINT16_MAX){ 
        uint32_t *v = mesh->triangles.node + 3*i;

        hxtPosAddTriangle(test,&mesh->vertices.coord[4*v[0]],&mesh->vertices.coord[4*v[1]],&mesh->vertices.coord[4*v[2]],0);
      }
    }
    hxtPosFinish(test);
  }

  if(opt->verbosity>=2) HXT_CHECK(hxtMeshWriteGmsh(mesh,"checkMesh.msh"));

  HXT_INFO_COND(opt->verbosity>=1,"    Deleting %lu triangles", countDeletedTriangles);

  uint64_t count=0;
  for (uint64_t i=0; i<mesh->triangles.num; i++){
    if (mesh->triangles.color[i] == UINT16_MAX) count++;
  }
  
  // Printing out 
  // TODO delete
  if(opt->verbosity>=2){
    hxtMeshWriteGmsh(mesh,"splitted8.msh");
    FILE *test;
    hxtPosInit("splitQuad8.pos","edges",&test);

    hxtPosNewView(test,"triangles");
    for (uint64_t i=0; i<mesh->triangles.num; i++){
      uint32_t *v = mesh->triangles.node + 3*i;
  
      if (mesh->triangles.color[i] == UINT16_MAX){
        hxtPosAddTriangle(test,&mesh->vertices.coord[4*v[0]],&mesh->vertices.coord[4*v[1]],&mesh->vertices.coord[4*v[2]],0);
      }

    }
    hxtPosNewView(test,"bin");
    for (uint32_t i=0; i<mesh->vertices.num; i++){
      hxtPosAddPoint(test,&mesh->vertices.coord[4*i],bin[i]);
    }
    hxtPosFinish(test);
  }


  // New index of vertices on triangles
  for (uint64_t i=0; i<mesh->triangles.num; i++){
    uint32_t *v = mesh->triangles.node + 3*i;
    v[0] = vInfo[v[0]].newInd;
    v[1] = vInfo[v[1]].newInd;
    v[2] = vInfo[v[2]].newInd;
  }




  //*************************************************************
  // Triangles
  //*************************************************************
  uint32_t *tris;
  uint16_t *col;
  HXT_CHECK(hxtAlignedMalloc(&tris,(3*mesh->triangles.size)*sizeof(uint32_t)));
  HXT_CHECK(hxtAlignedMalloc(&col,(mesh->triangles.size)*sizeof(uint16_t)));
  uint64_t cT = 0;
  for (uint64_t i=0; i<mesh->triangles.num; i++){
    if (mesh->triangles.color[i] == UINT16_MAX) continue;
    tris[3*cT+0] = mesh->triangles.node[3*i+0];
    tris[3*cT+1] = mesh->triangles.node[3*i+1];
    tris[3*cT+2] = mesh->triangles.node[3*i+2];
    col[cT] = mesh->triangles.color[i];
    cT++;
  }

  for (uint64_t i=0; i<cT; i++){
    mesh->triangles.node[3*i+0] = tris[3*i+0];
    mesh->triangles.node[3*i+1] = tris[3*i+1];
    mesh->triangles.node[3*i+2] = tris[3*i+2];
    mesh->triangles.color[i] = col[i];
  }
  mesh->triangles.num = mesh->triangles.num - countDeletedTriangles;
  HXT_CHECK(hxtAlignedFree(&tris));
  HXT_CHECK(hxtAlignedFree(&col));


  // New vertices
  uint32_t countVertices = 0;
  for (uint32_t i=0; i<mesh->vertices.num; i++){
    if (adj[i] == UINT32_MAX) continue;

    mesh->vertices.coord[4*countVertices+0] = vInfo[i].v[0];
    mesh->vertices.coord[4*countVertices+1] = vInfo[i].v[1];
    mesh->vertices.coord[4*countVertices+2] = vInfo[i].v[2];
    bin[countVertices] = vInfo[i].bin;
    p2t[countVertices] = vInfo[i].p2t;
    isBoundary[countVertices] = vInfo[i].isBoundary;
    vInfo[i].newInd = countVertices;

    countVertices++;

  }

  mesh->vertices.num = countVertices;


  // Relabel triangle vertices
  for (uint64_t i=0; i<mesh->triangles.num; i++){
    uint32_t *v = mesh->triangles.node + 3*i;
    v[0] = vInfo[v[0]].newInd;
    v[1] = vInfo[v[1]].newInd;
    v[2] = vInfo[v[2]].newInd;
  }


  // Relabel lines vertices
  for (uint64_t i=0; i<mesh->lines.num; i++){
    uint32_t *v = mesh->lines.node + 2*i;
    v[0] = vInfo[v[0]].newInd;
    v[1] = vInfo[v[1]].newInd;
  }

  // Relabel points vertices
  for (uint64_t i=0; i<mesh->points.num; i++){
    mesh->points.node[i] = vInfo[mesh->points.node[i]].newInd;
  }


  HXT_CHECK(hxtFree(&vInfo));
  HXT_CHECK(hxtFree(&v2v));
  HXT_CHECK(hxtFree(&adj));






  return HXT_STATUS_OK;
}



//**********************************************************************************************************
//**********************************************************************************************************
//
// FUNCTION to remove degenerate quads on the boundary
//
// After smoothing 'flat' triangles on the boundary can remain since we cannot move 
// the boundary vertices
// 
// In this functions only one point is added on the interior 
// so it may be necessary to do it recursively
// 
//**********************************************************************************************************
//**********************************************************************************************************
HXTStatus hxtPointGenQuadRemoveInvalidBoundary_Jacobian(HXTPointGenOptions *opt,
                                                        HXTMesh *omesh,
                                                        HXTMesh *mesh,
                                                        HXTEdges *edges,
                                                        void *dataTri,
                                                        uint32_t *isBoundary,
                                                        uint64_t *edges2lines,
                                                        uint64_t *p2t,
                                                        uint32_t *bin)
{

  HXT_INFO_COND(opt->verbosity>=1,"");
  HXT_INFO_COND(opt->verbosity>=1,"--- NEW VERSION Removing invalid quads on the boundary by pushing singularity to interior");
  HXT_INFO_COND(opt->verbosity>=1,"");

  //============================================================
  // Flag edges to be splitted
  //============================================================
  uint16_t *flagEdg;
  HXT_CHECK(hxtMalloc(&flagEdg,2*edges->numEdges*sizeof(uint16_t)));
  for (uint64_t i=0; i<2*edges->numEdges; i++) flagEdg[i] = UINT16_MAX;

  for (uint32_t i=0; i<edges->numEdges; i++){

    uint32_t v0 = edges->node[2*i+0];
    uint32_t v1 = edges->node[2*i+1];

    if (bin[v0] != bin[v1]) continue;
    if (isBoundary[v0] != 1 && isBoundary[v1] != 1) continue;

    uint32_t ov0 = UINT32_MAX;
    uint32_t ov1 = UINT32_MAX;

    uint64_t t0 = edges->edg2tri[2*i+0];
    uint64_t t1 = edges->edg2tri[2*i+1];
  
    for (uint32_t k=0; k<3; k++){
      uint32_t vt1 = mesh->triangles.node[3*t0+k];
      if (vt1 != v0 && vt1 != v1) ov0 = vt1;
      uint32_t vt2 = mesh->triangles.node[3*t1+k];
      if (vt2 != v0 && vt2 != v1) ov1 = vt2;
    }

    uint32_t quadNodes[4] = {v0,ov0,v1,ov1};

    double *p0 = mesh->vertices.coord + 4 *quadNodes[0];
    double *p1 = mesh->vertices.coord + 4 *quadNodes[1];
    double *p2 = mesh->vertices.coord + 4 *quadNodes[2];
    double *p3 = mesh->vertices.coord + 4 *quadNodes[3];

    int badNode = UINT32_MAX;
    double qual = hxtPointGenQuadScaledJacobian(p0,p1,p2,p3,&badNode);

    uint32_t newBin = bin[v0];

    if (qual <= 0.1 && isBoundary[quadNodes[badNode]]==1){

      for (uint32_t j=0; j<3; j++){

        uint32_t ce = edges->tri2edg[3*t0+j];

        uint32_t *v = edges->node + 2*ce;

        uint64_t tt0 = edges->edg2tri[2*ce+0];
        uint64_t tt1 = edges->edg2tri[2*ce+1];

        if (tt1 == UINT64_MAX) continue;
        if (mesh->triangles.color[tt0] != mesh->triangles.color[tt1]) continue;
        if (edges2lines[ce] != UINT64_MAX) continue;

        if (v[0]==v0 && v[1]==v1) flagEdg[ce] = !newBin;
        else flagEdg[ce] = newBin;

      }

      for (uint32_t j=0; j<3; j++){

        uint32_t ce = edges->tri2edg[3*t1+j];

        uint32_t *v = edges->node + 2*ce;

        uint64_t tt0 = edges->edg2tri[2*ce+0];
        uint64_t tt1 = edges->edg2tri[2*ce+1];

        if (tt1 == UINT64_MAX) continue;
        if (mesh->triangles.color[tt0] != mesh->triangles.color[tt1]) continue;
        if (edges2lines[ce] != UINT64_MAX) continue;


        if (v[0]==v0 && v[1]==v1) flagEdg[ce] = !newBin;
        else flagEdg[ce] = newBin;
      }
    }
  }


  uint32_t count = 0;
  for (uint32_t i=0; i<edges->numEdges; i++){
    if (flagEdg[i] == 1) count++;
  }
  HXT_INFO_COND(opt->verbosity>=1,"    A. Splitting %d edges", count);

  //Realloc
  HXT_CHECK(hxtVerticesReserve(mesh,mesh->vertices.size+count));
  HXT_CHECK(hxtTrianglesReserve(mesh,mesh->triangles.size+count*4));



  // Print out edges to be splitted
  // TODO delete
  if(opt->verbosity>=2){
    FILE *test;
    hxtPosInit("splitQuad5.pos","edges",&test);
    for (uint32_t i=0; i<edges->numEdges; i++){

      if (flagEdg[i] != UINT16_MAX){

        uint32_t *v = edges->node + 2*i;
        hxtPosAddLine(test,&mesh->vertices.coord[4*v[0]],&mesh->vertices.coord[4*v[1]],0);
      }
    }
    hxtPosFinish(test);
  }

  if(opt->verbosity>=2) HXT_CHECK(hxtMeshWriteGmsh(mesh,"splitted5.msh"));

  //============================================================
  // Split the edges that were flagged
  //============================================================
  for (uint32_t i=0; i<edges->numEdges; i++){

    if (flagEdg[i] == UINT16_MAX) continue;

    uint64_t t0 = edges->edg2tri[2*i+0];
    uint64_t t1 = edges->edg2tri[2*i+1];

    uint32_t ov0 = edges->node[2*i+0];
    uint32_t ov1 = edges->node[2*i+1];

    if (t1 == UINT64_MAX) continue;
    if (mesh->triangles.color[t0] != mesh->triangles.color[t1]) continue;
    if (isBoundary[ov0] == 1 && isBoundary[ov1] == 1) continue;
    //if (edges2lines[i] != UINT64_MAX) continue;

    uint32_t newEdge = UINT32_MAX;
    HXT_CHECK(hxtSplitEdgeIndex(mesh,edges,NULL,NULL,i,&newEdge));

    uint32_t v0 = edges->node[2*newEdge+0];
    uint32_t v1 = edges->node[2*newEdge+1];

    // Find parent triangle on initial mesh for new point
    p2t[v0] = UINT64_MAX;
    uint16_t color = mesh->triangles.color[t0];
    if (omesh->triangles.color[p2t[v1]] == color){
      p2t[v0] = p2t[v1];
    }
    else{
      // Search all triangles 
      // TODO search with distance to triangle to find the true parent  
      for (uint64_t j=0; j<omesh->triangles.num; j++){
        if (omesh->triangles.color[j] == color) p2t[v0] = j;
      }
    }

    if (bin[ov0] == bin[ov1]){
      bin[v0] = bin[ov0] == 1 ? 0: 1;
    }
    else{
      bin[v0] = flagEdg[i];
    }
  }


 
  uint16_t *flagTris;
  HXT_CHECK(hxtMalloc(&flagTris,3*mesh->triangles.num*sizeof(uint16_t)));
  for (uint64_t i=0; i<3*mesh->triangles.num; i++) flagTris[i] = UINT16_MAX;

  uint64_t countRemaining = 0;
  for (uint64_t i=0; i<mesh->triangles.num; i++){
    uint32_t *v = mesh->triangles.node + 3*i;
    if (bin[v[0]] == bin[v[1]] && bin[v[1]] == bin[v[2]]){
      flagTris[i] = 1;
      countRemaining++;
    }
  }
  HXT_INFO_COND(opt->verbosity>=1,"    B. %lu topologically invalid triangles remaining",countRemaining);

  // Printing out 
  // TODO delete
  if(opt->verbosity>=2){

    hxtPointGenQuadPrintInfo(mesh,bin,flagTris,"splitted6.msh","splitQuad6.pos");
  }


  //==================================================================================
  // Split the rest of the triangles (split longest edge) 
  //==================================================================================
  for (uint64_t i=0; i<mesh->triangles.num; i++){

    uint32_t *v = mesh->triangles.node + 3*i;
    int fl = 0;
    if (bin[v[0]] == bin[v[1]] && bin[v[1]] == bin[v[2]]) fl = 1;
    if (fl != 1) continue;

    uint32_t split[3] = {0,0,0};
    for (uint32_t j=0; j<3; j++){
      uint32_t ce = edges->tri2edg[3*i+j];

      uint32_t v0 = edges->node[2*ce+0];
      uint32_t v1 = edges->node[2*ce+1];
      
      uint64_t t0 = edges->edg2tri[2*ce+0];
      uint64_t t1 = edges->edg2tri[2*ce+1];

      if (t1 == UINT64_MAX) continue;
      if (mesh->triangles.color[t0] != mesh->triangles.color[t1]) continue;
      if (isBoundary[v0] == 1 && isBoundary[v1] == 1) continue;
      //if (edges2lines[ce] != UINT64_MAX) continue;

      //uint64_t ot = t0 == i ? t1:t0;
      //if (flagTris[ot] == 0) continue;

      uint32_t ov0 = UINT32_MAX;
      uint32_t ov1 = UINT32_MAX;

      for (uint32_t k=0; k<3; k++){
        uint32_t vt1 = mesh->triangles.node[3*t0+k];
        if (vt1 != v0 && vt1 != v1) ov0 = vt1;
        uint32_t vt2 = mesh->triangles.node[3*t1+k];
        if (vt2 != v0 && vt2 != v1) ov1 = vt2;
      }

      if (bin[v0] == bin[v1] && (bin[ov0] == bin[v0] || bin[ov1] == bin[v0])){
        split[j] = 1;
      }
    }

    // Choose the longest edge 
    double distMax = -1;
    uint32_t indexEdge = UINT32_MAX;
    for (uint32_t j=0; j<3; j++){
      if (split[j] == 0) continue;
      uint32_t ce = edges->tri2edg[3*i+j];
      double dist = distance(&mesh->vertices.coord[4*edges->node[2*ce+0]],&mesh->vertices.coord[4*edges->node[2*ce+1]]);
      if (dist > distMax){
        distMax = dist;
        indexEdge = ce;
      }
    }

    if (indexEdge == UINT32_MAX) continue;

    uint32_t newEdge = UINT32_MAX;
    HXT_CHECK(hxtSplitEdgeIndex(mesh,edges,NULL,NULL,indexEdge,&newEdge));

    uint32_t v0 = edges->node[2*newEdge+0];
    uint32_t v1 = edges->node[2*newEdge+1];

    // Find parent triangle on initial mesh for new point
    p2t[v0] = UINT64_MAX;
    uint16_t color = mesh->triangles.color[i];
    if (omesh->triangles.color[p2t[v1]] == color){
      p2t[v0] = p2t[v1];
    }
    else{
      // Search all triangles 
      // TODO search with distance to triangle to find the true parent  
      for (uint64_t j=0; j<omesh->triangles.num; j++){
        if (omesh->triangles.color[j] == color) p2t[v0] = j;
      }
    }


    if (bin[v0] == UINT32_MAX) bin[v0] = bin[v1]==1?0:1;
    if (bin[v1] == UINT32_MAX) bin[v1] = bin[v0]==1?0:1;

    flagTris[i] = 0;
  }

  // Printing out 
  // TODO delete
  if(opt->verbosity>=2){
    hxtPointGenQuadPrintInfo(mesh,bin,flagTris,"splitted7.msh","splitQuad7.pos");
  }

  // Check for remaining topologically invalid triangle (3 indices same)
  for (uint32_t i=0; i<mesh->triangles.num;i++){
    uint32_t *v = mesh->triangles.node + 3*i;
    if (bin[v[0]] == bin[v[1]] && bin[v[1]] == bin[v[2]] && bin[v[0]] == bin[v[2]]){
      return HXT_ERROR_MSG(HXT_STATUS_ERROR,"Remaining triangle with same indices !!! ");
    }
  }


  //==================================================================================
  // Create vertex 2 vertex connectivity of opposites  
  //==================================================================================

  // Supposing maximum valence of 5 !!! TODO  
  uint32_t *v2v;
  HXT_CHECK(hxtMalloc(&v2v,mesh->vertices.num*sizeof(uint32_t)*5));
  for (uint32_t i=0; i<5*mesh->vertices.num; i++) v2v[i] = UINT32_MAX;

  HXT_CHECK(hxtPointGenQuadBuildV2V(edges,bin,v2v));


  //==================================================================================
  // Smoothing 
  //==================================================================================
  
  HXT_CHECK(hxtPointGenQuadSmoothing(omesh,mesh,dataTri,isBoundary,p2t,v2v,0));
  
  // Printing out 
  // TODO delete
  if(opt->verbosity>=2){
    hxtPointGenQuadPrintInfo(mesh,bin,flagTris,"splitted8.msh","splitQuad8.pos");
  }




  HXT_CHECK(hxtFree(&flagTris));
  HXT_CHECK(hxtFree(&flagEdg));
  HXT_CHECK(hxtFree(&v2v));

  return HXT_STATUS_OK;
}



//**********************************************************************************************************
//**********************************************************************************************************
//
// FUNCTION to remove degenerate quads on the boundary
//
// After smoothing 'flat' triangles on the boundary can remain since we cannot move 
// the boundary vertices
// 
// In this functions three new points are added at once to 'push' singularity on the interior 
// 
//**********************************************************************************************************
//**********************************************************************************************************
HXTStatus hxtPointGenQuadRemoveInvalidBoundary(HXTPointGenOptions *opt,
                                               HXTMesh *omesh,
                                               HXTMesh *mesh,
                                               HXTEdges *edges,
                                               void *dataTri,
                                               uint32_t *isBoundary,
                                               uint64_t *edges2lines,
                                               uint64_t *p2t,
                                               uint32_t *bin)
{

  HXT_INFO_COND(opt->verbosity>=1,"");
  HXT_INFO_COND(opt->verbosity>=1,"--- Removing flat quads on the boundary by pushing singularity to interior");
  HXT_INFO_COND(opt->verbosity>=1,"");

  //============================================================
  // Flag edges to be splitted
  //============================================================
  uint16_t *flagEdg;
  HXT_CHECK(hxtMalloc(&flagEdg,2*edges->numEdges*sizeof(uint16_t)));
  for (uint64_t i=0; i<2*edges->numEdges; i++) flagEdg[i] = UINT16_MAX;

  for (uint32_t i=0; i<edges->numEdges; i++){

    uint32_t v0 = edges->node[2*i+0];
    uint32_t v1 = edges->node[2*i+1];

    if (bin[v0] != bin[v1]) continue;
    if (isBoundary[v0] != 1 && isBoundary[v1] != 1) continue;

    uint32_t ov0 = UINT32_MAX;
    uint32_t ov1 = UINT32_MAX;

    uint64_t t0 = edges->edg2tri[2*i+0];
    uint64_t t1 = edges->edg2tri[2*i+1];
  
    for (uint32_t k=0; k<3; k++){
      uint32_t vt1 = mesh->triangles.node[3*t0+k];
      if (vt1 != v0 && vt1 != v1) ov0 = vt1;
      uint32_t vt2 = mesh->triangles.node[3*t1+k];
      if (vt2 != v0 && vt2 != v1) ov1 = vt2;
    }

    double *p0 = mesh->vertices.coord + 4 * v0;
    double *p1 = mesh->vertices.coord + 4 *ov0;
    double *p2 = mesh->vertices.coord + 4 * v1;
    double *p3 = mesh->vertices.coord + 4 *ov1;

    double const a0 = 180 * hxtAngle3Vertices(p3,p0,p1) / M_PI;
    double const a1 = 180 * hxtAngle3Vertices(p3,p2,p1) / M_PI;


    //TODO correct way to do it is to choose the edge that 
    //are not boundary edges (lines) 
    //ENSURE THAT WE CAN CORRECTLY BUILD EDGES2LINES
    double lim = 170;
    if (isBoundary[v0] == 1 && a0>lim){
      for (uint32_t j=0; j<3; j++){

        uint64_t tt0 = edges->edg2tri[2*edges->tri2edg[3*t0+j]+0];
        uint64_t tt1 = edges->edg2tri[2*edges->tri2edg[3*t0+j]+1];

        if (tt1 == UINT64_MAX) continue;
        if (mesh->triangles.color[tt0] != mesh->triangles.color[tt1]) continue;
        if (edges2lines[edges->tri2edg[3*t0+j]] != UINT64_MAX) continue;

        flagEdg[edges->tri2edg[3*t0+j]] = bin[v0];
      }
      for (uint32_t j=0; j<3; j++){

        uint64_t tt0 = edges->edg2tri[2*edges->tri2edg[3*t1+j]+0];
        uint64_t tt1 = edges->edg2tri[2*edges->tri2edg[3*t1+j]+1];

        if (tt1 == UINT64_MAX) continue;
        if (mesh->triangles.color[tt0] != mesh->triangles.color[tt1]) continue;
        if (edges2lines[edges->tri2edg[3*t1+j]] != UINT64_MAX) continue;

        flagEdg[edges->tri2edg[3*t1+j]] = bin[v0];
      }
    }
    else if(isBoundary[v1] == 1 && a1 > lim){
      for (uint32_t j=0; j<3; j++){

        uint64_t tt0 = edges->edg2tri[2*edges->tri2edg[3*t0+j]+0];
        uint64_t tt1 = edges->edg2tri[2*edges->tri2edg[3*t0+j]+1];

        if (tt1 == UINT64_MAX) continue;
        if (mesh->triangles.color[tt0] != mesh->triangles.color[tt1]) continue;
        if (edges2lines[edges->tri2edg[3*t0+j]] != UINT64_MAX) continue;

        flagEdg[edges->tri2edg[3*t0+j]] = bin[v1];
      }
      for (uint32_t j=0; j<3; j++){

        uint64_t tt0 = edges->edg2tri[2*edges->tri2edg[3*t1+j]+0];
        uint64_t tt1 = edges->edg2tri[2*edges->tri2edg[3*t1+j]+1];

        if (tt1 == UINT64_MAX) continue;
        if (mesh->triangles.color[tt0] != mesh->triangles.color[tt1]) continue;
        if (edges2lines[edges->tri2edg[3*t1+j]] != UINT64_MAX) continue;

        flagEdg[edges->tri2edg[3*t1+j]] = bin[v1];
      }
    }
  }


  uint32_t count = 0;
  for (uint32_t i=0; i<edges->numEdges; i++){
    if (flagEdg[i] == 1) count++;
  }
  HXT_INFO_COND(opt->verbosity>=1,"    A. Splitting %d edges", count);

  //Realloc
  HXT_CHECK(hxtVerticesReserve(mesh,mesh->vertices.size+count));
  HXT_CHECK(hxtTrianglesReserve(mesh,mesh->triangles.size+count*4));



  // Print out edges to be splitted
  // TODO delete
  if(opt->verbosity>=2){
    FILE *test;
    hxtPosInit("splitQuad4_a.pos","edges",&test);
    for (uint32_t i=0; i<edges->numEdges; i++){

      if (flagEdg[i] == 1){

        uint32_t *v = edges->node + 2*i;
        hxtPosAddLine(test,&mesh->vertices.coord[4*v[0]],&mesh->vertices.coord[4*v[1]],0);
      }
    }
    hxtPosFinish(test);
  }

  //============================================================
  // Split the edges that were flagged
  //============================================================
  for (uint32_t i=0; i<edges->numEdges; i++){

    if (flagEdg[i] == UINT16_MAX) continue;

    uint64_t t0 = edges->edg2tri[2*i+0];
    uint64_t t1 = edges->edg2tri[2*i+1];

    uint32_t ov0 = edges->node[2*i+0];
    uint32_t ov1 = edges->node[2*i+1];

    if (t1 == UINT64_MAX) continue;
    if (mesh->triangles.color[t0] != mesh->triangles.color[t1]) continue;
    if (isBoundary[ov0] == 1 && isBoundary[ov1] == 1) continue;
    //if (edges2lines[i] != UINT64_MAX) continue;

    uint32_t newEdge = UINT32_MAX;
    HXT_CHECK(hxtSplitEdgeIndex(mesh,edges,NULL,NULL,i,&newEdge));

    uint32_t v0 = edges->node[2*newEdge+0];
    uint32_t v1 = edges->node[2*newEdge+1];

    // Find parent triangle on initial mesh for new point
    p2t[v0] = UINT64_MAX;
    uint16_t color = mesh->triangles.color[t0];
    if (omesh->triangles.color[p2t[v1]] == color){
      p2t[v0] = p2t[v1];
    }
    else{
      // Search all triangles 
      // TODO search with distance to triangle to find the true parent  
      for (uint64_t j=0; j<omesh->triangles.num; j++){
        if (omesh->triangles.color[j] == color) p2t[v0] = j;
      }
    }

    if (bin[ov0] == bin[ov1]){
      bin[v0] = bin[ov0] == 1 ? 0: 1;
    }
    else{
      bin[v0] = flagEdg[i];
    }
  }


 
  uint16_t *flagTris;
  HXT_CHECK(hxtMalloc(&flagTris,3*mesh->triangles.num*sizeof(uint16_t)));
  for (uint64_t i=0; i<3*mesh->triangles.num; i++) flagTris[i] = UINT16_MAX;

  uint64_t countRemaining = 0;
  for (uint64_t i=0; i<mesh->triangles.num; i++){
    uint32_t *v = mesh->triangles.node + 3*i;
    if (bin[v[0]] == bin[v[1]] && bin[v[1]] == bin[v[2]]){
      flagTris[i] = 1;
      countRemaining++;
    }
  }
  HXT_INFO_COND(opt->verbosity>=1,"    B. %lu topologically invalid triangles remaining",countRemaining);

  // Printing out 
  // TODO delete
  if(opt->verbosity>=2){

    hxtPointGenQuadPrintInfo(mesh,bin,flagTris,"splitted5.msh","splitQuad5.pos");
  }


  //==================================================================================
  // Split the rest of the triangles (split longest edge) 
  //==================================================================================
  for (uint64_t i=0; i<mesh->triangles.num; i++){

    uint32_t *v = mesh->triangles.node + 3*i;
    int fl = 0;
    if (bin[v[0]] == bin[v[1]] && bin[v[1]] == bin[v[2]]) fl = 1;
    if (fl != 1) continue;

    uint32_t split[3] = {0,0,0};
    for (uint32_t j=0; j<3; j++){
      uint32_t ce = edges->tri2edg[3*i+j];

      uint32_t v0 = edges->node[2*ce+0];
      uint32_t v1 = edges->node[2*ce+1];
      
      uint64_t t0 = edges->edg2tri[2*ce+0];
      uint64_t t1 = edges->edg2tri[2*ce+1];

      if (t1 == UINT64_MAX) continue;
      if (mesh->triangles.color[t0] != mesh->triangles.color[t1]) continue;
      if (isBoundary[v0] == 1 && isBoundary[v1] == 1) continue;
      //if (edges2lines[ce] != UINT64_MAX) continue;

      //uint64_t ot = t0 == i ? t1:t0;
      //if (flagTris[ot] == 0) continue;

      uint32_t ov0 = UINT32_MAX;
      uint32_t ov1 = UINT32_MAX;

      for (uint32_t k=0; k<3; k++){
        uint32_t vt1 = mesh->triangles.node[3*t0+k];
        if (vt1 != v0 && vt1 != v1) ov0 = vt1;
        uint32_t vt2 = mesh->triangles.node[3*t1+k];
        if (vt2 != v0 && vt2 != v1) ov1 = vt2;
      }

      if (bin[v0] == bin[v1] && (bin[ov0] == bin[v0] || bin[ov1] == bin[v0])){
        split[j] = 1;
      }
    }

    // Choose the longest edge 
    double distMax = -1;
    uint32_t indexEdge = UINT32_MAX;
    for (uint32_t j=0; j<3; j++){
      if (split[j] == 0) continue;
      uint32_t ce = edges->tri2edg[3*i+j];
      double dist = distance(&mesh->vertices.coord[4*edges->node[2*ce+0]],&mesh->vertices.coord[4*edges->node[2*ce+1]]);
      if (dist > distMax){
        distMax = dist;
        indexEdge = ce;
      }
    }

    if (indexEdge == UINT32_MAX) continue;

    uint32_t newEdge = UINT32_MAX;
    HXT_CHECK(hxtSplitEdgeIndex(mesh,edges,NULL,NULL,indexEdge,&newEdge));

    uint32_t v0 = edges->node[2*newEdge+0];
    uint32_t v1 = edges->node[2*newEdge+1];

    // Find parent triangle on initial mesh for new point
    p2t[v0] = UINT64_MAX;
    uint16_t color = mesh->triangles.color[i];
    if (omesh->triangles.color[p2t[v1]] == color){
      p2t[v0] = p2t[v1];
    }
    else{
      // Search all triangles 
      // TODO search with distance to triangle to find the true parent  
      for (uint64_t j=0; j<omesh->triangles.num; j++){
        if (omesh->triangles.color[j] == color) p2t[v0] = j;
      }
    }


    if (bin[v0] == UINT32_MAX) bin[v0] = bin[v1]==1?0:1;
    if (bin[v1] == UINT32_MAX) bin[v1] = bin[v0]==1?0:1;

    flagTris[i] = 0;
  }

  // Printing out 
  // TODO delete
  if(opt->verbosity>=2){
    hxtPointGenQuadPrintInfo(mesh,bin,flagTris,"splitted6.msh","splitQuad6.pos");
  }

  // Check for remaining topologically invalid triangle (3 indices same)
  int countA = 0;
  for (uint32_t i=0; i<mesh->triangles.num;i++){
    uint32_t *v = mesh->triangles.node + 3*i;
    if (bin[v[0]] == bin[v[1]] && bin[v[1]] == bin[v[2]] && bin[v[0]] == bin[v[2]])
      countA++;
  }
  HXT_INFO_COND(opt->verbosity>=1,"    C. %lu topologically invalid triangles remaining",countA);

  for (uint32_t i=0; i<mesh->triangles.num;i++){
    uint32_t *v = mesh->triangles.node + 3*i;
    if (bin[v[0]] == bin[v[1]] && bin[v[1]] == bin[v[2]] && bin[v[0]] == bin[v[2]]){
      return HXT_ERROR_MSG(HXT_STATUS_ERROR,"Remaining triangle with same indices !!! ");
    }
  }


  //==================================================================================
  // Create vertex 2 vertex connectivity of opposites  
  //==================================================================================

  // Supposing maximum valence of 5 !!! TODO  
  uint32_t *v2v;
  HXT_CHECK(hxtMalloc(&v2v,mesh->vertices.num*sizeof(uint32_t)*5));
  for (uint32_t i=0; i<5*mesh->vertices.num; i++) v2v[i] = UINT32_MAX;

  HXT_CHECK(hxtPointGenQuadBuildV2V(edges,bin,v2v));


  //==================================================================================
  // Smoothing 
  //==================================================================================
  
  HXT_CHECK(hxtPointGenQuadSmoothing(omesh,mesh,dataTri,isBoundary,p2t,v2v,0));
  
  // Printing out 
  // TODO delete
  if(opt->verbosity>=2){
    hxtPointGenQuadPrintInfo(mesh,bin,flagTris,"splitted7.msh","splitQuad7.pos");
  }




  HXT_CHECK(hxtFree(&flagTris));
  HXT_CHECK(hxtFree(&flagEdg));
  HXT_CHECK(hxtFree(&v2v));

  return HXT_STATUS_OK;
}


//**********************************************************************************************************
//**********************************************************************************************************
//
// FUNCTION to split edges with opposite indexed vertices that connect boundary curves
//
// This is a pre-processing step in order to have better conditions on small regions 
// We essentially impose two layers of quads instead of one on those regions 
// Having only one layer usually creates flat boundary quads due to mismatching quantization of opposing 
// feature curves 
// 
//**********************************************************************************************************
//**********************************************************************************************************
HXTStatus hxtPointGenQuadSplitEdgesConnectingCurves(HXTPointGenOptions *opt,
                                                    HXTMesh *omesh,
                                                    HXTMesh *mesh,
                                                    HXTEdges *edges,
                                                    void *dataTri,
                                                    uint32_t *isBoundary,
                                                    uint64_t *edges2lines,
                                                    uint64_t *p2t,
                                                    uint32_t *bin)
{

  HXT_INFO_COND(opt->verbosity>=1,"");
  HXT_INFO_COND(opt->verbosity>=1,"--- Splitting edges that connect opposing boundary curves");
  HXT_INFO_COND(opt->verbosity>=1,"");

  for (uint32_t i=0; i<edges->numEdges; i++){
    uint32_t v0 = edges->node[2*i+0];
    uint32_t v1 = edges->node[2*i+1];

    uint64_t t0 = edges->edg2tri[2*i+0];
    uint64_t t1 = edges->edg2tri[2*i+1];

    if (t1 == UINT64_MAX) continue;
    if (mesh->triangles.color[t0] != mesh->triangles.color[t1]) continue;
    if (edges2lines[i] != UINT64_MAX) continue;

    if (isBoundary[v0] == 1 && isBoundary[v1] ==1 && bin[v0]!=bin[v1]){
    //if (isBoundary[v0] == 1 && isBoundary[v1] ==1 ){

      uint32_t newEdge = UINT32_MAX;
      HXT_CHECK(hxtSplitEdgeIndex(mesh,edges,NULL,NULL,i,&newEdge));

      uint32_t v0 = edges->node[2*newEdge+0];
      uint32_t v1 = edges->node[2*newEdge+1];

      uint32_t v2 = edges->node[2*i+0] == v0 ? edges->node[2*i+1] : edges->node[2*i+0];

      // Find parent triangle on initial mesh for new point
      p2t[v0] = UINT64_MAX;
      uint16_t color = mesh->triangles.color[t0];
      if (omesh->triangles.color[p2t[v1]] == color){
        p2t[v0] = p2t[v1];
      }
      else if(omesh->triangles.color[p2t[v2]]==color){
        p2t[v0] = p2t[v2];
      }
      else{
        // Search all triangles 
        // TODO search with distance to triangle to find the true parent  
        for (uint64_t j=0; j<omesh->triangles.num; j++){
          if (omesh->triangles.color[j] == color) p2t[v0] = j;
        }
      }

      if (bin[v0] == UINT32_MAX) bin[v0] = bin[v1]==1?0:1;
      if (bin[v1] == UINT32_MAX) bin[v1] = bin[v0]==1?0:1;

    }

  }


  return HXT_STATUS_OK;
}

//**********************************************************************************************************
//**********************************************************************************************************
//
// FUNCTION to make a quad topological connection by splitting monochromatic triangles 
//
//**********************************************************************************************************
//**********************************************************************************************************
HXTStatus hxtPointGenQuadTopologyCorrection(HXTPointGenOptions *opt,
                                                    HXTMesh *omesh,
                                                    HXTMesh *mesh,
                                                    HXTEdges *edges,
                                                    void *dataTri,
                                                    uint32_t *isBoundary,
                                                    uint64_t *edges2lines,
                                                    uint64_t *p2t,
                                                    uint32_t *bin)
{

  //==================================================================================
  // Flag triangles to be splitted (3 same indices) 
  //==================================================================================
 
  uint16_t *flagTris;
  HXT_CHECK(hxtMalloc(&flagTris,3*mesh->triangles.num*sizeof(uint16_t)));
  for (uint64_t i=0; i<3*mesh->triangles.num; i++) flagTris[i] = UINT16_MAX;

  uint64_t countSplitTri=0;
  for (uint64_t i=0; i<mesh->triangles.num; i++){
    uint32_t *v = mesh->triangles.node + 3*i;
    if (bin[v[0]] == bin[v[1]] && bin[v[1]] == bin[v[2]]){
      flagTris[i] = 1;
      countSplitTri++;
    }
  }

  HXT_INFO_COND(opt->verbosity>=1,"    A. %lu topologically invalid triangles to be splitted",countSplitTri);

  // Printing out 
  // TODO delete
  if(opt->verbosity>=2){
    hxtPointGenQuadPrintInfo(mesh,bin,flagTris,"splitted1.msh","splitQuad1.pos");
  }


  //==================================================================================
  // Split edges with two flagged triangles 
  //==================================================================================
  for (uint32_t i=0; i<edges->numEdges; i++){

    uint64_t t0 = edges->edg2tri[2*i+0];
    uint64_t t1 = edges->edg2tri[2*i+1];

    if (t1 == UINT64_MAX) continue;
    if (mesh->triangles.color[t0] != mesh->triangles.color[t1]) continue;
    if (edges2lines[i] != UINT64_MAX) continue;

    if (flagTris[t0] == 1 && flagTris[t1] == 1){

      uint32_t newEdge = UINT32_MAX;
      HXT_CHECK(hxtSplitEdgeIndex(mesh,edges,NULL,NULL,i,&newEdge));

      flagTris[t0] = 0;
      flagTris[t1] = 0;
      flagTris[edges->edg2tri[2*newEdge+0]] = 0;
      flagTris[edges->edg2tri[2*newEdge+1]] = 0;

      uint32_t v0 = edges->node[2*newEdge+0];
      uint32_t v1 = edges->node[2*newEdge+1];

      uint32_t v2 = edges->node[2*i+0] == v0 ? edges->node[2*i+1] : edges->node[2*i+0];

      // Find parent triangle on initial mesh for new point
      p2t[v0] = UINT64_MAX;
      uint16_t color = mesh->triangles.color[t0];
      if (omesh->triangles.color[p2t[v1]] == color){
        p2t[v0] = p2t[v1];
      }
      else if(omesh->triangles.color[p2t[v2]]==color){
        p2t[v0] = p2t[v2];
      }
      else{
        // Search all triangles 
        // TODO search with distance to triangle to find the true parent  
        for (uint64_t j=0; j<omesh->triangles.num; j++){
          if (omesh->triangles.color[j] == color) p2t[v0] = j;
        }
      }

      if (bin[v0] == UINT32_MAX) bin[v0] = bin[v1]==1?0:1;
      if (bin[v1] == UINT32_MAX) bin[v1] = bin[v0]==1?0:1;
    }
  }

  // Printing out 
  // TODO delete
  if(opt->verbosity>=2){
    hxtPointGenQuadPrintInfo(mesh,bin,flagTris,"splitted2.msh","splitQuad2.pos");
  }


  uint64_t countSplitTrisIsolated=0;
  for (uint64_t i=0; i<mesh->triangles.num; i++){
    if (flagTris[i] == 1) countSplitTrisIsolated++;
  }
  HXT_INFO_COND(opt->verbosity>=1,"    B. %lu topologically invalid triangles after common edge pass",countSplitTrisIsolated);



  //==================================================================================
  // Split the rest of the triangles (split longest edge)
  //==================================================================================
  for (uint64_t i=0; i<mesh->triangles.num; i++){

    if (flagTris[i] != 1) continue;

    uint32_t split[3] = {0,0,0};
    for (uint32_t j=0; j<3; j++){
      uint32_t ce = edges->tri2edg[3*i+j];

      uint32_t v0 = edges->node[2*ce+0];
      uint32_t v1 = edges->node[2*ce+1];
      
      uint64_t t0 = edges->edg2tri[2*ce+0];
      uint64_t t1 = edges->edg2tri[2*ce+1];

      if (t1 == UINT64_MAX) continue;
      if (mesh->triangles.color[t0] != mesh->triangles.color[t1]) continue;
      if (edges2lines[ce] != UINT64_MAX) continue;

      //uint64_t ot = t0 == i ? t1:t0;
      //if (flagTris[ot] == 0) continue;

      uint32_t ov0 = UINT32_MAX;
      uint32_t ov1 = UINT32_MAX;

      for (uint32_t k=0; k<3; k++){
        uint32_t vt1 = mesh->triangles.node[3*t0+k];
        if (vt1 != v0 && vt1 != v1) ov0 = vt1;
        uint32_t vt2 = mesh->triangles.node[3*t1+k];
        if (vt2 != v0 && vt2 != v1) ov1 = vt2;
      }

      if (bin[v0] == bin[v1] && (bin[ov0] == bin[v0] || bin[ov1] == bin[v0])){
        split[j] = 1;
      }
    }

    // Choose the longest edge 
    double distMax = -1;
    uint32_t indexEdge = UINT32_MAX;
    for (uint32_t j=0; j<3; j++){
      if (split[j] == 0) continue;
      uint32_t ce = edges->tri2edg[3*i+j];
      double dist = distance(&mesh->vertices.coord[4*edges->node[2*ce+0]],
                             &mesh->vertices.coord[4*edges->node[2*ce+1]]);
      if (dist > distMax){
        distMax = dist;
        indexEdge = ce;
      }
    }

    if (indexEdge == UINT32_MAX) continue;

    uint32_t newEdge = UINT32_MAX;
    HXT_CHECK(hxtSplitEdgeIndex(mesh,edges,NULL,NULL,indexEdge,&newEdge));

    flagTris[i] = 0;

    uint32_t v0 = edges->node[2*newEdge+0];
    uint32_t v1 = edges->node[2*newEdge+1];

    uint32_t v2 = edges->node[2*i+0] == v0 ? edges->node[2*i+1] : edges->node[2*i+0];

    // Find parent triangle on initial mesh for new point
    p2t[v0] = UINT64_MAX;
    uint16_t color = mesh->triangles.color[i];
    if (omesh->triangles.color[p2t[v1]] == color){
      p2t[v0] = p2t[v1];
    }
    else if(omesh->triangles.color[p2t[v2]]==color){
      p2t[v0] = p2t[v2];
    }
    else{
      // Search all triangles 
      // TODO search with distance to triangle to find the true parent  
      for (uint64_t j=0; j<omesh->triangles.num; j++){
        if (omesh->triangles.color[j] == color) p2t[v0] = j;
      }
    }


    if (bin[v0] == UINT32_MAX) bin[v0] = bin[v1]==1?0:1;
    if (bin[v1] == UINT32_MAX) bin[v1] = bin[v0]==1?0:1;

  }


  // Printing out 
  // TODO delete
  if(opt->verbosity>=2){
    hxtPointGenQuadPrintInfo(mesh,bin,flagTris,"splitted3.msh","splitQuad3.pos");
  }


  // Check for remaining topologically invalid triangle (3 indices same)
  for (uint32_t i=0; i<mesh->triangles.num;i++){
    uint32_t *v = mesh->triangles.node + 3*i;
    if (flagTris[i] == 1){
      return HXT_ERROR_MSG(HXT_STATUS_ERROR,"Remaining triangle with same indices !!! ");
    }
    if (bin[v[0]] == bin[v[1]] && bin[v[1]] == bin[v[2]] && bin[v[0]] == bin[v[2]]){
      return HXT_ERROR_MSG(HXT_STATUS_ERROR,"Remaining triangle with same indices !!! ");
    }
  }

 
  //==================================================================================
  // Create vertex 2 vertex connectivity of opposites  
  //==================================================================================

  // Supposing maximum valence of 5 !!! TODO  
  uint32_t *v2v;
  HXT_CHECK(hxtMalloc(&v2v,mesh->vertices.num*sizeof(uint32_t)*5));
  for (uint32_t i=0; i<5*mesh->vertices.num; i++) v2v[i] = UINT32_MAX;

  HXT_CHECK(hxtPointGenQuadBuildV2V(edges,bin,v2v));


  //==================================================================================
  // Smoothing 
  //==================================================================================
  
  HXT_CHECK(hxtPointGenQuadSmoothing(omesh,mesh,dataTri,isBoundary,p2t,v2v,0));


  // Printing out 
  // TODO delete
  if(opt->verbosity>=2){
    hxtPointGenQuadPrintInfo(mesh,bin,flagTris,"splitted4.msh","splitQuad4.pos");
  }


  HXT_CHECK(hxtFree(&flagTris));
  HXT_CHECK(hxtFree(&v2v));

  return HXT_STATUS_OK; 
}

//**********************************************************************************************************
//**********************************************************************************************************
//
// Create final quad mesh 
//
//**********************************************************************************************************
//**********************************************************************************************************
HXTStatus hxtPointGenQuadFinal(HXTMesh *mesh,
                               HXTEdges *edges,
                               uint64_t *edges2lines,
                               uint32_t *bin,
                               HXTMesh *qmesh)
{

  HXT_CHECK(hxtVerticesReserve(qmesh, mesh->vertices.num));
  qmesh->vertices.num = mesh->vertices.num;
  qmesh->quads.num = 0;
  for (uint32_t i=0; i<edges->numEdges; i++){
    uint32_t v0 = edges->node[2*i+0];
    uint32_t v1 = edges->node[2*i+1];
    if (bin[v0] == bin[v1]) qmesh->quads.num++;
  }
  HXT_CHECK(hxtQuadsReserve(qmesh,qmesh->quads.num));

  for (uint32_t i=0; i<mesh->vertices.num; i++){
    qmesh->vertices.coord[4*i+0] = mesh->vertices.coord[4*i+0];
    qmesh->vertices.coord[4*i+1] = mesh->vertices.coord[4*i+1];
    qmesh->vertices.coord[4*i+2] = mesh->vertices.coord[4*i+2];
  }

  // Insert quads
  uint64_t cq = 0;
  for (uint32_t i=0; i<edges->numEdges; i++){

    uint32_t v0 = edges->node[2*i+0];
    uint32_t v1 = edges->node[2*i+1];
    if (bin[v0] != bin[v1]) continue;

/*    if (edges2lines[i] != UINT64_MAX)*/
     /*return HXT_ERROR_MSG(HXT_STATUS_ERROR,"A boundary edge should not be quad diagonal");*/

    uint32_t ov0 = UINT32_MAX;
    uint32_t ov1 = UINT32_MAX;
    uint64_t t0 = edges->edg2tri[2*i+0];
    uint64_t t1 = edges->edg2tri[2*i+1];

    if (mesh->triangles.color[t0] != mesh->triangles.color[t1]){
      FILE *outm;
      outm = fopen("outbnd.txt","w");
      fprintf(outm,"ERROR ");
      fclose(outm);
      return HXT_ERROR_MSG(HXT_STATUS_ERROR,"A boundary edge should not be quad diagonal");
    }
  
    for (uint32_t k=0; k<3; k++){
      uint32_t vt1 = mesh->triangles.node[3*t0+k];
      if (vt1 != v0 && vt1 != v1) ov0 = vt1;
      uint32_t vt2 = mesh->triangles.node[3*t1+k];
      if (vt2 != v0 && vt2 != v1) ov1 = vt2;
    }

    if (mesh->triangles.color[t0] == UINT16_MAX ||
        mesh->triangles.color[t1] == UINT16_MAX)
      return HXT_ERROR_MSG(HXT_STATUS_ERROR,"Deleted triangle in final quad formation");

    qmesh->quads.node[4*cq+0] = v0;
    qmesh->quads.node[4*cq+1] = ov0;
    qmesh->quads.node[4*cq+2] = v1;
    qmesh->quads.node[4*cq+3] = ov1;
    qmesh->quads.color[cq] = mesh->triangles.color[t0];
    cq++;

  }

  // Transfer lines and points from mesh to qmesh
  HXT_CHECK(hxtLinesReserve(qmesh,mesh->lines.num));
  qmesh->lines.num = mesh->lines.num;
  HXT_CHECK(hxtPointsReserve(qmesh,mesh->points.num));
  qmesh->points.num = mesh->points.num;

  for (uint64_t i=0; i<mesh->lines.num; i++){
    qmesh->lines.node[2*i+0] = mesh->lines.node[2*i+0];
    qmesh->lines.node[2*i+1] = mesh->lines.node[2*i+1];
    qmesh->lines.color[i] = mesh->lines.color[i];
  }
  for (uint64_t i=0; i<mesh->points.num; i++){
    qmesh->points.node[i] = mesh->points.node[i];
  }



  return HXT_STATUS_OK;
}
 
//**********************************************************************************************************
//**********************************************************************************************************
//
// Create and smooth a quad mesh template 
// 
// Input:
// 
//  - omesh: initial mesh (used for projection)
//  - mesh:  final mesh to be transformed to quad mesh
//  - p2t:   parent triangle on initial mesh for every vertex of mesh
//  - bin:   binary indices 
//
//**********************************************************************************************************
//**********************************************************************************************************
HXTStatus hxtPointGenQuadConvert(HXTPointGenOptions *opt,
                                 HXTMesh *omesh,
                                 HXTMesh *mesh, 
                                 uint64_t *p2t,
                                 uint32_t *bin)
{

  HXT_INFO_COND(opt->verbosity>=1,"");
  HXT_INFO_COND(opt->verbosity>=1,"========= Generation of bipartite quad mesh  ==========");
  HXT_INFO_COND(opt->verbosity>=1,"");

  HXT_INFO_COND(opt->verbosity>=2,"");
  HXT_INFO_COND(opt->verbosity>=2,"--- List of output files");
  HXT_INFO_COND(opt->verbosity>=2,"");
  HXT_INFO_COND(opt->verbosity>=2,"    splitted1.msh     - before (same as finalmeshTriangles.msh)");
  HXT_INFO_COND(opt->verbosity>=2,"    splitted2.msh     - after splitting common edges (1st pass)");
  HXT_INFO_COND(opt->verbosity>=2,"    splitted3.msh     - after splitting remaining isolated triangles");
  HXT_INFO_COND(opt->verbosity>=2,"    splitted4.msh     - after smoothing, before RemoveInvalidBoundary");
  HXT_INFO_COND(opt->verbosity>=2,"    splitted5.msh     - in RemoveInvalidBoundary, after removing flat boundary quads");
  HXT_INFO_COND(opt->verbosity>=2,"    splitted6.msh     - in RemoveInvalidBoundary, after removing remaining invalid triangles");
  HXT_INFO_COND(opt->verbosity>=2,"    splitted7.msh     - in RemoveInvalidBoundary, after smoothing in InvalidBoundary");
  HXT_INFO_COND(opt->verbosity>=2,"    splitted8.msh     - in RemoveDoubletQuads");
  HXT_INFO_COND(opt->verbosity>=2,"");


  // Create rtree with triangles of original mesh 
  double tol = 10e-16;
  void *dataTri;
  HXT_CHECK(hxtRTreeCreate64(&dataTri));
  for (uint64_t i=0; i<omesh->triangles.num; i++){
    double *p0 = omesh->vertices.coord + 4*omesh->triangles.node[3*i+0];
    double *p1 = omesh->vertices.coord + 4*omesh->triangles.node[3*i+1];
    double *p2 = omesh->vertices.coord + 4*omesh->triangles.node[3*i+2];
    HXT_CHECK(hxtAddTriangleInRTree64(p0,p1,p2,tol,i,dataTri));
  }

  //Realloc
  HXT_CHECK(hxtVerticesReserve(mesh,mesh->vertices.num));
  HXT_CHECK(hxtTrianglesReserve(mesh,mesh->triangles.num));


  // Create edges structure
  HXTEdges* edges;
  HXT_CHECK(hxtEdgesCreateNonManifold(mesh,&edges));


  //==================================================================================
  // Create lines to edges array;
  //==================================================================================
  uint32_t *lines2edges;
  HXT_CHECK(hxtMalloc(&lines2edges,mesh->lines.num*sizeof(uint32_t)));
  HXT_CHECK(hxtGetLinesToEdges(edges,lines2edges));

  //==================================================================================
  // Create edges to lines array;
  //==================================================================================
  uint64_t *edges2lines;
  HXT_CHECK(hxtMalloc(&edges2lines,2*edges->numEdges*sizeof(uint64_t)));
  for (uint32_t i=0; i<2*edges->numEdges; i++) edges2lines[i] = UINT64_MAX;
  HXT_CHECK(hxtGetEdgesToLines(edges,lines2edges,edges2lines));


  //==================================================================================
  // Flag boundary vertices
  //==================================================================================
  uint32_t *isBoundary;
  HXT_CHECK(hxtMalloc(&isBoundary,2*mesh->vertices.num*sizeof(uint32_t)));
  for (uint32_t i=0; i<2*mesh->vertices.num; i++) isBoundary[i] = UINT32_MAX;

  for (uint32_t i=0; i<edges->numEdges; i++){
    uint32_t v0 = edges->node[2*i+0];
    uint32_t v1 = edges->node[2*i+1];

    uint64_t t0 = edges->edg2tri[2*i+0];
    uint64_t t1 = edges->edg2tri[2*i+1];

    if (t0 == UINT64_MAX || t1 == UINT64_MAX){
      isBoundary[v0] = 1;
      isBoundary[v1] = 1;
      continue;
    }
    if (mesh->triangles.color[t0] != mesh->triangles.color[t1]){
      isBoundary[v0] = 1;
      isBoundary[v1] = 1;
      continue;
    }
  }

  for (uint64_t i=0; i<mesh->lines.num; i++){
    isBoundary[mesh->lines.node[2*i+0]] = 1;
    isBoundary[mesh->lines.node[2*i+1]] = 1;
  }

  //==================================================================================
  // Check for monochromatic boundary edges
  //==================================================================================
  
  int monochromaticBoundary = 0;
  for (uint32_t i=0; i<edges->numEdges; i++){

    if (edges2lines[i] == UINT64_MAX) continue;

    uint32_t v0 = edges->node[2*i+0];
    uint32_t v1 = edges->node[2*i+1];

    if (bin[v0] == bin[v1]){
      printf("GRAVE_ERROR\n");
      monochromaticBoundary = 1;
    }
  }

  FILE *outm;
  outm = fopen("outbnd.txt","w");
  if (monochromaticBoundary) fprintf(outm,"ERROR ");
  else fprintf(outm," ");
  fclose(outm);

  //==================================================================================
  // Pre-process step to better handle small regions  
  //==================================================================================

  HXT_CHECK(hxtPointGenQuadSplitEdgesConnectingCurves(opt,omesh,mesh,edges,dataTri,isBoundary,edges2lines,p2t,bin));
  if(opt->verbosity>=2) HXT_CHECK(hxtMeshWriteGmsh(mesh,"finalmeshConnectingCurves.msh"));
  if(opt->verbosity>=2) HXT_CHECK(hxtPointGenQuadPrintBin(mesh, bin, "binConnectingCurves.msh"));


  //==================================================================================
  // Topology Correction - MAIN PART 
  //==================================================================================

  HXT_CHECK(hxtPointGenQuadTopologyCorrection(opt,omesh,mesh,edges,dataTri,isBoundary,edges2lines,p2t,bin));
  if(opt->verbosity>=2) HXT_CHECK(hxtMeshWriteGmsh(mesh,"finalmeshTopologyCorrection.msh"));
  if(opt->verbosity>=2) HXT_CHECK(hxtPointGenQuadPrintBin(mesh, bin, "binTopologyCorrection.msh"));



  //==================================================================================
  // Remove remaining invalid triangles on the boundary with checking angles
  //==================================================================================
  
  //HXT_CHECK(hxtPointGenQuadRemoveInvalidBoundary(opt,omesh,mesh,edges,dataTri,isBoundary,edges2lines,p2t,bin));



  //==================================================================================
  // Remove remaining invalid triangles on the boundary with checking Jacobian
  //==================================================================================

  HXT_CHECK(hxtPointGenQuadRemoveInvalidBoundary_Jacobian(opt,omesh,mesh,edges,dataTri,isBoundary,edges2lines,p2t,bin));
  if(opt->verbosity>=2) HXT_CHECK(hxtMeshWriteGmsh(mesh,"finalmeshInvalidJacobian.msh"));
  if(opt->verbosity>=2) HXT_CHECK(hxtPointGenQuadPrintBin(mesh, bin, "binInvalidJacobian.msh"));



  //==================================================================================
  // Remove diamond quads 
  // TODO delete -> done in optimization 
  //==================================================================================


  HXT_CHECK(hxtEdgesDelete(&edges));
  HXT_CHECK(hxtEdgesCreateNonManifold(mesh,&edges));


/*  HXT_CHECK(hxtPointGenQuadRemoveDiamondQuads(opt,mesh,edges,p2t,bin,isBoundary,edges2lines));*/
  /*if(opt->verbosity>=2) HXT_CHECK(hxtMeshWriteGmsh(mesh,"finalmeshDiamondQuads.msh"));*/
  /*if(opt->verbosity>=2) HXT_CHECK(hxtPointGenQuadPrintBin(mesh, bin, "binDiamonds.msh"));*/


  /*HXT_CHECK(hxtEdgesDelete(&edges));*/
  /*HXT_CHECK(hxtEdgesCreateNonManifold(mesh,&edges));*/



  //==================================================================================
  // Remove doublet quads 
  // TODO delete -> done in optimization 
  //==================================================================================
  
/*  HXT_CHECK(hxtPointGenQuadRemoveDoubletQuads(opt,mesh,edges,p2t,bin,isBoundary,edges2lines));*/

  /*HXT_CHECK(hxtEdgesDelete(&edges));*/
  /*HXT_CHECK(hxtEdgesCreateNonManifold(mesh,&edges));*/


  /*if(opt->verbosity>=2) HXT_CHECK(hxtMeshWriteGmsh(mesh,"finalmeshDoubletQuads.msh"));*/
  /*if(opt->verbosity>=2) HXT_CHECK(hxtPointGenQuadPrintBin(mesh, bin, "binDoublets.msh"));*/




  //==================================================================================
  // Create final quad mesh
  //==================================================================================
  HXTMesh *qmesh;
  hxtMeshCreate(&qmesh);
  
  HXT_CHECK(hxtPointGenQuadFinal(mesh,edges,edges2lines,bin,qmesh));
  if(opt->verbosity>=2) HXT_CHECK(hxtMeshWriteGmsh(qmesh,"finalmeshQuadFinal.msh"));

  if(opt->verbosity>=2){
    FILE *test;
    hxtPosInit("binFinal.pos","points",&test);
    for (uint32_t i=0; i<mesh->vertices.num;i++){
      hxtPosAddPoint(test,&mesh->vertices.coord[4*i],bin[i]);
    }
    hxtPosFinish(test);
  }

  if(opt->verbosity>=2){
    FILE *test;
    hxtPosInit("binFinalPerfectMatching.pos","points",&test);
    for (uint32_t i=0; i<edges->numEdges; i++){
      uint32_t v0 = edges->node[2*i+0];
      uint32_t v1 = edges->node[2*i+1];
      if (bin[v0] != bin[v1]) continue;

      hxtPosAddLine(test,&mesh->vertices.coord[4*v0],&mesh->vertices.coord[4*v1],0);
    }


    hxtPosFinish(test);
  }



  //==================================================================================
  // Rewrite qmesh into nmesh
  //==================================================================================
  {
    HXT_CHECK(hxtMeshClear(&mesh));
    HXT_CHECK(hxtVerticesReserve(mesh, qmesh->vertices.num));
    mesh->vertices.num = qmesh->vertices.num;
    for (uint32_t i=0; i<mesh->vertices.num; i++){
      mesh->vertices.coord[4*i+0] = qmesh->vertices.coord[4*i+0];
      mesh->vertices.coord[4*i+1] = qmesh->vertices.coord[4*i+1];
      mesh->vertices.coord[4*i+2] = qmesh->vertices.coord[4*i+2];
    }
    HXT_CHECK(hxtQuadsReserve(mesh,qmesh->quads.num));
    mesh->quads.num = qmesh->quads.num;
    for (uint64_t i=0; i<qmesh->quads.num; i++){
      mesh->quads.node[4*i+0] = qmesh->quads.node[4*i+0]; 
      mesh->quads.node[4*i+1] = qmesh->quads.node[4*i+1]; 
      mesh->quads.node[4*i+2] = qmesh->quads.node[4*i+2]; 
      mesh->quads.node[4*i+3] = qmesh->quads.node[4*i+3]; 
      mesh->quads.color[i] = qmesh->quads.color[i];
    }
    HXT_CHECK(hxtLinesReserve(mesh,qmesh->lines.num));
    mesh->lines.num = qmesh->lines.num;
    for (uint64_t i=0; i<qmesh->lines.num; i++){
      mesh->lines.node[2*i+0] = qmesh->lines.node[2*i+0]; 
      mesh->lines.node[2*i+1] = qmesh->lines.node[2*i+1]; 
      mesh->lines.color[i] = qmesh->lines.color[i];
    }
    HXT_CHECK(hxtPointsReserve(mesh,qmesh->points.num));
    mesh->points.num = qmesh->points.num;
    for (uint64_t i=0; i<qmesh->points.num; i++){
      mesh->points.node[i] = qmesh->points.node[i]; 
    }
  }


  //==================================================================================
  // Final smoothing on quad mesh 
  // TODO 
  //==================================================================================
  //HXT_CHECK(hxtMeshWriteGmsh(qmesh,"quads.msh"));


  HXT_CHECK(hxtQuadMeshOptimize(opt,omesh, dataTri, p2t, mesh));

  if(opt->verbosity>=2) HXT_CHECK(hxtMeshWriteGmsh(qmesh,"finalmeshQuadOptimized.msh"));


  //==================================================================================
  // Output quality and bad vertices
  //==================================================================================
  if(opt->verbosity>=1){
    FILE *q;
    hxtPosInit("quadQuality.pos","scaledJacobian",&q);
    FILE *out = fopen("quadHistogram.txt","w");

    for (uint64_t i=0; i<mesh->quads.num; i++){
      double *p0 = mesh->vertices.coord + 4*mesh->quads.node[4*i+0];
      double *p1 = mesh->vertices.coord + 4*mesh->quads.node[4*i+1];
      double *p2 = mesh->vertices.coord + 4*mesh->quads.node[4*i+2];
      double *p3 = mesh->vertices.coord + 4*mesh->quads.node[4*i+3];
      int temp = UINT32_MAX;
      double qual = hxtPointGenQuadScaledJacobian(p0,p1,p2,p3,&temp);
      hxtPosAddQuad(q,p0,p1,p2,p3,qual);
      fprintf(out,"%f\n",qual);
    }
    fclose(out);
    hxtPosFinish(q);
  }

  if(opt->verbosity>=0){

    int countInvalid = 0;
    double total = 0.0;
    double min = 1;
    double max = -1;
    FILE *q;
    hxtPosInit("quadQualityINVALID.pos","scaledJacobian",&q);
    for (uint64_t i=0; i<mesh->quads.num; i++){
      double *p0 = mesh->vertices.coord + 4*mesh->quads.node[4*i+0];
      double *p1 = mesh->vertices.coord + 4*mesh->quads.node[4*i+1];
      double *p2 = mesh->vertices.coord + 4*mesh->quads.node[4*i+2];
      double *p3 = mesh->vertices.coord + 4*mesh->quads.node[4*i+3];
      int temp = UINT32_MAX;
      double qual = hxtPointGenQuadScaledJacobian(p0,p1,p2,p3,&temp);
      total+= qual;
      if(qual>max) max=qual;
      if(qual<min) min = qual;
      if (qual<=0.00){
        hxtPosAddQuad(q,p0,p1,p2,p3,qual);
        hxtPosAddVector(q,p0,p1);
        countInvalid++;
      }
    }
    hxtPosFinish(q);
    
    double avg = total/(float)mesh->quads.num;

    FILE *outq;
    outq = fopen("outquality.txt","w");
    fprintf(outq,"%d , %f , %f , %f , %d , ", mesh->quads.num, min, avg,max, countInvalid);
    fclose(outq);


    HXT_INFO_COND(opt->verbosity>=0, "");
    HXT_INFO_COND(opt->verbosity>=0, "PointGen | %d quads with negative quality", countInvalid);
    HXT_INFO_COND(opt->verbosity>=0, "");
    HXT_INFO_COND(opt->verbosity>=0, "PointGen | Quad quality min/avg/max: %f, %f, %f", min,avg,max);
    HXT_INFO_COND(opt->verbosity>=0, "");
  }







  //==================================================================================
  // Output singular vertices
  //==================================================================================
  if(0) HXT_CHECK(hxtSurfaceQuadSingularitiesOutput(opt,mesh));



  if(opt->verbosity>=2) HXT_CHECK(hxtMeshWriteGmsh(qmesh,"quads.msh"));
  HXT_CHECK(hxtMeshDelete(&qmesh));


  //==================================================================================
  // Clear things 
  //==================================================================================

  HXT_CHECK(hxtRTreeDelete(&dataTri));

  HXT_CHECK(hxtEdgesDelete(&edges));
  HXT_CHECK(hxtFree(&isBoundary));

  HXT_CHECK(hxtFree(&edges2lines));
  HXT_CHECK(hxtFree(&lines2edges));

  return HXT_STATUS_OK; 
}
 
