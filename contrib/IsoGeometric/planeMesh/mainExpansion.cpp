#include <iostream>
#include <memory>
#include <algorithm>
#include <chrono>

#include <gmsh.h>
#include <gmsh/MTriangle.h>
#include <gmsh/GModel.h>
#include <gmsh/GModelParametrize.h>

#include <gmsh/geodesic_mesh.h>
#include <gmsh/geodesic_algorithm_dijkstra.h>
#include <gmsh/geodesic_algorithm_exact.h>

#include <gmsh/meshGFaceGeodesic.h>

#include "laplacianSmoothing.h"
#include "femSmoothing.h"
#include "meshMacro.h"




#define EPS 1e-6

void periodicBoundaryConditions(const std::vector<MVertex *> & loop,
                                const std::vector<size_t> & indices,
                                const std::vector<double> & lengthFromFirst,
                                std::vector<bool> & bc,
                                std::vector<double> & u,
                                const int nodePerWavelength = 10,
                                const double l0 = 0,
				const double ratio = 1)
{
  size_t i = 1;
  double currentLength, currentAngle, edgeLength, edgeAngle;
  for(size_t k = 0; k < indices.size(); k++) {
    edgeLength = lengthFromFirst[k];
    int nbrNodes = indices[k];
    if (k > 0) {
      edgeLength -= lengthFromFirst[k-1];
      nbrNodes -= indices[k-1];
    }
    if (l0 == 0) {
      edgeAngle = floor(nbrNodes / nodePerWavelength);
      if (edgeAngle == 0) edgeAngle = 1;
    }
    else if (l0 > 0) {
      edgeAngle = floor(edgeLength / l0);
      if (edgeAngle == 0) edgeAngle = 1;
    }
    
    edgeAngle *= 2 * M_PI;
    

      
    currentLength = currentAngle = 0;
    if (k%2) currentLength = edgeLength;
    for(; i <= indices[k]; i++) {
      double dist = loop[i]->point().distance(loop[i - 1]->point());

      int index = loop[i-1]->getIndex();
      bc[index] = true;
      //u[index] = sin(currentAngle);
      u[index] += sin(currentAngle) * dist/2 * ratio;
      
      currentLength += dist;
      currentAngle = currentLength/edgeLength * edgeAngle;

      index = loop[i]->getIndex();
      bc[index] = true;
      //u[index] = sin(currentAngle);
      u[index] += sin(currentAngle) * dist/2 * ratio;

    }
  }
}


void expandCoord(std::vector<double> & coord,
                std::vector<MTriangle*> & triangles,
		 std::map<MVertex *, std::vector<MTriangle *>> & node2Triangles,
                std::vector<double> & u)
{
  for (size_t i = 0; i < triangles.size(); i++) {
    auto t = triangles[i];
    auto v0 = t->getVertex(0)->point();
    auto v1 = t->getVertex(1)->point();
    auto v2 = t->getVertex(2)->point();
    auto d0 = v1 - v0;
    auto d1 = v2 - v0;
    double dx = d0.y() * d1.z() - d0.z() * d1.y();
    double dy = d0.z() * d1.x() - d0.x() * d1.z();
    double dz = d0.x() * d1.y() - d0.y() * d1.x();
    double norm = sqrt(dx*dx + dy*dy + dz*dz);
    dx /= norm;
    dy /= norm;
    dz /= norm;
    for (size_t j = 0; j < 3; j++) {
      auto v = t->getVertex(j);
      int index = v->getIndex();
      int nbrT = node2Triangles[v].size();
      coord[3*index+0] += dx*u[index]/nbrT;
      coord[3*index+1] += dy*u[index]/nbrT;
      coord[3*index+2] += dz*u[index]/nbrT;
    }
  }
}






