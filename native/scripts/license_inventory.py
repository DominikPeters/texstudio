#!/usr/bin/env python3
"""Inventory an assembled macOS app and preserve upstream licensing evidence.

This is evidence collection, not a legal clearance tool. Missing information is
reported explicitly; finding a COPYING file does not certify compliance.
Run before signing (or read-only with an output directory outside a signed app).
"""
import argparse
from collections import defaultdict
from functools import lru_cache
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import xml.etree.ElementTree as ET
import zipfile

from common import ROOT, sha256
from deploy_macos import macho_files


def spdx_records(text):
    """Read SPDX 2.x tag/value records, including multiline <text> fields."""
    records = []
    current = {}
    pending = None
    for line in text.splitlines():
        if pending:
            current[pending][-1] += '\n' + line
            if '</text>' in line:
                pending = None
            continue
        if ': ' not in line:
            continue
        key, value = line.split(': ', 1)
        if key in ('PackageName', 'FileName', 'LicenseID'):
            if current:
                records.append(current)
            current = {}
        current.setdefault(key, []).append(value)
        if '<text>' in value and '</text>' not in value:
            pending = key
    if current:
        records.append(current)
    return records


def first(record, field, default='NOASSERTION'):
    return record.get(field, [default])[0].removeprefix('<text>').removesuffix('</text>').strip()


@lru_cache(maxsize=None)
def uuids(file):
    result = subprocess.run(['otool', '-l', str(file)], check=True,
                            capture_output=True, text=True).stdout
    return tuple(sorted(re.findall(r'\buuid ([0-9A-Fa-f-]+)', result)))


def notice_files(prefix):
    """Keep original paths and text; do not treat a license file as an SPDX ID."""
    candidates = list(prefix.iterdir())
    for folder in ('share/licenses', 'share/doc'):
        if (prefix / folder).exists():
            candidates.extend((prefix / folder).rglob('*'))
    return sorted({p for p in candidates if p.is_file() and
                   re.match(r'(?i)^(licen[cs]e|copying|notice|authors|copyright)([._-]|$)', p.name)})


def package_metadata(prefix):
    path = prefix / 'sbom.spdx.json'
    if not path.is_file():
        return {'name': prefix.parent.name, 'version': prefix.name,
                'license': 'NOASSERTION', 'source': 'NOASSERTION'}
    data = json.loads(path.read_text())
    packages = [p for p in data.get('packages', [])
                if p.get('name') == prefix.parent.name and 'Archive' in p.get('SPDXID', '')]
    package = packages[0] if packages else {}
    return {'name': prefix.parent.name, 'version': prefix.name,
            'license': package.get('licenseConcluded', 'NOASSERTION'),
            'source': package.get('downloadLocation', 'NOASSERTION'),
            'sourceChecksums': package.get('checksums', []),
            'evidence': 'Homebrew source-package SPDX metadata; not a file-level conclusion'}


def classify_resource(path):
    if re.search(r'\.(dic|dat)\.zip$', path.name):
        return 'dictionary/thesaurus'
    if '/share/poppler/' in path.as_posix():
        return 'Poppler CMap/data'
    if path.suffix in ('.qm', '.ts'):
        return 'translation'
    if path.suffix in ('.pfb', '.ttf', '.otf', '.cff'):
        return 'font'
    if path.suffix in ('.icns', '.car', '.svg', '.svgz', '.png', '.jpg', '.gif'):
        return 'artwork'
    if '/html/' in path.as_posix() or path.suffix in ('.html', '.css', '.js'):
        return 'documentation/script'
    if path.name.startswith(('template_', 'tabletemplate_')):
        return 'template'
    return 'other resource/metadata'


