# Sysroot

Target headers and prebuilt runtime libraries live here. Common C headers are under `include/`; `kr32/include/bits/`, `kr16/include/bits/`, and `kr64/include/bits/` provide target-width `types.h` and `stdint.h`. `krc::target::generate_bits_types` and `generate_bits_stdint` regenerate those files from `TargetProfile`; `krc_gen_sysroot_bits` writes them into the staged sysroot at `build/<preset>/stage/sysroot`, and `test_sysroot_bits` verifies the checked-in fallback copies. Runtime libraries are staged under `build/<preset>/stage/sysroot/<target>/lib/`. KR16 runtime support follows at M7.

Layout and rules: `plan.md` §5.3, §14.3. Host system headers are never searched implicitly.
