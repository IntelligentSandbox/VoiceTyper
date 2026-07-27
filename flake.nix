{
  description = "VoiceTyper native build";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { self, nixpkgs }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
      packages = forAllSystems (
        system:
        let
          pkgs = import nixpkgs { inherit system; };
          runtimeLibs = with pkgs; [
            SDL2
            alsa-lib
            libGL
            libglvnd
            libxkbcommon
            mesa
            pipewire
            pulseaudio
            wayland
            libX11
            libXcursor
            libXext
            libXfixes
            libXi
            libXinerama
            libXrandr
          ];
        in
        {
          default = pkgs.stdenv.mkDerivation {
            pname = "voicetyper";
            version = pkgs.lib.strings.removeSuffix "\n" (builtins.readFile ./VERSION);
            src = ./.;

            nativeBuildInputs = with pkgs; [
              cmake
              makeWrapper
              pkg-config
            ];

            buildInputs = with pkgs; [
              SDL2
              libGL
              libX11
              libXcursor
              libXext
              libXfixes
              libXi
              libXinerama
              libXrandr
            ];

            cmakeBuildType = "Release";
            cmakeFlags = [
              "-DVOICETYPER_CUDA=OFF"
              "-DVOICETYPER_APP_IPO=OFF"
            ];

            postInstall = ''
              wrapProgram $out/bin/VoiceTyper \
                --prefix LD_LIBRARY_PATH : ${pkgs.lib.makeLibraryPath runtimeLibs} \
                --run 'export VOICETYPER_DATA_DIR="''${VOICETYPER_DATA_DIR:-''${XDG_DATA_HOME:-$HOME/.local/share}/voicetyper}"'
            '';
          };

          cuda =
            let
              unfreePkgs = import nixpkgs {
                inherit system;
                config.allowUnfree = true;
              };
              cudaPackages = unfreePkgs.cudaPackages_13_0;
              cudaRuntimeLibs = [
                cudaPackages.cuda_cudart
                cudaPackages.libcublas
              ];
              # Opt-in ccache for dev iteration. nix builds run as a nixbld user
              # under a strict sandbox (sandbox=true, sandbox-fallback=false), so
              # the cache must live at a stable path outside any private home and
              # be exposed per-build via `--option extra-sandbox-paths`. tools/
              # nix-build.sh wires VOICETYPER_CCACHE and that sandbox hole together
              # so the hole only exists on ccache builds. Default dir is chmod 1777
              # so both the repo owner and nixbld can populate it; override with
              # CCACHE_DIR. Reading env at eval time requires --impure.
              enableCcache = builtins.getEnv "VOICETYPER_CCACHE" == "1";
              ccacheDir =
                let
                  d = builtins.getEnv "CCACHE_DIR";
                in
                if d != "" then d else "/var/cache/voicetyper-ccache";
            in
            cudaPackages.backendStdenv.mkDerivation {
              pname = "voicetyper-cuda";
              version = pkgs.lib.strings.removeSuffix "\n" (builtins.readFile ./VERSION);
              src = ./.;

              nativeBuildInputs =
                with unfreePkgs;
                [
                  cmake
                  makeWrapper
                  pkg-config
                  cudaPackages.cuda_nvcc
                ]
                ++ unfreePkgs.lib.optional enableCcache ccache;

              buildInputs = with unfreePkgs; [
                SDL2
                libGL
                libX11
                libXcursor
                libXext
                libXfixes
                libXi
                libXinerama
                libXrandr
                cudaPackages.cuda_cudart
                cudaPackages.libcublas
              ];

              cmakeBuildType = "Release";
              cmakeFlags = [
                "-DVOICETYPER_CUDA=ON"
                "-DVOICETYPER_APP_IPO=OFF"
              ]
              ++ unfreePkgs.lib.optionals enableCcache [
                "-DCMAKE_C_COMPILER_LAUNCHER=ccache"
                "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
                "-DCMAKE_CUDA_COMPILER_LAUNCHER=ccache"
              ];

              CCACHE_DIR = unfreePkgs.lib.optionalString enableCcache ccacheDir;
              CCACHE_NOHASHDIR = unfreePkgs.lib.optionalString enableCcache "true";
              CCACHE_COMPILERCHECK = unfreePkgs.lib.optionalString enableCcache "content";

              preConfigure = unfreePkgs.lib.optionalString enableCcache ''
                export CCACHE_BASEDIR="$PWD"
              '';

              postInstall = ''
                wrapProgram $out/bin/VoiceTyper \
                  --prefix LD_LIBRARY_PATH : ${unfreePkgs.lib.makeLibraryPath (runtimeLibs ++ cudaRuntimeLibs)} \
                  --run 'export VOICETYPER_DATA_DIR="''${VOICETYPER_DATA_DIR:-''${XDG_DATA_HOME:-$HOME/.local/share}/voicetyper}"'
              '';
            };

          static = pkgs.stdenv.mkDerivation {
            pname = "voicetyper-static";
            version = pkgs.lib.strings.removeSuffix "\n" (builtins.readFile ./VERSION);
            src = ./.;

            nativeBuildInputs = with pkgs; [
              cmake
              patchelf
              pkg-config
            ];

            buildInputs = with pkgs; [
              sdl2-compat
              libGL
              libX11
              libXcursor
              libXext
              libXfixes
              libXi
              libXinerama
              libXrandr
            ];

            cmakeBuildType = "Release";
            cmakeFlags = [
              "-DVOICETYPER_CUDA=OFF"
              "-DVOICETYPER_APP_IPO=OFF"
              "-DVOICETYPER_STATIC=ON"
              "-DCMAKE_DISABLE_FIND_PACKAGE_OpenMP=TRUE"
            ];

            preConfigure = ''
              export LDFLAGS="-static-libgcc -static-libstdc++"
            '';

            postInstall = ''
              for Bin in VoiceTyper VoiceTyperBench; do
                patchelf --set-interpreter /lib64/ld-linux-x86-64.so.2 \
                  --remove-rpath \
                  $out/bin/$Bin || true
              done
            '';
          };

          # Portable CUDA build: a statically-linked (C/C++ runtime) binary with the
          # CUDA runtime libs (cudart + cublas + cublasLt) bundled next to it and
          # rpath set to $ORIGIN/../lib. libcuda.so (the driver lib) is provided by
          # the host NVIDIA driver and is NOT bundled. SDL2/X11/GL come from the host.
          cuda-static =
            let
              unfreePkgs = import nixpkgs {
                inherit system;
                config.allowUnfree = true;
              };
              cudaPackages = unfreePkgs.cudaPackages_13_0;
              enableCcache = builtins.getEnv "VOICETYPER_CCACHE" == "1";
              ccacheDir =
                let
                  d = builtins.getEnv "CCACHE_DIR";
                in
                if d != "" then d else "/var/cache/voicetyper-ccache";
            in
            cudaPackages.backendStdenv.mkDerivation {
              pname = "voicetyper-cuda-static";
              version = pkgs.lib.strings.removeSuffix "\n" (builtins.readFile ./VERSION);
              src = ./.;

              nativeBuildInputs =
                with unfreePkgs;
                [
                  cmake
                  patchelf
                  pkg-config
                  cudaPackages.cuda_nvcc
                ]
                ++ unfreePkgs.lib.optional enableCcache ccache;

              buildInputs = with unfreePkgs; [
                sdl2-compat
                libGL
                libX11
                libXcursor
                libXext
                libXfixes
                libXi
                libXinerama
                libXrandr
                cudaPackages.cuda_cudart
                cudaPackages.libcublas
              ];

              cmakeBuildType = "Release";
              cmakeFlags = [
                "-DVOICETYPER_CUDA=ON"
                "-DVOICETYPER_APP_IPO=OFF"
                "-DVOICETYPER_STATIC=ON"
                "-DCMAKE_DISABLE_FIND_PACKAGE_OpenMP=TRUE"
              ]
              ++ unfreePkgs.lib.optionals enableCcache [
                "-DCMAKE_C_COMPILER_LAUNCHER=ccache"
                "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
                "-DCMAKE_CUDA_COMPILER_LAUNCHER=ccache"
              ];

              CCACHE_DIR = unfreePkgs.lib.optionalString enableCcache ccacheDir;
              CCACHE_NOHASHDIR = unfreePkgs.lib.optionalString enableCcache "true";
              CCACHE_COMPILERCHECK = unfreePkgs.lib.optionalString enableCcache "content";

              preConfigure = ''
                export LDFLAGS="-static-libgcc -static-libstdc++"
              ''
              + unfreePkgs.lib.optionalString enableCcache ''
                export CCACHE_BASEDIR="$PWD"
              '';

              postInstall = ''
                mkdir -p $out/lib
                cp -L ${cudaPackages.cuda_cudart}/lib/libcudart.so.* $out/lib/ 2>/dev/null || true
                cp -L ${cudaPackages.libcublas.lib}/lib/libcublas.so.* $out/lib/
                cp -L ${cudaPackages.libcublas.lib}/lib/libcublasLt.so.* $out/lib/
                for Bin in VoiceTyper VoiceTyperBench; do
                  patchelf --set-interpreter /lib64/ld-linux-x86-64.so.2 \
                    --set-rpath '$ORIGIN/../lib' \
                    $out/bin/$Bin || true
                done
              '';
            };

          appimage = pkgs.stdenv.mkDerivation {
            pname = "VoiceTyper";
            version = pkgs.lib.strings.removeSuffix "\n" (builtins.readFile ./VERSION);
            src = ./.;

            nativeBuildInputs = with pkgs; [
              cmake
              patchelf
              pkg-config
              squashfsTools
            ];

            buildInputs = with pkgs; [
              sdl2-compat
              libGL
              libX11
              libXcursor
              libXext
              libXfixes
              libXi
              libXinerama
              libXrandr
            ];

            cmakeBuildType = "Release";
            cmakeFlags = [
              "-DVOICETYPER_CUDA=OFF"
              "-DVOICETYPER_APP_IPO=OFF"
              "-DVOICETYPER_STATIC=ON"
              "-DCMAKE_DISABLE_FIND_PACKAGE_OpenMP=TRUE"
            ];

            preConfigure = ''
              export LDFLAGS="-static-libgcc -static-libstdc++"
            '';

            # AppImage Type 2 runtime: a small ELF stub that self-mounts the
            # appended squashfs payload. We fetch a known-good x86_64 build.
            appimagetoolRuntime = pkgs.fetchurl {
              url = "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage";
              hash = "sha256-uQ9KixiWdUX9p4pEWydoChZC8e+UiM7Si2U5jyvnrdI=";
            };

            buildPhase = ''
              runHook preBuild

              cmake --build . --config Release --parallel $NIX_BUILD_CORES

              Binary="$PWD/VoiceTyper"
              cp -f Release_cpu/VoiceTyper "$Binary"
              patchelf --set-interpreter /lib64/ld-linux-x86-64.so.2 --remove-rpath "$Binary"

              AppDir="$PWD/AppDir"
              mkdir -p "$AppDir/usr/bin" "$AppDir/usr/lib"

              cp -f "$Binary" "$AppDir/usr/bin/VoiceTyper"
              chmod +x "$AppDir/usr/bin/VoiceTyper"

              SdlLib=$(find ${pkgs.sdl2-compat}/lib -name 'libSDL2-2.0.so.0' | head -1)
              if [ -n "$SdlLib" ]; then
                cp -L "$SdlLib" "$AppDir/usr/lib/"
                patchelf --set-rpath '$ORIGIN/../lib' "$AppDir/usr/bin/VoiceTyper"
              fi

              printf '%s\n' \
                '[Desktop Entry]' \
                'Name=VoiceTyper' \
                'Exec=usr/bin/VoiceTyper' \
                'Icon=VoiceTyper' \
                'Type=Application' \
                'Categories=Audio;Utility;' \
                'Terminal=false' \
                > "$AppDir/VoiceTyper.desktop"

              cp ${./media/voicetyper-icon.png} "$AppDir/VoiceTyper.png"

              printf '%s\n' \
                '#!/bin/bash' \
                'dir="$(dirname "$(readlink -f "$0")")"' \
                'export VOICETYPER_DATA_DIR="''${VOICETYPER_DATA_DIR:-''${XDG_DATA_HOME:-$HOME/.local/share}/voicetyper}"' \
                'exec "$dir/usr/bin/VoiceTyper" "$@"' \
                > "$AppDir/AppRun"
              chmod +x "$AppDir/AppRun"

              runHook postBuild
            '';

            installPhase = ''
              runHook preInstall

              AppDir="$PWD/AppDir"
              Version="${pkgs.lib.strings.removeSuffix "\n" (builtins.readFile ./VERSION)}"
              mkdir -p "$out/bin"
              Output="$out/VoiceTyper-$Version-x86_64.AppImage"

              # Extract the Type 2 runtime (ELF portion) from appimagetool.
              # The runtime is the ELF binary; the squashfs payload follows
              # immediately after the ELF section header table. Parse the
              # ELF64 header to find the exact boundary.
              e_shoff=$(od -A n -t u8 --endian=little -j 40 -N 8 "$appimagetoolRuntime" | tr -d ' ')
              e_shentsize=$(od -A n -t u2 --endian=little -j 58 -N 2 "$appimagetoolRuntime" | tr -d ' ')
              e_shnum=$(od -A n -t u2 --endian=little -j 60 -N 2 "$appimagetoolRuntime" | tr -d ' ')
              ElfSize=$((e_shoff + e_shentsize * e_shnum))
              echo "Runtime ELF size: $ElfSize bytes"

              dd if="$appimagetoolRuntime" of="$PWD/runtime" bs=1 count=$ElfSize 2>/dev/null
              chmod +x "$PWD/runtime"

              mksquashfs "$AppDir" "$PWD/payload.squashfs" -comp xz -noappend -root-owned -no-progress

              cat "$PWD/runtime" "$PWD/payload.squashfs" > "$Output"
              chmod +x "$Output"

              ln -s "$Output" "$out/bin/VoiceTyper.AppImage"

              runHook postInstall
            '';

            # Prevent Nix fixupPhase from patchelf/strip-ing the AppImage
            # (it looks like an ELF but isn't a normal one — patchelf corrupts it)
            dontStrip = true;
            fixupPhase = "true";
          };

          # Self-contained CUDA AppImage: like `appimage` but built with CUDA and
          # bundles SDL2 + the CUDA runtime libs (cudart + cublas + cublasLt) into
          # the AppDir so it runs on any x86_64 Linux box with an NVIDIA driver.
          cuda-appimage =
            let
              unfreePkgs = import nixpkgs {
                inherit system;
                config.allowUnfree = true;
              };
              cudaPackages = unfreePkgs.cudaPackages_13_0;
              enableCcache = builtins.getEnv "VOICETYPER_CCACHE" == "1";
              ccacheDir =
                let
                  d = builtins.getEnv "CCACHE_DIR";
                in
                if d != "" then d else "/var/cache/voicetyper-ccache";
            in
            cudaPackages.backendStdenv.mkDerivation {
              pname = "VoiceTyper-cuda";
              version = pkgs.lib.strings.removeSuffix "\n" (builtins.readFile ./VERSION);
              src = ./.;

              nativeBuildInputs =
                with unfreePkgs;
                [
                  cmake
                  patchelf
                  pkg-config
                  squashfsTools
                  cudaPackages.cuda_nvcc
                ]
                ++ unfreePkgs.lib.optional enableCcache ccache;

              buildInputs = with unfreePkgs; [
                sdl2-compat
                libGL
                libX11
                libXcursor
                libXext
                libXfixes
                libXi
                libXinerama
                libXrandr
                cudaPackages.cuda_cudart
                cudaPackages.libcublas
              ];

              cmakeBuildType = "Release";
              cmakeFlags = [
                "-DVOICETYPER_CUDA=ON"
                "-DVOICETYPER_APP_IPO=OFF"
                "-DVOICETYPER_STATIC=ON"
                "-DCMAKE_DISABLE_FIND_PACKAGE_OpenMP=TRUE"
              ]
              ++ unfreePkgs.lib.optionals enableCcache [
                "-DCMAKE_C_COMPILER_LAUNCHER=ccache"
                "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
                "-DCMAKE_CUDA_COMPILER_LAUNCHER=ccache"
              ];

              CCACHE_DIR = unfreePkgs.lib.optionalString enableCcache ccacheDir;
              CCACHE_NOHASHDIR = unfreePkgs.lib.optionalString enableCcache "true";
              CCACHE_COMPILERCHECK = unfreePkgs.lib.optionalString enableCcache "content";

              preConfigure = ''
                export LDFLAGS="-static-libgcc -static-libstdc++"
              ''
              + unfreePkgs.lib.optionalString enableCcache ''
                export CCACHE_BASEDIR="$PWD"
              '';

              appimagetoolRuntime = pkgs.fetchurl {
                url = "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage";
                hash = "sha256-uQ9KixiWdUX9p4pEWydoChZC8e+UiM7Si2U5jyvnrdI=";
              };

              buildPhase = ''
                runHook preBuild

                cmake --build . --config Release --parallel $NIX_BUILD_CORES

                Binary="$PWD/VoiceTyper"
                cp -f Release_cuda/VoiceTyper "$Binary"
                patchelf --set-interpreter /lib64/ld-linux-x86-64.so.2 --remove-rpath "$Binary"

                AppDir="$PWD/AppDir"
                mkdir -p "$AppDir/usr/bin" "$AppDir/usr/lib"

                cp -f "$Binary" "$AppDir/usr/bin/VoiceTyper"
                chmod +x "$AppDir/usr/bin/VoiceTyper"

                SdlLib=$(find ${pkgs.sdl2-compat}/lib -name 'libSDL2-2.0.so.0' | head -1)
                if [ -n "$SdlLib" ]; then
                  cp -L "$SdlLib" "$AppDir/usr/lib/"
                fi

                cp -L ${cudaPackages.cuda_cudart}/lib/libcudart.so.* "$AppDir/usr/lib/" 2>/dev/null || true
                cp -L ${cudaPackages.libcublas.lib}/lib/libcublas.so.* "$AppDir/usr/lib/"
                cp -L ${cudaPackages.libcublas.lib}/lib/libcublasLt.so.* "$AppDir/usr/lib/"

                patchelf --set-rpath '$ORIGIN/../lib' "$AppDir/usr/bin/VoiceTyper"

                printf '%s\n' \
                  '[Desktop Entry]' \
                  'Name=VoiceTyper (CUDA)' \
                  'Exec=usr/bin/VoiceTyper' \
                  'Icon=VoiceTyper' \
                  'Type=Application' \
                  'Categories=Audio;Utility;' \
                  'Terminal=false' \
                  > "$AppDir/VoiceTyper.desktop"

                cp ${./media/voicetyper-icon.png} "$AppDir/VoiceTyper.png"

                printf '%s\n' \
                  '#!/bin/bash' \
                  'dir="$(dirname "$(readlink -f "$0")")"' \
                  'export VOICETYPER_DATA_DIR="''${VOICETYPER_DATA_DIR:-''${XDG_DATA_HOME:-$HOME/.local/share}/voicetyper}"' \
                  'exec "$dir/usr/bin/VoiceTyper" "$@"' \
                  > "$AppDir/AppRun"
                chmod +x "$AppDir/AppRun"

                runHook postBuild
              '';

              installPhase = ''
                runHook preInstall

                AppDir="$PWD/AppDir"
                Version="${pkgs.lib.strings.removeSuffix "\n" (builtins.readFile ./VERSION)}"
                mkdir -p "$out/bin"
                Output="$out/VoiceTyper-$Version-x86_64-cuda.AppImage"

                e_shoff=$(od -A n -t u8 --endian=little -j 40 -N 8 "$appimagetoolRuntime" | tr -d ' ')
                e_shentsize=$(od -A n -t u2 --endian=little -j 58 -N 2 "$appimagetoolRuntime" | tr -d ' ')
                e_shnum=$(od -A n -t u2 --endian=little -j 60 -N 2 "$appimagetoolRuntime" | tr -d ' ')
                ElfSize=$((e_shoff + e_shentsize * e_shnum))
                echo "Runtime ELF size: $ElfSize bytes"

                dd if="$appimagetoolRuntime" of="$PWD/runtime" bs=1 count=$ElfSize 2>/dev/null
                chmod +x "$PWD/runtime"

                mksquashfs "$AppDir" "$PWD/payload.squashfs" -comp xz -noappend -root-owned -no-progress

                cat "$PWD/runtime" "$PWD/payload.squashfs" > "$Output"
                chmod +x "$Output"

                ln -s "$Output" "$out/bin/VoiceTyper-cuda.AppImage"

                runHook postInstall
              '';

              dontStrip = true;
              fixupPhase = "true";
            };
        }
      );

      apps = forAllSystems (system: {
        default = {
          type = "app";
          program = "${self.packages.${system}.default}/bin/VoiceTyper";
        };
      });

      devShells = forAllSystems (
        system:
        let
          pkgs = import nixpkgs { inherit system; };
        in
        {
          default = pkgs.mkShell {
            packages = with pkgs; [
              SDL2
              cmake
              gcc
              gdb
              libGL
              libX11
              libXcursor
              libXext
              libXfixes
              libXi
              libXinerama
              libXrandr
              ninja
              nixfmt
              pkg-config
            ];

            shellHook = ''
              export VOICETYPER_DATA_DIR="''${VOICETYPER_DATA_DIR:-$PWD/.local/share/voicetyper}"
            '';
          };
        }
      );

      checks = forAllSystems (system: {
        default = self.packages.${system}.default;
      });
    };
}
