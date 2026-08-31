# Third-party components

QuantaPDF's permissive backend migration pins the following components. Release
packages install these notices alongside the library; floating "latest" assets
and unverified system PDFium installations are not accepted.

## PDFium

- Version: `154.0.8021.0`, release `chromium/8021`
- Binary distributor: <https://github.com/bblanchon/pdfium-binaries>
- Upstream: <https://pdfium.googlesource.com/pdfium/>
- License: BSD-style terms plus the notices shipped in the artifact's complete
  `licenses/` directory
- Build boundary: V8 disabled, XFA disabled, partition allocator disabled

| Platform | SHA-256 |
|---|---|
| Windows x64 | `adac8ce034015427b5daa81f8eeddfcc8e84bc2a9f036f007890ff18bd4388c4` |
| Linux x64 | `685f7930cd184ea22cd77afe707c1cf53b173d18118b6e16cb213c9277d7cdc3` |
| macOS x64 | `0e770fda56c6726a08fab84c6306ad91eceb10589020ce3a407fad3ebcbe7bb2` |
| macOS arm64 | `994600fa28974ce09a1c51c35039e808a6bc8ea3839050322c101ab229ad5c96` |

## qpdf

- Version: `12.4.0`
- Upstream: <https://github.com/qpdf/qpdf>
- Package source: vcpkg baseline
  `f74a2eade17a628413746557d04db25ccf6e76f9`
- License: Apache License 2.0 or Artistic License 2.0
- Initial feature boundary: no optional OpenSSL, GnuTLS, or Zopfli feature

The qpdf package's installed copyright file and PDFium's root `LICENSE` plus
complete `licenses/` directory are the authoritative redistribution notices.
