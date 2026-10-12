# Releasing a new firmware version

Clocks update from **GitHub Releases**. A clock checks for a new release by itself about twice a day
(`UPDATE_AUTO_CHECK_INTERVAL_MS` in `config.h`), so a new release shows up on its diagnostics page and settings page
within about 12 hours; "Check for updates" on the settings page checks immediately. The owner of a clock always
confirms the install ("Install"); nothing is installed by itself.

## Publish a release

1. Merge your changes into `main` and wait for the **Build** workflow to be green (it compiles every display variant).
2. Pick the next version, `vMAJOR.MINOR.PATCH` (for example `v1.2.0`), and push the tag:

   ```
   git tag v1.2.0
   git push origin v1.2.0
   ```

3. The **Release firmware** workflow builds five files per release and publishes them with generated notes:

   | File                                 | For display variant (PlatformIO environment) |
   | ------------------------------------ | -------------------------------------------- |
   | `deskclock-esp32dev.bin`             | black tab (default)                          |
   | `deskclock-esp32dev-green.bin`       | green tab                                    |
   | `deskclock-esp32dev-green2.bin`      | green tab, 2 px offset                       |
   | `deskclock-esp32dev-green3.bin`      | green tab, other offset                      |
   | `deskclock-esp32dev-red.bin`         | red tab                                      |
   | `<file>.bin.sha256` for each of them | SHA-256 the clock checks before it installs  |

   **Do not rename these files.** A clock looks for `deskclock-<its variant>.bin` and its `.sha256`.
4. Tags with a dash (`v1.3.0-beta`) become pre-releases. Clocks ignore pre-releases, so use them for testing.

## Before you tag

- Flash the build to a real clock over USB and let it run for a while. There is **no automatic rollback**:
  a release that crashes at boot leaves every clock that installed it in a boot loop until it is flashed
  over USB again.
- The first time, clocks must get a firmware that contains the update feature by USB or the
  `esp32dev-ota` environment. After that they can update themselves.

## A bad release

Delete the release (and tag) on GitHub: clocks then see the previous release as the latest one. Publish a
fixed version with a **higher** number; clocks only offer a version that is newer than the one they run.

## Trusted certificates

The clock checks GitHub's TLS certificate against the root certificates in `include/update_certs.h`.
If "Check for updates" starts to report a secure-connection error on every clock, GitHub probably moved to
another certificate authority. Run `python tools/make_update_certs.py` (needs `pip install certifi`),
add the new root's name to `WANTED` in that script if needed, and ship a release (or a USB flash) with the
regenerated file.
