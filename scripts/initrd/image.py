"""
Unified API for working with initrd images (v1 and v2).
"""

from typing import List, Optional, Union, cast
from struct import unpack

from .image_v1 import InitrdV1Image, InitrdV1Entry, V1_HEADER_SIZE, V1_ENTRY_SIZE
from .image_v2 import InitrdV2Image, InitrdV2Entry, V2_HEADER_SIZE, V2_ENTRY_SIZE
from .directive import Directive


class InitrdEntry:
    """Unified entry type that works with both v1 and v2."""

    def __init__(self, entry_type: str, path: str, data_offset: int, data_size: int,
                 mode: int = 0o644, uid: int = 0, gid: int = 0, mtime: int = 0):
        self.entry_type = entry_type
        self.path = path
        self.data_offset = data_offset
        self.data_size = data_size
        self.mode = mode
        self.uid = uid
        self.gid = gid
        self.mtime = mtime

    def __repr__(self) -> str:
        return f'InitrdEntry(type={self.entry_type}, path={self.path}, size={self.data_size})'


class InitrdImage:
    """Unified interface for working with initrd images."""

    def __init__(self, version: int = 2):
        self.version = version
        self._img: Union[InitrdV1Image, InitrdV2Image]
        if version == 1:
            self._img = InitrdV1Image()
        elif version == 2:
            self._img = InitrdV2Image()
        else:
            raise ValueError(f'unsupported initrd version: {version}')

    @property
    def signature(self) -> str:
        return self._img.signature

    @property
    def flags(self) -> int:
        return self._img.flags

    @property
    def total_size(self) -> int:
        return self._img.total_size

    @property
    def data_offset(self) -> int:
        return self._img.data_offset

    @property
    def entry_count(self) -> int:
        return self._img.entry_count

    @property
    def entries(self) -> List[InitrdEntry]:
        result = []

        if self.version == 1:
            img = cast(InitrdV1Image, self._img)
            for entry in img.entries:
                result.append(InitrdEntry(
                    entry.entry_type, entry.path,
                    entry.data_offset, entry.data_size
                ))
        elif self.version == 2:
            img = cast(InitrdV2Image, self._img)
            for idx, entry in enumerate(img.entries):
                full_path = img.get_full_path(idx)
                result.append(InitrdEntry(
                    entry.entry_type, full_path,
                    entry.data_offset, entry.data_size,
                    entry.mode, entry.uid, entry.gid, entry.mtime
                ))

        return result

    @staticmethod
    def load(filename: str, verify_checksum: bool = True) -> 'InitrdImage':
        """Load an initrd image, automatically detecting the version.

        Args:
            filename: Path to the initrd image file
            verify_checksum: Whether to verify checksums (v2 only, default: True)
        """
        with open(filename, 'rb') as f:
            header = f.read(32)
            if len(header) < 6:
                raise ValueError('file too small to be a valid initrd image')

            signature = ''.join(chr(b) for b in header[:6])

            if signature == 'INITv1':
                img = InitrdImage(version=1)
                img._img = InitrdV1Image.load(filename)
                return img
            elif signature == 'INITv2':
                img = InitrdImage(version=2)
                img._img = InitrdV2Image.load(filename, verify_checksum=verify_checksum)
                return img
            else:
                raise ValueError(f'unknown initrd signature: {signature}')

    def save(self, filename: str, directives: List[Directive]):
        if self.version == 1:
            img = cast(InitrdV1Image, self._img)
            img.save(filename, directives)
        elif self.version == 2:
            img = cast(InitrdV2Image, self._img)
            img.save(filename, directives)

    def find_entry(self, path: str) -> Optional[InitrdEntry]:
        if self.version == 1:
            img = cast(InitrdV1Image, self._img)
            entry = img.find_entry(path)
            if entry:
                return InitrdEntry(entry.entry_type, entry.path,
                                   entry.data_offset, entry.data_size)
        elif self.version == 2:
            img = cast(InitrdV2Image, self._img)
            result = img.find_entry(path)
            if result:
                idx, entry = result
                full_path = img.get_full_path(idx)
                return InitrdEntry(entry.entry_type, full_path,
                                   entry.data_offset, entry.data_size,
                                   entry.mode, entry.uid, entry.gid, entry.mtime)
        return None

    def read_file_data(self, filename: str, entry: InitrdEntry) -> bytes:
        if self.version == 1:
            img = cast(InitrdV1Image, self._img)
            v1_entry = img.find_entry(entry.path)
            if v1_entry:
                return InitrdV1Image.read_file_data(filename, v1_entry)
        elif self.version == 2:
            img = cast(InitrdV2Image, self._img)
            result = img.find_entry(entry.path)
            if result:
                _, v2_entry = result
                return img.read_file_data(filename, v2_entry)
        return b''

    def to_directives(self, filename: str) -> List[Directive]:
        return self._img.to_directives(filename)

    def get_metadata_size(self) -> int:
        if self.version == 1:
            img = cast(InitrdV1Image, self._img)
            size = V1_HEADER_SIZE
            for entry in img.entries:
                size += V1_ENTRY_SIZE + len(entry.path) + 1
            return size
        elif self.version == 2:
            img = cast(InitrdV2Image, self._img)
            if img.metadata_size > 0:
                return V2_HEADER_SIZE + img.metadata_size
            else:
                return img.data_offset
        return 0

    def get_data_size(self) -> int:
        return self.total_size - self.data_offset


def create_image(version: int = 2) -> InitrdImage:
    return InitrdImage(version=version)
