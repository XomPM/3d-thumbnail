"""Create original PLY, glTF/GLB and FBX fixtures from our bearing OBJ (stdlib only)."""
import base64
import json
from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
SAMPLES = ROOT / "samples"
vertices, faces = [], []
for line in (SAMPLES / "bearing.obj").read_text().splitlines():
    fields = line.split()
    if fields and fields[0] == "v":
        vertices.append(tuple(map(float, fields[1:4])))
    elif fields and fields[0] == "f":
        indices = [int(value.split('/')[0]) for value in fields[1:]]
        faces.append(tuple(index - 1 if index > 0 else len(vertices) + index for index in indices))
assert all(len(face) == 3 for face in faces)

header = ("ply\nformat {format} 1.0\ncomment Original 3D Thumbnail fixture\n"
          f"element vertex {len(vertices)}\nproperty float x\nproperty float y\nproperty float z\n"
          f"element face {len(faces)}\nproperty list uchar int vertex_indices\nend_header\n")
(SAMPLES / "bearing_ascii.ply").write_text(header.format(format="ascii") +
    ''.join(' '.join(map(str, point)) + '\n' for point in vertices) +
    ''.join('3 ' + ' '.join(map(str, face)) + '\n' for face in faces), encoding="ascii")
(SAMPLES / "bearing.ply").write_bytes(header.format(format="binary_little_endian").encode() +
    b''.join(struct.pack('<3f', *point) for point in vertices) +
    b''.join(struct.pack('<B3i', 3, *face) for face in faces))

gltf_vertices = [(x, z, -y) for x, y, z in vertices]  # glTF uses Y-up.
positions = b''.join(struct.pack('<3f', *point) for point in gltf_vertices)
indices = b''.join(struct.pack('<3I', *face) for face in faces)
binary = positions + indices
scene = {
    "asset": {"version": "2.0", "generator": "3D Thumbnail original fixture"},
    "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
    "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}],
    "buffers": [{"byteLength": len(binary)}],
    "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": len(positions)},
                    {"buffer": 0, "byteOffset": len(positions), "byteLength": len(indices)}],
    "accessors": [{"bufferView": 0, "componentType": 5126, "count": len(vertices), "type": "VEC3",
                   "min": [min(v[i] for v in gltf_vertices) for i in range(3)],
                   "max": [max(v[i] for v in gltf_vertices) for i in range(3)]},
                  {"bufferView": 1, "componentType": 5125, "count": len(faces) * 3, "type": "SCALAR"}]
}
def write_glb(path, document):
    data = json.dumps(document, separators=(',', ':')).encode()
    data += b' ' * (-len(data) % 4)
    payload = binary + b'\0' * (-len(binary) % 4)
    path.write_bytes(struct.pack('<4sII', b'glTF', 2, 12 + 8 + len(data) + 8 + len(payload)) +
                     struct.pack('<I4s', len(data), b'JSON') + data +
                     struct.pack('<I4s', len(payload), b'BIN\0') + payload)

write_glb(SAMPLES / 'bearing.glb', scene)
scene['buffers'][0]['uri'] = 'data:application/octet-stream;base64,' + base64.b64encode(binary).decode()
(SAMPLES / 'bearing_embedded.gltf').write_text(json.dumps(scene), encoding='utf-8')
scene['buffers'][0]['uri'] = 'bearing.bin'
(SAMPLES / 'bearing.bin').write_bytes(binary)
(SAMPLES / 'bearing.gltf').write_text(json.dumps(scene), encoding='utf-8')
scene['nodes'].append({'mesh': 0, 'translation': [100, 0, -20]})
scene['scenes'][0]['nodes'].append(1)
scene['buffers'][0].pop('uri')
write_glb(SAMPLES / 'placed_meshes.glb', scene)

# Minimal FBX 7.4 scene, with both ASCII and binary encodings of the same geometry.
class Id(int):
    pass
def node(name, *properties, children=()):
    return (name, properties, children)
def prop(name, kind, values):
    return node('P', name, kind, '', 'A', *values)
tree = [
    node('FBXHeaderExtension', children=[node('FBXHeaderVersion', 1003), node('FBXVersion', 7400)]),
    node('GlobalSettings', children=[node('Version', 1000), node('Properties70', children=[
        prop('UpAxis', 'int', [2]), prop('UpAxisSign', 'int', [1]),
        prop('FrontAxis', 'int', [1]), prop('FrontAxisSign', 'int', [-1]),
        prop('CoordAxis', 'int', [0]), prop('CoordAxisSign', 'int', [1]),
        prop('UnitScaleFactor', 'double', [1.0])])]),
    node('Objects', children=[
        node('Geometry', Id(1001), 'Geometry::Bearing', 'Mesh', children=[
            node('Vertices', ('d', [v for point in vertices for v in point])),
            node('PolygonVertexIndex', ('i', [v for a, b, c in faces for v in (a, b, -c - 1)]))]),
        node('Model', Id(1002), 'Model::Bearing', 'Mesh', children=[node('Version', 232),
            node('Properties70', children=[prop('Lcl Translation', 'Lcl Translation', [0.0, 0.0, 0.0]),
                prop('Lcl Rotation', 'Lcl Rotation', [0.0, 0.0, 0.0]),
                prop('Lcl Scaling', 'Lcl Scaling', [1.0, 1.0, 1.0])])])]),
    node('Connections', children=[node('C', 'OO', Id(1001), Id(1002)), node('C', 'OO', Id(1002), Id(0))])
]
def encode_property(value):
    if isinstance(value, tuple):
        kind, values = value
        raw = struct.pack('<' + kind * len(values), *values)
        compressed = zlib.compress(raw)
        return kind.encode() + struct.pack('<III', len(values), 1, len(compressed)) + compressed
    if isinstance(value, str):
        data = value.encode()
        return b'S' + struct.pack('<I', len(data)) + data
    if isinstance(value, Id):
        return b'L' + struct.pack('<q', value)
    return (b'D' + struct.pack('<d', value)) if isinstance(value, float) else (b'I' + struct.pack('<i', value))

def binary_node(item, offset):
    name, properties, children = item
    encoded = b''.join(encode_property(p) for p in properties)
    payload = name.encode() + encoded
    for child in children:
        payload += binary_node(child, offset + 13 + len(payload))
    if children:
        payload += bytes(13)
    return struct.pack('<IIIB', offset + 13 + len(payload), len(properties), len(encoded), len(name)) + payload

fbx = b'Kaydara FBX Binary  \0\x1a\0' + struct.pack('<I', 7400)
for item in tree:
    fbx += binary_node(item, len(fbx))
(SAMPLES / 'bearing.fbx').write_bytes(fbx + bytes(13))

def ascii_node(item, indent=''):
    name, properties, children = item
    if len(properties) == 1 and isinstance(properties[0], tuple):
        values = properties[0][1]
        return indent + name + ': *' + str(len(values)) + ' {\n' + indent + ' a: ' + ','.join(map(str, values)) + '\n' + indent + '}\n'
    data = ','.join(json.dumps(value) for value in properties)
    if children:
        return indent + name + ': ' + data + ' {\n' + ''.join(ascii_node(child, indent + '  ') for child in children) + indent + '}\n'
    return indent + name + ': ' + data + '\n'
(SAMPLES / 'bearing_ascii.fbx').write_text('; FBX 7.4.0 project file\n' + ''.join(map(ascii_node, tree)), encoding='ascii')
print(f'Created original mesh samples: {len(faces)} triangles.')
