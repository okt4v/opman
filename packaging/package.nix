# Nix package for opman (used by flake.nix: `nix build`, `nix run`).
# CMake's FetchContent can't download in the sandbox, so every dependency is pinned here and handed
# over via FETCHCONTENT_SOURCE_DIR_<NAME>. Keep these in sync with the FetchContent_Declare()s in CMakeLists.txt.
{ lib, gcc14Stdenv, src, fetchFromGitHub, fetchzip, cmake, ninja, pkg-config, makeWrapper, librsvg
, glfw, libGL, wayland, libxkbcommon, libx11, libxcursor, libxi, libxrandr }:
let
  deps = {
    IMGUI = fetchFromGitHub { owner = "ocornut"; repo = "imgui"; rev = "v1.93.0-docking"; hash = "sha256-NCD0li4ECPSFhhU0egSWMiYNBKSJDoB80rSOUiRAQ6c="; };
    STB = fetchFromGitHub { owner = "nothings"; repo = "stb"; rev = "003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5"; hash = "sha256-tvt5gGQjeYNaoYzKgN6dPDtfN1Ylmqae5tYGYNsoi1M="; };
    JSON = fetchzip { url = "https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz"; hash = "sha256-tOInM4YYi/cbHCWP2R0jlFzlaTN0ZVgHFMWagz9Wnvk="; };
  };
  # GLFW dlopens wayland/x11/GL
  runtimeLibs = [ libGL wayland libxkbcommon libx11 libxcursor libxi libxrandr ];
in
gcc14Stdenv.mkDerivation {
  pname = "opman";
  version = "0.1.0";
  inherit src;

  nativeBuildInputs = [ cmake ninja pkg-config makeWrapper librsvg ];
  buildInputs = [ glfw libGL ];

  cmakeFlags = [ "-DFETCHCONTENT_FULLY_DISCONNECTED=ON" ]
    ++ lib.mapAttrsToList (name: dir: "-DFETCHCONTENT_SOURCE_DIR_${name}=${dir}") deps;

  installPhase = ''
    runHook preInstall
    cmake --install . --prefix $out --component opman
    runHook postInstall
  '';

  postInstall = ''
    for s in 64 128 256 512; do
      mkdir -p $out/share/icons/hicolor/''${s}x''${s}/apps
      rsvg-convert -w $s -h $s ${./opman.svg} -o $out/share/icons/hicolor/''${s}x''${s}/apps/opman.png
    done
    wrapProgram $out/bin/opman --suffix LD_LIBRARY_PATH : ${lib.makeLibraryPath runtimeLibs}
  '';

  doCheck = true;
  checkPhase = "./opman_check";

  meta = {
    description = "Open math notes: a notes editor that typesets and calculates as you type, with vi keys";
    mainProgram = "opman";
    license = lib.licenses.gpl3Plus;
    platforms = lib.platforms.linux;
  };
}
