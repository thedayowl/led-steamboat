# Steamboat LED string routing

Each panel has one controller output feeding one continuous string. The controller sits at the bottom center of Panel 2. These are the routes, plus what the firmware needs to match them.

| Output | Panel | Bulbs | Bulb steps | Splices (extension steps) | Extension wire | Feed from controller |
|---|---|---|---|---|---|---|
| 1 | Panel 1 | 175 | 59 | 16 | 103" | 48" |
| 2 | Panel 2 | 331 | 92 | 33 | 316" | 18" |
| 3 | Panel 3 | 291 | 72 | 33 | 198" | 47" |
| **All** | | **797** | | **82** | **617" (about 51 ft)** | |

Bulb spacing is 3.5". Output 4 is unused.

## How the routes were found

- **Every member is lit end to end in one direction.** Where a column meets a rail partway along, the rail can be cut there, so the string can come off the column and go either way along the rail.
- **Short gaps are free.** The wire between bulbs is at most 6", and mounted at 3.5" that leaves 2.5" of slack. Two member ends closer than that join with no cut.
- **Longer gaps get a splice.** The string is cut and an extension spliced in. The extension runs along the frame members, behind the bulbs, rather than across open air, except where no member connects (the waves to the hull, and between the clouds).
- **The search** tries many orders and directions. It minimizes the number of splices first, then the extension wire.

**The splice counts are the minimum possible.** Each T-junction (a column meeting a rail) and each isolated piece forces a jump, because a single string can't pass through a junction twice. The minimum works out to 16, 33 and 34 splices for the three panels, and the routes reach it (Panel 3 manages 33 because two joints fall within the free slack).

## Files

| File | What it is |
|---|---|
| `route_panel1.png` … `route_panel3.png` (screen), `route_panel1_print.png` … `route_panel3_print.png` and `routing-print.pdf` (white background for printing, one panel per letter-size page, with a key) | Route diagrams. Numbers are the order along the string, with the number at the start of each step. White boxes are bulb steps, and the colored line's arrow shows the data direction. Light red boxes are extension steps: a red dashed wire spliced in, with its own arrow, drawn just to one side of any member it follows. Gray lines are free joins. The yellow line is the feed from the controller (yellow dot). |
| `panel1_routed.svg` … `panel3_routed.svg` | The same routes in Inkscape. One path per bulb step, in wiring order and drawn in the data direction, labeled `channel.step section length =bulbs`. Extension steps are on their own layer, labeled `channel.step extension length`. |
| `wiring.csv` | The build list, one row per step in the same numbering as the diagrams. Bulb steps give the section, bulb count, and first and last LED number on the string. Extension steps give the length of wire to splice in, including 2" for the joint. Both give start and end points. |
| `config_sections.h` | The firmware section table and channel LED counts generated from the routes, ready to replace the ones in `include/config.h`. |
| `layout-restore.txt` | The same layout as console commands (`chan`, `set`), to restore on the web page or paste into the serial console, for firmware that already has these section names. |

## Firmware changes needed

1. **Raise `MAX_RUNS_PER_SECTION` to 16.** The busiest section (roof) has 14 runs. A run is any stretch of consecutive LED numbers, even across a splice.
2. **Replace the section table** with the one in `config_sections.h`. It has 43 sections: the frame parts, the waterline, three smoke puffs, and 16 single spokes.
3. **Spoke sets:** the spokes alternate between sets A, B, C round the wheel, ordered so the top of the wheel moves toward the bow (`wheeldir` flips it).
4. **Waterline:** its runs are listed bow to stern across all three panels. Runs that are wired backwards are written first > last, so the waves always flow toward the stern.

## Building it

- Follow the numbers on each route diagram. A light red number is an extension: cut the string after the previous step's last bulb, splice in the extension, and continue with the next number. Wherever a member is lit in the opposite direction to the one you'd naturally run the string, the arrow shows which way the string's data arrow must point.
- **Splices:** the longest extensions are on the center panel: about 44" up to the far smoke cloud, 36" from the roof to the smokestack, and 30" at the ring. Cut on the wire side of a node so both ends keep enough wire to solder, and use three-conductor wire matching the string.
- **Power:** each output carries up to 331 nodes. Inject 12 V power at the far end of each string as well as at the start, and somewhere in the middle of Panel 2's string.
- **Lengths are approximate.** They come from the layout traced from the photos. Measure each extension against the frame before cutting.
