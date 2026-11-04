#!/usr/bin/env python3
"""Prepare project sources and reusable dependency sources; never publishes/signs.

--plan records source archives/patches and installed build recipes. --assemble
requires every listed input; --fetch explicitly allows HTTPS downloads. There
is no successful mode that silently omits required source inputs. Dependency
archives are checksum-named files beside the project ZIP, reusable by releases.
"""
import argparse
import hashlib
import html
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import tarfile
import time
import urllib.request
import zipfile

from common import ROOT, sha256


def export_editor_source(directory, revision, output):
    """Exclude unused legacy binary Qt translations; current TS sources are supplied.

    Do not remove these historical files from the user's checkout. They are no
    longer app build inputs, and are not corresponding source for the new bundle.
    """
    excluded = []
    process = subprocess.Popen(['git', 'archive', '--format=tar', revision], cwd=directory, stdout=subprocess.PIPE)
    try:
        with tarfile.open(fileobj=process.stdout, mode='r|') as source, tarfile.open(str(output), mode='w|gz', compresslevel=6) as target:
            for member in source:
                path = Path(member.name)
                if len(path.parts) == 2 and path.parts[0] == 'translation' and path.name.startswith('qt_') and path.suffix == '.qm':
                    excluded.append(member.name)
                    continue
                target.addfile(member, source.extractfile(member) if member.isfile() else None)
        if process.wait() != 0:
            raise ValueError('Editor Git export failed')
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait()
        process.stdout.close()
    return excluded


def archive_record(name, url, checksums, **extra):
    digest = next((c['checksumValue'] for c in checksums if c['algorithm'] == 'SHA256'), None)
    if not digest or not url.startswith('https://'):
        raise ValueError(f'No immutable HTTPS/SHA256 source evidence: {name}')
    return {'name': name, 'url': url, 'sha256': digest, **extra}


