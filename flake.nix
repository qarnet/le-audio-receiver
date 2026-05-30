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
    in {
      devShells.${system}.default = pkgs.mkShell {
        name = "le-audio-receiver";

        buildInputs = with pkgs; [
          nrfutil
          pyocd
        ];

        shellHook = ''
          eval "$(
            ${pkgs.nrfutil}/bin/nrfutil sdk-manager toolchain env --ncs-version v3.3.0 --as-script sh \
              | grep -v '^export LD_LIBRARY_PATH=' \
              | grep -v '^export PYTHONHOME=' \
              | grep -v '^export PYTHONPATH='
          )"

          export ZEPHYR_BASE=/home/thomas-workstation/ncs/v3.3.0/zephyr
          export ZEPHYR_SDK_INSTALL_DIR=/home/thomas-workstation/ncs/toolchains/911f4c5c26/opt/zephyr-sdk
        '';
      };
    };
}
