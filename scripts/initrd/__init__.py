"""
Initrd filesystem image library.

Supports v1 and v2 initrd formats with a common api.
"""

from .image import InitrdImage, create_image
from .directive import Directive

__all__ = ['InitrdImage', 'Directive', 'create_image']
