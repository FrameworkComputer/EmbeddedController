# Build a single platform: nix build .#sakura
# Build multiple platforms: nix build .#sakura .#lotus
# Build all platforms: nix build
# Enter dev shell: nix develop
# Incremental build: nix develop -c bash -c 'zmake build sakura'
{
  description = "Framework Laptop Embedded Controller (EC) build environment";

  inputs = {
    nixpkgs.url = github:NixOS/nixpkgs/nixos-unstable;

    zephyr-nix.url = github:adisbladis/zephyr-nix;
    zephyr-nix.inputs.nixpkgs.follows = "nixpkgs";

    cmsis.url = git+https://chromium.googlesource.com/chromiumos/third_party/zephyr/cmsis?ref=chromeos-main;
    cmsis.flake = false;

    zephyr.url = git+ssh://git@github.com/FrameworkComputer/zephyr-ec?ref=fwk-main;
    zephyr.flake = false;

    u-boot.url = git+https://chromium.googlesource.com/chromiumos/third_party/u-boot?rev=923f25080339770806369e41767f8d0464f2b451;
    u-boot.flake = false;
  };

  outputs = {
    self,
    nixpkgs,
    flake-utils,
    zephyr-nix,
    zephyr,
    cmsis,
    u-boot,
  }:
    flake-utils.lib.eachDefaultSystem (system: let
      pkgs = import nixpkgs {inherit system;};

      zephyr-sdk = zephyr-nix.packages.${system}.sdk-0_16.override {
        targets = ["arm-zephyr-eabi"];
      };

      python = pkgs.python312;
      pythonPkgs = pkgs.python312Packages;

      # Bundled Python with all required packages for the build
      buildPython = python.withPackages (ps: [
        ps.pyyaml
        ps.pykwalify
        ps.packaging
        ps.pyelftools
        ps.colorama
        ps.setuptools
      ]);

      setProjectDynamicToLicense = ''
        ${pkgs.toml-cli}/bin/toml set pyproject.toml project.dynamic license | ${pkgs.moreutils}/bin/sponge pyproject.toml
        sed -e 's/dynamic = "license"/dynamic = ["license"]/' -i pyproject.toml
      '';

      ec = ./.;

      # Get version from self.shortRev (git commit) or use "dirty" for uncommitted changes
      ecVersion = "0.0.0-${self.shortRev or "dirty"}";

      mkBuild = packages: build:
        pkgs.stdenv.mkDerivation {
          name = build;

          srcs = [
            "${ec}?ec"
            "${cmsis}?cmsis"
            "${zephyr}?zephyr"
          ];

          preUnpack = ''
            unpackCmdHooks+=(_unpack_named)
            _unpack_named() {
              local src="$1"
              if ! [[ "$src" =~ ^(.*)\?([a-z]+)$ ]]; then return 1; fi
              local path="''${BASH_REMATCH[1]}"
              local name="''${BASH_REMATCH[2]}"
              cp -pr --reflink=auto -- "$path" "$name"
            }
          '';

          sourceRoot = ".";

          postPatch = ''
            mkdir .repo

            mkdir -p src/platform
            mv ec src/platform/ec

            mkdir -p src/third_party/zephyr
            mv cmsis src/third_party/zephyr/cmsis

            mkdir -p src/third_party/zephyr
            mv zephyr src/third_party/zephyr/main
          '';

          nativeBuildInputs = [
            zephyr-sdk
            pkgs.cmake
            pkgs.git
            pkgs.ninja
            buildPython
            packages.binman
          ];

          dontUseCmakeConfigure = true;

          # Ensure the zephyr SDK is found by zmake toolchain probing
          ZEPHYR_SDK_INSTALL_DIR = "${zephyr-sdk}";

          buildPhase = ''
            ${packages.zmake}/bin/zmake -j8 build --static -v "${ecVersion}" -t zephyr ${build}
          '';

          installPhase = ''
            mkdir $out
            cp src/platform/ec/build/zephyr/${build}/output/* $out/
          '';

          dontFixup = true;
        };
    in rec {
      formatter = pkgs.alejandra;

      packages.default = packages.all;
      packages.all = pkgs.runCommand "ec-all" {} ''
        mkdir -p $out
        ln -s ${packages.lotus} $out/lotus
        ln -s ${packages.azalea} $out/azalea
        ln -s ${packages.marigold} $out/marigold
        ln -s ${packages.tulip} $out/tulip
        ln -s ${packages.sakura} $out/sakura
      '';
      packages.lotus = mkBuild packages "lotus";
      packages.azalea = mkBuild packages "azalea";
      packages.marigold = mkBuild packages "marigold";
      packages.tulip = mkBuild packages "tulip";
      packages.sakura = mkBuild packages "sakura";

      packages.zmake = pythonPkgs.buildPythonPackage {
        name = "zmake";
        src = "${ec}/zephyr/zmake";

        pyproject = true;
        build-system = [pythonPkgs.setuptools];

        # These dependencies are needed by zmake's Python (sys.executable)
        # which gets passed to CMake as Python3_EXECUTABLE
        dependencies = [
          pythonPkgs.pyyaml
          pythonPkgs.pykwalify
          pythonPkgs.packaging
          pythonPkgs.pyelftools
          pythonPkgs.colorama
        ];

        postPatch = ''
          sed -e 's#"/bin:/usr/bin"#"/bin:/usr/bin:${pkgs.gcc}/bin:${pkgs.dtc}/bin:${pkgs.ninja}/bin"${
            if pkgs.stdenv.hostPlatform.isDarwin
            then '',"DYLD_LIBRARY_PATH":"${pkgs.dtc}/lib"''
            else ""
          }#' -i zmake/jobserver.py

          # Replace sys.executable with the bundled Python that has all required packages
          sed -e 's#"Python3_EXECUTABLE": sys.executable#"Python3_EXECUTABLE": "${buildPython}/bin/python3"#' -i zmake/zmake.py
        '';
      };

      packages.binman = pythonPkgs.buildPythonPackage {
        name = "binman";
        src = "${u-boot}/tools/binman";
        pyproject = false;
        build-system = [pythonPkgs.setuptools];

        buildInputs = [
          pythonPkgs.pypaBuildHook
          pythonPkgs.pipInstallHook
        ];

        dependencies = [
          pythonPkgs.setuptools
          packages.u_boot_pylib
          pythonPkgs.libfdt
          packages.dtoc
        ];

        # Remove libfdt from pyproject.toml dependencies since we provide it via nix
        postPatch = ''
          sed -e 's/"pylibfdt"/"libfdt"/' -i pyproject.toml
          sed -e '/"libfdt"/d' -i pyproject.toml
          ${setProjectDynamicToLicense}
        '';

        # zmake calls (sys.executable, path-to-binman, ...) on purpose, so we
        # can't wrap it. This makes it unsuitable for calling directly, however.
        dontWrapPythonPrograms = true;
      };

      packages.dtoc = pythonPkgs.buildPythonPackage {
        name = "dtoc";
        src = "${u-boot}/tools/dtoc";
        pyproject = false;
        build-system = [pythonPkgs.setuptools];

        buildInputs = [
          pythonPkgs.pypaBuildHook
          pythonPkgs.pipInstallHook
        ];

        dependencies = [
          packages.u_boot_pylib
          pythonPkgs.libfdt
        ];

        # Remove libfdt from pyproject.toml dependencies since we provide it via nix
        # pip would otherwise fail to find it as a pip package
        postPatch = ''
          sed -e 's/"pylibfdt"/"libfdt"/' -i pyproject.toml
          sed -e '/"libfdt"/d' -i pyproject.toml
          ${setProjectDynamicToLicense}
        '';

        makeWrapperArgs = ["--set DYLD_LIBRARY_PATH ${pkgs.dtc}/lib"];
      };

      packages.u_boot_pylib = pythonPkgs.buildPythonPackage {
        name = "u_boot_pylib";
        src = pkgs.fetchPypi {
          pname = "u_boot_pylib";
          version = "0.0.6";
          sha256 = "mwbw339O51qNObu6Mn4FvLARI7Y6zSkdLxeCx49tNd0=";
        };
        pyproject = true;

        buildInputs = [pythonPkgs.setuptools];
      };

      devShells.default = pkgs.mkShell {
        name = "EmbeddedController";

        buildInputs = [
          zephyr-sdk
          pkgs.cmake
          pkgs.git
          pkgs.ninja
          buildPython
          packages.zmake
          packages.binman
        ];

        shellHook = ''
          mkdir -p .repo

          # Set up source tree with symlinks
          rm -rf src
          mkdir -p src/platform
          ln -fs ../.. src/platform/ec

          mkdir -p src/third_party/zephyr
          ln -fs ${cmsis} src/third_party/zephyr/cmsis
          ln -fs ${zephyr} src/third_party/zephyr/main

          # Set environment variables for zmake
          export ZEPHYR_SDK_INSTALL_DIR="${zephyr-sdk}"

          # Create zmake wrapper that forces zephyr toolchain
          zmake() {
            if [[ "$1" == "build" ]]; then
              command zmake build -t zephyr "''${@:2}"
            elif [[ "$1" == "configure" ]]; then
              command zmake configure -t zephyr "''${@:2}"
            else
              command zmake "$@"
            fi
          }
          export -f zmake

          echo "EC development shell ready."
          echo "  - Run 'zmake build <target>' to build firmware"
        '';
      };
    });
}
