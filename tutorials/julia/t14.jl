# See the corresponding Python tutorial for detailed comments.

import gmsh

gmsh.initialize()

gmsh.model.add("t14")

m = 0.5  # mesh size
h = 2  # geometry height in the z-direction

gmsh.model.geo.addPoint(0, 0, 0, m, 1)
gmsh.model.geo.addPoint(10, 0, 0, m, 2)
gmsh.model.geo.addPoint(10, 10, 0, m, 3)
gmsh.model.geo.addPoint(0, 10, 0, m, 4)

gmsh.model.geo.addPoint(4, 4, 0, m, 5)
gmsh.model.geo.addPoint(6, 4, 0, m, 6)
gmsh.model.geo.addPoint(6, 6, 0, m, 7)
gmsh.model.geo.addPoint(4, 6, 0, m, 8)

gmsh.model.geo.addPoint(2, 0, 0, m, 9)
gmsh.model.geo.addPoint(8, 0, 0, m, 10)
gmsh.model.geo.addPoint(2, 10, 0, m, 11)
gmsh.model.geo.addPoint(8, 10, 0, m, 12)

gmsh.model.geo.addLine(1, 9, 1)
gmsh.model.geo.addLine(9, 10, 2)
gmsh.model.geo.addLine(10, 2, 3)

gmsh.model.geo.addLine(2, 3, 4)
gmsh.model.geo.addLine(3, 12, 5)
gmsh.model.geo.addLine(12, 11, 6)

gmsh.model.geo.addLine(11, 4, 7)
gmsh.model.geo.addLine(4, 1, 8)
gmsh.model.geo.addLine(5, 6, 9)

gmsh.model.geo.addLine(6, 7, 10)
gmsh.model.geo.addLine(7, 8, 11)
gmsh.model.geo.addLine(8, 5, 12)

gmsh.model.geo.addCurveLoop([6, 7, 8, 1, 2, 3, 4, 5], 13)
gmsh.model.geo.addCurveLoop([11, 12, 9, 10], 14)
gmsh.model.geo.addPlaneSurface([13, 14], 15)

e = gmsh.model.geo.extrude([(2, 15)], 0, 0, h)

gmsh.model.geo.synchronize()

domain_tag = e[2][2]
domain_physical_tag = 1001
gmsh.model.addPhysicalGroup(3,[domain_tag],domain_physical_tag,"Whole domain")

terminal_tags = [e[4][2], e[6][2], e[8][2], e[10][2]]
terminals_physical_tag = 2001
gmsh.model.addPhysicalGroup(2,terminal_tags,terminals_physical_tag,"Terminals")

boundary_dimtags = gmsh.model.getBoundary([(3, domain_tag)])
boundary_tags = []
complement_tags = []
for tag in boundary_dimtags
    append!(complement_tags,tag[2])
    append!(boundary_tags,tag[2])
end

for tag in terminal_tags
    deleteat!(terminal_tags, findall(x->x==tag,terminal_tags))
end

boundary_physical_tag = 2002
gmsh.model.addPhysicalGroup(2,
                            boundary_tags,
                            boundary_physical_tag,
                            "Boundary")

complement_physical_tag = 2003
gmsh.model.addPhysicalGroup(2,
                            complement_tags,
                            complement_physical_tag,
                            "Complement")

gmsh.model.mesh.addHomologyRequest("Homology", [domain_physical_tag],
                                   [terminals_physical_tag],
                                   [0, 1, 2, 3])

gmsh.model.mesh.addHomologyRequest("Homology",[domain_physical_tag],
                                   [complement_physical_tag],
                                   [0, 1, 2, 3])


gmsh.model.mesh.addHomologyRequest("Cohomology",[domain_physical_tag],
                                   [terminals_physical_tag],
                                   [0, 1, 2, 3])

# more examples
# gmsh.model.mesh.addHomologyRequest()
# gmsh.model.mesh.addHomologyRequest("Homology", domainTags=[domain_physical_tag])
# gmsh.model.mesh.addHomologyRequest("Homology", domainTags=[domain_physical_tag],
#                                    subdomainTags=[boundary_physical_tag],
#                                    dims=[0,1,2,3])

# Generate the mesh and perform the requested homology computations
gmsh.model.mesh.generate(3)

# For more information, see M. Pellikka, S. Suuriniemi, L. Kettunen and
# C. Geuzaine. Homology and cohomology computation in finite element
# modeling. SIAM Journal on Scientific Computing 35(5), pp. 1195-1214, 2013.

gmsh.write("t14.msh")

if !("-nopopup" in ARGS)
    gmsh.fltk.run()
end

gmsh.finalize()
