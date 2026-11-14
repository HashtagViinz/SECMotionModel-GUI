# Embedded assets

## `earth_1024.jpg`

Equirectangular Earth texture, 1024x512, JPEG quality 85 (about 80 KB).

- Source: `earth_atmos_2048.jpg` from the three.js examples
  (`https://threejs.org/examples/textures/planets/earth_atmos_2048.jpg`),
  derived from NASA Blue Marble imagery (public domain).
- Downscaled to 1024x512 so the texture is small enough to embed in the
  binaries while staying sharp at the default globe zoom.
- Longitude 0 is at the centre of the image and latitude +90 at the top, which
  matches the ECEF UV mapping used by the renderer.

The image is compiled into `src/render/EarthTextureData.cpp`. To regenerate the
header after replacing the JPEG:

```bash
python3 tools/embed_texture.py assets/earth_1024.jpg \
  src/render/EarthTextureData.hpp src/render/EarthTextureData.cpp
```

The generated files are committed; the build does not need Python or network
access.
