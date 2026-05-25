#!/usr/bin/env python3
"""
Updates web-ui/package.json files in a folder tree with current version constraints
from the esp32m core packages.

Finds all web-ui/package.json files under the given folder(s) and replaces any
dependency version that differs from the core's constraints with the core's version.
Internal @esp32m/* and @ts-libs/* packages are always left as "*" (they are
resolved via yarn link at build time).

Usage:
    python update-ui.py <folder> [folder2 ...]
    python update-ui.py --esp32m-dir <path> <folder> [folder2 ...]

If --esp32m-dir is omitted it defaults to the esp32m component directory
alongside this script (one level up from scripts/).
"""

import os
import re
import sys
import glob
import json
import logging
import argparse

# Package name prefixes that are always kept as "*" in project package.json
# because they are resolved via yarn link to the local core installation.
_INTERNAL_SCOPES = ('@esp32m/', '@ts-libs/')

_LOCK_ENTRY_RE   = re.compile(r'^"([^"]+)":\s*$')
_LOCK_VERSION_RE = re.compile(r'^\s{2}version:\s+(\S+)\s*$')

def _version_key(ver_str):
    try:
        return tuple(int(x) for x in ver_str.split('.')[:3])
    except (ValueError, AttributeError):
        return (0, 0, 0)

def _parse_yarn_lock(lockfile_path):
    """Parse a Yarn v3+ lock file and return {pkg_name: '^resolved_version'}.
    Used as a fallback for packages that are transitive dependencies and
    therefore don't appear directly in any package.json file (e.g. lodash)."""
    versions = {}
    try:
        with open(lockfile_path, 'r', encoding='utf-8', errors='replace') as f:
            lines = f.readlines()
    except IOError:
        return versions
    i = 0
    while i < len(lines):
        m = _LOCK_ENTRY_RE.match(lines[i])
        if m:
            # Each spec looks like:  pkg@npm:range  or  @scope/pkg@npm:range
            pkg_names = set()
            for spec in m.group(1).split(', '):
                for registry in ('@npm:', '@workspace:'):
                    if registry in spec:
                        name = spec.split(registry)[0].strip('"')
                        if not any(name.startswith(s) for s in _INTERNAL_SCOPES):
                            pkg_names.add(name)
                        break
            j = i + 1
            resolved = None
            while j < len(lines) and (not lines[j].strip() or lines[j][0] == ' '):
                vm = _LOCK_VERSION_RE.match(lines[j])
                if vm:
                    resolved = vm.group(1)
                    break
                j += 1
            if resolved and pkg_names:
                constraint = f'^{resolved}'
                for name in pkg_names:
                    existing = versions.get(name)
                    if existing is None or _version_key(resolved) > _version_key(existing.lstrip('^~=')):
                        versions[name] = constraint
        i += 1
    return versions

# Default esp32m dir is one level up from this script (i.e. core/esp32m/)
_DEFAULT_ESP32M_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))


def collect_versions(esp32m_dir):
    """Collect all 3rd-party dependency version constraints from the core web-ui
    packages. Returns a dict mapping package name -> version constraint string."""
    web_ui_dir = os.path.join(esp32m_dir, 'web-ui')
    if not os.path.isdir(web_ui_dir):
        raise SystemExit(f"esp32m web-ui directory not found: {web_ui_dir}")
    versions = {}
    for path in glob.glob(os.path.join(web_ui_dir, '**', 'package.json'), recursive=True):
        if 'node_modules' in path.replace('\\', '/'):
            continue
        try:
            with open(path, 'r') as f:
                pkg = json.load(f)
        except (json.JSONDecodeError, IOError) as e:
            logging.warning(f"Could not read {path}: {e}")
            continue
        for section in ('dependencies', 'devDependencies'):
            for name, ver in pkg.get(section, {}).items():
                if ver == '*':
                    continue
                if any(name.startswith(s) for s in _INTERNAL_SCOPES):
                    continue
                if name not in versions:
                    versions[name] = ver
    # Fallback: fill in transitive deps (e.g. lodash) from yarn.lock
    lockfile = os.path.join(web_ui_dir, 'yarn.lock')
    if os.path.exists(lockfile):
        for name, ver in _parse_yarn_lock(lockfile).items():
            if name not in versions:
                versions[name] = ver
    logging.info(f"Collected {len(versions)} package versions from {web_ui_dir}")
    return versions


