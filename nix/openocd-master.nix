{ pkgs }:

pkgs.openocd.overrideAttrs (old: {
  pname = "openocd-master";

  src = pkgs.fetchFromGitHub {
    owner = "openocd-org";
    repo = "openocd";
    rev = "e6752ecbcf72efe4e213e8418e381ff2e0ffdf54";
    hash = "sha256-5aW7C061BUmbNPENrCeEUg6PRqukLRF+asnJ4KPrL0w=";
    fetchSubmodules = true;
  };

  # Git checkout needs bootstrap/autoreconf.
  nativeBuildInputs = (old.nativeBuildInputs or [ ]) ++ [
    pkgs.autoreconfHook
    pkgs.pkg-config
  ];
})
