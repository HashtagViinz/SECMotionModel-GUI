# Embedded assets

## `earth_2048.jpg`

Equirectangular Earth day map, 2048x1024, JPEG quality 90 (about 450 KB).

- Source: [Solar System Scope — Earth textures](https://www.solarsystemscope.com/textures/)
  (`2k_earth_daymap.jpg`), licensed under
  [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
- The map is deliberately vivid: deep blue oceans, saturated green vegetation,
  warm deserts and white ice. It replaces the previous washed-out 1024x512
  Blue Marble derivation so the globe reads well against the black space
  background.
- Longitude 0 is at the centre of the image and latitude +90 at the top, which
  matches the ECEF UV mapping used by the renderer.

The image is compiled into `src/render/EarthTextureData.cpp`. To regenerate the
header after replacing the JPEG:

```bash
python3 tools/embed_texture.py assets/earth_2048.jpg \
  src/render/EarthTextureData.hpp src/render/EarthTextureData.cpp
```

The generated files are committed; the build does not need Python or network
access.

## Attribution

The Earth texture is by [Solar System Scope](https://www.solarsystemscope.com/)
and is used under CC BY 4.0. Keep this attribution and the one in
`THIRD_PARTY.md` when redistributing the binaries.