void manualSmoothing(const double gravity,
                     const double ratio,
                     const std::vector<MTriangle*> & triangles,
                     const std::vector<MVertex*> & nodes,
                     const std::vector<std::vector<MVertex *>> & loops,
                     const std::vector<std::vector<size_t>> & loopIndices,
                     const std::vector<std::vector<double>> & loopLengths,
                     const std::map<MVertex*,bool> & nodeIsVertex,
                     std::vector<double> & u,
                     std::vector<bool> & bc,
                     const std::map<MEdge, std::vector<MTriangle *>, MEdgeLessThan> & edge2Triangles)
{
  u.resize(nodes.size(),0);
  for (size_t i = 0; i < nodes.size(); ++i) {
    if (nodeIsVertex.at(nodes[i]))
      bc[i] = true;
    else
      bc[i] = false;
  }

  size_t maxC = 1e3;
  for (size_t j = 0; j < loops.size(); ++j) {
    const std::vector<MVertex *>& loop = loops[j];
    const std::vector<size_t>& indices = loopIndices[j];
    const std::vector<double>& lengths = loopLengths[j];

    double mult = +1;
    double length = lengths[0];
    int n = indices[0];
    double density = length / n;
    size_t k = 0;


    std::vector<double> t(loop.size());
    std::vector<double> dxdt(loop.size());
    std::vector<double> dydt(loop.size());
    std::vector<double> d2xdt2(loop.size());
    std::vector<double> d2ydt2(loop.size());
    
    // Boundary initialization
    t[0] = 0.;
    u[0] = 0.;
    bc[0] = true;
    for (size_t i = 0; i < loop.size()-1; ++i) {
      auto v0 = loop[i];
      auto v1 = loop[i+1]; 
      size_t i0 = v0->getIndex();
      size_t i1 = v1->getIndex();
      double dist = v1->distance(v0);
      t[i+1] = t[i] + dist;
    }
    
    for (size_t i = 0; i < loop.size(); ++i) {
      auto v0 = loop[i];
      size_t i0 = v0->getIndex();
      t[i] /= t[loop.size()-1];
    }

    for (size_t i = 0; i < loop.size()-1; ++i) {
      auto v0 = loop[i];
      auto v1 = loop[i+1];
      size_t i0 = v0->getIndex();
      dxdt[i] = (v1->x() - v0->x()) / (t[i+1] - t[i]);
      dydt[i] = (v1->y() - v0->y()) / (t[i+1] - t[i]);
    }

    for (size_t i = 0; i < loop.size()-2; ++i) {
      auto v0 = loop[i];
      auto v1 = loop[i+1];
      size_t i0 = v0->getIndex();
      d2xdt2[i] = (dxdt[i+1] - dxdt[i]) / (t[i+1] - t[i]);
      d2ydt2[i] = (dydt[i+1] - dydt[i]) / (t[i+1] - t[i]);
      
      u[i0] = ratio * abs( (dxdt[i] * d2ydt2[i] - d2xdt2[i] * dydt[i])
			   / std::pow(dxdt[i]*dxdt[i] + dydt[i]*dydt[i], 1.5) );
      bc[i0] = true;
    }

    // // Boundary smoothing
    // size_t index0 = loop[0]->getIndex();
    // size_t index1 = loop[indices[0]]->getIndex();
    // for (size_t c = 0; c < maxC; ++c) {
    //   mult = +1;
    //   length = lengths[0];
    //   n = indices[0];
    //   density = length / n;
    //   k = 0;
    //   for (size_t i = 0; i < loop.size()-1; ++i) {
    //     auto v0 = loop[i];
    //     auto i0 = v0->getIndex();
    //     auto v1 = loop[i+1];
    //     auto i1 = v1->getIndex();
    // 	double dist = v1->distance(v0);
    // 	currentLength += dist;

	
    //     if (i > indices[k]) {
    //       ++k;
    //       //mult *= -1;
    //       length = lengths[k] - lengths[k-1];
    //       n = indices[k] - indices[k-1];
    //       density = length / n;
    // 	  currentLength = 0.;
    // 	  index0 = loop[indices[k-1]]->getIndex();
    // 	  index1 = loop[indices[k]]->getIndex();
    //     }
	
    // 	/*
    //     auto vm = loop[(i+loop.size()-1)%loop.size()];
    //     auto im = vm->getIndex();
    //     auto vp = loop[(i+1)%loop.size()];
    //     auto ip = vp->getIndex();
    //     //u[i0] = (u[im] + u[ip])/2 - gravity * (u[i0]-ratio * mult);
    //     u[i0] = (u[im] + u[ip])/2 - gravity * (u[i0] - 10 * density * mult);
    // 	*/

    // 	double mean = (u[i0] + u[i1])/2;
    // 	//mean += - gravity * dist * (mean - 2*ratio);
    // 	//mean += - gravity * dist * (mean - ratio/2);
    // 	double level = currentLength/length;
    // 	level = (1-level) * u[index0] + level * u[index1];
    // 	// std::cout << "ok" << gravity << std::endl;
    // 	mean += - gravity * (mean - level/2);
    // 	//mean += - gravity * (mean - ratio/2);
    // 	if (bc[i0] == false)
    // 	  u[i0] += .2 * ( mean - u[i0] );
    // 	if (bc[i1] == false)
    // 	  u[i1] += .2 * ( mean - u[i1] );

    //   }
    // }

    
    

  //   for (size_t i = 0; i < loop.size(); ++i) {
  //     auto v0 = loop[i];
  //     auto i0 = v0->getIndex();
  //     bc[i0] = true;
  //   }
  }


  // Bulk smoothing
  for (size_t c = 0; c < maxC; ++c) {
    for (size_t i = 0; i < triangles.size(); ++i) {
      auto t =  triangles[i];
      double mean = 0.;
      for (int j = 0; j < 3; ++j) {
        mean += u[t->getVertex(j)->getIndex()];
      }
      mean /= 3;

      auto v0 = t->getVertex(0)->point();
      auto v1 = t->getVertex(1)->point();
      auto v2 = t->getVertex(2)->point();
      double area = 0.5 * abs(v0.x() * (v1.y() - v2.y()) + v1.x() * (v2.y() - v0.y()) + v2.x() * (v0.y() - v1.y()));
    
      //mean += - 10*gravity * area * (mean);
      mean += - gravity * (mean);
      
      for (int j = 0; j < 3; ++j) {
        size_t index = t->getVertex(j)->getIndex();
        if (bc[index]) continue;
        //u[index] += .2 * ((mean - u[index]) - gravity * (u[index]));
        u[index] += .1 * (mean - u[index]);
      }

    }
  }
  
  
  

  //laplacianSmoothing(nodes, edge2Triangles, loops, bc, u, 0.);

}

