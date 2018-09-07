// Gmsh - Copyright (C) 1997-2018 C. Geuzaine, J.-F. Remacle
//
// See the LICENSE.txt file for license information. Please report all
// bugs and problems to the public mailing list <gmsh@onelab.info>.

#ifndef _MESH_VOLUME_H_
#define _MESH_VOLUME_H_

#include "Plugin.h"

extern "C"{
  GMSH_Plugin *GMSH_RegisterMeshVolumePlugin();
}

class GMSH_MeshVolumePlugin: public GMSH_MeshPlugin{
public:
  GMSH_MeshVolumePlugin(){}
  std::string    getName()      const{ return "MeshVolume"; }
  std::string    getShortHelp() const{ return "Volume of a mesh"; }
  std::string    getHelp()      const;
  std::string    getAuthor()    const{ return "N. Marsic"; }
  int            getNbOptions() const;
  StringXNumber* getOption(int iopt);

  void run();
};

#endif
