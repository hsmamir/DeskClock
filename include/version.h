#pragma once

// Firmware version and display variant.
//
// Both are set at build time (GitHub Actions does this for a release, see
// .github/workflows/release.yml and docs/releasing.md):
//   FW_VERSION  "1.2.0"          - the release tag without the leading "v"
//   FW_VARIANT  "esp32dev-green" - the PlatformIO environment = the display type;
//                                  the clock only offers the release file that matches it
// A build you make yourself shows "dev" as its version.
#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

#ifndef FW_VARIANT
#define FW_VARIANT "unknown"
#endif
