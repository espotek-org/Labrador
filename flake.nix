{
  description = "EspoTek Labrador - USB oscilloscope, signal generator, and more";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs {
          inherit system;
        };

      in
      {
        devShells.default = pkgs.mkShell {
          name = "labrador-dev";

          nativeBuildInputs = with pkgs; [
            # Build tools
            gnumake
            pkg-config
            qt5.wrapQtAppsHook

            # Qt tools
            qt5.qmake
          ];

          buildInputs = with pkgs; [
            # Qt5 libraries
            qt5.qtbase

            # Required libraries (from PKGCONFIG in Labrador.pro)
            libusb1
            fftw
            fftwFloat
            eigen

            # OpenMP support for fftw3_omp
            llvmPackages.openmp

            # For building
            gcc
          ];

          # Set up environment for Qt and pkg-config
          shellHook = ''
            echo "╔══════════════════════════════════════════════════════════════════╗"
            echo "║       EspoTek Labrador Development Environment                   ║"
            echo "╚══════════════════════════════════════════════════════════════════╝"
            echo ""
            echo "To build and install locally:"
            echo "  cd Desktop_Interface"
            echo "  qmake PREFIX=\$PWD/_install"
            echo "  make -j\$(nproc)"
            echo "  make install"
            echo ""
            echo "To run after installing:"
            echo "  ./_install/bin/labrador"
            echo ""
            echo "Or use 'nix build' from repo root for a proper package."
            echo ""

            # Help Qt find plugins
            export QT_PLUGIN_PATH="${pkgs.qt5.qtbase}/${pkgs.qt5.qtbase.qtPluginPrefix}"

            # Add local install to data dirs so Qt can find resources
            export XDG_DATA_DIRS="$PWD/Desktop_Interface/_install/share:''${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
          '';
        };

        packages.default = pkgs.stdenv.mkDerivation {
          pname = "labrador";
          version = "unstable-${self.shortRev or "dirty"}";

          src = ./.;

          nativeBuildInputs = with pkgs; [
            gnumake
            pkg-config
            qt5.wrapQtAppsHook
            qt5.qmake
          ];

          buildInputs = with pkgs; [
            qt5.qtbase
            libusb1
            fftw
            fftwFloat
            eigen
            llvmPackages.openmp
          ];

          # Patch the .pro file to use system libraries and skip udev rules installation
          preConfigure = ''
            cd Desktop_Interface
          '';

          configurePhase = ''
            runHook preConfigure
            qmake PREFIX=$out
            runHook postConfigure
          '';

          buildPhase = ''
            runHook preBuild
            make -j$NIX_BUILD_CORES
            runHook postBuild
          '';

          installPhase = ''
            runHook preInstall

            # Install binary
            mkdir -p $out/bin
            cp labrador $out/bin/

            # Install firmware
            mkdir -p $out/share/EspoTek/Labrador/firmware
            cp resources/firmware/labrafirm* $out/share/EspoTek/Labrador/firmware/

            # Install waveforms
            mkdir -p $out/share/EspoTek/Labrador/waveforms
            cp resources/waveforms/* $out/share/EspoTek/Labrador/waveforms/

            # Install icons
            mkdir -p $out/share/icons/hicolor/48x48/apps
            mkdir -p $out/share/icons/hicolor/256x256/apps
            cp build_linux/icon48/espotek-labrador.png $out/share/icons/hicolor/48x48/apps/
            cp build_linux/icon256/espotek-labrador.png $out/share/icons/hicolor/256x256/apps/

            # Install desktop file
            mkdir -p $out/share/applications
            cp build_linux/espotek-labrador.desktop $out/share/applications/

            runHook postInstall
          '';

          meta = with pkgs.lib; {
            description = "EspoTek Labrador - oscilloscope, signal generator, logic analyzer, and more";
            homepage = "https://github.com/espotek-org/Labrador";
            license = licenses.gpl3;
            platforms = platforms.linux;
            mainProgram = "labrador";
          };
        };
      });
}

