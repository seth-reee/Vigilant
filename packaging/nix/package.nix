{ lib, stdenv, cmake, ninja, qt6, source, systemd }:

stdenv.mkDerivation {
  pname = "vigilant";
  version = builtins.elemAt (builtins.match ".*project\\(Vigilant VERSION ([^ ]+) LANGUAGES.*"
    (builtins.readFile ../../CMakeLists.txt)) 0;
  src = lib.cleanSourceWith {
    src = source;
    filter = path: type:
      let name = baseNameOf path; in
      !(lib.hasInfix ".sync-conflict-" name)
      && !(builtins.elem name [ ".git" "build" "dist" "stage" "result" ])
      && !(lib.hasSuffix ".pkg.tar.zst" name)
      && !(lib.hasSuffix ".pkg.tar.xz" name);
  };
  nativeBuildInputs = [ cmake ninja qt6.wrapQtAppsHook ];
  buildInputs = [ qt6.qtbase qt6.qtwayland ];
  cmakeFlags = [ "-DBUILD_TESTING=ON" ];
  doCheck = true;
  preCheck = ''
    export HOME="$TMPDIR/home"
    mkdir -p "$HOME"
    export TMPDIR=/tmp
    export QT_QPA_PLATFORM=offscreen
    export QT_QPA_PLATFORMTHEME=
    export QT_QUICK_CONTROLS_STYLE=Basic
    export QT_QUICK_BACKEND=software
  '';


  # Prefer the host's systemctl; retain a Nix-store fallback for minimal profiles.
  qtWrapperArgs = [ "--suffix" "PATH" ":" (lib.makeBinPath [ systemd ]) ];
  meta = {
    description = "Qt startup entry manager";
    homepage = "https://github.com/seth-reee/Vigilant";
    license = lib.licenses.mit;
    mainProgram = "vigilant";
    platforms = [ "x86_64-linux" "aarch64-linux" ];
  };
}
