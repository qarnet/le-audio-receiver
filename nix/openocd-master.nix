{ pkgs }:

pkgs.openocd.overrideAttrs (old: {
  pname = "openocd-master";

  src = pkgs.fetchFromGitHub {
    owner = "openocd-org";
    repo = "openocd";

    # Use a fixed commit eventually.
    # For first testing, you can use master, but Nix needs the hash.
    rev = "master";

    hash = pkgs.lib.fakeHash;
  };

  # Git checkout needs bootstrap/autoreconf.
  nativeBuildInputs = (old.nativeBuildInputs or [ ]) ++ [
    pkgs.autoreconfHook
    pkgs.pkg-config
  ];
})
