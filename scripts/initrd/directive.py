"""
Directive class for specifying initrd entries.
"""

from typing import Optional, List
import argparse
import posixpath
import os
import time

from .format import DEFAULT_FILE_MODE, DEFAULT_DIR_MODE, DEFAULT_LINK_MODE, DEFAULT_UID, DEFAULT_GID


class Directive:
    """Represents a file, directory, or symlink to be added to an initrd image.

    Supported directives:
    - File: <srcfile>:<path>['@('<extra attrs>')']
    - Directory: :<path>/['@('<extra attrs>')']
    - Symlink: l<target>:<path>['@('<extra attrs>')']

    Extra attributes (optional, only supported in v2 images):
    - mode=<mode>    File mode in octal (e.g., 0755)
    - uid=<uid>      User ID (default: 0)
    - gid=<gid>      Group ID (default: 0)
    - mtime=<mtime>  Modification time as a Unix timestamp (default: current time)
    """

    kind: str
    path: str
    operand: Optional[str]
    size: int
    uid: int
    gid: int
    mtime: int
    mode: int

    def __init__(self, kind: str, path: str, operand: Optional[str] = None,
                 mode: Optional[int] = None, uid: int = DEFAULT_UID, gid: int = DEFAULT_GID,
                 mtime: Optional[int] = None):
        self.kind = kind
        self.path = path
        self.operand = operand
        self.size = 0
        self.uid = uid
        self.gid = gid
        self.mtime = mtime or int(time.time())

        if mode is None:
            if kind == 'f':
                self.mode = DEFAULT_FILE_MODE
            elif kind == 'd':
                self.mode = DEFAULT_DIR_MODE
            else:
                self.mode = DEFAULT_LINK_MODE
        else:
            self.mode = mode

        if kind == 'f':
            assert operand is not None
            try:
                if not os.path.isfile(operand):
                    raise argparse.ArgumentTypeError(f'path is not a file: {operand}')
                self.size = os.path.getsize(operand)
                if mtime is None:
                    self.mtime = int(os.path.getmtime(operand))
            except FileNotFoundError:
                raise argparse.ArgumentTypeError(f'file not found: {operand}')
            except Exception as e:
                raise argparse.ArgumentTypeError(f'{e}')
        elif kind == 'l':
            assert operand is not None
            self.size = len(operand) + 1
        elif kind != 'd':
            raise ValueError(f'invalid directive kind: {kind}')

    def isfile(self) -> bool:
        return self.kind == 'f'

    def islink(self) -> bool:
        return self.kind == 'l'

    def isdir(self) -> bool:
        return self.kind == 'd'

    def __repr__(self) -> str:
        return f'Directive(kind=\'{self.kind}\', path={self.path}, operand={self.operand}, size={self.size})'


def parse_directive(s: str, uid: int = DEFAULT_UID, gid: int = DEFAULT_GID, mode: Optional[int] = None) -> Directive:
    """Parse a directive string into a Directive object.

    Supports extended attribute syntax:
    - <srcfile>:<path>@(mode=0755,uid=1000,gid=1000,mtime=123456)
    - :<path>/@(mode=0755)
    - l<target>:<path>@(mode=0777)
    """
    if ':' not in s:
        raise argparse.ArgumentTypeError(f'invalid directive: {s}')

    # check for extended attributes
    ext_mode = mode
    ext_uid = uid
    ext_gid = gid
    ext_mtime = None

    if '@(' in s:
        main_part, attr_part = s.split('@(', 1)
        if not attr_part.endswith(')'):
            raise argparse.ArgumentTypeError(f'invalid directive: missing closing parenthesis')
        attr_part = attr_part[:-1]

        # parse attributes
        for attr in attr_part.split(','):
            attr = attr.strip()
            if '=' not in attr:
                raise argparse.ArgumentTypeError(f'invalid attribute: {attr}')
            key, value = attr.split('=', 1)
            key = key.strip()
            value = value.strip()

            if key == 'mode':
                try:
                    ext_mode = int(value, 8) if value.startswith('0') else int(value)
                except ValueError:
                    raise argparse.ArgumentTypeError(f'invalid mode value: {value}')
            elif key == 'uid':
                try:
                    ext_uid = int(value)
                except ValueError:
                    raise argparse.ArgumentTypeError(f'invalid uid value: {value}')
            elif key == 'gid':
                try:
                    ext_gid = int(value)
                except ValueError:
                    raise argparse.ArgumentTypeError(f'invalid gid value: {value}')
            elif key == 'mtime':
                try:
                    ext_mtime = int(value)
                except ValueError:
                    raise argparse.ArgumentTypeError(f'invalid mtime value: {value}')
            else:
                raise argparse.ArgumentTypeError(f'unknown attribute: {key}')

        s = main_part

    if s[0] == ':':
        # :<path>/|directory
        kind = 'd'
        path = posixpath.normpath(s[1:]) + '/'
        operand = None
    elif s[0] == 'l':
        # l<target>:<path>|symlink
        kind = 'l'
        path = s[s.index(':') + 1:]
        operand = s[1:s.index(':')]
    else:
        # <srcfile>:<path>|file
        kind = 'f'
        path = s[s.index(':') + 1:]
        operand = s[:s.index(':')]
        if not os.path.isfile(operand):
            raise argparse.ArgumentTypeError(f'path is not a file: {operand}')

    if not posixpath.isabs(path):
        raise argparse.ArgumentTypeError(f'invalid path: {path}')

    return Directive(kind, path, operand, mode=ext_mode, uid=ext_uid, gid=ext_gid, mtime=ext_mtime)


def get_intermediate_paths(path: str) -> List[str]:
    """Get all intermediate directory paths for a given path."""
    parts = []
    if path == '/':
        return parts
    if path.endswith('/'):
        path = path[:-1]

    while True:
        dirname, _ = posixpath.split(path)
        if dirname != '.' and dirname != '/':
            parts += [dirname + '/']
        else:
            break
        path = dirname
    return list(reversed(parts))
