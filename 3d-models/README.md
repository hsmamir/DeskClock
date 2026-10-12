# DeskClock 3D Models

This folder is the home for community-created, 3D-printable parts for DeskClock.

## What we need

The goal is a practical, attractive enclosure for the existing hardware:

- ESP32 development board (`esp32dev`, 4 MB flash)
- 1.8-inch ST7735 TFT display, 128 × 160 pixels
- JQ6500-16P audio module and small speaker
- Hobby servo (for example, SG90) for the moving head
- Three push buttons: Pet, Mode, and Alarm

The current wiring and GPIO assignments are documented in the main README and `include/pins.h`. Please check the actual dimensions of your own modules before finalizing a design: boards and displays sold under the same name can have different PCB outlines, mounting holes, and component clearances.

## Suggested deliverables

For each design, please provide:

- Editable CAD source (native file and/or STEP)
- STL files for each printable part
- A few renders or assembly photos
- Print orientation and recommended settings
- Fastener sizes, inserts, and any extra hardware required
- Notes on fit, cable routing, speaker openings, button access, display visibility, and servo movement

## Folder conventions

Organize each distinct design in its own folder, for example:

```text
3d-models/
  README.md
  designs/
    compact-enclosure/
      source/
      stl/
      renders/
      README.md
```

Do not overwrite another contributor's design. Use a descriptive folder name and mention the designer in its README.

## Design considerations

- Keep the TFT display visible and securely mounted.
- Leave access for USB programming and power.
- Provide clearance for the servo's full movement and avoid pinching wires.
- Keep the speaker grille acoustically open.
- Make the three buttons easy to reach and identify.
- Allow the enclosure to be opened for repair and firmware updates.
- Consider printability without excessive supports and use sensible wall thicknesses.
- Avoid assuming that a CAD render proves fit: mark the design as “untested” until a physical test-fit has been completed.

## How to submit a design

Open an Issue using the **3D Design Proposal** template before starting a large design. Once the design is ready, submit a Pull Request and include the files and checklist described in [`CONTRIBUTING.md`](../CONTRIBUTING.md).

## Current status

No official enclosure dimensions or reference CAD model are defined here yet. Contributors should open an Issue to confirm measurements and constraints before treating any design as the official enclosure.
