#!/usr/bin/env python3
"""Regenerate macOS icons with Apple's Icon Composer and asset compiler."""
import argparse
import plistlib
import shutil
import subprocess
import tempfile
from pathlib import Path

from common import ROOT, run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--variant', choices=['original', 'coffee', 'all'], default='all')
    parser.add_argument('--icon-composer', type=Path, help='Path to Icon Composer.app')
    args = parser.parse_args()
    developer = Path(subprocess.check_output(['xcode-select', '-p'], text=True).strip())
    composer = args.icon_composer or developer.parent / 'Applications/Icon Composer.app'
    renderer = composer / 'Contents/Executables/ictool'
    if not renderer.is_file():
        parser.error('Install Xcode 26 with Icon Composer, or supply --icon-composer')
    icons = ROOT / 'native/icons'
    variants = [('AppIcon', icons), ('AppIconCoffee', icons / 'alternatives/coffee')]
    if args.variant != 'all':
        variants = [variants[args.variant == 'coffee']]
    # The original .icns contains a 512px image. Center-crop its existing border;
    # Icon Composer positions bitmap layers in pixels on a 1024px canvas.
    with tempfile.TemporaryDirectory(prefix='texstudio-icons-') as temporary:
        temporary = Path(temporary)
        source = temporary / 'original.iconset'
        run(['iconutil', '-c', 'iconset', ROOT / 'texstudio.icns', '-o', source])
        for name, output in variants:
            document = icons / (name + '.icon')
            artwork = document / 'Assets/TeXstudio.png'
            run(['sips', '--cropToHeightWidth', '480', '480',
                 source / 'icon_512x512.png', '--out', artwork])
            run(['sips', '--resampleHeightWidth', '1024', '1024', artwork, '--out', artwork])
            compiled = temporary / name
            compiled.mkdir()
            run(['xcrun', 'actool', document, '--compile', compiled, '--platform', 'macosx',
                 '--target-device', 'mac', '--minimum-deployment-target', '13.0',
                 '--app-icon', name, '--output-partial-info-plist', compiled / 'icon-info.plist',
                 '--output-format', 'human-readable-text', '--notices', '--warnings'])
            with (compiled / 'icon-info.plist').open('rb') as stream:
                metadata = plistlib.load(stream)
            if metadata.get('CFBundleIconName') != name:
                raise RuntimeError('Unexpected asset compiler icon name')
            output.mkdir(parents=True, exist_ok=True)
            for filename in (name + '.icns', 'Assets.car'):
                asset = compiled / filename
                if not asset.is_file() or not asset.stat().st_size:
                    raise RuntimeError(f'Missing compiled icon: {asset}')
                shutil.copy2(asset, output / filename)
            run([renderer, document, '--export-image', '--output-file', output / 'preview.png',
                 '--platform', 'macOS', '--rendition', 'Default', '--width', '1024',
                 '--height', '1024', '--scale', '1'])


if __name__ == '__main__':
    main()