def plan(inventory, destination, evidence_directory=None, build_directory=None):
    if destination.exists():
        raise ValueError('Source plan output already exists')
    destination.mkdir(parents=True)
    records = []
    unresolved = [issue for issue in inventory.get('releaseWorkRemaining', [])
                  if not issue.startswith('Publish complete corresponding source')]
    recipes = destination / 'recipes'
    recipes.mkdir()
    for key, package in inventory['packages'].items():
        try:
            records.append(archive_record(key, package['source'], package.get('sourceChecksums', [])))
        except ValueError as error:
            unresolved.append(str(error))
        image = next((b for b in inventory['binaries'] if b.get('package') == key and 'origin' in b), None)
        if not image:
            continue
        origin = Path(image['origin'])
        prefix = next((p for p in origin.parents if (p / 'INSTALL_RECEIPT.json').is_file()), None)
        if prefix is None:
            continue  # local builder's source/configuration is in the app inventory
        recipe = prefix / '.brew' / (prefix.parent.name + '.rb')
        if recipe.is_file():
            shutil.copyfile(recipe, recipes / (key + '.rb'))
        spdx = prefix / 'sbom.spdx.json'
        if spdx.is_file():
            shutil.copyfile(spdx, recipes / (key + '.spdx.json'))
            data = json.loads(spdx.read_text())
            for item in data.get('packages', []):
                if 'Patch' in item.get('SPDXID', '') or 'patch' in item.get('name', '').lower():
                    records.append(archive_record(key + '-' + item['SPDXID'],
                                                  item['downloadLocation'], item.get('checksums', [])))
        elif recipe.is_file():
            # Older installed formulas have no SPDX. Retain the complete recipe
            # and every explicit URL/hash pair (including patches/resources).
            for index, (url, digest) in enumerate(re.findall(
                    r'url "([^"]+)"\s*\n\s*(?:mirror[^\n]*\n\s*)*sha256 "([0-9a-f]{64})"', recipe.read_text())):
                records.append(archive_record(key + f'-recipe-input-{index}', url,
                                              [{'algorithm': 'SHA256', 'checksumValue': digest}]))
    lock = json.loads((ROOT / 'native/dependencies.lock.json').read_text())
    for name, item in json.loads((ROOT / 'native/macos-dependencies.lock.json').read_text()).items():
        records.append(archive_record(name, item['url'], [{'algorithm': 'SHA256', 'checksumValue': item['sha256']}]))
    records.append(archive_record('icu-private-data-source', lock['icu']['sourceUrl'],
                                  [{'algorithm': 'SHA256', 'checksumValue': lock['icu']['sourceSha256']}]))
    # Git snapshots are pinned by immutable commits, not mutable branch names.
    # Their transport SHA256 is recorded when fetched and retained in the ZIP.
    git_sources = [{'name': 'mupdf', 'repository': 'ArtifexSoftware/mupdf',
                    'revision': inventory['engineManifest']['mupdfRevision'], 'extractTo': 'mupdf'},
                   {'name': 'lcms2-static', 'repository': 'ArtifexSoftware/thirdparty-lcms2',
                    'revision': lock['mupdf']['submodules']['thirdparty/lcms2'],
                    'extractTo': 'mupdf/thirdparty/lcms2'},
                   {'name': 'zlib-ng', 'repository': 'zlib-ng/zlib-ng',
                    'revision': inventory['engineManifest']['zlibNg']['revision'], 'extractTo': 'zlib-ng'}]
    # License/source notices for dictionary inputs are part of the checkout;
    # retain separately available original contributor archives as well.
    for row in json.loads((ROOT / 'native/dictionary-provenance.json').read_text())['dictionaries']:
        if row.get('sourceArchive'):
            item = row['sourceArchive']
            records.append(archive_record(row['name'] + '-original', item['url'],
                                          [{'algorithm': 'SHA256', 'checksumValue': item['sha256']}]))
    unique = {}
    for record in records:
        unique[(record['url'], record['sha256'])] = record
    revision = inventory['release']['sourceRevision']
    if subprocess.check_output(['git', 'rev-parse', revision], cwd=ROOT, text=True).strip() != revision:
        raise ValueError('Editor source revision unavailable locally')
    engine_revision = inventory['release']['engineRevision']
    expected_engine = subprocess.check_output(['git', 'rev-parse', revision + ':native/texpresso'], cwd=ROOT, text=True).strip()
    if expected_engine != engine_revision:
        raise ValueError('App engine differs from its editor source gitlink')
    result = {'schema': 1, 'editorRevision': revision, 'engineRevision': engine_revision,
              'archives': sorted(unique.values(), key=lambda r: r['name']), 'gitSources': git_sources,
              'unresolved': unresolved, 'recipes': sorted(p.name for p in recipes.iterdir()),
              'scope': 'Source/build inputs for inspected binary distribution; release licensing review remains separate'}
    (destination / 'manifest.json').write_text(json.dumps(result, indent=2) + '\n')
    (destination / 'binary-inventory.json').write_text(json.dumps(inventory, indent=2) + '\n')
    if evidence_directory:
        shutil.copytree(evidence_directory, destination / 'distribution-evidence')
    if build_directory:
        shutil.copyfile(build_directory / 'CMakeCache.txt', destination / 'editor-CMakeCache.txt')
        flags = build_directory / 'CMakeFiles/texstudio.dir/flags.make'
        if flags.is_file():
            shutil.copyfile(flags, destination / 'editor-compiler-flags.txt')
    return result


