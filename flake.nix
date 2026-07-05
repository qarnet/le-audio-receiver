{
  description = "LE Audio Receiver — nRF5340 Audio DK";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.11";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
      ...
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs {
          inherit system;
          config.allowUnfree = true;
        };

        openocd-master = import ./nix/openocd-master.nix { inherit pkgs; };

        openocdWrapped = pkgs.writeShellScriptBin "openocd" ''
          export LD_LIBRARY_PATH="${pkgs.systemd}/lib:''${LD_LIBRARY_PATH:-}"
          exec ${openocd-master}/bin/openocd "$@"
        '';

        # Minimal upstream nrfutil packaging for sdk-manager usage in the dev
        # shell. Avoid nixpkgs nrfutil because it depends on SEGGER J-Link.
        nrfutilCoreSrc =
          {
            x86_64-linux = {
              triplet = "x86_64-unknown-linux-gnu";
              version = "8.1.1";
              hash = "sha256-SAD4tx/uwMqvPBQ9KbC3/W8zxqJY2hDmYHQ/DbGJCgs=";
            };
            aarch64-linux = {
              triplet = "aarch64-unknown-linux-gnu";
              version = "8.1.1";
              hash = "sha256-y7ywCr9Ze3Uz1JQh0hNg2BOPKW2yEftYDaD8WzHWSxY=";
            };
          }
          .${system} or null;

        nrfutil-core =
          if nrfutilCoreSrc == null then
            null
          else
            pkgs.stdenvNoCC.mkDerivation {
              pname = "nrfutil-core";
              inherit (nrfutilCoreSrc) version;
              src = pkgs.fetchurl {
                url = "https://files.nordicsemi.com/artifactory/swtools/external/nrfutil/executables/${nrfutilCoreSrc.triplet}/nrfutil";
                hash = nrfutilCoreSrc.hash;
              };
              dontUnpack = true;
              nativeBuildInputs = pkgs.lib.optionals pkgs.stdenv.isLinux [ pkgs.autoPatchelfHook ];
              buildInputs = pkgs.lib.optionals pkgs.stdenv.isLinux [
                pkgs.glibc
                pkgs.stdenv.cc.cc.lib
                pkgs.zlib
                pkgs.xz
                pkgs.libusb1
                pkgs.udev
              ];
              installPhase = ''
                runHook preInstall
                mkdir -p $out/bin
                install -Dm755 $src $out/bin/nrfutil
                runHook postInstall
              '';
              meta = with pkgs.lib; {
                description = "Nordic nrfutil core CLI";
                homepage = "https://www.nordicsemi.com/Products/Development-tools/nRF-Util";
                license = licenses.unfree;
                platforms = [ system ];
                sourceProvenance = with sourceTypes; [ binaryNativeCode ];
              };
            };
      in
      {
        packages = {
          openocd-master = openocd-master;
        };

        devShells.default = pkgs.mkShell {
          name = "le-audio-receiver";

          packages = [
            openocdWrapped
          ]
          ++ pkgs.lib.optionals (nrfutil-core != null) [ nrfutil-core ]
          ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
            # native_sim builds pass -m32 on x86_64-linux. Use multilib GCC in
            # shell so Zephyr host builds work on NixOS too.
            pkgs.gccMultiStdenv.cc
          ];

          shellHook = ''
            ${pkgs.lib.optionalString pkgs.stdenv.isLinux ''
              # ── NCS toolchain env ──────────────────────────────────────────
              # Dynamically loaded via nrfutil. All toolchain/SDK paths are
              # resolved at runtime — no hardcoded hashes.
              if command -v nrfutil >/dev/null 2>&1; then
                eval "$(nrfutil sdk-manager toolchain env --ncs-version v3.3.0 --as-script sh)"
              else
                printf 'nrfutil not found — NCS toolchain not loaded.\n' >&2
                printf 'Install nrfutil or enter nix develop.\n' >&2
              fi

              # ── ZEPHYR_BASE derivation ─────────────────────────────────────
              if [ -z "''${ZEPHYR_BASE:-}" ]; then
                _zephyr_candidate=""
                # Strategy 1: derive from toolchain layout (nrfutil-managed)
                if [ -n "''${ZEPHYR_SDK_INSTALL_DIR:-}" ]; then
                  _ncs_root="$(dirname "$(dirname "$(dirname "$(dirname "$ZEPHYR_SDK_INSTALL_DIR")")")")"
                  _zephyr_candidate="$_ncs_root/v3.3.0/zephyr"
                fi
                # Strategy 2: well-known user-home path
                if [ ! -d "''${_zephyr_candidate:-}" ] && [ -d "$HOME/ncs/v3.3.0/zephyr" ]; then
                  _zephyr_candidate="$HOME/ncs/v3.3.0/zephyr"
                fi
                if [ -n "''${_zephyr_candidate:-}" ] && [ -d "$_zephyr_candidate" ]; then
                  export ZEPHYR_BASE="$_zephyr_candidate"
                else
                  printf 'ZEPHYR_BASE could not be derived.\n' >&2
                  printf 'Set it manually: export ZEPHYR_BASE=/path/to/ncs/v3.3.0/zephyr\n' >&2
                fi
              fi

              export PATH="$PWD/scripts/bin:$PATH"
              export PATH="${pkgs.gccMultiStdenv.cc}/bin:$PATH"
            ''}
            echo "LE Audio Receiver dev shell"
            ${pkgs.lib.optionalString pkgs.stdenv.isLinux ''
              if command -v west >/dev/null 2>&1; then
                echo "west: $(west --version 2>/dev/null | head -n 1)"
              fi
              if [ -n "''${ZEPHYR_BASE:-}" ]; then
                echo "ZEPHYR_BASE: $ZEPHYR_BASE"
              fi
            ''}
          '';
        };
      }
    );
}
