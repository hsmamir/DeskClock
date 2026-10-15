# DeskClock Mechanical Design Specification

## Purpose

This document describes the mechanical requirements for a 3D-printable enclosure for DeskClock. The project uses an ESP32 DevKit V1, an ST7735 TFT display, a JQ6500-16P audio module, a speaker, an SG90 servo, and three buttons.

**Important:** This is a design brief, not a verified dimensioned drawing. Dimensions marked as references must be checked against the physical modules before a final enclosure is released.

## Hardware list

| Component    | Confirmed type                     | Mechanical notes                                                                                |
| ------------ | ---------------------------------- | ----------------------------------------------------------------------------------------------- |
| Controller   | ESP32 DevKit V1, headers soldered  | Keep USB port accessible; leave clearance above soldered headers and connected wires            |
| Display      | ST7735, 1.8-inch, 128 × 160 pixels | Measure the full PCB, visible screen area, PCB thickness, header position, and mounting options |
| Audio module | JQ6500-16P                         | Measure the actual PCB and connector locations                                                  |
| Speaker      | Actual project speaker             | Measure diameter, depth, mounting rim, and cable exit                                           |
| Servo        | SG90                               | Measure mounting tabs, screw spacing, body, horn, and full movement envelope                    |
| Buttons      | Three physical buttons             | Confirm the exact button/switch body and required travel                                        |
| Wiring       | Project wiring and connectors      | Leave routing space and avoid pinching wires                                                    |

## ESP32 board reference

A common 30-pin DOIT ESP32 DevKit V1 is often listed at approximately **51.5 × 28.3 mm**. This is only a reference: DevKit V1 clones and board variants can differ. Measure the actual board, including the USB connector and soldered headers, before creating tight-fit features.

## GPIO and access requirements

The current project pin configuration is documented in `include/pins.h`:

| Function          | ESP32 GPIO |
| ----------------- | ---------: |
| TFT MOSI          |         23 |
| TFT SCLK          |         18 |
| TFT CS            |          5 |
| TFT DC            |          2 |
| TFT RST           |          4 |
| TFT backlight     |         15 |
| TTP223 touch OUT  |         32 |
| KY-040 CLK        |         33 |
| KY-040 DT         |         27 |
| KY-040 SW         |         26 |
| SG90 servo signal |         13 |
| JQ6500 UART RX    |         16 |
| JQ6500 UART TX    |         17 |

The GPIO list describes electrical connections; it does not specify the physical placement of modules inside the enclosure.

## Mechanical requirements

- Provide a clean, unobstructed viewing window for the display.
- Keep the ESP32 USB connector accessible for programming and power.
- Avoid putting stress on the display PCB or its soldered header.
- Keep the SG90 mounting tabs secure and reserve space for the complete horn/servo movement.
- Provide a speaker opening without blocking the speaker cone.
- Make all three buttons reachable during normal use.
- Leave room for wires, connectors, and servicing.
- Prefer a design that can be opened without damaging the printed parts.
- Avoid sharp edges, trapped wires, and supports that cannot be removed from internal cavities.
- Confirm print orientation and tolerances with a physical test print.

## Required measurements

Record all measurements in millimetres. Use the sheet in [`hardware-measurements.md`](hardware-measurements.md). For every component, record length, width, maximum height, connector position, and any mounting-hole locations. Take top and side photographs with a ruler or caliper visible.

## Suggested CAD deliverables

- Editable native CAD source
- STEP export when supported
- STL files for each printable part
- Exploded view or assembly render
- Print settings and required hardware
- Assembly notes
- Validation status: concept, CAD checked, printed, test-fitted, or fully tested

A design must not be described as physically tested unless it has actually been printed and fitted to the real hardware.
