{
  description = "LE Audio Receiver — nRF5340 Audio DK";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.11";
  };

  outputs = { nixpkgs, ... }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs {
        inherit system;
        config = {
          allowUnfree = true;
          segger-jlink.acceptLicense = true;
        };
      };
      toolchain = "/home/thomas-workstation/ncs/toolchains/911f4c5c26";
    in {
      devShells.${system}.default = pkgs.mkShell {
        name = "le-audio-receiver";
        buildInputs = with pkgs; [
          nrfutil
          pyocd
        ];
        shellHook = ''
          # nRF Connect SDK v3.3.0 toolchain environment.
          TC=${toolchain}
          export PATH="$TC/usr/bin:$TC/usr/local/bin:$TC/opt/bin:$TC/opt/zephyr-sdk/arm-zephyr-eabi/bin:$TC/opt/zephyr-sdk/riscv64-zephyr-elf/bin:$TC/opt/nanopb/generator-bin:$TC/nrfutil/bin:$PATH"
          export LD_LIBRARY_PATH="$TC/usr/lib:$TC/usr/lib/x86_64-linux-gnu:$TC/usr/local/lib:$LD_LIBRARY_PATH"
          export PYTHONHOME="$TC/usr/local"
          export PYTHONPATH="$TC/usr/local/lib/python3.12:$TC/usr/local/lib/python3.12/site-packages:$PYTHONPATH"
          export ZEPHYR_BASE=/home/thomas-workstation/ncs/v3.3.0/zephyr
          export ZEPHYR_SDK_INSTALL_DIR=$TC
        '';
      };
    };
}
