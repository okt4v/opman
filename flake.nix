{
  description = "opman - open math notes";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAll = f: nixpkgs.lib.genAttrs systems (s: f nixpkgs.legacyPackages.${s});
    in {
      packages = forAll (pkgs: rec {
        opman = pkgs.callPackage ./packaging/package.nix { src = self; };
        default = opman;
      });
      apps = forAll (pkgs: {
        default = { type = "app"; program = "${self.packages.${pkgs.stdenv.hostPlatform.system}.opman}/bin/opman"; };
      });
      devShells = forAll (pkgs: {
        default = pkgs.mkShell {
          packages = with pkgs; [ cmake ninja pkg-config gcc14 glfw libGL ];
          # GLFW dlopens wayland/x11/GL at runtime
          LD_LIBRARY_PATH = pkgs.lib.makeLibraryPath (with pkgs; [ libGL wayland libxkbcommon libx11 libxcursor libxi libxrandr ]);
        };
        # Windows cross build (x86_64): cmake -DCMAKE_TOOLCHAIN_FILE=packaging/mingw-w64.cmake
        windows = pkgs.pkgsCross.mingwW64.mkShell {
          nativeBuildInputs = with pkgs; [ cmake ninja nsis ];
          buildInputs = [ pkgs.pkgsCross.mingwW64.windows.pthreads ];
        };
      });
    };
}
