#!/usr/bin/env python3
"""Record in-tree artwork metadata and explicit template/document notices."""
import gzip
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET

from common import ROOT, sha256


def svg_metadata(file):
    data = gzip.decompress(file.read_bytes()) if file.suffix == '.svgz' else file.read_bytes()
    root = ET.fromstring(data)
    namespaces = {'dc': 'http://purl.org/dc/elements/1.1/', 'cc': 'http://creativecommons.org/ns#'}
    licenses = [e.get('{http://www.w3.org/1999/02/22-rdf-syntax-ns#}resource')
                for e in root.findall('.//cc:license', namespaces)]
    authors = [' '.join(e.itertext()).strip() for e in root.findall('.//dc:creator', namespaces)]
    rights = [' '.join(e.itertext()).strip() for e in root.findall('.//dc:rights', namespaces)]
    return {'licenseDeclarations': sorted(set(filter(None, licenses))),
            'authors': sorted(set(filter(None, authors))), 'rights': sorted(set(filter(None, rights)))}


def collect():
    records = []
    for file in sorted((ROOT / 'images-ng').rglob('*')):
        if file.suffix not in ('.svg', '.svgz'):
            continue
        metadata = svg_metadata(file)
        if any(metadata.values()):
            records.append({'path': str(file.relative_to(ROOT)), 'sha256': sha256(file), **metadata})
    templates = []
    for file in sorted((ROOT / 'templates').glob('template_*.json')):
        info = json.loads(file.read_text())
        templates.append({'metadataPath': str(file.relative_to(ROOT)), 'author': info.get('Author'),
                          'license': info.get('License'), 'date': info.get('Date'),
                          'files': [{'path': str(p.relative_to(ROOT)), 'sha256': sha256(p)}
                                    for p in sorted(file.parent.glob(file.stem + '.*')) if p.is_file()]})
    logo = ROOT / 'native/texpresso/doc/texpresso_logo_v3.svg'
    return {'schema': 1, 'artworkMetadata': records, 'templates': templates,
        'cup': {'source': str(logo.relative_to(ROOT)), 'sha256': sha256(logo),
                'license': 'MIT (TeXpresso repository LICENSE, no file-specific override)',
                'copyright': 'Frédéric Bour and TeXpresso contributors',
                'modifications': 'Removed background and TeX lettering; cropped, scaled and composited cup and steam onto existing TeXstudio icon/splash',
                'evidence': ['native/texpresso/LICENSE', 'native/icons/README.md'],
                'note': 'Uses the v3 file under the project MIT license; does not infer CC0 from a different older logo.'},
        'fontawesome': {'scope': 'symbols-ng/fontawesome5 SVG glyphs',
                        'license': 'CC-BY-4.0 for icons; OFL-1.1 for source fonts; MIT for code',
                        'copyright': 'Fonticons, Inc.; Font Awesome Free authors',
                        'notice': 'native/notices/assets/fontawesome-free-5.15.4-LICENSE.txt'},
        'documentation': [
            {'scope': 'utilities/latex2e.html', 'license': 'Verbatim/modified/translated copying permission embedded in document header',
             'copyright': 'Karl Berry, Stephen Gilmore, Torsten Martinsen and credited contributors'},
            {'scope': 'Generated TeXstudio manual and translations', 'license': 'Project GPLv3 baseline; third-party scripts/styles retain their own notices',
             'copyright': 'TeXstudio Team and contributors'},
            {'scope': 'jQuery 3.6.0', 'license': 'MIT', 'notice': 'native/notices/assets/jquery-3.6.0-LICENSE.txt'},
            {'scope': 'Underscore 1.13.1', 'license': 'MIT', 'notice': 'native/notices/assets/underscore-1.13.1-LICENSE'},
            {'scope': 'Sphinx JavaScript/CSS', 'license': 'BSD-2-Clause and retained third-party terms', 'notice': 'native/notices/assets/sphinx-LICENSE.rst'},
            {'scope': 'Snowball 3.0.1 search stemmer', 'license': 'BSD-3-Clause', 'notice': 'native/notices/assets/snowball-3.0.1-COPYING'},
            {'scope': 'sphinx-design and sphinx-inline-tabs', 'license': 'MIT', 'notices': ['native/notices/assets/sphinx-design-LICENSE', 'native/notices/assets/sphinx-inline-tabs-LICENSE']},
            {'scope': 'Furo bundled theme', 'license': 'MIT and included third-party terms', 'notice': 'utilities/manual/build/html/_static/scripts/furo.js.LICENSE.txt'}],
        'scopeNotes': [
            'Explicit SVG metadata and template declarations are preserved verbatim; contradictory declarations remain visible rather than normalized away.',
            'Crystal/Oxygen/Colibre artwork credits from About remain in force; the licenses page includes per-file metadata and full collected license texts.',
            'Other inherited project assets retain the TeXstudio license baseline unless their file-specific terms say otherwise; generated symbol sources and original SVG data accompany corresponding source.',
            'This records declarations and attribution, not a claim that each simple mathematical glyph creates a separate copyright.']}


if __name__ == '__main__':
    output = ROOT / 'native/asset-provenance.json'
    output.write_text(json.dumps(collect(), indent=2) + '\n')
    print(output)
