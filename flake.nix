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

      # opencode launched inside this devShell inherits the NCS toolchain env
      # sourced in shellHook. The toolchain's LD_LIBRARY_PATH ships brotli 1.0.7
      # (in .../usr/lib/x86_64-linux-gnu), which shadows the system
      # brotli-1.2.0-lib that node 24 links against. brotli 1.0.7 lacks
      # BrotliEncoderAttachPreparedDictionary (added in 1.1+), so node crashes
      # on startup with "undefined symbol: BrotliEncoderAttachPreparedDictionary",
      # which kills every npx-launched MCP server (brave-search, etc.) even
      # though BRAVE_API_KEY is correctly exported by the system opencode
      # wrapper. Stripping LD_LIBRARY_PATH / PYTHON* here lets the system
      # opencode wrapper and the node binaries it spawns fall back to their
      # nix-store rpaths (which point at the correct brotli 1.2.0). Toolchain
      # env stays intact for west/nrfutil/openocd/pyocd outside opencode.
      opencodeClean = pkgs.writeShellScriptBin "opencode" ''
        exec env -u LD_LIBRARY_PATH -u PYTHONHOME -u PYTHONPATH \
          -u _PYTHON_HOST_PLATFORM -u _PYTHON_SYSCONFIGDATA_NAME \
          /run/current-system/sw/bin/opencode "$@"
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
          opencodeClean
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
          # opencodeClean must come first so `opencode` inside this shell
          # resolves to the env-stripping wrapper, not the system binary.
          export PATH="${opencodeClean}/bin:${openocdWrapped}/bin:${pyocdWrapped}/bin:$PATH"
        '';
      };
    };
}
