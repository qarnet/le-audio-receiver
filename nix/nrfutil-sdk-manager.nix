{ pkgs }:
let
  sdkManagerName = "nrfutil-sdk-manager";
  sdkManagerVersion = "1.16.1";
  sdkManager = pkgs.stdenvNoCC.mkDerivation {
    pname = sdkManagerName;
    version = sdkManagerVersion;

    # Nordic's versioned package archive is fixed by its content hash. Keep
    # this separate from nixpkgs because the receiver's locked nixpkgs only
    # carries sdk-manager 1.8.0, while NCS v3.3.0 needs 1.16.1 here.
    src = pkgs.fetchurl {
      url = "https://files.nordicsemi.com/artifactory/swtools/external/nrfutil/packages/nrfutil-sdk-manager/nrfutil-sdk-manager-x86_64-unknown-linux-gnu-1.16.1.tar.gz";
      hash = "sha256-0v6X8UP4iKZ5Ij2cbgtR1zDrYLSl9KXa/JcKzSAg/jg=";
    };

    nativeBuildInputs = [ pkgs.autoPatchelfHook ];
    buildInputs = [
      pkgs.xz
      pkgs.zlib
      pkgs.libusb1
      pkgs.stdenv.cc.cc.lib
      pkgs.segger-jlink-headless
    ];

    dontConfigure = true;
    dontBuild = true;

    installPhase = ''
      runHook preInstall

      mkdir -p "$out"
      mv data/* "$out/"

      runHook postInstall
    '';

    doInstallCheck = true;
    nativeInstallCheckInputs = [ pkgs.versionCheckHook ];
    versionCheckProgramArg = "--version";

    meta = pkgs.nrfutil.meta // {
      mainProgram = sdkManagerName;
    };
  };
in
pkgs.symlinkJoin {
  name = "nrfutil-with-sdk-manager-${sdkManagerVersion}";
  paths = [
    pkgs.nrfutil
    sdkManager
  ];
  nativeBuildInputs = [ pkgs.makeWrapper ];

  # nrfutil resolves extensions from PATH. Put this joined output ahead of
  # nixpkgs' core wrapper so sdk-manager 1.16.1 is always selected.
  postBuild = ''
    wrapProgram "$out/bin/nrfutil" \
      --prefix PATH : "$out/bin"
  '';

  passthru = {
    inherit sdkManager;
  };

  meta = pkgs.nrfutil.meta // {
    mainProgram = "nrfutil";
  };
}
