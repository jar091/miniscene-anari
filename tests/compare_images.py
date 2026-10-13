# SPDX-License-Identifier: Apache-2.0

"""Compare renders of one view: brightness ratio and error against a reference.

  blender -b --factory-startup --python compare_images.py -- <reference.png> <image.png>...

The images are compared in linear light. The ratio is mean(image) / mean(reference);
the error is the root mean square of the difference of the two after both are
blurred to an eighth of their size, which hides the noise of a path tracer, and is
relative to the mean of the reference.
"""
import sys

import bpy
import numpy as np


def load(path):
    image = bpy.data.images.load(path)
    width, height = image.size
    pixels = np.empty(width * height * 4, dtype=np.float32)
    image.pixels.foreach_get(pixels)
    bpy.data.images.remove(image)
    srgb = pixels.reshape(height, width, 4)[:, :, :3]
    return np.where(srgb <= 0.04045, srgb / 12.92, ((srgb + 0.055) / 1.055) ** 2.4)


def blurred(image):
    height, width, _ = image.shape
    height, width = height // 8 * 8, width // 8 * 8
    return image[:height, :width].reshape(height // 8, 8, width // 8, 8, 3).mean(axis=(1, 3))


paths = sys.argv[sys.argv.index("--") + 1:]
reference = load(paths[0])
print("COMPARE reference {:s} mean {:.4f}".format(paths[0], reference.mean()))
for path in paths[1:]:
    image = load(path)
    if image.shape != reference.shape:
        print("COMPARE {:s}: size differs".format(path))
        continue
    error = np.sqrt(((blurred(image) - blurred(reference)) ** 2).mean()) / reference.mean()
    channels = image.mean(axis=(0, 1)) / reference.mean(axis=(0, 1))
    print("COMPARE {:s} brightness {:.3f} (r {:.3f} g {:.3f} b {:.3f}) error {:.3f}".format(
        path, image.mean() / reference.mean(), channels[0], channels[1], channels[2], error))
