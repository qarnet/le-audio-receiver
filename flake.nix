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

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
      nix-nrf-dev,
      ...
    }:
    flake-utils.lib.eachDefaultSystem (system: {
      devShells.default = nix-nrf-dev.lib.${system}.mkNrfShell {
        name = "le-audio-receiver";
        ncsVersion = "v3.3.0";
      };
    });
}
