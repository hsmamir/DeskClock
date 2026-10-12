# Contributing to DeskClock

Thanks for helping improve DeskClock! Contributions are welcome from firmware developers, electronics enthusiasts, CAD designers, and 3D-printing makers.

## Ways to contribute

- Design or improve a 3D-printable enclosure.
- Share editable CAD files, STL files, renders, and print settings.
- Improve documentation, wiring diagrams, or assembly instructions.
- Report bugs or suggest firmware improvements.

## Before starting

1. Check existing Issues and Pull Requests to avoid duplicate work.
2. For substantial changes, open an Issue first to discuss the approach.
3. For enclosure work, review [`3d-models/README.md`](../3d-models/README.md) and confirm any unclear dimensions before finalizing the design.

## Pull Request workflow

1. Fork this repository.
2. Create a focused branch, for example `feature/enclosure-v1` or `docs/assembly-guide`.
3. Make and commit your changes.
4. Push the branch to your fork.
5. Open a Pull Request against `main`, explaining what changed and how it was checked.

You do not need write access to the original repository. Please do not include unrelated changes in the same PR.

## 3D model contribution checklist

- [ ] Include editable source files when possible (for example, STEP or native CAD files).
- [ ] Include STL files for printable parts.
- [ ] State units and intended print orientation.
- [ ] Document material, layer height, supports, infill, and any other important print settings.
- [ ] Include renders or photos if available.
- [ ] Identify assumptions about component dimensions and clearances.
- [ ] Do not claim a design has been physically tested unless it has actually been printed and test-fitted.

## Firmware and documentation checklist

- Describe the purpose and user-visible impact of the change.
- Include screenshots, photos, or test steps when useful.
- Do not commit Wi-Fi passwords, OTA passwords, personal tokens, or other secrets.
- Keep pull requests focused and update documentation if behavior or wiring changes.

By submitting a contribution, you agree that it may be distributed under the applicable license(s) in this repository. If the repository does not yet specify a suitable license for a contribution type, please discuss licensing before submitting substantial work.