def sync_package_json(path, versions, dry_run):
    """Update a single package.json with versions from the core map.
    Returns True if any change was made."""
    try:
        with open(path, 'r') as f:
            content = f.read()
        pkg = json.loads(content)
    except (json.JSONDecodeError, IOError) as e:
        logging.warning(f"Could not read {path}: {e}")
        return False

    changes = []
    for section in ('dependencies', 'devDependencies'):
        for name in pkg.get(section, {}):
            if any(name.startswith(s) for s in _INTERNAL_SCOPES):
                continue
            if name in versions and pkg[section][name] != versions[name]:
                changes.append(f"  {section}/{name}: {pkg[section][name]!r} -> {versions[name]!r}")
                pkg[section][name] = versions[name]

    if not changes:
        return False

    for c in changes:
        logging.info(c)

    if dry_run:
        logging.info(f"[dry-run] would update {path}")
    else:
        # Detect indentation from source (default 2)
        indent = 2
        for line in content.splitlines()[1:6]:
            stripped = line.lstrip()
            if stripped and line != stripped:
                indent = len(line) - len(stripped)
                break
        with open(path, 'w', newline='\n') as f:
            json.dump(pkg, f, indent=indent)
            f.write('\n')
        logging.info(f"Updated {path}")

    return True


def find_project_package_jsons(root, esp32m_dir):
    """Yield all web-ui/package.json paths under root, skipping node_modules
    and the esp32m core's own web-ui directory."""
    core_web_ui = os.path.normpath(os.path.join(esp32m_dir, 'web-ui'))
    for dirpath, dirnames, filenames in os.walk(root):
        # Skip build artifacts, downloaded components, and node_modules
        dirnames[:] = [d for d in dirnames if d not in ('node_modules', 'build', 'managed_components')]
        # Skip the core's own web-ui to avoid modifying core packages
        if os.path.normpath(dirpath) == core_web_ui:
            dirnames.clear()
            continue
        if os.path.basename(dirpath) == 'web-ui' and 'package.json' in filenames:
            yield os.path.join(dirpath, 'package.json')


def main():
    logging.basicConfig(format='%(levelname)s: %(message)s', level=logging.INFO)
    parser = argparse.ArgumentParser(
        description='Update web-ui/package.json files with current esp32m core versions'
    )
    parser.add_argument(
        '--esp32m-dir', dest='esp32mDir', default=_DEFAULT_ESP32M_DIR,
        help=f'esp32m component root (default: {_DEFAULT_ESP32M_DIR})'
    )
    parser.add_argument(
        '--dry-run', action='store_true',
        help='show what would change without modifying any files'
    )
    parser.add_argument(
        'folders', nargs='+', metavar='folder',
        help='folder(s) to search for web-ui/package.json files'
    )
    args = parser.parse_args()

    versions = collect_versions(args.esp32mDir)

    total = updated = 0
    for folder in args.folders:
        folder = os.path.abspath(folder)
        if not os.path.isdir(folder):
            logging.warning(f"Folder not found, skipping: {folder}")
            continue
        for pkg_path in find_project_package_jsons(folder, args.esp32mDir):
            total += 1
            if sync_package_json(pkg_path, versions, args.dry_run):
                updated += 1

    action = "Would update" if args.dry_run else "Updated"
    logging.info(f"{action} {updated} of {total} package.json file(s)")
    return 0


if __name__ == '__main__':
    sys.exit(main())
