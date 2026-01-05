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

        # Map Nix system to Qt arch naming
        qtArch = {
          "x86_64-linux" = "x86_64";
          "aarch64-linux" = "arm64";
          "i686-linux" = "i386";
          "armv7l-linux" = "arm";
        }.${system} or "x86_64";

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

            # Ensure the bundled libdfuprog can be found
            export LD_LIBRARY_PATH="$PWD/Desktop_Interface/build_linux/libdfuprog/lib/${qtArch}:$LD_LIBRARY_PATH"

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

            # Create a library path for the bundled libdfuprog
            mkdir -p $out/lib

            # Copy the appropriate libdfuprog for this architecture
            cp build_linux/libdfuprog/lib/${qtArch}/libdfuprog-0.9.so $out/lib/
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

          # Wrap the executable to find libdfuprog
          preFixup = ''
            qtWrapperArgs+=(--prefix LD_LIBRARY_PATH : "$out/lib")
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

