{
  description = "Framework Laptop Embedded Controller (EC) build environment";

  inputs = {
    nixpkgs.url = github:NixOS/nixpkgs/nixos-unstable;

    zephyr-nix.url = github:adisbladis/zephyr-nix;
    zephyr-nix.inputs.nixpkgs.follows = "nixpkgs";

    cmsis.url = git+https://chromium.googlesource.com/chromiumos/third_party/zephyr/cmsis?rev=4aa3ff8e4f8a21e31cd9831b943acb7a7cd56ac8;
    cmsis.flake = false;

    zephyr.url = github:FrameworkComputer/zephyr?ref=lotus-zephyr&name=zephyr;
    zephyr.flake = false;

    u-boot.url = git+https://chromium.googlesource.com/chromiumos/third_party/u-boot?rev=39759bf9fe3a5b6d4788164fc046f5f8aee5cbff;
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

      zephyr-sdk = zephyr-nix.packages.${system}.sdk-0_16.override {targets = ["arm-zephyr-eabi"];};

      python = pkgs.python312;
      pythonPkgs = pkgs.python312Packages;

      setProjectDynamicToLicense = ''
        ${pkgs.toml-cli}/bin/toml set pyproject.toml project.dynamic license | ${pkgs.moreutils}/bin/sponge pyproject.toml
        sed -e 's/dynamic = "license"/dynamic = ["license"]/' -i pyproject.toml
      '';

      ec = ./.;

      build_version = "awawa";

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
            pythonPkgs.pyyaml
            pythonPkgs.pykwalify
            pythonPkgs.packaging
            pythonPkgs.pyelftools
            pythonPkgs.colorama
            packages.binman
          ];

          dontUseCmakeConfigure = true;

          buildPhase = ''
            ${packages.zmake}/bin/zmake -j8 build ${build}
          '';

          installPhase = ''
            mkdir $out
            cp src/platform/ec/build/zephyr/${build}/output/* $out/
          '';

          dontFixup = true;
        };
    in rec {
      formatter = pkgs.alejandra;

      packages.default = packages.lotus;
      packages.lotus = mkBuild packages "lotus";
      packages.azalea = mkBuild packages "azalea";

      packages.zmake = pythonPkgs.buildPythonPackage {
        name = "zmake";
        src = "${ec}/zephyr/zmake";

        pyproject = true;
        build-system = [pythonPkgs.setuptools];

        postPatch = ''
          sed -e 's#"/bin:/usr/bin"#"/bin:/usr/bin:${pkgs.gcc}/bin:${pkgs.dtc}/bin:${pkgs.ninja}/bin"${
            if pkgs.stdenv.hostPlatform.isDarwin
            then '',"DYLD_LIBRARY_PATH":"${pkgs.dtc}/lib"''
            else ""
          }#' -i zmake/jobserver.py
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

        postPatch = ''
          sed -e 's/"pylibfdt"/"libfdt"/' -i pyproject.toml
          ${setProjectDynamicToLicense}
        '';

        # zmake calls (sys.executable, path-to-binman, ...) on purpose, so we
        # can't wrap it. This makes it unsuitable for calling directly, however.
        dontWrapPythonPrograms = true;

        # No wrapper => no need for args. I'd like to be able to still do this
        # to have a nicer binman derivation.
        # makeWrapperArgs = ["--set DYLD_LIBRARY_PATH ${pkgs.dtc}/lib"];
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

        postPatch = ''
          sed -e 's/"pylibfdt"/"libfdt"/' -i pyproject.toml
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
          pythonPkgs.pyyaml
          pythonPkgs.pykwalify
          pythonPkgs.packaging
          pythonPkgs.pyelftools
          pythonPkgs.colorama
          pythonPkgs.setuptools
          packages.zmake
          packages.binman
        ];
        
        shellHook = ''
          mkdir -p .repo
          
          rm -rf src
          mkdir -p src/platform
          ln -fs ../.. src/platform/ec
          mkdir -p src/third_party/zephyr
          ln -fs ${cmsis} src/third_party/zephyr/cmsis
          ln -fs ${zephyr} src/third_party/zephyr/main
        '';
      };
    });
}
