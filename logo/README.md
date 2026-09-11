# logo/

The source model is `Meshy_AI_Crossed_Ascension_0910024256_texture.{obj,mtl,png}`,
generated with Meshy AI: a crossed-arrows mark that reads as an **X**, one arrow
ascending in dark navy and one descending in crimson.

The three rendered files are produced from it by `tools/render-logo.py`, a small
textured software rasteriser — z-buffered, backface-culled, Lambert plus a rim
term, 2× supersampled. There is no Blender in the build closure and there is no
reason to put one there for three images; the model is 1,493 vertices.

| file | what it is |
|---|---|
| `exsecutor-logo.png` | 1024², transparent background. The one to use in a page or a README. |
| `exsecutor-logo.jpg` | 1200², on the dark ground. For anywhere that will not take alpha. |
| `exsecutor-logo.gif` | 400², 28 frames. **Not a turntable:** the mark is an extruded flat form, so a full 360° would turn it edge-on and lose it. It rocks ±0.45 rad instead, and stays readable in every frame. |

Regenerate with `python3 tools/render-logo.py` from the repository root; it
writes all three and is deterministic. The GIF is then run through
`gifsicle -O3` (lossless). An earlier version used `--lossy=40`, which halved
the file and put visible dither noise across the flat faces — flat shading on a
flat ground quantises cleanly at 64 colours with no dithering at all, which is
both smaller and better.