def fetch_record(record, cache, allow_fetch):
    name = record['name'] + '-' + record.get('sha256', record.get('revision', ''))[:12]
    file = cache / re.sub(r'[^A-Za-z0-9_.-]', '_', name)
    # Reuse the common source cache filename when its SHA256 verifies.
    basename = record['url'].split('?', 1)[0].rsplit('/', 1)[-1]
    candidate = cache / basename
    if record.get('sha256') and candidate.is_file() and sha256(candidate) == record['sha256']:
        return candidate
    if record.get('sha256'):
        # A source can be shared by the package entry and an installed formula
        # resource/patch entry with a different descriptive name.
        for candidate in sorted(cache.glob('*-' + record['sha256'][:12])):
            if candidate.is_file() and sha256(candidate) == record['sha256']:
                return candidate
    if not file.exists():
        if not allow_fetch:
            raise ValueError(f'Missing cached source: {record["name"]}; use --fetch')
        temporary = file.with_name(file.name + '.partial')
        try:
            deadline = time.monotonic() + 900
            with urllib.request.urlopen(record['url'], timeout=60) as response, temporary.open('wb') as stream:
                while chunk := response.read(1024 * 1024):
                    if time.monotonic() > deadline:
                        raise TimeoutError(f'Source download exceeded 15 minutes: {record["name"]}')
                    stream.write(chunk)
            if record.get('sha256') and sha256(temporary) != record['sha256']:
                raise ValueError(f'Source checksum mismatch: {record["name"]}')
            temporary.rename(file)
        except BaseException:
            temporary.unlink(missing_ok=True)
            raise
    if record.get('sha256') and sha256(file) != record['sha256']:
        raise ValueError(f'Cached source checksum mismatch: {record["name"]}')
    if record.get('revision'):
        with tarfile.open(file) as tar:
            first = tar.next()
            if first is None or not first.name.split('/')[0].endswith('-' + record['revision']):
                raise ValueError(f'Git snapshot root does not match pinned revision: {record["name"]}')
    return file


def fetch_inputs(manifest, cache, allow_fetch, source_mirror=None):
    cache.mkdir(parents=True, exist_ok=True)
    entries = []
    mirror_index = source_mirror / 'index.json' if source_mirror else None
    mirrored = json.loads(mirror_index.read_text()) if mirror_index and mirror_index.exists() else []
    for record in manifest['archives'] + [
            {**s, 'url': f'https://codeload.github.com/{s["repository"]}/tar.gz/{s["revision"]}'}
            for s in manifest['gitSources']]:
        previous = next((r for r in mirrored if r['url'] == record['url'] and
                         ((record.get('sha256') and r['sha256'] == record['sha256']) or
                          (record.get('revision') and r.get('revision') == record['revision']))), None)
        if previous:
            name = previous['sourceFile']
            if Path(name).name != name:
                raise ValueError('Unsafe dependency index filename')
            file = source_mirror / name
            if sha256(file) != previous['sha256']:
                raise ValueError(f'Dependency mirror checksum mismatch: {file}')
        else:
            file = fetch_record(record, cache, allow_fetch)
        entries.append((record, file))
        print('Source ready:', record['name'], flush=True)
    return entries


def source_filename(file, digest):
    """Content-based names let versions and architectures share exact sources."""
    with file.open('rb') as stream:
        magic = stream.read(6)
    suffix = '.tar.gz' if magic.startswith(b'\x1f\x8b') else '.tar.xz' if magic.startswith(b'\xfd7zXZ') else '.tar.bz2' if magic.startswith(b'BZh') else '.zip' if magic.startswith(b'PK\x03\x04') else '.source'
    return digest + suffix


def mirror_source(file, destination, digest):
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        if sha256(destination) != digest:
            raise ValueError(f'Dependency mirror checksum mismatch: {destination}')
        return
    temporary = destination.with_name(destination.name + '.partial')
    try:
        shutil.copyfile(file, temporary)
        if sha256(temporary) != digest:
            raise ValueError(f'Dependency source changed while copying: {file}')
        temporary.replace(destination)
    finally:
        temporary.unlink(missing_ok=True)


def write_dependency_index(directory, records):
    """Index all retained sources, including ones reused by earlier releases."""
    index = directory / 'index.json'
    previous = json.loads(index.read_text()) if index.exists() else []
    combined = {r['sourceFile']: r for r in previous + records if 'sourceFile' in r}
    rows = sorted(combined.values(), key=lambda r: r['name'])
    for record in rows:
        if Path(record['sourceFile']).name != record['sourceFile']:
            raise ValueError('Unsafe dependency index filename')
        path = directory / record['sourceFile']
        if sha256(path) != record['sha256']:
            raise ValueError(f'Dependency index checksum mismatch: {path}')
    index.write_text(json.dumps(rows, indent=2) + '\n')
    lines = ['<!doctype html><meta charset="utf-8"><title>Dependency sources</title>',
             '<h1>TeXstudio Espresso dependency sources</h1>',
             '<p>Unmodified source archives shared across releases. Use the project source ZIP\'s SOURCE-MANIFEST.json for the exact version set, patches and build recipes.</p>',
             '<ul>']
    for row in rows:
        lines.append('<li><a href="' + row['sourceFile'] + '">' + html.escape(row['name']) + '</a> — SHA256 <code>' + row['sha256'] + '</code></li>')
    (directory / 'index.html').write_text('\n'.join(lines + ['</ul>']) + '\n')


