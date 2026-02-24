{
  description = "ChromiumOS EC UART Update Tool (UUT) for NPCX chips";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
      {
        packages = {
          default = self.packages.${system}.uut;

          uut = pkgs.stdenv.mkDerivation {
            pname = "uut";
            version = "2.0.1";

            src = ./util/uut;

            # Copy in the required headers from parent directories
            preBuild = ''
              mkdir -p include
              cp ${./util/misc_util.h} include/misc_util.h
              cp ${./include/compile_time_macros.h} include/compile_time_macros.h
            '';

            buildPhase = ''
              runHook preBuild

              $CXX -std=c++17 -O2 -Wall \
                -I. -Iinclude \
                -o uut \
                main.cc cmd.cc l_com_port.cc lib_crc.cc opr.cc

              runHook postBuild
            '';

            installPhase = ''
              runHook preInstall

              mkdir -p $out/bin
              cp uut $out/bin/

              runHook postInstall
            '';

            meta = with pkgs.lib; {
              description = "Linux UART Update Tool for ChromiumOS EC (NPCX chips)";
              homepage = "https://chromium.googlesource.com/chromiumos/platform/ec";
              license = licenses.bsd3;
              platforms = platforms.linux;
              mainProgram = "uut";
            };
          };
        };

        devShells.default = pkgs.mkShell {
          packages = with pkgs; [
            gcc
            gnumake
          ];

          shellHook = ''
            echo "UUT development shell"
            echo "Build with: nix build"
          '';
        };
      }
    );
}
