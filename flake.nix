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
          # Load the full nRF Connect SDK v3.3.0 toolchain environment.
          # export -p inside the toolchain shell gives safe, quotable exports.
          eval "$(${pkgs.nrfutil}/bin/nrfutil sdk-manager toolchain launch --ncs-version v3.3.0 -- bash -c 'export -p' | grep -v "^declare -x LS_COLORS=" | grep -v "^declare -x SUDO_" | sed 's/^declare -x /export /')"
        '';
      };
    };
}
