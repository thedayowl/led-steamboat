# Steamboat LED layout: from the frame photos

These replace the sketch-based drafts. Every segment is now traced from the photos of the frames. Each photo was first straightened to remove the camera's perspective, using the frame tubes and the straight support rods as reference lines. The support rods, the frame tubes and Panel 2's heavy center cross are **not** in the output.

| File | What it is |
|---|---|
| `layout.svg` | All three panels assembled in one drawing |
| `panel1.svg`, `panel2.svg`, `panel3.svg` | One file per panel. They share one coordinate system, so they line up when combined. |
| `preview.png` | Render of `layout.svg` |
| `segments.csv` | Every segment: your labeled length, the length measured in the photo, and LED counts at 3", 3.5" and 4" |
| `layout.json` | The same data, for the converter |

Units are inches. Each panel file also has its straightened photo as a faded, locked layer called "Photo (reference)", so you can check the trace in Inkscape. The path label format is unchanged; see the earlier README. Channel = panel number and sequence = trace order are still placeholders for your wiring.

## What changed from the sketch drafts

- **Proportions are real.** The sketches had stretched some parts and squashed others by up to 25%. Panel 2 is about 91" × 121", and the wheel's outer rim is 57" across.
- **The panels line up.** The roof lines, deck rails, hull line and the ring on the Panel 2/3 seam all meet. The small gap at each seam is the frame tube, where the wire stops.
- **The ring** is one circle on the Panel 2/3 seam. A small slice sits on Panel 2's right frame tube. The Panel 3 part runs from Panel 3's left frame tube round the right side. Nothing crosses the gap between the two tubes. In the photo the two parts measure about 6.5" and 14", against your labels of 8" and 18".
- **The wheel:** rims, hub and axle ring are circles fitted to the photo. The 16 spokes keep the six existing spokes in their real positions and put two new ones evenly in each gap, except the gap where the mount arm meets the hub. Spokes 1–11 have the 3" stub outside the outer rim.
- **The clouds and flag** are traced along the actual wire.
- **The waves** are arcs between the crests. They dip behind the bottom frame bar to about its center line.
- **The wheelhouse roof** on Panel 1 is closed at the right end with a short vertical, assumed to be the same 2" as the left.

## Smoke puffs

- **Puff 1:** the cloud right above the stack crown.
- **Puff 2:** the big middle cloud.
- **Puff 3:** the cloud that spans the Panel 2/3 seam. It has two paths, one on each panel, both in the `smoke-puff-3` section.

The zigzag crown itself stays `smokestack-top`.

## Please check

**Your labeled length differs a lot from what the photo shows.** The label is used for the LED count either way.

| Segment | Label | Photo |
|---|---|---|
| Cloud above the stack crown (smoke puff 1) | 85" | ~47" |
| Middle cloud (smoke puff 2) | 190" | ~94" |
| Panel 2 part of the seam cloud (smoke puff 3) | 35" | ~42" |
| Upper deck columns (Panels 1 and 2) | 10" | ~12.3" |
| Top spoke (spoke04) | 18" | ~13" |
| Wheel mount arm / brace | 96" / 28" | ~75" / ~22" |
| Panel 3 rear structures | 7" / 10" | ~13" / ~7.5" (labels may be swapped) |

**Segments that sit on a support rod or frame tube.** If those are frame, not LED wire, delete them:
- Panel 1: the right post of the middle and right windows
- Panel 2: the right post of the middle window, which is the heavy center bar

**Parts outside the photos,** placed from the sketch:
- Panel 1: the tip of the bow
- Panel 3: the waves. Only the stern cusp and the crest to its right show in the photo, and they set those two heights. The other crests are placed just below the photo's bottom edge. The arc bottoms are about 2" higher than on Panels 1 and 2, as in the lit-display photo.

**Panel 1's bow:** the curve from the hull tip down to the waterline is now its own `hull` segment (about 20", marked `est`). The remaining 65" of your 85" line is the waterline.

## Totals

| Spacing | 3" | 3.5" | 4" |
|---|---|---|---|
| Panel 1 | 207 | 175 | 160 |
| Panel 2 | 430 | 365 | 327 |
| Panel 3 | 335 | 290 | 255 |
| **All** | **972** | **830** | **742** |
