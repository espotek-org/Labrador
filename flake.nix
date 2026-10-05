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

        inherit (pkgs) lib stdenv;

        # Platform detection
        isDarwin = stdenv.isDarwin;
        isLinux = stdenv.isLinux;

        # Map Nix system to architecture names used in bundled libdfuprog (Linux only)
        dfuprogArch = {
          "x86_64-linux" = "x86_64";
          "aarch64-linux" = "arm64";
          "i686-linux" = "i386";
          "armv7l-linux" = "arm";
        }.${system} or null;

        # Library file extension differs between platforms
        libExt = if isDarwin then "dylib" else "so";

        # Build directory differs between platforms
        buildDir = if isDarwin then "build_mac" else "build_linux";

        # Library path environment variable differs between platforms
        libPathVar = if isDarwin then "DYLD_LIBRARY_PATH" else "LD_LIBRARY_PATH";

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
          ] ++ lib.optionals isLinux [
            # For building on Linux
            gcc
          ];

          # Set up environment for Qt and pkg-config
          shellHook = ''
            echo "╔══════════════════════════════════════════════════════════════════╗"
            echo "║       EspoTek Labrador Development Environment                   ║"
            echo "╚══════════════════════════════════════════════════════════════════╝"
            echo ""
            echo "Platform: ${if isDarwin then "macOS" else "Linux"}"
            echo ""
            echo "To build and install locally:"
            echo "  cd Desktop_Interface"
            echo "  qmake PREFIX=\$PWD/_install"
            echo "  make -j\$(nproc 2>/dev/null || sysctl -n hw.ncpu)"
            echo "  make install"
            echo ""
            echo "To run after installing:"
            echo "  ./_install/bin/labrador"
            echo ""
            echo "Or use 'nix build' from repo root for a proper package."
            echo ""

            # Ensure the bundled libdfuprog can be found
            ${if isDarwin then ''
              export DYLD_LIBRARY_PATH="$PWD/Desktop_Interface/${buildDir}/libdfuprog/lib:$DYLD_LIBRARY_PATH"
            '' else ''
              export LD_LIBRARY_PATH="$PWD/Desktop_Interface/${buildDir}/libdfuprog/lib/${dfuprogArch}:$LD_LIBRARY_PATH"
            ''}

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

          preConfigure = ''
            cd Desktop_Interface

            # Create a library path for the bundled libdfuprog
            mkdir -p $out/lib

            # Copy the appropriate libdfuprog for this platform/architecture
            ${if isDarwin then ''
              if [ -f "${buildDir}/libdfuprog/lib/libdfuprog-0.9.${libExt}" ]; then
                cp ${buildDir}/libdfuprog/lib/libdfuprog-0.9.${libExt} $out/lib/
              fi
            '' else if dfuprogArch != null then ''
              if [ -f "${buildDir}/libdfuprog/lib/${dfuprogArch}/libdfuprog-0.9.${libExt}" ]; then
                cp ${buildDir}/libdfuprog/lib/${dfuprogArch}/libdfuprog-0.9.${libExt} $out/lib/
              fi
            '' else ""}
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

            # Install binary (name differs on macOS)
            mkdir -p $out/bin
            if [ -f labrador ]; then
              cp labrador $out/bin/
            elif [ -f Labrador ]; then
              cp Labrador $out/bin/labrador
            fi

            # Install firmware
            mkdir -p $out/share/EspoTek/Labrador/firmware
            cp resources/firmware/labrafirm* $out/share/EspoTek/Labrador/firmware/

            # Install waveforms
            mkdir -p $out/share/EspoTek/Labrador/waveforms
            cp resources/waveforms/* $out/share/EspoTek/Labrador/waveforms/

            ${lib.optionalString isLinux ''
              # Install icons (Linux only)
              mkdir -p $out/share/icons/hicolor/48x48/apps
              mkdir -p $out/share/icons/hicolor/256x256/apps
              cp build_linux/icon48/espotek-labrador.png $out/share/icons/hicolor/48x48/apps/
              cp build_linux/icon256/espotek-labrador.png $out/share/icons/hicolor/256x256/apps/

              # Install desktop file (Linux only)
              mkdir -p $out/share/applications
              cp build_linux/espotek-labrador.desktop $out/share/applications/
            ''}

            runHook postInstall
          '';

          # Wrap the executable to find libdfuprog
          preFixup = if isDarwin then ''
            qtWrapperArgs+=(--prefix DYLD_LIBRARY_PATH : "$out/lib")
          '' else ''
            qtWrapperArgs+=(--prefix LD_LIBRARY_PATH : "$out/lib")
          '';

          meta = with pkgs.lib; {
            description = "EspoTek Labrador - oscilloscope, signal generator, logic analyzer, and more";
            longDescription = ''
              The EspoTek Labrador is an open-source board that turns your PC into a
              full-featured electronics lab bench, complete with oscilloscope, signal
              generator, logic analyzer, and more. This package provides the Qt5
              desktop application for interfacing with the Labrador hardware.
            '';
            homepage = "https://github.com/espotek-org/Labrador";
            license = licenses.gpl3Only;
            platforms = platforms.linux ++ platforms.darwin;
            mainProgram = "labrador";
          };
        };
      });
}