def inventory(app, output, qt_prefix, cellar, library_paths=(), hash_stage='as inspected'):
    if output.exists():
        raise ValueError(f'Refusing to overwrite inventory: {output}')
    if output.resolve().is_relative_to(app.resolve()):
        # Allowed only for packaging, before signing. File manifest excludes it.
        exclude = output.resolve()
    else:
        exclude = None
    output.mkdir(parents=True)
    notices = output / 'notices'
    notices.mkdir()
    issues = []
    qt_documents = {}
    qt_files = defaultdict(list)
    for path in sorted((qt_prefix / 'share/qt/sbom').glob('*.spdx')):
        text = path.read_text()
        records = spdx_records(text)
        packages = {first(r, 'SPDXID'): r for r in records if 'PackageName' in r}
        owners = defaultdict(list)
        for parent, child in re.findall(r'^Relationship: (\S+) CONTAINS (\S+)$', text, re.M):
            owners[child].append(parent)
        for record in records:
            if 'FileName' not in record:
                continue
            filename = first(record, 'FileName')
            if '.framework/' not in filename and not filename.endswith('.dylib'):
                continue
            for owner in owners[first(record, 'SPDXID')]:
                if owner in packages:
                    qt_files[Path(filename).name].append((path, packages[owner], filename))
        qt_documents[path.name] = text

    copied_packages = {}
    copied_qt = set()

    def collect(prefix):
        key = prefix.parent.name + '-' + prefix.name
        if key in copied_packages:
            return key
        record = package_metadata(prefix)
        destination = notices / key
        destination.mkdir()
        record['noticeFiles'] = []
        for file in notice_files(prefix):
            target = destination / file.relative_to(prefix)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(file, target)
            record['noticeFiles'].append(str(target.relative_to(output)))
        fallback = ROOT / 'native/notices' / (prefix.parent.name + '-COPYING')
        if not record['noticeFiles'] and fallback.is_file():
            shutil.copyfile(fallback, destination / fallback.name)
            record['noticeFiles'].append(str((destination / fallback.name).relative_to(output)))
        spdx = prefix / 'sbom.spdx.json'
        if spdx.is_file():
            shutil.copyfile(spdx, destination / spdx.name)
        if not record['noticeFiles']:
            issues.append(f'{key}: installed package has no collected license/attribution text')
        copied_packages[key] = record
        return key

    binaries = []
    for file in macho_files(app):
        relative = str(file.relative_to(app))
        entry = {'path': relative, 'sha256': sha256(file), 'uuid': list(uuids(file))}
        if relative.startswith('Contents/MacOS/'):
            entry['component'] = 'TeXstudio Espresso (see sourceComponents)'
        elif relative.startswith('Contents/Helpers/texpresso/bin/'):
            entry['component'] = 'TeXpresso frontend/worker (see engineManifest and sourceComponents)'
        else:
            candidates = []
            framework = next((p for p in file.parts if p.endswith('.framework')), None)
            if framework:
                candidates.append(qt_prefix / 'lib' / framework / 'Versions/A' / file.name)
            elif relative.startswith('Contents/PlugIns/'):
                suffix = relative.split('Contents/PlugIns/', 1)[1]
                candidates.append(qt_prefix / 'share/qt/plugins' / suffix)
            else:
                candidates.extend(sorted(cellar.glob('*/*/lib/' + file.name)))
                candidates.extend(p / file.name for p in library_paths)
            origins = sorted({p.resolve() for p in candidates if p.is_file() and
                              uuids(p.resolve()) == uuids(file) and uuids(file)})
            if len(origins) != 1:
                entry['origin'] = 'UNRESOLVED'
                issues.append(f'{relative}: expected one UUID-matched installed source, found {len(origins)}')
            else:
                origin = origins[0]
                entry['origin'] = str(origin)
                prefix = next((p for p in origin.parents if (p / 'INSTALL_RECEIPT.json').is_file()), None)
                if prefix:
                    entry['package'] = collect(prefix)
                else:
                    issues.append(f'{relative}: source matched, but no package receipt')
                    entry['provenanceStatus'] = 'Local build: preserve exact source/configuration separately'
                matches = qt_files.get(file.name, []) if (framework or relative.startswith('Contents/PlugIns/')) else []
                if len(matches) == 1:
                    document, record, upstream_path = matches[0]
                    entry['qt'] = {'module': first(record, 'PackageName'),
                                   'version': first(record, 'PackageVersion'),
                                   'license': first(record, 'PackageLicenseConcluded'),
                                   'copyright': first(record, 'PackageCopyrightText'),
                                   'source': first(record, 'PackageDownloadLocation'),
                                   'installedPath': upstream_path,
                                   'evidence': 'qt-sbom/' + document.name}
                    copied_qt.add(document.name)
                elif framework or relative.startswith('Contents/PlugIns/'):
                    issues.append(f'{relative}: missing or ambiguous Qt per-target SPDX record')
        binaries.append(entry)

    if copied_qt:
        (output / 'qt-sbom').mkdir()
    for name in sorted(copied_qt):
        (output / 'qt-sbom' / name).write_text(qt_documents[name])

    source_components = json.loads((ROOT / 'native/license-components.json').read_text())
    for component in source_components:
        component['noticeFiles'] = []
        for name in component.get('evidence', []):
            file = ROOT / name
            if file.is_file():
                target = notices / 'source' / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(file, target)
                component['noticeFiles'].append(str(target.relative_to(output)))

    resources = []
    code_paths = {b['path'] for b in binaries}
    for file in sorted(app.rglob('*')):
        if file.is_symlink() or not file.is_file() or (exclude and file.resolve().is_relative_to(exclude)):
            continue
        relative = str(file.relative_to(app))
        if relative in code_paths or '/_CodeSignature/' in relative:
            continue
        record = {'path': relative, 'bytes': file.stat().st_size,
                  'sha256': sha256(file), 'category': classify_resource(file)}
        if record['category'] == 'dictionary/thesaurus':
            with zipfile.ZipFile(file) as archive:
                record['members'] = [{'name': n, 'sha256': hashlib.sha256(archive.read(n)).hexdigest()}
                                     for n in sorted(archive.namelist())]
            record['licenseStatus'] = 'Per-file provenance/license reconciliation required; see dictionary READMEs'
        resources.append(record)

    embedded = []
    for qrc in sorted(ROOT.glob('*.qrc')):
        for element in ET.parse(qrc).iter('file'):
            file = ROOT / element.text
            if file.is_file():
                embedded.append({'qrc': qrc.name, 'path': element.text,
                                 'sha256': sha256(file), 'category': classify_resource(file)})
    manifest = app / 'Contents/Resources/texpresso/build-manifest.json'
    release = app / 'Contents/Resources/texpresso-release.json'
    static = json.loads((ROOT / 'native/license-static-components.json').read_text())
    engine_manifest = json.loads(manifest.read_text()) if manifest.exists() else {}
    if (engine_manifest.get('renderer', {}).get('reusedArchives') !=
            {name: record['sha256'] for name, record in static['archives'].items()}):
        issues.append('Static renderer snapshot does not match this build: repeat archive/link/font inventory')
    issues.extend([
        'Dictionaries/thesauri: reconcile each exact payload with its upstream license and copyright notice; generic URL is insufficient evidence',
        'Qt: retained SPDX documents include build tools/unshipped targets; only binaries[].qt selects shipped targets. Collect full standard license texts and applicable third-party attributions from matching Qt sources',
        'Static MuPDF: reconcile archive members and font data with final linker retention; engine MIT and MuPDF AGPL notices alone are incomplete',
        'Publish complete corresponding source matching editor, engine, dependency binaries, data, patches and build scripts; verify submodule commits are publicly accessible',
        'Artwork, generated symbols, templates, bundled documentation and translations: preserve upstream attribution and reconcile file-level exceptions before release',
    ])
    result = {'schema': 1, 'scope': 'macOS app inventory; evidence, not release clearance',
              'hashStage': hash_stage,
              'release': json.loads(release.read_text()) if release.exists() else {},
              'engineManifest': engine_manifest, 'staticRendererSnapshot': static,
              'binaries': binaries, 'packages': copied_packages,
              'sourceComponents': source_components, 'resources': resources,
              'embeddedResources': embedded, 'releaseWorkRemaining': issues}
    (output / 'inventory.json').write_text(json.dumps(result, indent=2) + '\n')
    lines = ['# TeXstudio Espresso distribution inventory', '',
             'Evidence collection, **not release clearance**. See `inventory.json` for every code image, resource, dictionary member hash and embedded QRC input.', '',
             'Hash stage: ' + hash_stage + '.', '',
             f'{len(binaries)} Mach-O images; {len(copied_packages)} installed packages; '
             f'{len(resources)} resource files; {len(embedded)} embedded resource inputs.', '',
             '## Shipped library packages', '', '| Package | Version | Upstream metadata license |',
             '| --- | --- | --- |']
    for package in copied_packages.values():
        lines.append(f"| {package['name']} | {package['version']} | {package['license']} |")
    lines += ['', '## Shipped Qt targets', '', '| Target | Version | Upstream target license |', '| --- | --- | --- |']
    for binary in binaries:
        if 'qt' in binary:
            qt = binary['qt']
            lines.append(f"| {qt['module']} | {qt['version']} | {qt['license']} |")
    lines += ['', '## Source and data components', '', '| Component | License evidence / review status |', '| --- | --- |']
    for component in source_components:
        lines.append(f"| {component['name']} | {component['license']} |")
    lines += ['', '## Work remaining before distribution', ''] + ['- ' + issue for issue in issues]
    (output / 'README.md').write_text('\n'.join(lines) + '\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--qt-prefix', type=Path, required=True)
    parser.add_argument('--cellar', type=Path, required=True)
    parser.add_argument('--library-path', type=Path, action='append', default=[])
    args = parser.parse_args()
    result = inventory(args.app.resolve(), args.output.resolve(), args.qt_prefix.resolve(), args.cellar.resolve(),
                       [p.resolve() for p in args.library_path])
    print(json.dumps({'binaries': len(result['binaries']), 'packages': len(result['packages']),
                      'reviewItems': len(result['releaseWorkRemaining'])}))
