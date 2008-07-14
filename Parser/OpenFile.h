#ifndef _OPENFILE_H_
#define _OPENFILE_H_

// Copyright (C) 1997-2008 C. Geuzaine, J.-F. Remacle
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
// Please report all bugs and problems to <gmsh@geuz.org>.

int ParseFile(const char *filename, int close, int warn_if_missing=0);
void ParseString(const char *str);
void OpenProject(const char *filename);
void OpenProjectMacFinder(const char *filename);
int MergeFile(const char *filename, int warn_if_missing=0);
void FixRelativePath(const char *in, char *out);
void FixWindowsPath(const char *in, char *out);
void SplitFileName(const char *name, char *no_ext, char *ext, char *base);
void SetBoundingBox(double xmin, double xmax,
                    double ymin, double ymax, 
                    double zmin, double zmax);
void SetBoundingBox(void);
void AddToTemporaryBoundingBox(double x, double y, double z);

#endif
