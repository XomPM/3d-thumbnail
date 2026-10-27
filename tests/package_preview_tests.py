"""End-to-end stored-preview regression tests, using only Python's standard library."""
import argparse
import binascii
import hashlib
import io
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile
import zlib


def png(width=48, height=24, pixel=(255, 0, 0, 128)):
    """RGBA (color type 6) for 4-tuple pixels, RGB (color type 2) for 3-tuples, like slicer previews."""
    def chunk(name, data):
        return struct.pack('>I', len(data)) + name + data + struct.pack('>I', binascii.crc32(name + data))
    color_type = 6 if len(pixel) == 4 else 2
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, color_type, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress((b'\0' + bytes(pixel) * width) * height)) + chunk(b'IEND', b''))


def center_pixel(data):
    """Decodes an 8-bit RGBA/RGB non-interlaced PNG with the standard library and returns its center pixel."""
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    position, idat = 8, b''
    while position < len(data):
        length, name = struct.unpack('>I4s', data[position:position + 8])
        body = data[position + 8:position + 8 + length]
        if name == b'IHDR':
            width, height, depth, color_type, _, _, interlace = struct.unpack('>IIBBBBB', body)
            assert depth == 8 and color_type in (2, 6) and interlace == 0, 'unexpected PNG layout'
        elif name == b'IDAT':
            idat += body
        position += 12 + length
    channels = 4 if color_type == 6 else 3
    stride, raw, rows = width * channels, zlib.decompress(idat), []
    previous = bytearray(stride)
    for y in range(height):
        kind, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            left = line[x - channels] if x >= channels else 0
            up, corner = previous[x], previous[x - channels] if x >= channels else 0
            if kind == 1: line[x] = (line[x] + left) & 255
            elif kind == 2: line[x] = (line[x] + up) & 255
            elif kind == 3: line[x] = (line[x] + (left + up) // 2) & 255
            elif kind == 4:
                estimate = left + up - corner
                pa, pb, pc = abs(estimate - left), abs(estimate - up), abs(estimate - corner)
                line[x] = (line[x] + (left if pa <= pb and pa <= pc else up if pb <= pc else corner)) & 255
        rows.append(line)
        previous = line
    offset = (width // 2) * channels
    pixel = tuple(rows[height // 2][offset:offset + channels])
    return pixel if channels == 4 else pixel + (255,)


def package(image, *, target='/Metadata/preview.png', mode='', relation=True,
            duplicate=False, model=True, valid_geometry=False, metadata=False, paint=False, large=False):
    with zipfile.ZipFile(SAMPLES / 'bearing.3mf') as source:
        entries = {name: source.read(name) for name in source.namelist()}
    model_name = next(name for name in entries if name.endswith('.model'))
    text = entries[model_name].decode()
    if not valid_geometry:
        text = text[:text.index('<build>')] + '<build/></model>'
    if metadata:
        text = text.replace('<resources>', '<metadata name="BambuStudio:fixture">x</metadata><resources>')
    if paint:
        text = text.replace('<triangle ', '<triangle paint_supports="4" ', 1)
    if large:
        start, end = text.index('<build>') + len('<build>'), text.index('</build>')
        text = text[:start] + text[start:end] * 500 + text[end:]
    entries[model_name] = text.encode()
    rel_type = 'http://schemas.openxmlformats.org/package/2006/relationships/metadata/thumbnail'
    root = '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
    if model:
        root += f'<Relationship Id="model" Target="/{model_name}" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>'
    if relation:
        root += f'<Relationship Id="preview" Target="{target}" Type="{rel_type}" {mode}/>'
    if duplicate:
        root += f'<Relationship Id="preview2" Target="/Metadata/other.png" Type="{rel_type}"/>'
        entries['Metadata/other.png'] = image
    entries['_rels/.rels'] = (root + '</Relationships>').encode()
    types = entries['[Content_Types].xml'].decode()
    if 'Extension="png"' not in types:
        types = types.replace('</Types>', '<Default Extension="png" ContentType="image/png"/></Types>')
    entries['[Content_Types].xml'] = types.encode()
    entries['Metadata/preview.png'] = image
    result = io.BytesIO()
    with zipfile.ZipFile(result, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, data in entries.items():
            archive.writestr(name, data)
    return result.getvalue()


def run_case(folder, name, data, success, stored=True, expected_center=None):
    source, output = folder / (name + '.3mf'), folder / (name + '.png')
    source.write_bytes(data)
    before = hashlib.sha256(data).hexdigest()
    process = subprocess.run([str(CLI), str(source), str(output)], capture_output=True, timeout=25)
    diagnostic = process.stdout.decode('utf-8', errors='replace') + process.stderr.decode('utf-8', errors='replace')
    assert (process.returncode == 0) == success, f'{name}: {diagnostic}'
    assert output.exists() == success, name
    assert hashlib.sha256(source.read_bytes()).hexdigest() == before, name + ': input changed'
    if success:
        assert ('Using stored 3MF preview' in diagnostic) == stored, name + ': wrong path\n' + diagnostic
        assert struct.unpack('>II', output.read_bytes()[16:24]) == (256, 256), name
        if expected_center is not None:
            actual = center_pixel(output.read_bytes())
            assert all(abs(a - b) <= 2 for a, b in zip(actual, expected_center)), f'{name}: center {actual} != {expected_center}'
    print('PASS', name)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('cli', type=Path)
    parser.add_argument('samples', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    CLI, SAMPLES = args.cli.resolve(), args.samples.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    folder = Path(tempfile.mkdtemp(prefix='preview-한글-', dir=args.output.resolve()))
    image = png()
    run_case(folder, 'stored-preview', package(image), True)
    # A creator's stored preview wins even when the geometry could be rendered.
    run_case(folder, 'preview-priority', package(image, valid_geometry=True), True)
    run_case(folder, 'geometry-without-preview', package(image, valid_geometry=True, relation=False), True, False)
    run_case(folder, 'broken-preview-renders-model', package(b'not a PNG', valid_geometry=True), True, False)
    # Vendor metadata/attributes are tolerated (lib3mf non-strict mode), so these render directly.
    run_case(folder, 'bambu-metadata', package(image, valid_geometry=True, metadata=True, relation=False), True, False)
    run_case(folder, 'bambu-paint', package(image, valid_geometry=True, paint=True, relation=False), True, False)
    # Slicer previews are often 24-bit RGB; red must stay red (regression: red/blue were swapped).
    run_case(folder, 'rgb-preview-colors', package(png(32, 32, (230, 180, 20))), True, True, (230, 180, 20, 255))
    run_case(folder, 'rgba-preview-colors', package(png(32, 32, (20, 120, 230, 255))), True, True, (20, 120, 230, 255))
    run_case(folder, 'large-instances', package(image, valid_geometry=True, large=True), True)
    run_case(folder, 'no-preview-relation', package(image, relation=False), False)
    run_case(folder, 'missing-model-relation', package(image, model=False), False)
    run_case(folder, 'missing-preview', package(image, target='/Metadata/missing.png'), False)
    run_case(folder, 'external-preview', package(image, target='https://example.invalid/p.png', mode='TargetMode="External"'), False)
    run_case(folder, 'ambiguous-preview', package(image, duplicate=True), False)
    run_case(folder, 'broken-png', package(b'not a PNG'), False)
    run_case(folder, 'huge-dimensions', package(png(4097, 1)), False)
    run_case(folder, 'huge-encoded-image', package(image + b'\0' * (4 * 1024 * 1024)), False)
    data = package(image)
    run_case(folder, 'broken-zip', data[:-10], False)
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        entry = archive.getinfo('Metadata/preview.png')
        offset = entry.header_offset
    damaged = bytearray(data)
    name_size, extra_size = struct.unpack_from('<HH', damaged, offset + 26)
    damaged[offset + 30 + name_size + extra_size + 8] ^= 255
    run_case(folder, 'corrupted-image-data', bytes(damaged), False)
    print('Evidence:', folder)
