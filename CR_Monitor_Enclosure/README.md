# CR Monitor enclosure — revision A

Editable Blender 4.5 LTS enclosure for the hardware documented at [Classroom Environment Monitor](https://reyvanair.github.io/CR_Monitor/), revision 2026-09-13.

**Status: dimensioned prototype.** The documentation identifies the hardware and wiring but does not supply mechanical drawings, breakout-board variants, a battery holder, or a battery power circuit. The component sizes below are explicit design assumptions. Digital mesh and interference checks do not establish a physical fit.

## Open the design

Open **CR_Monitor_Enclosure.blend** in Blender 4.5 or newer. The active scene is `CR Monitor | assembled`.

- **Frame 1:** assembled enclosure. **Frame 80:** exploded view. Drag the timeline between them to open the assembly.
- Press **Numpad 0** for the camera view. Camera markers switch between the assembled and exploded cameras.
- Collections `01`, `02`, and `03` contain the printable parts.
- Collections `04` and `05` contain reference hardware and visual legends. They are not printable components.
- Collection `06` contains hidden wireframe fit envelopes. Unhide its objects to inspect the assumed spaces, at frame 1.
- Read `START HERE — design notes` in Blender's Text Editor. The generator is also embedded as `build_enclosure.py`.

The model uses **millimetres**. STL files are already centered and placed on Z = 0 in their intended print orientations. The front bezel is exported face down. Do not apply an additional 1,000× conversion when importing an STL into a slicer.

## Dimensions and layout

| Feature | Design value |
|---|---:|
| Outside enclosure, width × height × depth | **186 × 136 × 62 mm** |
| Rear shell side walls / rear floor | 2.4 / 3.0 mm |
| Front face thickness | 3.0 mm |
| Seam between shell and face | 0.30 mm |
| Registration skirt clearance | 0.35 mm per side |
| LCD opening, landscape | 76 × 51 mm |
| Shared ESP32 USB opening, height × width along the left wall | 16 × 31 mm |
| Separate power-cable opening, height × width | 10 × 14 mm |
| BH1750 opening | 22 × 16 mm |
| MAX4466 sound port | 8 mm diameter |
| SHT3x front grille | Seven 29 × 2 mm slots |
| Buzzer cup / assumed buzzer | 13.4 mm bore / 12 mm diameter |
| Rear wall-mount keyholes | 85 mm horizontal spacing; 8 mm entry, 4.4 mm neck |

The TFT occupies the upper left of the front. The ESP32 sits behind it, with USB connections on the left. The battery holder sits lower left. A separate carrier above the batteries reserves space for the chosen power electronics. Three compartments on the right place BH1750 at the top, MAX4466 in the middle, and SHT3x at the bottom. The temperature/humidity compartment has front, side and bottom vents, separated from the main electronics by a wall with small wire passages.

## Measure these parts before printing

These are **assumed envelopes**, not dimensions verified against your physical parts. Sensor names refer to the full breakout boards, not just their ICs.

| Part | Assumed size / position | Required fit check |
|---|---|---|
| ILI9481 3.5-inch Uno-style shield | PCB 98 × 64 × 1.6 mm; glass 84 × 57 × 4.6 mm; active area 73.9 × 49.3 mm | Measure PCB, glass, active-area offset, all headers and plugged-in connector depth. |
| ESP32-S3 DevKitC-1 N16R8 | PCB 64 × 27 mm | Measure the board, USB positions, header direction, shield thickness and buttons. |
| Purchased two-cell 18650 holder | 84 × 44 × 23 mm maximum nominal envelope | Measure the complete insulated holder with the chosen cells installed, including contacts and leads. |
| Two cell proxies | Diameter 18.6 mm; length 65.5 mm | Protected/button-top cells may be longer. The actual holder determines compatibility. |
| BH1750 breakout | 31 × 16 mm | Verify the light-sensitive element lines up with the opening. |
| MAX4466 breakout | 20 × 14 mm; capsule 9.6 mm diameter × 5.5 mm high | Verify capsule position, gain trimmer, headers and gasket clearance. |
| SHT3x breakout | 23 × 18 mm | Verify sensing element and connector positions; leave the sensing area uncovered. |
| Power electronics | Reserved envelope 42 × 32 × 20 mm | Choose and measure the real module(s), including wires. This is not a specified charger design. |

The TFT's assumed connected-header envelope reaches 19 mm below its PCB, to Z = 33.6 mm. The holder envelope ends at Z = 30.4 mm; the ESP32 wire envelope ends at Z = 27 mm. These leave **3.2 mm** and **6.6 mm** respectively in depth, before any additional cable bends. Check the real plug housings and route the wiring in the open channels. An overall clearance is not a guarantee that every connector orientation fits.

The ESP32 is mounted **component side toward the rear, long header pins toward the TFT**. Its open carrier clears the underside module and USB bodies. Two small rear probe holes assume button centers at X = −78 mm, Y = 25 and 35 mm. Move or omit these holes if your button locations differ. Keep the antenna end clear of the power module, cells and bundled wiring.

## Printable parts

Print one of each of the **12 STL files**:

| Files | Purpose |
|---|---|
| `01_rear_tray.stl` | Rear shell, reinforced case bosses, partitions, vents, rear keyholes and mount bosses |
| `02_front_bezel.stl` | LCD and sensor openings, registration skirt, display/sensor posts and buzzer cup |
| `03_battery_cradle.stl` | Removable cradle for the purchased insulated holder; two strap positions |
| `04_esp32_carrier.stl` | Open carrier, USB relief and tie slots |
| `05_power_carrier.stl` | Separate removable power-module carrier |
| Four `06_tft_clamp_*.stl` files | Slotted PCB-edge retainers; approximately ±1.5 mm screw travel |
| Three `07_*_carrier.stl` files | Sensor-specific open carriers, edge guides and tie slots |

PETG is a reasonable prototype material. A starting slicer setup is a 0.4 mm nozzle, 0.2 mm layers, four walls and five top/bottom layers. Adjust supports for the horizontal vent/USB bridges after checking your slicer preview. The main parts fit within a nominal 220 × 220 mm print bed. Printed legends and the sample screen are visualizations only; they are not raised lettering in the STL files.

The four main case bosses have **4.2 mm bores, 5.7 mm deep**, for suitably sized M3 heat-set inserts. Match the bores to the actual insert manufacturer's specification. Internal mount pilots are nominally 2.8 mm: confirm grip with the selected M3 plastic thread-forming screws and printed material before assembly. Do not assume every M3 screw/insert has the same fit.

## Assembly

1. Check dimensions and slicer scale, then test-print the smallest relevant carrier/clamp before the shells. Verify the intended screw/insert fit on scrap material.
2. Install the four M3 case inserts. Install the ESP32 and power carriers. Secure electronics at clear PCB edges with nonconductive ties, appropriate shims and insulation; keep solder joints off printed supports.
3. Fit the purchased holder in its cradle. Use straps through the side-wall passages. The printed cradle supplies mechanical retention, **not electrical contacts**.
4. Place the display behind the front bezel. Use a thin compressible strip at the glass perimeter and approximately 0.2 mm shims between the PCB back and retaining fingers. Adjust the four fingers to touch bare PCB edges, clear of components, headers and the active touch surface. Tighten gently.
5. Attach the three sensors to their carriers and align the sensing elements with their openings. Use narrow nonconductive ties at the provided edge positions. Keep the SHT3x unobstructed. A soft gasket around the microphone capsule can couple it to the port. Secure the piezo in its cup using suitable foam tape.
6. Route sensor wires through the partition passages. Keep the parallel TFT bus short, allow enough movement to service the front, and use the documented grounds. Route the chosen power input through the separate cable opening and secure the cable to a nearby anchor.
7. Check all connectors and wire bends with the bezel resting in place. Close with four appropriately sized M3 countersunk screws. Fit wall screws to the rear keyholes, or stand the enclosure on its bottom edge with vents unobstructed.

Suggested fasteners are **starting selections to verify**, not a guarantee for every screw head or printed thread: four M3 × 10 countersunk case screws; four M3 × 8 TFT clamp screws; two M3 × 6 BH1750 screws; two M3 × 8 SHT3x screws; two M3 × 10 MAX4466 screws. Rear carrier and cradle screws need short engagement; use washers as necessary and check blind-hole depth so the tips cannot bottom out or penetrate the rear floor. Hardware, ties, gaskets and insulation are not supplied as printable parts.

## Electrical and sensing integration

The [hardware documentation](https://reyvanair.github.io/CR_Monitor/hardware.html) specifies SHT3x and BH1750 on GPIO8/9, MAX4466 on GPIO5 and the buzzer on GPIO7. The ILI9481 shield uses the documented parallel bus, a 5 V backlight supply and, for this particular shield, a separate 3.3 V logic supply. The enclosure does not change those connections.

The source does **not** specify how the two 18650 cells are connected or charged. Select a compatible insulated holder, cell arrangement, protection/charging and regulated supply before assembly. Do not connect the raw cells directly to a rail labeled 5 V or 3V3. The power-board shape in Blender only reserves space.

After assembly, compare SHT3x readings with an external reference during warm-up, recheck light readings in the intended mounting orientation, and recalibrate the microphone with the case closed. Check whether the buzzer affects sound readings. Do not cover the BH1750 opening with tinted plastic without validating its effect. The open sensor ports make this an indoor ventilated prototype, not a weatherproof enclosure.

## Edit and regenerate

Edit the `P` dictionary in `build_enclosure.py`, then run it in Blender's Scripting workspace or with `blender --background --python build_enclosure.py`. The script creates a new scene and preserves existing scenes. `CR_MONITOR_OUTPUT` can specify an output folder. Set `render` to `False` to skip preview renders.

`parameters.json` is an exported record of assumptions; editing that JSON alone does not regenerate the model. Major changes to board sizes, depth or layout also require checking the carrier positions, posts, connector openings and wire spaces in the script. Always re-inspect the assembled result after edits.

`validation_report.json` records digital checks of closed meshes, connected solids, normals, modeled part interference and the exploded animation. It does not certify print tolerances, physical component fit, thermal performance or battery behavior.

Sources: [overview](https://reyvanair.github.io/CR_Monitor/), [pins and hardware](https://reyvanair.github.io/CR_Monitor/hardware.html), [firmware](https://reyvanair.github.io/CR_Monitor/firmware.html), and [display wiring notes](https://github.com/reyvanair/CR_Monitor/blob/main/WIRING_TFT.md). Read on 2026-09-14.