def assemble(plan_dir, cache, output, allow_fetch=False, dependencies=None):
    if output.exists():
        raise ValueError('Source ZIP output already exists')
    manifest = json.loads((plan_dir / 'manifest.json').read_text())
    if manifest['unresolved']:
        raise ValueError('Unresolved source inputs: ' + '; '.join(manifest['unresolved']))
    output.parent.mkdir(parents=True, exist_ok=True)
    dependencies = dependencies or output.parent / 'dependency-sources'
    entries = fetch_inputs(manifest, cache, allow_fetch, dependencies)
    manifest.update(schema=2, layout='project ZIP with separate dependency sources',
                    dependencyDirectory='dependency-sources')
    temporary = output.with_suffix('.partial')
    try:
        with tempfile.TemporaryDirectory(dir=output.parent) as temp:
            temp = Path(temp)
            with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
                for name, directory, revision in [('texstudio', ROOT, manifest['editorRevision']),
                                                  ('texpresso', ROOT / 'native/texpresso', manifest['engineRevision'])]:
                    file = temp / (name + '.tar.gz')
                    if name == 'texstudio':
                        manifest['excludedUnusedLegacyQtBinaries'] = export_editor_source(directory, revision, file)
                    else:
                        subprocess.run(['git', 'archive', '--format=tar.gz', '--output=' + str(file), revision],
                                       cwd=directory, check=True)
                    archive.write(file, 'project/' + file.name, compress_type=zipfile.ZIP_STORED)
                inputs = []
                for record, file in entries:
                    digest = sha256(file)
                    is_patch = 'patch' in record['name'].lower() or record['url'].split('?', 1)[0].endswith(('.patch', '.diff'))
                    source_file = digest + '.patch' if is_patch else source_filename(file, digest)
                    mirror_source(file, dependencies / source_file, digest)
                    item = {**record, 'sourceFile': source_file, 'sha256': digest}
                    if is_patch:
                        name = 'patches/' + digest + '.patch'
                        if name not in archive.namelist():
                            archive.write(file, name)
                        item['archiveMember'] = name
                    inputs.append(item)
                manifest['inputs'] = inputs
                write_dependency_index(dependencies, inputs)
                for file in (plan_dir / 'recipes').iterdir():
                    archive.write(file, 'recipes/' + file.name)
                for name in ('binary-inventory.json', 'editor-CMakeCache.txt', 'editor-compiler-flags.txt'):
                    if (plan_dir / name).is_file():
                        archive.write(plan_dir / name, name)
                if (plan_dir / 'distribution-evidence').is_dir():
                    for file in (plan_dir / 'distribution-evidence').rglob('*'):
                        if file.is_file():
                            archive.write(file, str(file.relative_to(plan_dir)))
                archive.writestr('SOURCE-MANIFEST.json', json.dumps(manifest, indent=2) + '\n')
                archive.writestr('README.md', SOURCE_README)
                archive.write(ROOT / 'native/scripts/unpack_source_release.py', 'unpack_source_release.py')
                archive.writestr('__main__.py', 'from unpack_source_release import main\nmain()\n')
        with zipfile.ZipFile(temporary) as archive:
            if archive.testzip():
                raise ValueError('Source ZIP integrity check failed')
        temporary.rename(output)
    finally:
        temporary.unlink(missing_ok=True)
    output.with_suffix(output.suffix + '.sha256').write_text(sha256(output) + '  ' + output.name + '\n')


