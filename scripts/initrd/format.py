"""
Initrd format definitions and constants.
"""

from struct import calcsize

# Common constants
U16_MAX = 0xFFFF
U32_MAX = 0xFFFFFFFF
PAGE_SIZE = 0x1000

# Default values for v2
DEFAULT_UID = 0
DEFAULT_GID = 0
DEFAULT_FILE_MODE = 0o644
DEFAULT_DIR_MODE = 0o755
DEFAULT_LINK_MODE = 0o777


def align_up(n: int, align: int) -> int:
    """Align n up to the next multiple of align."""
    return (n + align - 1) & ~(align - 1)
