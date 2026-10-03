#!/usr/bin/env bash
# Build the pinned vendor-modified OpenSSL archive with visible diagnostics.
# Upstream Makefile.library discards output; prebuild its exact link input here.
set -euo pipefail
test "$#" -eq 1 || { echo "Usage: bash scripts/build-bsim-crypto.sh COMPONENT_ROOT" >&2; exit 2; }
component="$(realpath "$1/ext_libCryptov1")"
archive="$component/source-1.0.2g.tar.gz"
expected=bc0664e717df9fb05df0bb24dd77276b33fefe40287dd300e95ee87fbf1980f7
actual="$(sha256sum "$archive")"
test "${actual%% *}" = "$expected" || { echo "ERROR: bundled crypto archive changed; re-audit required" >&2; exit 1; }
real_compiler="$(command -v gcc)"
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# Legacy util/domd recognizes the gcc command name, not an absolute gcc path.
# Underlying compiler was captured from the pinned shell before the local shim.
compiler=gcc
perl="$(command -v perl)"
ar="$(command -v ar)"
ranlib="$(command -v ranlib)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
tar xfz "$archive" --directory "$work"
# Keep domd's gcc-name detection while routing every compiler invocation
# (including nested make) through the archive-specific strict policy.
mkdir "$work/bin"
printf '#!/usr/bin/env bash\nexec python3 %q %q %q "$@"\n' \
    "$script_dir/openssl-component-cc.py" "$real_compiler" "$work/source-1.0.2g" > "$work/bin/gcc"
chmod +x "$work/bin/gcc"
(
    cd "$work/source-1.0.2g"
    export CC="$compiler" PERL="$perl"
    export PATH="$work/bin:$PATH"
    setarch i386 ./config -m32 -g -fPIC -Werror no-idea no-camellia no-seed no-bf no-cast no-rc2 no-rc4 no-rc5 \
        no-md2 no-md4 no-ripemd no-mdc2 no-dsa no-dh no-ec no-ecdsa no-ecdh no-sock no-ssl2 no-ssl3 no-err no-krb5 no-engine no-hw
    make CC="$compiler" AR="$ar r" RANLIB="$ranlib" PERL="$perl" depend
    make CC="$compiler" AR="$ar r" RANLIB="$ranlib" PERL="$perl" build_libcrypto
)
cp "$work/source-1.0.2g/libcrypto.a" "$component/libcrypto.a"
