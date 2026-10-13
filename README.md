# miniscene-anari

An ANARI device which renders nothing: when a frame is rendered, it writes the
world of the frame to a [miniScene](https://github.com/ingowald/miniScene)
(`.mini`) file. Any ANARI application becomes an exporter to the format, which
Vela (`-mini`, built with `VSR_USE_MINISCENE=ON`), hayStack and barney read.

The library is `anari_library_miniscene`, the device subtype `default`.

## Build

The device needs the ANARI SDK (with its `code_gen` component and helium) and
Python for the code generator. The miniScene sources are the submodule
`external/miniScene` ([jar091/miniScene](https://github.com/jar091/miniScene),
branch `mjar/devel`); `MINISCENE_ANARI_MINISCENE_DIR` points the build at
another checkout. A reader has to read the file layout of that commit.

```
git clone --recurse-submodules https://github.com/jar091/miniscene-anari
cmake -S miniscene-anari -B build/miniscene-anari -Danari_DIR=<sdk>/lib/cmake/anari-0.17.0
cmake --build build/miniscene-anari --config Release
cmake --install build/miniscene-anari --config Release --prefix <sdk>
```

## Use

Device parameters:

| Parameter    | Default      | |
|--------------|--------------|-|
| `fileName`   | `scene.mini` | file which is written; the environment variable `MINISCENE_ANARI_FILE` names it for applications which cannot set device parameters |
| `spotLight`  | `power`      | quad light written for a spot light: `power` keeps the emitted power, `intensity` the intensity on the axis, `none` leaves spot lights out |
| `pointLight` | `cube`       | `cube` writes a point light as six quad lights with its power, `none` leaves point lights out |

The file is written when a frame is rendered and the scene has changed since
the last time. The image of the frame is a flat gray.

In blenderphi: add `miniscene` to *Additional Libraries* of the ANARI add-on,
select the device, and set `fileName=F:/path/scene.mini` in *Device Parameters*.
A render (or the first viewport sample) writes the file.

## What the format holds, and what is done about the rest

A `.mini` file has instanced triangle meshes with normals, one set of texture
coordinates and one material each; materials with one texture per parameter;
quad, directional and environment lights. It has no camera, no renderer
settings, no point or spot lights, no vertex colors or further attributes, and
its textures have no transform, wrap mode or color space.

| ANARI | Written as |
|-------|------------|
| `physicallyBased` material | `ANARIMaterial`, all parameters |
| `matte` material | `ANARIMaterial` with roughness 1, no metal, no specular |
| image sampler, `outTransform`/`outOffset`, sRGB texels | float texture with the texels decoded and transformed (exact) |
| image sampler `inAttribute`, `inTransform` | the attribute read by the base color becomes the texture coordinates of the mesh, transformed |
| texture on another attribute than the base color | constant: the average of the texture |
| parameter read from a vertex attribute, `transform` sampler | constant: the average of the attribute over the mesh (the material is then written once per mesh) |
| `quad` light | quad light, `emission = color * radiance` |
| `directional` light | directional light, `radiance = color * irradiance / 2` (the convention of the readers) |
| `hdri` light | environment map |
| `spot` light | one quad light facing along the axis, see `spotLight` |
| `point` light | six quad lights, see `pointLight` |
| light `visible = false` | not in the format: the light is visible to the camera |
| curves, volumes, other geometry | left out |
| camera, renderer parameters, frame size | not in the format: written to the report |

Everything which was changed or left out is counted in a report next to the
file, `<fileName>.txt`, together with the camera and the renderer parameters.

## Test

`tests/render_mini.lua` renders a `.mini` file with Vela's `vsrLua` and an
ANARI library, with the camera and the renderer parameters given on the
command line (taken from the report).

## Development

AI agent harnesses were used for selected development, testing, and optimization tasks

## License

Apache License 2.0, see [LICENSE](LICENSE). miniScene, which is built into the
library, is under the same license (see the headers of its sources).
