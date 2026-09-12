{
  description = "LE Audio Receiver — nRF5340 + nRF54L15";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.11";
    flake-utils.url = "github:numtide/flake-utils";
    nix-nrf-dev = {
      url = "github:qarnet/nix-nrf-dev";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = {
    self,
    nixpkgs,
    flake-utils,
    nix-nrf-dev,
    ...
  }:
    flake-utils.lib.eachDefaultSystem (
      system: let
        pkgs = nixpkgs.legacyPackages.${system};
      in {
        devShells.default = nix-nrf-dev.lib.${system}.mkNrfShell {
          name = "le-audio-receiver";
          ncsVersion = "v3.3.0";
          # Runtime deps for scripts/bap_central.py (BlueZ BAP source endpoint
          # via D-Bus). These land on the shell's nixpkgs python — the NCS
          # toolchain python stays scoped inside the west wrapper, so there is
          # no collision with the firmware build toolchain.
          # Host-only system HIL dependencies. ALSA tools and NumPy support
          # capture schema/oracle tests; they do not start a capture by
          # themselves.
          packages =
            [
              pkgs.gcovr
              pkgs.alsa-utils
            ]
            ++ (with pkgs.python3Packages; [
              dbus-python
              intelhex
              numpy
              pygobject3
              pytest
              pyserial
            ]);
        };
      }
    );
}
