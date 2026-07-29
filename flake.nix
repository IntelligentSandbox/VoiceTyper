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
          # allowUnsupportedSystem: pkgsStatic's libpulseaudio / pipewire pull
          # in transitive deps that mark { isStatic = true; } as a bad platform.
          # Those restrictions are conservative guards against truly-broken
          # static builds; the audio client libs we link against build and link
          # fine and only talk to their user-session daemons over sockets at
          # runtime. Flipping this on the pkgsStatic branch lets them evaluate.
          pkgs = import nixpkgs {
            inherit system;
            config.allowUnsupportedSystem = true;
          };
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

          # nixpkgs's SDL2 attribute is sdl2-compat, which dlopens SDL3 at
          # runtime and therefore cannot be statically linked. For the static
          # packages we instead build the real SDL2 (libsdl-org/SDL 2.32.x)
          # from upstream source against pkgsStatic (musl + static X11/audio
          # client libs). Only ALSA is enabled as the audio backend (see
          # cmakeFlags below — PipeWire and PulseAudio are OFF); ALSA is the
          # lowest common denominator and PipeWire/PulseAudio expose an ALSA
          # compat shim, so audio still works on those systems. ALSA only
          # needs the kernel API at runtime, so the resulting binary still
          # runs on any Linux (including NixOS) without system .so deps.
          staticSDL2 =
            let
              spkgs = pkgs.pkgsStatic;
            in
            spkgs.stdenv.mkDerivation (finalAttrs: {
              pname = "SDL2-static";
              version = "2.32.10";
              src = pkgs.fetchurl {
                url = "https://github.com/libsdl-org/SDL/releases/download/release-${finalAttrs.version}/SDL2-${finalAttrs.version}.tar.gz";
                hash = "sha256-X1mTxTDwhFNcZaaHnpsmrUQRabPiXXidgyhwQKnKUWU=";
              };
              nativeBuildInputs = [
                spkgs.buildPackages.cmake
                spkgs.buildPackages.pkg-config
              ];
              # GL headers come from glibc libglvnd (mesa would also work);
              # we don't link libGL — SDL2's X11 GL backend dlopens it at
              # runtime via SDL_GL_LoadLibrary. pkgsStatic.libglvnd fails to
              # build (its GLdispatch asm doesn't cross-compile to musl), but
              # the non-static libglvnd is header-only enough for SDL2's
              # <GL/gl.h> / <GL/glext.h> / <GL/glx.h> feature probes.
              buildInputs = [
                pkgs.libglvnd
              ];
              propagatedBuildInputs = with spkgs; [
                xorg.libX11
                xorg.libXcursor
                xorg.libXext
                xorg.libXfixes
                xorg.libXi
                xorg.libXinerama
                xorg.libXrandr
                xorg.libXScrnSaver
                xorg.libxcb
                alsa-lib
              ];
              cmakeFlags = [
                "-DSDL_SHARED=OFF"
                "-DSDL_STATIC=ON"
                "-DSDL_TEST=OFF"
                # SDL_OPENGL is needed so SDL_GL_CreateContext /
                # SDL_GL_GetProcAddress / SDL_GL_*Attribute are compiled in.
                # The actual libGL.so is dlopen'd at runtime by SDL2's X11 GL
                # backend (X11_GL_LoadLibrary -> GL_LoadObject), so we don't
                # link libGL here. The GL headers come from libglvnd above.
                "-DSDL_OPENGL=ON"
                "-DSDL_OPENGLES=OFF"
                "-DSDL_VULKAN=OFF"
                "-DSDL_X11=ON"
                "-DSDL_WAYLAND=OFF"
                "-DSDL_ALSA=ON"
                "-DSDL_PIPEWIRE=OFF"
                "-DSDL_PULSEAUDIO=OFF"
                "-DSDL_RPATH=OFF"
                "-DSDL_LIBC=ON"
                "-DSDL_PTHREADS=ON"
                "-DSDL_LOADSO=OFF"
              ];
              # Two trivial build fixes against modern nixpkgs headers.
              #   1. ALSA: snd_pcm_info_free returns void (per <alsa/pcm.h>)
              #      but SDL2's function pointer alias was declared int,
              #      causing a type mismatch.
              #   2. X11: SDL2 forward-declares XGenericEventCookie when
              #      SDL_VIDEO_DRIVER_X11_SUPPORTS_GENERIC_EVENTS is unset,
              #      but every libx11 since 1.6 (2012) already provides it in
              #      <X11/Xlib.h>; the duplicate typedef conflicts. The cmake
              #      probe for HAVE_XGENERICEVENT is brittle in static
              #      cross-builds, so just drop the fallback typedef — modern
              #      libx11 always supplies the type.
              postPatch = ''
                sed -i 's|^static int (\*ALSA_snd_pcm_info_free)(snd_pcm_info_t \*);$|static void (*ALSA_snd_pcm_info_free)(snd_pcm_info_t *);|' \
                  src/audio/alsa/SDL_alsa_audio.c
                sed -i '/#ifndef SDL_VIDEO_DRIVER_X11_SUPPORTS_GENERIC_EVENTS/,/^#endif$/d' \
                  src/video/x11/SDL_x11xinput2.h
              '';
              postInstall = ''
                rm -f $out/lib/*.la
                rm -f $out/lib/libSDL2main.a
                # SDL2's installed cmake config files (SDL2Config*.cmake,
                # SDL2Targets.cmake) hit a double-rooted path bug in their
                # INTERFACE_INCLUDE_DIRECTORIES under nixpkgs (the includes
                # end up as "<dev-path>/<dev-path>/include"). Our CMakeLists
                # discovers SDL2 via pkg-config (find_package(PkgConfig sdl2)),
                # so we don't need the cmake config at all — drop it before
                # fixupPhase moves it to $dev.
                rm -rf $out/lib/cmake
              '';
              # fixupPhase moves pkgconfig to $dev and then nixpkgs'
              # pkg-config wrapper tries to relativize the absolute paths that
              # GNUInstallDirs baked into sdl2.pc, but the relativization
              # produces double-rooted paths like
              #   libdir=$prefix//nix/store/.../lib
              #   includedir=/nix/store/...-dev//nix/store/...-dev/include
              # Rewriting the .pc file from scratch in postFixup (after the
              # move) avoids the issue and gives downstream pkg-config users
              # clean -L/-I flags. The heredoc is quoted ('EOF') so bash
              # leaves the pkg-config variable references alone, then we
              # substitute in the two absolute store paths we actually need.
              # The leading ''${...} escapes are nix's way of writing a
              # literal ${...} inside a '' multi-line string.
              postFixup = ''
                cat > $dev/lib/pkgconfig/sdl2.pc <<'EOF'
                prefix=@out@
                exec_prefix=''${prefix}
                libdir=''${prefix}/lib
                includedir=@dev@/include

                Name: sdl2
                Description: Simple DirectMedia Layer static library (VoiceTyper internal build)
                Version: @version@
                Requires.private: alsa
                Libs: -L''${libdir} -lSDL2 -pthread -lm
                Libs.private: -lasound -lxcb -lX11 -lXau -lXdmcp -lXcursor -lXext -lXfixes -lXi -lXinerama -lXrandr -lXss -lXrender
                Cflags: -I''${includedir} -I''${includedir}/SDL2 -D_REENTRANT
                EOF
                substituteInPlace $dev/lib/pkgconfig/sdl2.pc \
                  --replace-fail '@out@' "$out" \
                  --replace-fail '@dev@' "$dev" \
                  --replace-fail '@version@' "${finalAttrs.version}"
              '';
              # bin/sdl2-config hardcodes paths to the includes in dev, so
              # without outputBin=dev it lives in out and creates an out->dev
              # reference cycle. Put dev-only tools in dev (matches what
              # nixpkgs's sdl2-compat does).
              outputBin = "dev";
              outputs = [
                "out"
                "dev"
              ];
            });

          # ---- ccache toggle (shared by every package) ----------------------
          # Reading $VOICETYPER_CCACHE / $CCACHE_DIR at eval time requires
          # --impure; tools/nix-build.sh sets VOICETYPER_CCACHE=1 and opens
          # the sandbox hole only when --ccache is passed. Default dir is
          # chmod 1777 so both the repo owner and nixbld can populate it.
          # Override the location with $CCACHE_DIR.
          enableCcache = builtins.getEnv "VOICETYPER_CCACHE" == "1";
          ccacheDir =
            let
              d = builtins.getEnv "CCACHE_DIR";
            in
            if d != "" then d else "/var/cache/voicetyper-ccache";

          # CMAKE_<LANG>_COMPILER_LAUNCHER=ccache flags. nvcc is supported
          # by ccache (>=4.x) via the cuda launcher flag, so callers that
          # compile CUDA sources pass { cuda = true; }.
          ccacheCmakeFlags =
            {
              cuda ? false,
            }:
            pkgs.lib.optionals enableCcache (
              [
                "-DCMAKE_C_COMPILER_LAUNCHER=ccache"
                "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
              ]
              ++ pkgs.lib.optional cuda "-DCMAKE_CUDA_COMPILER_LAUNCHER=ccache"
            );

          # Env vars merged into each derivation via `// ccacheEnv`. They are
          # only set when ccache is on, so non-ccache builds are unchanged.
          ccacheEnv = pkgs.lib.optionalAttrs enableCcache {
            CCACHE_DIR = ccacheDir;
            CCACHE_NOHASHDIR = "true";
            CCACHE_COMPILERCHECK = "content";
          };

          # preConfigure fragment. Composed with `+` when a derivation already
          # has a preConfigure (e.g. cuda-static's LDFLAGS export).
          ccachePreConfigure = pkgs.lib.optionalString enableCcache ''
            export CCACHE_BASEDIR="$PWD"
          '';
        in
        {
          default = pkgs.stdenv.mkDerivation (
            {
              pname = "voicetyper";
              version = pkgs.lib.strings.removeSuffix "\n" (builtins.readFile ./VERSION);
              src = ./.;

              nativeBuildInputs =
                with pkgs;
                [
                  cmake
                  makeWrapper
                  pkg-config
                ]
                ++ pkgs.lib.optional enableCcache ccache;

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
              ]
              ++ ccacheCmakeFlags { };

              preConfigure = ccachePreConfigure;

              postInstall = ''
                wrapProgram $out/bin/VoiceTyper \
                  --prefix LD_LIBRARY_PATH : ${pkgs.lib.makeLibraryPath runtimeLibs} \
                  --run 'export VOICETYPER_DATA_DIR="''${VOICETYPER_DATA_DIR:-''${XDG_DATA_HOME:-$HOME/.local/share}/voicetyper}"'
              '';
            }
            // ccacheEnv
          );

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
            in
            cudaPackages.backendStdenv.mkDerivation (
              {
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
                  ++ pkgs.lib.optional enableCcache ccache;

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
                ++ ccacheCmakeFlags { cuda = true; };

                preConfigure = ccachePreConfigure;

                postInstall = ''
                  wrapProgram $out/bin/VoiceTyper \
                    --prefix LD_LIBRARY_PATH : ${unfreePkgs.lib.makeLibraryPath (runtimeLibs ++ cudaRuntimeLibs)} \
                    --run 'export VOICETYPER_DATA_DIR="''${VOICETYPER_DATA_DIR:-''${XDG_DATA_HOME:-$HOME/.local/share}/voicetyper}"'
                '';
              }
              // ccacheEnv
            );

          # Portable CPU build: a truly static binary (musl libc, static SDL2
          # and X11). No dynamic linker, no .so dependencies — runs on any
          # x86_64 Linux including NixOS, where /lib64/ld-linux-x86-64.so.2
          # does not exist. GL is loaded via dlopen() at runtime. Audio is
          # ALSA-only (staticSDL2 disables PipeWire / PulseAudio); ALSA is
          # driven via the kernel API, with PipeWire/PulseAudio exposing
          # ALSA compat shims on systems that use them.
          static =
            let
              spkgs = pkgs.pkgsStatic;
            in
            spkgs.stdenv.mkDerivation (
              {
                pname = "voicetyper-static";
                version = pkgs.lib.strings.removeSuffix "\n" (builtins.readFile ./VERSION);
                src = ./.;

                nativeBuildInputs = [
                  spkgs.buildPackages.cmake
                  spkgs.buildPackages.pkg-config
                ]
                ++ pkgs.lib.optional enableCcache pkgs.ccache;

                buildInputs = [
                  staticSDL2
                ];

                cmakeBuildType = "Release";
                cmakeFlags = [
                  "-DVOICETYPER_CUDA=OFF"
                  "-DVOICETYPER_APP_IPO=OFF"
                  "-DVOICETYPER_STATIC=ON"
                  "-DCMAKE_DISABLE_FIND_PACKAGE_OpenMP=TRUE"
                ]
                ++ ccacheCmakeFlags { };

                preConfigure = ccachePreConfigure;

                # pkgsStatic already wires -static into LDFLAGS for the whole
                # stdenv, so the resulting binary has no PT_INTERP and no
                # DT_NEEDED entries (other than what ggml/whisper pull in,
                # which is also built static here). Nothing to patch in
                # postInstall.
              }
              // ccacheEnv
            );

          # Portable CUDA build. CUDA itself can't be statically linked
          # (nvcc requires glibc and the CUDA runtime ships only as .so),
          # so unlike `static` this is NOT a truly-static binary. Instead we
          # bundle ld-linux-x86-64.so.2 plus the full transitive .so closure
          # alongside the binary and ship a wrapper script that invokes it
          # through the bundled ld-linux directly. This works on NixOS —
          # where the kernel cannot find /lib64/ld-linux-x86-64.so.2 —
          # because the wrapper bypasses the kernel's PT_INTERP lookup
          # entirely and exec's the bundled ld-linux as a regular program.
          # libcuda.so (the driver lib) is provided by the host NVIDIA
          # driver and is NOT bundled.
          cuda-static =
            let
              unfreePkgs = import nixpkgs {
                inherit system;
                config.allowUnfree = true;
              };
              cudaPackages = unfreePkgs.cudaPackages_13_0;
            in
            cudaPackages.backendStdenv.mkDerivation (
              {
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
                  ++ pkgs.lib.optional enableCcache ccache;

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
                ++ ccacheCmakeFlags { cuda = true; };

                preConfigure = ''
                  export LDFLAGS="-static-libgcc -static-libstdc++"
                ''
                + ccachePreConfigure;

                postInstall = ''
                                  mkdir -p $out/libexec $out/lib

                                  # Move the real binaries out of bin/ — bin/ becomes wrapper
                                  # scripts so the user-facing entry point transparently uses
                                  # the bundled dynamic linker.
                                  mv $out/bin/VoiceTyper $out/libexec/VoiceTyper
                                  mv $out/bin/VoiceTyperBench $out/libexec/VoiceTyperBench

                                  # ldd against the freshly-built binary resolves the full
                                  # transitive .so closure via the binary's nix-store rpath.
                                  # Copy every dep into $out/lib/ so the extracted tarball is
                                  # self-contained. Skip files that already landed in $out/lib/
                                  # on a previous iteration (ldd would then resolve them via
                                  # rpath to $out/lib/... and cp would fail "same file").
                                  # libcuda.so.1 is excluded: the CUDA toolkit ships a small
                                  # stub that we must NOT bundle, because the host's NVIDIA
                                  # driver supplies the real libcuda.so.1 at runtime (bundling
                                  # the stub would shadow it and break CUDA initialization).
                                  for bin in $out/libexec/VoiceTyper $out/libexec/VoiceTyperBench; do
                                    ldd "$bin" 2>/dev/null \
                                      | awk '/=> \// {print $3}' \
                                      | sort -u \
                                      | while read -r lib; do
                                          base="$(basename "$lib")"
                                          if [ -n "$base" ] && [ -f "$lib" ] && [ ! -e "$out/lib/$base" ]; then
                                            case "$base" in
                                              libcuda.so*) ;;
                                              *) cp -Lf "$lib" $out/lib/ ;;
                                            esac
                                          fi
                                        done
                                  done

                                  # Also copy the CUDA runtime libs (the binary dlopens these
                                  # from its rpath at runtime rather than listing them as
                                  # DT_NEEDED, so ldd won't pick them up). ldd may already have
                                  # pulled some of these in from the binary's rpath, so make the
                                  # existing copies writable before overwriting.
                                  chmod +w $out/lib/libcudart.so.* $out/lib/libcublas*.so.* 2>/dev/null || true
                                  cp -Lf ${cudaPackages.cuda_cudart}/lib/libcudart.so.* $out/lib/ 2>/dev/null || true
                                  cp -Lf ${cudaPackages.libcublas.lib}/lib/libcublas.so.* $out/lib/
                                  cp -Lf ${cudaPackages.libcublas.lib}/lib/libcublasLt.so.* $out/lib/

                                  # Bundle the glibc dynamic linker. On NixOS the kernel
                                  # cannot find /lib64/ld-linux-x86-64.so.2, so the wrapper
                                  # script invokes this copy directly. ldd already copied it
                                  # from the interpreter line, but it lands read-only — make
                                  # it writable so the wrapper can later replace it if needed
                                  # and so patchelf can rewrite it.
                                  chmod +w $out/lib/ld-linux-x86-64.so.2 2>/dev/null || true
                                  cp -Lf ${cudaPackages.backendStdenv.cc.bintools.dynamicLinker} \
                                    $out/lib/ld-linux-x86-64.so.2

                                  # Rewrite the binaries to look in the bundled lib dir first,
                                  # then fall back to standard NVIDIA driver locations so
                                  # libcuda.so.1 (which we deliberately do NOT bundle) is found
                                  # at runtime. The interpreter path doesn't actually matter —
                                  # the wrapper never lets the kernel consult PT_INTERP — but
                                  # we set it to the FHS path so the binary still works on
                                  # non-NixOS systems if someone runs it directly.
                                  #
                                  # rpath entries (in search order):
                                  #   $ORIGIN/../lib              bundled .so closure
                                  #   /run/opengl-driver/lib      NixOS NVIDIA driver location
                                  #   /usr/lib/x86_64-linux-gnu   Debian/Ubuntu FHS libcuda.so.1
                                  #   /lib/x86_64-linux-gnu       Debian/Ubuntu alt
                                  #   /usr/lib64                  RPM-based FHS libcuda.so.1
                                  #   /lib64                      RPM-based alt
                                  #   /usr/local/cuda/lib64       manually-installed CUDA toolkit
                                  for bin in $out/libexec/VoiceTyper $out/libexec/VoiceTyperBench; do
                                    patchelf --set-interpreter /lib64/ld-linux-x86-64.so.2 \
                                             --set-rpath '$ORIGIN/../lib:/run/opengl-driver/lib:/usr/lib/x86_64-linux-gnu:/lib/x86_64-linux-gnu:/usr/lib64:/lib64:/usr/local/cuda/lib64' \
                                             "$bin"
                                  done

                                  # Wrapper scripts that exec the bundled ld-linux directly,
                                  # bypassing kernel PT_INTERP lookup (which fails on NixOS).
                                  # We deliberately do NOT pass --library-path to ld-linux here
                                  # because that would shadow the rpath-based libcuda.so.1
                                  # resolution: ld.so treats --library-path as a replacement
                                  # for LD_LIBRARY_PATH and skips system search paths in some
                                  # glibc versions, which would hide the host's libcuda.so.1.
                                  # Using rpath on the binary instead keeps the system search
                                  # path available as a fallback.
                                  # Uses /bin/sh — present on every Linux including NixOS — so
                                  # the wrapper runs the same everywhere.
                                  for bin in VoiceTyper VoiceTyperBench; do
                                    cat > $out/bin/$bin <<'WRAPPER'
                  #!/bin/sh
                  DIR="$(cd "$(dirname "$0")/.." && pwd)"
                  exec "$DIR/lib/ld-linux-x86-64.so.2" "$DIR/libexec/BIN_PLACEHOLDER" "$@"
                  WRAPPER
                                    sed -i "s|BIN_PLACEHOLDER|$bin|" $out/bin/$bin
                                    chmod +x $out/bin/$bin
                                  done
                '';

                # nixpkgs fixupPhase rewrites the wrapper shebang to point at
                # the bash in /nix/store, which defeats the whole point of the
                # wrapper on non-NixOS systems. Force it back to /bin/sh.
                dontPatchShebangs = true;
              }
              // ccacheEnv
            );

          appimage = pkgs.stdenv.mkDerivation (
            {
              pname = "VoiceTyper";
              version = pkgs.lib.strings.removeSuffix "\n" (builtins.readFile ./VERSION);
              src = ./.;

              nativeBuildInputs =
                with pkgs;
                [
                  cmake
                  patchelf
                  pkg-config
                  squashfsTools
                ]
                ++ pkgs.lib.optional enableCcache ccache;

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
              ]
              ++ ccacheCmakeFlags { };

              preConfigure = ''
                export LDFLAGS="-static-libgcc -static-libstdc++"
              ''
              + ccachePreConfigure;

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
            }
            // ccacheEnv
          );

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
            in
            cudaPackages.backendStdenv.mkDerivation (
              {
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
                  ++ pkgs.lib.optional enableCcache ccache;

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
                ++ ccacheCmakeFlags { cuda = true; };

                preConfigure = ''
                  export LDFLAGS="-static-libgcc -static-libstdc++"
                ''
                + ccachePreConfigure;

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
              }
              // ccacheEnv
            );
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
