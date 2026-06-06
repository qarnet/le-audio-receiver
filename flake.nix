{
  description = "LE Audio Receiver — nRF5340";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.11";
  };

  outputs =
    { nixpkgs, ... }:
    let
      system = "x86_64-linux";

      pkgs = import nixpkgs {
        inherit system;
        config = {
          allowUnfree = true;
          segger-jlink.acceptLicense = true;
          permittedInsecurePackages = [ "segger-jlink-qt4-874" ];
        };
      };

      openocd-master = import ./nix/openocd-master.nix {
        inherit pkgs;
      };

      openocdWrapped = pkgs.writeShellScriptBin "openocd" ''
        export LD_LIBRARY_PATH="${pkgs.systemd}/lib:''${LD_LIBRARY_PATH:-}"
        exec ${openocd-master}/bin/openocd "$@"
      '';

      pyocdWrapped = pkgs.writeShellScriptBin "pyocd" ''
        unset PYTHONHOME
        unset PYTHONPATH
        unset _PYTHON_HOST_PLATFORM
        unset _PYTHON_SYSCONFIGDATA_NAME
        exec ${pkgs.pyocd}/bin/pyocd "$@"
      '';
    in
    {
      packages.${system}.openocd-master = openocd-master;

      devShells.${system}.default = pkgs.mkShell {
        name = "le-audio-receiver";

        buildInputs = [
          pkgs.segger-jlink
          pkgs.nrfutil
          pkgs.systemd
          openocdWrapped
          pyocdWrapped
        ];

        shellHook = ''
          # Source NCS toolchain env AFTER nix shell is up, so nix binary
          # isn't broken by toolchain LD_LIBRARY_PATH during eval.
          eval "$(nrfutil sdk-manager toolchain env --ncs-version v3.3.0 --as-script sh 2>/dev/null)"

          # Derive ZEPHYR_BASE from toolchain path.
          # ZEPHYR_SDK_INSTALL_DIR = .../ncs/toolchains/<hash>/opt/zephyr-sdk
          # NCS root = .../ncs (4 dirname levels up)
          NCS_ROOT="$(dirname "$(dirname "$(dirname "$(dirname "$ZEPHYR_SDK_INSTALL_DIR")")")")"
          export ZEPHYR_BASE="$NCS_ROOT/v3.3.0/zephyr"

          # Prepend wrappers so they win over toolchain-bundled versions.
          export PATH="${openocdWrapped}/bin:${pyocdWrapped}/bin:$PATH"
        '';
      };
    };
}
