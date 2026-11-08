# DPDFNet optimized runtime

Unmodified copies of the DPDFNet authors' optimized W7A8 inference runtime (`include/`, `src/`, from `optimized_inference/`) at <https://github.com/ceva-ip/DPDFNet> commit `98df76589cdf7787553b9e05a112d5820e6c8db6` (branch `optimized-inference`). The AVX-VNNI kernels and the Linux export map are left out. Apache 2.0 (`LICENSE`).

The weight files aren't vendored; CMake downloads them from the same commit and checks them against upstream's SHA-256 sums.

Upstream only supports Linux. `windows_abi.h` is ours: it's force-included so the C sources call the assembly kernels with the SysV calling convention they're written for. CMake strips the ELF-only directives from the `.S` files into the build tree.

`tests/contracts.ps1` runs upstream's contract tests for the assembly kernels on Windows; run it after updating.