SOURCE_README = '''# TeXstudio Espresso project source

Restore the editor and engine offline with Python 3.12+ and Git:

    python3 this-sources.zip --output ./espresso-source

This source ZIP contains the editor and pinned engine, notices, patches and build
recipes. Full upstream dependency archives are separate, reusable checksum-named
files in dependency-sources/. SOURCE-MANIFEST.json records their exact versions,
original URLs and SHA256 values. The release download page must provide access
to this complete matching set; the project ZIP alone does not contain all of it.

To also restore the pinned MuPDF/LCMS2/zlib-ng engine build inputs, supply that
directory (only those three source snapshots are needed by the restore helper):

    python3 this-sources.zip --output ./espresso-source --dependencies ./dependency-sources

Missing snapshots fail with instructions; they are never fetched implicitly.
Explicit --fetch allows HTTPS retrieval from the recorded original URLs into
that directory, verifying full SHA256 before extraction. Other dependencies
(Qt, Poppler, OpenSSL, etc.) remain individually available using the manifest.

The helper initializes editable local Git snapshots. Original upstream revisions
are recorded in SOURCE-ORIGIN.json; when dependencies are restored, new local
commits rebind the native builder's lock references. Only Git identity/lock
references change, not implementation. Commit edits before using the pinned
builder, and update its lock references if modifying dependency snapshots.

SOURCE-MANIFEST.json identifies the editor, recursive engine snapshot, dependency
sources, patches, checksums and original build recipes for the inspected app.
project/ contains Git exports at the app's recorded revisions; the separate
texpresso export belongs at texstudio/native/texpresso after extraction.
Unused historical binary Qt4 translations are excluded from the editor export;
their paths are recorded. The app uses current QtBase translations instead, with
editable TS source in the separately available Qt translations source archive.
patches/ contains the exact patches; dependency archive formats are preserved.
The Git snapshots' extractTo fields identify MuPDF, LCMS2's submodule and zlib-ng.
Verify each archive member against its recorded SHA256 before extraction.

Preserve and apply the patches from recipes/ when rebuilding the corresponding
Homebrew dependency versions. These recipes include configuration and resources;
their SBOM records identify source URLs/checksums independently of this archive.
Use native/scripts/build_poppler.py for the locked local Poppler configuration,
native/scripts/build.py for the engine, and native/LOCAL_TEST_APP.md and
native/MACOS_RELEASE.md for the editor and packaging commands. ICU filtering and
MuPDF's feature profile are in native/profiles/. TeX Live is installed separately.

The source is provided for inspection, modification and rebuilding, without a
claim of bit-identical output across SDK/compiler versions. Apple SDK/system
frameworks and unmodified external build tools are not included. Building a
modified local app does not require the distributor's signing credentials:
local_test_app.py applies an ad-hoc signature; no notarization is performed.
Licenses and notices remain under their original terms; do not relicense all
dependencies as GPL or MIT merely because they share this source archive.
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--plan', type=Path)
    mode.add_argument('--assemble', type=Path)
    mode.add_argument('--prefetch', type=Path, help='Fetch plan inputs only; does not clear licensing review or produce a ZIP')
    parser.add_argument('--inventory', type=Path)
    parser.add_argument('--cache', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--dependencies', type=Path, help='Reusable dependency source directory; default: beside output ZIP')
    parser.add_argument('--fetch', action='store_true')
    parser.add_argument('--build-dir', type=Path, help='Include the exact editor CMake cache')
    args = parser.parse_args()
    if args.plan:
        if args.inventory is None:
            parser.error('--plan requires --inventory')
        result = plan(json.loads(args.inventory.read_text()), args.plan,
                      args.inventory.parent, args.build_dir)
        print(json.dumps({'archives': len(result['archives']), 'gitSources': len(result['gitSources']),
                          'unresolved': result['unresolved']}))
    elif args.prefetch:
        if args.cache is None:
            parser.error('--prefetch requires --cache')
        fetch_inputs(json.loads((args.prefetch / 'manifest.json').read_text()), args.cache, args.fetch)
    else:
        if args.cache is None or args.output is None:
            parser.error('--assemble requires --cache and --output')
        assemble(args.assemble, args.cache, args.output, args.fetch, args.dependencies)


if __name__ == '__main__':
    main()
