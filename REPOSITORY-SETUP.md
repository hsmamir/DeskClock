# GitHub repository setup checklist

These are suggested manual settings for the repository owner; this pack does not change the live GitHub repository.

## About section

Suggested description:

`An open-source ESP32 desk clock with expressive animated eyes, weather, alarm, sound, servo motion, and a community-designed 3D-printed enclosure.`

Suggested topics:
- `esp32`
- `platformio`
- `arduino`
- `desk-clock`
- `iot`
- `tft-display`
- `st7735`
- `3d-printing`
- `open-source-hardware`
- `weather-station`

## Enable community participation

- Enable Issues.
- Enable Discussions if you want a place for design ideas and community questions.
- Add `CONTRIBUTING.md` and the Issue / Pull Request templates in this pack.
- Use labels such as `3d-printing`, `hardware`, `help wanted`, `good first issue`, `documentation`, and `firmware`.
- Keep `main` protected if you want all changes reviewed through Pull Requests. Contributors can fork the repository and submit PRs without direct write access.

## Licensing

Check the current repository for a license before accepting community CAD or firmware contributions. Software and hardware design files may need different licenses. MIT is commonly used for code; CERN-OHL-S-2.0 is one option to evaluate for hardware design files. Choose intentionally and make the applicable license clear before inviting substantial contributions.

## Important missing input for enclosure design

The current project documentation identifies the major modules and pin assignments, but not a complete mechanical drawing with PCB outlines, mounting-hole coordinates, display window dimensions, servo geometry, and speaker dimensions. Before asking designers to finalize a case, publish measurements or invite contributors to coordinate measurement of their exact modules. Similar-looking modules can differ between suppliers.
