# DPDFNet native runtime

Unmodified copies of the DPDFNet authors' C inference runtime (`runtime/`, from `native_inference/native/`) and the generated model code (`dpdfnet*_48khz_hr/`, from `native_inference/artifacts/v1/`) at <https://github.com/ceva-ip/DPDFNet> commit `6a5dbd3ea5dbea88c30a2be8e7c689b5eb26b533`. Apache 2.0 (`LICENSE`).

The weight files aren't vendored; CMake downloads them from the same commit and checks them against upstream's SHA-256 sums.

`msvc_compat.h` is ours: it's force-included to supply the GCC builtins the runtime uses, so it builds with MSVC.
