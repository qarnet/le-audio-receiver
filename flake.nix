{
  description = "LE Audio Receiver — nRF5340 Audio DK";

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
        };
      };

      openocd-master = import ./nix/openocd-master.nix {
        inherit pkgs;
      };

      toolchain = "/home/thomas-workstation/ncs/toolchains/911f4c5c26";
      ncs = "/home/thomas-workstation/ncs/v3.3.0";

      openocdWrapped = pkgs.writeShellScriptBin "openocd" ''
        export LD_LIBRARY_PATH="${pkgs.systemd}/lib:''${LD_LIBRARY_PATH:-}"
        exec ${openocd-master}/bin/openocd "$@"
      '';
    in
    {
      packages.${system}.openocd-master = openocd-master;
      devShells.${system}.default = pkgs.mkShell {
        name = "le-audio-receiver";

        buildInputs =
          with pkgs;
          [
            nrfutil
            pyocd
            systemd
          ]
          ++ [
            openocdWrapped
          ];

        shellHook = ''
          TC=${toolchain}
          NCS=${ncs}

          export ZEPHYR_BASE="$NCS/zephyr"
          export ZEPHYR_SDK_INSTALL_DIR="$TC"

          # NCS / Zephyr toolchain binaries.
          export PATH="$TC/usr/bin:$TC/usr/local/bin:$TC/opt/bin:$TC/opt/zephyr-sdk/arm-zephyr-eabi/bin:$TC/opt/zephyr-sdk/riscv64-zephyr-elf/bin:$TC/opt/nanopb/generator-bin:$TC/nrfutil/bin:$PATH"

          # Do not globally poison Python or dynamic linker state.
          unset PYTHONHOME
          unset PYTHONPATH
          unset _PYTHON_HOST_PLATFORM
          unset _PYTHON_SYSCONFIGDATA_NAME
          unset LD_LIBRARY_PATH

          west-ncs() {
            "$TC/usr/local/bin/python3" -m west "$@"
          }

          alias west='west-ncs'
        '';
      };
    };
}
