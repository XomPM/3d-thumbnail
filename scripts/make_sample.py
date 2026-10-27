"""Create original STL, OBJ and 3MF fixtures using Python's standard library."""

import math
import struct
import zipfile
import xml.etree.ElementTree as ET
from pathlib import Path

CORE = "http://schemas.microsoft.com/3dmanufacturing/core/2015/02"
ET.register_namespace("", CORE)


def element(parent, name, **attributes):
    return ET.SubElement(parent, f"{{{CORE}}}{name}", attributes)


def model_document():
    model = ET.Element(f"{{{CORE}}}model", {"unit": "millimeter", "{http://www.w3.org/XML/1998/namespace}lang": "en-US"})
    return model, element(model, "resources")


def mesh_object(resources, identifier, triangles):
    obj = element(resources, "object", id=str(identifier), type="model")
    mesh = element(obj, "mesh")
    vertices = element(mesh, "vertices")
    faces = element(mesh, "triangles")
    indices = {}
    for triangle in triangles:
        face = []
        for point in triangle:
            if point not in indices:
                indices[point] = len(indices)
                element(vertices, "vertex", **dict(zip(("x", "y", "z"), (format(v, ".9g") for v in point))))
            face.append(indices[point])
        element(faces, "triangle", **dict(zip(("v1", "v2", "v3"), map(str, face))))


def write_3mf(path, model):
    parts = {
        "[Content_Types].xml": b'''<?xml version="1.0" encoding="UTF-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>
</Types>''',
        "_rels/.rels": b'''<?xml version="1.0" encoding="UTF-8"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>
</Relationships>''',
        "3D/3dmodel.model": ET.tostring(model, encoding="utf-8", xml_declaration=True),
    }
    with zipfile.ZipFile(path, "w") as archive:
        for name, data in parts.items():
            entry = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(entry, data)


def additional_fixtures(destination, triangles):
    lines = ["# Original bearing fixture; no materials or external files"]
    for triangle in triangles:
        lines.extend("v " + " ".join(format(v, ".9g") for v in point) for point in triangle)
        lines.append("f -3 -2 -1")
    (destination / "bearing.obj").write_text("\n".join(lines) + "\n", encoding="ascii")
    model, resources = model_document()
    mesh_object(resources, 1, triangles)
    element(element(model, "build"), "item", objectid="1")
    write_3mf(destination / "bearing.3mf", model)

    points = [(0, 0, 0), (2, 0, 0), (0, 2, 0), (0, 0, 2)]
    tetrahedron = [[points[i] for i in face] for face in [(0, 2, 1), (0, 1, 3), (1, 2, 3), (2, 0, 3)]]
    model, resources = model_document()
    mesh_object(resources, 1, tetrahedron)
    obj = element(resources, "object", id="2", type="model")
    element(element(obj, "components"), "component", objectid="1", transform="1 0 0 0 1 0 0 0 1 10 0 0")
    obj = element(resources, "object", id="3", type="model")
    element(element(obj, "components"), "component", objectid="2", transform="0 1 0 -1 0 0 0 0 1 0 0 0")
    # Unused resources must not enlarge the rendered build's bounds.
    mesh_object(resources, 99, [[(x + 10000, y, z) for x, y, z in face] for face in tetrahedron])
    build = element(model, "build")
    for offset in (100, 150):
        element(build, "item", objectid="3", transform=f"1 0 0 0 1 0 0 0 1 {offset} 0 0")
    write_3mf(destination / "nested_instances.3mf", model)
    build.clear()
    write_3mf(destination / "empty_build.3mf", model)


def box(x0, y0, z0, x1, y1, z1):
    points = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
              (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
    faces = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4),
             (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    triangles = []
    for a, b, c, d in faces:
        triangles.extend([[points[a], points[b], points[c]], [points[a], points[c], points[d]]])
    return triangles


def tube(cx, cy, bottom, top, outer_radius, inner_radius, segments=64):
    triangles = []
    for index in range(segments):
        angles = [2 * math.pi * index / segments, 2 * math.pi * (index + 1) / segments]
        points = [(cx + radius * math.cos(angle), cy + radius * math.sin(angle), height)
                  for height, radius in [(bottom, outer_radius), (top, outer_radius),
                                         (bottom, inner_radius), (top, inner_radius)]
                  for angle in angles]
        for a, b, c, d in [(0, 1, 3, 2), (4, 6, 7, 5), (2, 3, 7, 6), (0, 4, 5, 1)]:
            triangles.extend([[points[a], points[b], points[c]], [points[a], points[c], points[d]]])
    return triangles


def main():
    destination = Path(__file__).resolve().parent.parent / "samples"
    destination.mkdir(exist_ok=True)
    triangles = box(-30, -22, 0, 30, 22, 5) + tube(0, 0, 5, 30, 17, 10)
    header = b"3D Thumbnail original bearing fixture; not a manufacturing model".ljust(80, b" ")
    binary = bytearray(header + struct.pack("<I", len(triangles)))
    ascii_lines = ["solid bearing"]
    for triangle in triangles:
        values = [coordinate for vertex in triangle for coordinate in vertex]
        binary.extend(struct.pack("<12fH", 0, 0, 0, *values, 0))
        ascii_lines.extend(["facet normal 0 0 0", "outer loop"])
        ascii_lines.extend("vertex " + " ".join(format(v, ".9g") for v in vertex) for vertex in triangle)
        ascii_lines.extend(["endloop", "endfacet"])
    ascii_lines.append("endsolid bearing")
    (destination / "bearing_binary.stl").write_bytes(binary)
    (destination / "bearing_ascii.stl").write_text("\n".join(ascii_lines) + "\n", encoding="ascii")
    additional_fixtures(destination, triangles)
    print(f"Created original STL, OBJ and 3MF fixtures: {len(triangles)} bearing triangles each.")


if __name__ == "__main__":
    main()
