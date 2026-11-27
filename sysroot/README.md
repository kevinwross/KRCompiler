# Sysroot

Target headers and prebuilt runtime libraries live here. Common C headers are under `include/`; `kr32/include/bits/`, `kr16/include/bits/`, `kr216/include/bits/`, `kr232/include/bits/`, and `kr64/include/bits/` provide target-width `types.h`, `stdint.h`, and `float.h`. `krc::target::generate_bits_types`, `generate_bits_stdint`, and `generate_bits_float` regenerate those files from `TargetProfile`; `krc_gen_sysroot_bits` writes them into the staged sysroot at `build/<preset>/stage/sysroot`, and `test_sysroot_bits` verifies the checked-in fallback copies. `include/krc/host.h` declares the KR32 host-service wrappers, and `include/krc/kr16_file.h` the KR16 ones. Runtime libraries are staged under `build/<preset>/stage/sysroot/<target>/lib/`; KR32, KR16, and KR232 have runtime objects there.

Layout and rules: `plan.md` §5.3, §14.3. Host system headers are never searched implicitly.
