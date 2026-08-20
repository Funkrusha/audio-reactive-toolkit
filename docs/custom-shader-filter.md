# ART Custom Shader Filter

**ART Custom Shader** is a native OBS video filter that lets you write your own audio-reactive pixel shader instead of
picking from the built-in ART effects (Mosaic, RGB Split, Shake, Zoom/Punch, Pixelate, Wave, Glow/Pulse, Glitch, 3D
Tile). It is aimed at people comfortable with basic HLSL who want a look ART doesn't ship yet.

This document covers what the filter is built on, its two modes, the uniform contract your shader can rely on, and a
walkthrough of the bundled example.

## What it's built on

Every native ART filter maps one or more audio-reactive parameters through a shared binding layer
(`ArtModulation`, `src/modulation/modulation-binding.{hpp,cpp}`) instead of re-implementing smoothing/envelope logic
per filter:

- **Source** – which analysis signal drives a parameter: `None`, `Level`, `Bass`, `Mids`, `Highs`, `Beat`, `Transient`.
- **Binding** – the `[min, max]` range that source is mapped onto.
- **Channel** – turns a binding plus the live audio snapshot into a smoothed value each frame. Continuous signals
  (Level/Bass/Mids/Highs) are smoothed directly; event signals (Beat/Transient) latch an envelope per event and let it
  decay, so the parameter springs back to `min` between events. `Source::None` always resolves to a constant `min` –
  i.e. "off".
- **Return speed** – the shared damping/spring speed for that decay and smoothing, exposed as a slider on every filter.
- **Effect strength (%)** – how far a bound parameter is allowed to swing from its resting `min` towards the
  audio-driven value; `0%` pins every binding at rest regardless of audio, `100%` lets it swing fully.

ART Custom Shader reuses this exact layer for its own parameters (see below) instead of inventing a separate
binding/UI system just for itself. If you've used any other ART filter, the Source/Min/Max/Return-speed/Strength
controls behave identically here.

## Two modes

### Wrapper mode (default)

You write only the body of one function:

```hlsl
float4 mainImage(float2 uv)
{
    // your code, must end in a return
}
```

ART generates the rest of the `.effect` file around it (`ViewProj`, `VSDefault`, sampler state, `technique`) at
compile time, so there's no OBS shader boilerplate to get right. The default body is a plain passthrough:

```hlsl
// uv: 0..1 screen coordinates. Available: image, textureSampler, source_size, elapsed_time, mod_1..mod_4.
return image.Sample(textureSampler, uv);
```

### Advanced mode

Toggle **"Advanced: load a raw .effect file instead of the shader body below"** to point the filter at a complete
`.effect` file on disk instead. You write the full file yourself, including `technique`/`pass`. Use this when you need
multiple techniques, helper functions outside `mainImage`'s scope, or you're adapting an existing `.effect` file.
Advanced mode pre-fills the file picker with the bundled example (see below) so toggling it on shows something working
immediately.

Both modes recompile only when the shader body, file path, or mode switch actually changes – not on every settings
save (e.g. dragging the Return speed slider). If a recompile fails, the previous working version (if any) keeps
running instead of the filter going blank; the error is written to the OBS log
(**Help > Log Files**) rather than the properties UI.

## Uniform contract

| Uniform | Type | Wrapper mode | Advanced mode |
| --- | --- | --- | --- |
| `image` | `texture2d` | always available | you declare and sample it yourself |
| `source_size` | `float2` | always available | optional – ignored if you don't declare it |
| `elapsed_time` | `float` | always available, wraps every 100000s | optional |
| `effect_mix` | `float`, `0..1` | applied automatically after `mainImage` | optional |
| `mod_1` .. `mod_4` | `float` | always available | optional |

"Optional" in Advanced mode means: declare `uniform float name;` in your `.effect` file only if you want that value.
ART looks the uniform up by name after every successful compile and simply skips setting it if your file doesn't
declare it – an unused uniform is not an error.

### `mod_1..mod_4` – four generic modulation slots

Four interchangeable float slots with no built-in meaning; each has its own **Source / Min / Max** group in the
filter's properties, using the same binding UI as every other ART filter. What `mod_1` *does* is entirely up to your
shader code – zoom amount, rotation, a threshold, a color mix factor, anything. Range on the Min/Max sliders is
`-10..10` so slots can drive multipliers and offsets, not just `0..1` amounts.

### `effect_mix` – crossfade against the original, not scene transparency

`effect_mix` is **not** alpha/scene compositing – it's a plain crossfade between your shader's own output and the
*same* source's untouched pixel, computed entirely inside the pass:

```hlsl
float4 original = image.Sample(textureSampler, input.uv);
float4 color = mainImage(input.uv);      // or your own pixel shader's result
color = lerp(original, color, saturate(effect_mix));
```

This is what "Effect mix (%)" in the properties controls. At `100%` (the default, unbound) you see only the shader's
output; at `0%` you see only the original, untouched source. Binding it to an audio source (e.g. Transient, Min `0` /
Max `100`) punches the whole effect in and out instead of leaving it on constantly – normally you see the clean
source, and the effect appears only on the bound event.

If you output your own partial alpha and want the filter to become genuinely see-through to whatever is *behind it in
the OBS scene* (a different feature from `effect_mix`), remember that OBS composites some source types with
premultiplied-alpha blending: premultiply before returning (`color.rgb *= color.a`) or you'll get a bright/wrong-
colored fringe instead of a clean fade. Wrapper mode does this premultiply automatically around `mainImage`'s result;
Advanced mode does not, since it runs your technique untouched.

## The bundled example

`data/effects/custom-shader-example.effect` ("Audio Kaleidoscope Pulse") is a complete Advanced-mode `.effect` file
that mirrors the image into rotating, slowly-spinning segments around the center, then applies a zoom pulse and
chromatic aberration. It's the file Advanced mode pre-fills by default, so toggling Advanced mode on shows it running
immediately (with all modulation slots unbound, i.e. static).

To make it audio-reactive, set on the filter's own groups after loading it:

| Group | Suggested source | Suggested range | Effect |
| --- | --- | --- | --- |
| Modulation slot 1 | Bass | `0 .. 1` | Zoom pulse |
| Modulation slot 2 | Highs | `0 .. 1` | Chromatic aberration amount |
| Modulation slot 3 | Beat | `0 .. 1` | Brightness pulse |
| Effect mix | Transient | `0 .. 100` | Punches the whole effect in on transients, clean video otherwise |

The file's own header comment documents the same table and is the fastest way to see a complete, working example of
every uniform in the contract above (including the `effect_mix` crossfade written out by hand, since Advanced mode
doesn't add it for you).

## Writing your first wrapper shader

1. Add the **ART Custom Shader** filter to a source.
2. Leave Advanced mode off.
3. Replace the shader body with something small, e.g. a static color tint:

   ```hlsl
   float4 color = image.Sample(textureSampler, uv);
   color.rgb = lerp(color.rgb, float3(1.0, 0.2, 0.2), 0.5);
   return color;
   ```

4. Bind `mod_1` to **Level** (Min `0`, Max `1`) and use it in place of the constant `0.5` above to make the tint pulse
   with the audio level.
5. If something doesn't compile, the filter keeps showing whatever last compiled successfully (or the plain original
   video, if this is the first edit) – check **Help > Log Files** in OBS for the HLSL compiler error.
