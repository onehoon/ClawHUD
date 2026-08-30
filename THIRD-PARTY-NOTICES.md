# Third-Party Notices

ClawHUD includes or redistributes the following third-party components. Each component remains subject to its own license terms.

## Velopack

- Component: Velopack `velopack_libc.dll`
- Version currently pinned by ClawHUD: 1.2.0
- License: MIT
- Project: https://github.com/velopack/velopack

```text
Copyright © 2021 Caelan Sayler
Copyright © 2024 Velopack Ltd.

Permission is hereby granted,  free of charge,  to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to  use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
```

## Intel Graphics Control Library (IGCL) ABI declarations

- Component: selected ABI declarations derived from Intel igcl_api.h v1.1
- Purpose: compile-time declarations for dynamically loading the driver-installed ControlLib.dll
- Project: https://github.com/intel/drivers.gpu.control-library
- License: Intel Software License Agreement 10.07.21
- Runtime binary: not redistributed by ClawHUD
- Bundled license copy: `third_party/Intel-IGCL-LICENSE.txt` (verbatim upstream `LICENSE`)
- Upstream revision: `b6c462933502e13d1537dd5024949a51be30e63d`
- The derived compatibility header retains Intel's copyright notice and points to the bundled license copy.

## MangoHud

- Purpose: visual style reference and adapted visual implementation
- License: MIT
- Project: https://github.com/flightlessmango/MangoHud
- Reference revision: `00b63717ed7220e4476df7936cf047a11964ea2d`

```text
MIT License

Copyright (c) 2020 flightlessmango

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Unispace

- Component: bundled `fonts/Unispace.otf`
- Designer: Ray Larabie / Typodermic Fonts
- Purpose: private HUD font
- License: public domain (CC0), according to the official Typodermic Fonts distribution
- Source: https://typodermicfonts.com/public-domain/
- Bundled source notice: `fonts/Unispace-LICENSE.txt`

## PresentMon

- Component: PresentMon `tools/PresentMon.exe`
- Version currently pinned by ClawHUD: 2.5.1
- License: MIT
- Project: https://github.com/GameTechDev/PresentMon
- Bundled license copy: `tools/PresentMon-LICENSE.txt`
- Repository license source: `third_party/PresentMon-LICENSE.txt`

```text
Copyright (C) 2017-2024 Intel Corporation

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom
the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
OR OTHER DEALINGS IN THE SOFTWARE.
```

## PresentMon API2 shared-service runtime

- Components: `third_party/presentmon/2.5.1/PresentMonAPI2Loader.dll` and `ClawHUD.PresentMonRuntime.msi`
- Version: PresentMon v2.5.1, API 3.3
- Project: https://github.com/GameTechDev/PresentMon
- Upstream commit: `3e06c7dcb922e411bae38503b51ab501be61c37f`
- License: MIT
- Provenance and artifact hashes: `third_party/presentmon/2.5.1/PROVENANCE.md` and `SHA256SUMS.txt`
- The MSI contains the matching shared service, `PresentMonService.exe`, and `PresentMonAPI2.dll`; those files are not shipped separately.