int main(int argc, char* argv[]) {
  GmshFem gmshFem(argc, argv);
  //gmsh::initialize();

  std::string filename = "../0.geo"; // X, C, S, 0, 8, c3, uk
  double coeff = .2;
  int div = 10;
  int opt = 0;
  int smoothing = 3;
  double gravity = 1;
  double ratio = 1.;
  double clscale = 50; //64;
  double clrefine = 1.;
  int refineOption = 0;
  bool swap = false;
  
  for (int i = 1; i < argc; i++) {
    if (std::string(argv[i]) == "-f" && i + 1 < argc) {
      filename = argv[++i];
      continue;
    } else if (std::string(argv[i]) == "-c" && i + 1 < argc) {
      coeff = std::stod(argv[++i]);
      continue;
    } else if (std::string(argv[i]) == "-d" && i + 1 < argc) {
      div = std::stoi(argv[++i]);
      continue;
    } else if (std::string(argv[i]) == "-o" && i + 1 < argc) {
      opt = std::stoi(argv[++i]);
      continue;
    } else if (std::string(argv[i]) == "-s" && i + 1 < argc) {
      smoothing = std::stoi(argv[++i]);
      continue;
    } else if (std::string(argv[i]) == "-g" && i + 1 < argc) {
      gravity = std::stod(argv[++i]);
      continue;
    } else if (std::string(argv[i]) == "-r" && i + 1 < argc) {
      ratio = std::stod(argv[++i]);
      continue;
    } else if (std::string(argv[i]) == "-clscale" && i + 1 < argc) {
      clscale = std::stod(argv[++i]);
      continue;
    } else if (std::string(argv[i]) == "-clrefine" && i + 1 < argc) {
      clrefine = std::stod(argv[++i]);
      continue;
    } else if (std::string(argv[i]) == "-refineOption" && i + 1 < argc) {
      refineOption = std::stoi(argv[++i]);
      continue;
    } else if (std::string(argv[i]) == "-swap") {
      swap = !swap;
      continue;
    }
  }

  //
  // L O A D S
  //
  std::vector<std::size_t> macroElementNodeTags;
  std::vector<size_t> vertexTags;
  int physicalSurface, physicalBoundary, physicalPoints;
  macroMesh(filename, clscale, clrefine, vertexTags,macroElementNodeTags, refineOption);

  std::vector<MVertex*> nodes;
  std::vector<size_t> nodeTags;
  std::vector<double> nodeCoord;
  std::map<size_t,size_t> nodeTag2Index;
  std::map<MVertex *, bool> nodeIsVertex;
  constructNodes(vertexTags, nodes, nodeTags, nodeCoord, nodeTag2Index, nodeIsVertex);
  
  std::vector<MTriangle*> triangles;
  std::vector<size_t> triangleTags;
  std::vector<size_t> triangleNodeTags;
  std::map<size_t,size_t> triangleTag2Index;
  std::map<MVertex *, std::vector<MTriangle *>> node2Triangles;
  std::map<MEdge, std::vector<MTriangle *>, MEdgeLessThan> edge2Triangles;
  constructTriangles(nodes, nodeTag2Index, triangles, triangleTags,
		     triangleNodeTags, triangleTag2Index, node2Triangles, edge2Triangles);

  std::vector<std::vector<MVertex *>> loops;
  std::vector<std::vector<size_t>> loopIndices;
  std::vector<std::vector<double>> loopLengthFromFirst;
  constructLoops(edge2Triangles, triangles, nodeIsVertex, loops, loopIndices, loopLengthFromFirst);

  
  // Dimentional coeffs
  double lMax = 0, lMin = std::numeric_limits<double>::max();
  for (size_t i = 0; i < triangles.size(); ++i) {
    for (int j =0; j < 3; ++j) {
      auto index0 = 3*triangleNodeTags[3*i+j];
      auto index1 = 3*triangleNodeTags[3*i+(j+1)%3];
      double dx = nodeCoord[index0+0] - nodeCoord[index1+0];
      double dy = nodeCoord[index0+1] - nodeCoord[index1+1];
      double dz = nodeCoord[index0+2] - nodeCoord[index1+2];
      double l = sqrt(dx*dx + dy*dy + dz*dz);
      if (l > lMax)
        lMax = l;
      if (l < lMin)
        lMin = l;
    }
  }
  coeff *= lMax;
  // std::cout << "lMax: " << lMax << std::endl;
  // std::cout << "lMin: " << lMin << std::endl;
  // std::cout << "coeff: " << coeff << std::endl;

  // untangle(nodeTags, nodeCoord, nodes, loops, triangles, tags);

  // std::map<int, std::vector<int>> startEnds;
  // for (size_t i = 0; i < macroElementNodeTags.size()/3; ++i) {
  //   for (int j = 0; j < 3; ++j) {
  //     size_t i0 = macroElementNodeTags[3*i+j];
  //     size_t i1 = macroElementNodeTags[3*i+(j+1)%3];
  //     if (std::find(startEnds[i0].begin(), startEnds[i0].end(), i1) == startEnds[i0].end())
  // 	startEnds[i0].push_back(i1);
  //     if (std::find(startEnds[i1].begin(), startEnds[i1].end(), i0) == startEnds[i1].end())
  // 	startEnds[i1].push_back(i0);
  //   }
  // }

  //
  // E X P A N S I O N
  //
  std::vector<double> u(nodes.size(), 0.);
  std::vector<bool> bc(nodes.size(), false);
  if (smoothing == 0) {
    // Waves on boundary
    for (size_t j = 0; j < loops.size(); ++j) {
      if (opt == 0)
        periodicBoundaryConditions(loops[j], loopIndices[j], loopLengthFromFirst[j], bc, u, div, 0, ratio);
      else if (opt == 1)
        periodicBoundaryConditions(loops[j], loopIndices[j], loopLengthFromFirst[j], bc, u, div, lMin, ratio);
      else
        Msg::Error("Option not defined !");
    }
    laplacianSmoothing(nodes, edge2Triangles, loops, bc, u, 0.);

  }
  else if (smoothing == 1) {
    // Gravity on mesh with GmshFEM
    femSmoothing(physicalSurface, physicalBoundary, physicalPoints, gravity, ratio, nodes, u);

  }
  else if (smoothing == 2) {
    // Gravity on mesh with repeating loops
    manualSmoothing(gravity, ratio, triangles, nodes, loops, loopIndices, loopLengthFromFirst, nodeIsVertex, u, bc, edge2Triangles);

  }
  else {
    // Base on the distance to the boundary
    std::vector<std::size_t> eTags;
    std::vector<std::size_t> eNodeTags;
    gmsh::model::mesh::getElementsByType(MSH_TRI_3, eTags, eNodeTags);
   
    geodesic::Mesh mesh;
    std::vector<size_t> _faces(eNodeTags.size());
    for (size_t i = 0; i < eNodeTags.size(); ++i) {
      _faces[i] = (long) nodeTag2Index[eNodeTags[i]];
    }
    mesh.initialize_mesh_data(nodeCoord, _faces);

    std::vector<geodesic::SurfacePoint> points(nodeTags.size());
    for (size_t i = 0; i < nodeTags.size(); ++i) {
      points[i] = geodesic::SurfacePoint(&mesh.vertices()[i], nodeCoord[3*i],
					 nodeCoord[3*i+1], nodeCoord[3*i+2], geodesic::VERTEX);
    }

    std::vector<geodesic::SurfacePoint> sources;
    for (size_t j = 0; j < loops.size(); ++j) {
      std::vector<MVertex *>& loop = loops[j];
      for (size_t i = 0; i < loop.size()-1; ++i) {
	sources.push_back(points[loop[i]->getIndex()]);
      }
    }

    geodesic::GeodesicAlgorithmExact algorithm(&mesh);
    algorithm.propagate(sources);

    double max = 0.;
    for (size_t i = 0; i < nodes.size(); ++i) {
      double d;
      algorithm.best_source(points[i],d);
      u[i] = d;
      if (max < d)
	max = d;
    }
    for (size_t i = 0; i < nodes.size(); ++i) {
      u[i] /= max;
    }
    for (size_t i = 0; i < nodes.size(); ++i) {
      // u[i] = sqrt(u[i]);
      u[i] = sin(acos(u[i]-1));
	
      u[i] *= max;
      u[i] *= ratio;
    }
    
    

  }

  // gmsh::view::add("ZDistance", 1);
  // std::string currentModel;
  // gmsh::model::getCurrent(currentModel);
  // gmsh::view::addHomogeneousModelData(1, 0, currentModel, "NodeData", nodeTags, u);


  // Move nodes perpendicularly to the surface
  std::vector<double> oldCoord(nodeCoord);
  expandCoord(nodeCoord, triangles, node2Triangles, u);
  std::cout << "Pre Expanded"<< std::endl;

  // Move nodes in the representation
  for (size_t i = 0; i < nodes.size(); i++) {
    gmsh::model::mesh::setNode(nodeTags[i], {nodeCoord[3*i], nodeCoord[3*i+1], nodeCoord[3*i+2]}, {});
  }
  std::cout << "Expanded"<< std::endl;

  // gmsh::fltk::run();

  
  // Compute geodesic
  // geodesic::Mesh mesh;
  // std::vector<size_t> _faces(elementNodeTags.size());
  // for (size_t i = 0; i < elementNodeTags.size(); ++i) {
  //   _faces[i] = (long) nodeTag2Index[elementNodeTags[i]];
  // }
  // mesh.initialize_mesh_data(nodeCoord, _faces);

  // std::vector<geodesic::SurfacePoint> points(nodeTags.size());
  // for (size_t i = 0; i < nodeTags.size(); ++i) {
  //   points[i] = geodesic::SurfacePoint(&mesh.vertices()[i],
  //               nodeCoord[3*i], nodeCoord[3*i+1], nodeCoord[3*i+2], geodesic::VERTEX);
  // }

  // int count = 0;
  // std::map<std::pair<size_t,size_t>, std::vector<geodesic::SurfacePoint>> paths;
  // for (auto startEnd: startEnds) {
  //   std::vector<geodesic::SurfacePoint> pts_start = {points[nodeTag2Index[startEnd.first]]};
  //   std::vector<geodesic::SurfacePoint> pts_end;
  //   for (auto end: startEnd.second) {
  //     pts_end.push_back(points[nodeTag2Index[end]]);
  //     // std::cout << startEnd.first << " -> " << end << " (" << nodeTag2Index[end] << ")" << std::endl;
  //   }
  //   geodesic::GeodesicAlgorithmExact algorithm(&mesh);
  //   // geodesic::GeodesicAlgorithmDijkstra algorithm(&mesh, u);
  //   algorithm.propagate(pts_start, 0, &pts_end);
  //   for (size_t j = 0; j < pts_end.size(); j++) {
  //     std::vector<geodesic::SurfacePoint> & path = paths[{nodeTag2Index[startEnd.first],
  // 	                                                  pts_end[j].base_element()->id()}];
  //     algorithm.trace_back(pts_end[j], path);
  //     // std::cout << startEnd.first << " -> " << pts_end[j].base_element()->id() << std::endl;

  //     if (path.size() == 0) {
  // 	std::cerr << "No paths !" << std::endl;
  // 	return 0;
  //     }
  //     filterPath(path, EPS); // fixme use relative error
      
  //     // Add path
  //     std::vector<int> pathTags(path.size());
  //     std::vector<int> pathLines(path.size()-1);

  //     for (size_t i = 0; i < path.size(); ++i)
  //       pathTags[i] = gmsh::model::geo::addPoint(path[i].x(), path[i].y(), path[i].z());
  //     for (size_t i = 0; i < path.size()-1; ++i)
  //       pathLines[i] = gmsh::model::geo::addLine(pathTags[i], pathTags[i+1]);
  //     gmsh::model::geo::synchronize();

  //     if (false) {
  //       // Write path
  //       std::ofstream outputFile3(filename + "Path"+ std::to_string(count) +".csv");
  //       if (outputFile3.is_open()) {
  //         outputFile3 << "id,x,y,z" << std::endl;
  //         for (size_t i = 0; i < path.size(); ++i) {
  //           int index = path[i].base_element()->id();
  //           outputFile3 << index << "," << path[i].x() << "," << path[i].y() << "," << path[i].z() << "\n";
  //         }

  //         outputFile3.close();
  //         std::cout << "Data saved to " << filename + "Path"+ std::to_string(count) +".csv" << std::endl;
  //       }
  //     }
  //     count++;
  //   }
  // }

  // cutMesh(nodes, triangles, edge2Triangles, macroElementNodeTags, nodeTag2Index, triangle2Index, paths);


  std::vector<double> pts(3*nodes.size());
  for (size_t i = 0; i < nodes.size(); ++i) {
    auto v = nodes[i];
    pts[3*i] = nodeCoord[3*i];
    pts[3*i+1] = nodeCoord[3*i+1];
    pts[3*i+2] = nodeCoord[3*i+2];
  }
  std::vector<size_t> tris(3*triangles.size());
  for (size_t i = 0; i < triangles.size(); ++i) {
    auto t = triangles[i];
    tris[3*i] = t->getVertex(0)->getIndex();
    tris[3*i+1] = t->getVertex(1)->getIndex();
    tris[3*i+2] = t->getVertex(2)->getIndex();
  }
  PolyMesh *pm = createPolyMesh(pts, tris);

  tris.resize(macroElementNodeTags.size());
  for (size_t i = 0; i < macroElementNodeTags.size(); ++i) {
    tris[i] = nodeTag2Index[macroElementNodeTags[i]];
  }

  highOrderPolyMesh hop(pm, tris);

  hop.createGeodesics();

  if (swap)
    hop.swapEdges(1,0,true);


  // std::vector<size_t> newVertexTags;
  // for (size_t t: macroElementNodeTags) {
  //   if (std::find(newVertexTags.begin(), newVertexTags.end(), t) == newVertexTags.end())
  //     newVertexTags.push_back(t);
  // }
  // for (size_t i = 0; i < newVertexTags.size(); ++i)
  //   std::cout << i << " " << newVertexTags[i] << std::endl;

  
  // Create geometry points and lines
  for (size_t i = 0; i < hop.triangles.size()/3; i++) {
    for (int j = 0; j < 3; ++j) {
      size_t i0 = hop.triangles[3*i+j];
      size_t i1 = hop.triangles[3*i+(j+1)%3];
      if (i0 > i1)
	std::swap(i0,i1);

      auto path = hop.geodesics[{i0,i1}];
      std::vector<size_t> pathTags(path.size());
      for (size_t i = 0; i < path.size(); ++i) {
	pathTags[i] = gmsh::model::geo::addPoint(path[i].x(), path[i].y(), path[i].z());
      }
      std::vector<size_t> pathLines(path.size()-1);
      for (size_t i = 0; i < path.size()-1; ++i)
	pathLines[i] = gmsh::model::geo::addLine(pathTags[i], pathTags[i+1]);
    }
  }
  gmsh::model::geo::synchronize();

  // gmsh::fltk::run();

  // Paste
  auto & points = hop.points;
  std::map<size_t, size_t> vertex2Index;
  for (size_t i = 0; i < points.size(); ++i) {
    vertex2Index[points[i].base_element()->id()] = i;
  }
  
  auto & geodesics = hop.geodesics;
  auto & vertices = hop.geoMesh.vertices();
  for (size_t j = 0; j < loops.size(); ++j) {
    auto & loop = loops[j];
    MVertex *v0, *v1;
    size_t k = 0;
    size_t last = 0;
    while (k < loopIndices[j].size()) {
      v0 = loop[last];
      v1 = loop[loopIndices[j][k]];

      size_t i0 = vertex2Index[v0->getIndex()];
      size_t i1 = vertex2Index[v1->getIndex()];

      auto & geodesic0 = geodesics[{i0,i1}];
      geodesic0.clear();
      for (size_t l = last; l <= loopIndices[j][k]; ++l) {
	geodesic0.push_back(geodesic::SurfacePoint(&vertices[loop[l]->getIndex()]));	
      }

      auto & geodesic1 = geodesics[{i1,i0}];
      geodesic1.clear();
      for (size_t l = loopIndices[j][k] + 1; l-- > last; ) {
	geodesic1.push_back(geodesic::SurfacePoint(&vertices[loop[l]->getIndex()]));	
      }


      last = loopIndices[j][k];
      ++k;
    }
  }
  

  // Unexpand
  auto & verts = hop.pm->vertices;
  for (size_t i = 0; i < verts.size(); ++i) {
    verts[i]->position = SVector3(oldCoord[3*i], oldCoord[3*i+1], oldCoord[3*i+2]);
  }
  for (auto it = geodesics.begin(); it != geodesics.end(); ++it) {
    auto & path = it->second;
    for (size_t i = 0; i < path.size(); ++i) {
      auto be = path[i].base_element();
      double x, y, z;
      if (be->type() == geodesic::VERTEX) {
	size_t index = ((geodesic::Vertex *) be)->id();
	x = oldCoord[3*index];
	y = oldCoord[3*index+1];
	z = oldCoord[3*index+2];
      }
      else if (be->type() == geodesic::EDGE) {
	size_t i0 = ((geodesic::Edge *) be)->v0()->id();
	size_t i1 = ((geodesic::Edge *) be)->v1()->id();
	SVector3 e = SVector3(nodeCoord[3*i1] - nodeCoord[3*i0],
			      nodeCoord[3*i1+1] - nodeCoord[3*i0+1],
			      nodeCoord[3*i1+2] - nodeCoord[3*i0+2]);
	SVector3 E = SVector3(path[i].x() - nodeCoord[3*i0],
			      path[i].y() - nodeCoord[3*i0+1],
			      path[i].z() - nodeCoord[3*i0+2]);
	double t = norm(E)/norm(e);	
	e = SVector3(oldCoord[3*i1] - oldCoord[3*i0],
		     oldCoord[3*i1+1] - oldCoord[3*i0+1],
		     oldCoord[3*i1+2] - oldCoord[3*i0+2]);
	e *= t;
	x = oldCoord[3*i0] + e.x();
	y = oldCoord[3*i0+1] + e.y();
	z = oldCoord[3*i0+2] + e.z();
      }
      else
	std::cerr << "FACE SURFACE POINT NOT TAKEN INTO ACCOUNT" << std::endl;
   
      path[i].set(x,y,z);
    }
  }

  // expandCoord(oldCoord, triangles, nbrTriangles, u);
  // for (size_t i = 0; i < nodes.size(); i++) {
  //   gmsh::model::mesh::setNode(tags[i], {oldCoord[3*i], oldCoord[3*i+1], oldCoord[3*i+2]}, {});
  // }


  
  // Cut
  PolyMesh *pm_new = hop.cutMesh();


  
  //
  // W R I T I N G S
  //
  
  // if (false) {
  //   // Write points
  //   std::ofstream outputFile(filename + "Coord.csv");
  //   if (outputFile.is_open()) {
  //     outputFile << "id,x,y,z" << std::endl;
  //     for (size_t i = 0; i < nodeTags.size(); ++i) {
  //       outputFile << i << "," << nodeCoord[3*i] << "," << nodeCoord[3*i+1] << "," << nodeCoord[3*i+2] << "\n";
  //     }

  //     outputFile.close();
  //     std::cout << "Data saved to " << filename + "Coord.csv" << std::endl;
  //   }
  // }

  // gmsh::write(filename+".msh");

  // for (auto kv: startEnds) {
  //   auto start = kv.first;
  //   auto ends = kv.second;
  //   std::cout << nodeTag2Index[start] << ": " << std::endl;
  //   for (auto e: ends)
  //     std::cout << nodeTag2Index[e] << " ";
  //   std::cout << std::endl;
  // }
  
  // std::cout << tags[64] << " "  << tags[57] << " " <<std::endl;
  hop.write(pm_new);

  // for (auto v: pm_new->vertices)
  //   std::cout << v->data << std::endl;

  // for (auto p: hop.points)
  //   std::cout << tags[((geodesic::Vertex *) p.base_element())->id()] << "->" << ((geodesic::Vertex *) p.base_element())->id() << std::endl;
  // for (auto p: hop.triangles)
  //   std::cout << p << std::endl;


  if (argc < 2 || std::string(argv[1]) != "-nopopup") {
    gmsh::fltk::run();
  }
  
  for (auto triangle: triangles) {
    delete triangle;
  }
  for (auto node: nodes) {
    delete node;
  }

  //gmsh::finalize();

  return 0;
}

