# Native dependency notices

These sources are vendored to make offline builds reproducible. No Valheim shader
bytecode or other game assets are included in the native component or test stage.

- `tokens.hpp`: Microsoft's DirectXShaderCompiler token-format header, downloaded
  from `https://github.com/microsoft/DirectXShaderCompiler/blob/main/include/dxc/Support/d3d12TokenizedProgramFormat.hpp`
  on 2026-09-24. University of Illinois Open Source License; see `DXC-LICENSE.txt`.
  SHA-256: `CF808B363406FCE9387D7EC8DAEC19940175AF966589C0D3E084A4C41C3F5060`.
- `DXBCChecksum.cpp` and `.h`: AMD GPUOpen ShaderUtils,
  `https://github.com/GPUOpen-Archive/common-src-ShaderUtils/tree/master/DX10`,
  downloaded on 2026-09-24. See `AMD-LICENSE.txt` and the notices in both source
  files. This checksum implementation is derived from the **RSA Data Security,
  Inc. MD5 Message Digest Algorithm**; its notices are included in
  `DXBCChecksum.h` alongside the test binaries.
  CPP SHA-256: `B643A548A60966B28BCE280A3A9ACF5961D2AE63DD28A68A72288DBE18085462`.
  Header SHA-256: `96B3D5D27E09A2C8D5EEA860E9EAC595C29A2B68F0F2233BF53AFCB9A165FAD1`.
- MinHook **v1.3.4**, `https://github.com/TsudaKageyu/minhook/tree/v1.3.4`.
  See `minhook/LICENSE.txt` (renamed `MinHook-LICENSE.txt` in the test stage).
  The included HDE disassembler also retains its source copyright notices.

Only these public dependencies and LeafClip's own code are compiled into the
native DLL. The Windows D3D compiler/runtime is loaded from the operating system.
