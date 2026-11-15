# Embedded assets

## `earth_2048.jpg`

Equirectangular Earth day map, 2048x1024, JPEG quality 92 (about 330 KB).

- Source: [Solar System Scope — Earth textures](https://www.solarsystemscope.com/textures/)
  (`2k_earth_daymap.jpg`), licensed under
  [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
- The map is deliberately vivid: deep blue oceans, saturated green vegetation,
  warm deserts and white ice. It replaces the previous washed-out 1024x512
  Blue Marble derivation so the globe reads well against the black space
  background.
- The desert pixels are graded to a sandy tone (roughly `(224, 191, 140)` over
  the Sahara) while oceans, vegetation and ice stay untouched. Bright deserts
  would match the near-white satellite markers and hide them; sand keeps the
  constellation readable.
- Longitude 0 is at the centre of the image and latitude +90 at the top, which
  matches the ECEF UV mapping used by the renderer.

The image is compiled into `src/render/EarthTextureData.cpp`. To rebuild the
graded map from the original Solar System Scope download and then refresh the
embedded header:

```bash
python3 tools/grade_deserts.py 2k_earth_daymap.jpg assets/earth_2048.jpg
python3 tools/embed_texture.py assets/earth_2048.jpg \
  src/render/EarthTextureData.hpp src/render/EarthTextureData.cpp
```

`grade_deserts.py` is deterministic: running it on the original download
reproduces the committed JPEG byte for byte. The specification is: warm pixels
(`r > g > b`, luminance > 0.40, saturation 0.08-0.60, `r - b > 0.06`), then
luminance-preserving saturation 1.20, channel weights R 1.05 / G 1.00 /
B 0.86 and a 10% darkening. The feather keeps coastlines clean; the mask covers
about 6% of the map.

The generated files are committed; the build does not need Python or network
access.

## Attribution

The Earth texture is by [Solar System Scope](https://www.solarsystemscope.com/)
and is used under CC BY 4.0. Keep this attribution and the one in
`THIRD_PARTY.md` when redistributing the binaries.
