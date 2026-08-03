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

          # X11/ALSA client libs that the portable bundles ship next to the
          # bundled libSDL2.so. The dynamic SDL2 build loads them via dlopen
          # (SDL_X11_SHARED / SDL_ALSA_SHARED) rather than DT_NEEDED, so they
          # don't show up in ldd's closure of libSDL2 and must be copied
          # explicitly.
          xorgLibs = with pkgs; [
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

          # nixpkgs's SDL2 attribute is sdl2-compat, which dlopens SDL3 at
          # runtime and therefore cannot be bundled in a self-contained way.
          # For the portable packages we instead build the real SDL2
          # (libsdl-org/SDL 2.32.x) from upstream source. buildSDL2
          # parameterizes the two variants used across the flake:
          #   static = true  -> pkgsStatic (musl), SDL_STATIC=ON, for the
          #                     truly-static `static` package.
          #   static = false -> glibc, SDL_SHARED=ON, for the portable bundles
          #                     (cuda-static, appimage, cuda-appimage) that
          #                     ship the libSDL2 .so closure next to the app.
          # Only ALSA is enabled as the audio backend (see cmakeFlags below —
          # PipeWire and PulseAudio are OFF); ALSA is the lowest common
          # denominator and PipeWire/PulseAudio expose an ALSA compat shim, so
          # audio still works on those systems. ALSA only needs the kernel API
          # at runtime, so the resulting static binary still runs on any Linux
          # (including NixOS) without system .so deps.
          buildSDL2 =
            { static }:
            let
              spkgs = if static then pkgs.pkgsStatic else pkgs;
              xorgLibs = with spkgs; [
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
            in
            spkgs.stdenv.mkDerivation (finalAttrs: {
              pname = if static then "SDL2-static" else "SDL2-dynamic";
              version = "2.32.10";
              src = pkgs.fetchurl {
                url = "https://github.com/libsdl-org/SDL/releases/download/release-${finalAttrs.version}/SDL2-${finalAttrs.version}.tar.gz";
                hash = "sha256-X1mTxTDwhFNcZaaHnpsmrUQRabPiXXidgyhwQKnKUWU=";
              };
              nativeBuildInputs = [
                spkgs.buildPackages.cmake
                spkgs.buildPackages.pkg-config
              ];
              buildInputs = pkgs.lib.optionals (!static) xorgLibs;
              # The static build pulls the X11/ALSA client libs in at link
              # time via sdl2.pc's Libs.private, so they must be propagated to
              # downstream consumers. The shared build records them as
              # DT_NEEDED of libSDL2.so instead. Both variants still propagate
              # xorgLibs so downstream apps can find the X11 headers that
              # SDL_syswm.h includes.
              propagatedBuildInputs = xorgLibs;
              cmakeFlags = [
                (if static then "-DSDL_SHARED=OFF" else "-DSDL_SHARED=ON")
                (if static then "-DSDL_STATIC=ON" else "-DSDL_STATIC=OFF")
                "-DSDL_TEST=OFF"
                # The app renders via SDL's software renderer
                # (imgui_impl_sdlrenderer2) and never creates an OpenGL
                # window, so GLX/EGL support is compiled out entirely. This
                # removes the runtime libGL.so dlopen that a truly-static
                # musl binary cannot perform.
                "-DSDL_OPENGL=OFF"
                "-DSDL_OPENGLES=OFF"
                "-DSDL_VULKAN=OFF"
                "-DSDL_X11=ON"
                "-DSDL_WAYLAND=OFF"
                "-DSDL_ALSA=ON"
                "-DSDL_PIPEWIRE=OFF"
                "-DSDL_PULSEAUDIO=OFF"
                # SDL_RPATH=ON gives libSDL2.so an install rpath pointing at
                # its X11/ALSA deps in the nix store so the shared lib works
                # directly out of the nix build; the portable bundles
                # re-patchelf the bundled copy to $ORIGIN in their postFixup.
                (if static then "-DSDL_RPATH=OFF" else "-DSDL_RPATH=ON")
                "-DSDL_LIBC=ON"
                "-DSDL_PTHREADS=ON"
                # The static build links everything at link time and cannot
                # dlopen (musl has no dlopen); the shared build needs
                # SDL_LoadObject for its runtime X11/ALSA client library
                # lookups (SDL_X11_SHARED / SDL_ALSA_SHARED).
                (if static then "-DSDL_LOADSO=OFF" else "-DSDL_LOADSO=ON")
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
                Description: Simple DirectMedia Layer @kind@ library (VoiceTyper internal build)
                Version: @version@
                Requires.private: alsa
                Libs: -L''${libdir} -lSDL2 -pthread -lm
                @libs_private_line@
                Cflags: -I''${includedir} -I''${includedir}/SDL2 -D_REENTRANT
                EOF
                substituteInPlace $dev/lib/pkgconfig/sdl2.pc \
                  --replace-fail '@out@' "$out" \
                  --replace-fail '@dev@' "$dev" \
                  --replace-fail '@version@' "${finalAttrs.version}"
                ${
                  if static then
                    ''
                      # Static build: pull in the X11/ALSA client libs from
                      # Libs.private (pkg-config adds them automatically because
                      # the resolved library is an archive).
                      substituteInPlace $dev/lib/pkgconfig/sdl2.pc \
                        --replace-fail '@kind@' 'static' \
                        --replace-fail '@libs_private_line@' 'Libs.private: -lasound -lxcb -lX11 -lXau -lXdmcp -lXcursor -lXext -lXfixes -lXi -lXinerama -lXrandr -lXss -lXrender'
                    ''
                  else
                    ''
                      # Shared build: the X11/ALSA libs are DT_NEEDED of
                      # libSDL2.so, so no Libs.private is needed.
                      sed -i '/^@libs_private_line@$/d' $dev/lib/pkgconfig/sdl2.pc
                      substituteInPlace $dev/lib/pkgconfig/sdl2.pc \
                        --replace-fail '@kind@' 'shared'
                    ''
                }
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

          # musl static SDL2 for the `static` package.
          staticSDL2 = buildSDL2 { static = true; };

          # glibc shared SDL2 for the portable bundles (cuda-static, appimage,
          # cuda-appimage). The app links libSDL2.so; the bundles ship the
          # .so and its X11/ALSA closure next to the binary.
          dynamicSDL2 = buildSDL2 { static = false; };

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
                # Clean up build artifacts from ggml/whisper cmake install targets
                rm -rf $out/lib $out/include
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
                  # Clean up build artifacts from ggml/whisper cmake install targets
                  rm -rf $out/lib $out/include
                '';
              }
              // ccacheEnv
            );

          # Portable CPU build: a truly static binary (musl libc, static SDL2
          # and X11). No dynamic linker, no .so dependencies — runs on any
          # x86_64 Linux including NixOS, where /lib64/ld-linux-x86-64.so.2
          # does not exist. The UI renders via SDL's software renderer (no
          # OpenGL / no dlopen needed). Audio is ALSA-only (staticSDL2
          # disables PipeWire / PulseAudio); ALSA is driven via the kernel
          # API, with PipeWire/PulseAudio exposing ALSA compat shims on
          # systems that use them.
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
                  "-DCMAKE_DISABLE_FIND_PACKAGE_OpenMP=TRUE"
                ]
                ++ ccacheCmakeFlags { };

                preConfigure = ccachePreConfigure;

                # pkgsStatic already wires -static into LDFLAGS for the whole
                # stdenv, so the resulting binary has no PT_INTERP and no
                # DT_NEEDED entries. Clean up build artifacts left by the
                # ggml/whisper cmake install targets.
                postInstall = ''
                  rm -rf $out/lib $out/include
                '';
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

                buildInputs = [
                  dynamicSDL2
                  cudaPackages.cuda_cudart
                  cudaPackages.libcublas
                ];

                cmakeBuildType = "Release";
                cmakeFlags = [
                  "-DVOICETYPER_CUDA=ON"
                  "-DVOICETYPER_APP_IPO=OFF"
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

                                  # The bundled libSDL2.so dlopens the X11/ALSA
                                  # client libs at runtime (SDL_X11_SHARED /
                                  # SDL_ALSA_SHARED) instead of listing them as
                                  # DT_NEEDED, so ldd of the binary never sees
                                  # them. Copy the full X11/ALSA .so set
                                  # explicitly, then pull the transitive closure
                                  # of everything in the bundle (libxcb ->
                                  # libXau/libXdmcp, libXcursor -> libXrender,
                                  # ...) via ldd.
                                  for libdir in ${
                                    pkgs.lib.replaceStrings [ ":" ] [ " " ] (pkgs.lib.makeLibraryPath xorgLibs)
                                  }; do
                                    for so in "$libdir"/lib*.so*; do
                                      [ -f "$so" ] || continue
                                      base="$(basename "$so")"
                                      case "$base" in
                                        ld-linux*|libc.so.*|libm.so.*|libpthread.so.*|libdl.so.*|librt.so.*|libgcc_s.so.*) ;;
                                        *) [ -e "$out/lib/$base" ] || cp -Lf "$so" $out/lib/ 2>/dev/null || true ;;
                                      esac
                                    done
                                  done
                                  for so in $out/lib/*.so*; do
                                    for dep in $(ldd "$so" 2>/dev/null | awk '/=> \// {print $3}' | sort -u); do
                                      base="$(basename "$dep")"
                                      case "$base" in
                                        ld-linux*|libc.so.*|libm.so.*|libpthread.so.*|libdl.so.*|librt.so.*|libgcc_s.so.*) ;;
                                        *) [ -e "$out/lib/$base" ] || cp -Lf "$dep" $out/lib/ 2>/dev/null || true ;;
                                      esac
                                    done
                                  done

                                  # cmake install targets for the ggml/whisper
                                  # subprojects place static archives, cmake
                                  # config files, pkg-config files, and header
                                  # files into $out/. These are build artifacts,
                                  # not runtime deps — strip them so the
                                  # portable bundle contains only the .so
                                  # closure needed at runtime.
                                  rm -f $out/lib/*.a
                                  rm -rf $out/lib/cmake
                                  rm -rf $out/lib/pkgconfig
                                  rm -rf $out/include

                                  # Bundle the glibc dynamic linker. On NixOS the kernel
                                  # cannot find /lib64/ld-linux-x86-64.so.2, so the wrapper
                                  # script invokes this copy directly. ldd already copied it
                                  # from the interpreter line, but it lands read-only — make
                                  # it writable so the wrapper can later replace it if needed
                                  # and so patchelf can rewrite it.
                                  chmod +w $out/lib/ld-linux-x86-64.so.2 2>/dev/null || true
                                  cp -Lf ${cudaPackages.backendStdenv.cc.bintools.dynamicLinker} \
                                    $out/lib/ld-linux-x86-64.so.2

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
                  # Point the bundled glibc's lazy dlopens (libgcc_s for pthread
                  # cancellation, NSS modules) at the bundled lib dir first: the
                  # bundled ld-linux otherwise searches a NixOS-only store path
                  # for libgcc_s that doesn't exist on other distros. libcuda.so.1
                  # is not in the bundle, so the search falls through to the
                  # rpath'd NVIDIA driver locations.
                  export LD_LIBRARY_PATH="$DIR/lib''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
                  exec "$DIR/lib/ld-linux-x86-64.so.2" "$DIR/libexec/BIN_PLACEHOLDER" "$@"
                  WRAPPER
                                    sed -i "s|BIN_PLACEHOLDER|$bin|" $out/bin/$bin
                                    chmod +x $out/bin/$bin
                                  done
                '';

                # Re-apply the rpaths in postFixup (after nixpkgs's fixupPhase
                # runs patchelf --shrink-rpath, which trims absolute fallback
                # paths that don't exist on the build machine — notably the
                # NVIDIA driver locations — down to just $ORIGIN/../lib).
                postFixup = ''
                  chmod +w $out/libexec/VoiceTyper $out/libexec/VoiceTyperBench

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

                  # Point every bundled non-glibc .so at its own directory so
                  # the bundled X11/ALSA/SDL2 libs find each other (and their
                  # own deps) on machines without those libs installed. glibc
                  # core files are skipped — patchelf must not touch ld-linux.
                  for so in $out/lib/*.so*; do
                    base="$(basename "$so")"
                    case "$base" in
                      ld-linux*|libc.so.*|libm.so.*|libpthread.so.*|libdl.so.*|librt.so.*|libgcc_s.so.*) ;;
                      *) chmod +w "$so"; patchelf --set-rpath '$ORIGIN' "$so" ;;
                    esac
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

              buildInputs = [
                dynamicSDL2
              ];

              cmakeBuildType = "Release";
              cmakeFlags = [
                "-DVOICETYPER_CUDA=OFF"
                "-DVOICETYPER_APP_IPO=OFF"
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
                chmod +w "$Binary"
                patchelf --set-interpreter /lib64/ld-linux-x86-64.so.2 --remove-rpath "$Binary"

                AppDir="$PWD/AppDir"
                mkdir -p "$AppDir/usr/bin" "$AppDir/usr/lib"

                cp -f "$Binary" "$AppDir/usr/bin/VoiceTyper"
                chmod +x "$AppDir/usr/bin/VoiceTyper"

                SdlLib=$(find ${dynamicSDL2}/lib -name 'libSDL2-2.0.so.0' | head -1)
                if [ -n "$SdlLib" ]; then
                  cp -L "$SdlLib" "$AppDir/usr/lib/"
                  # Copy the X11/ALSA closure that libSDL2 needs (ldd resolves
                  # the full transitive DT_NEEDED closure). glibc core libs are
                  # copied separately below — they must be bundled so the app
                  # runs on any host glibc.
                  for lib in $(ldd "$SdlLib" 2>/dev/null | awk '/=> \// {print $3}' | sort -u); do
                    base="$(basename "$lib")"
                    case "$base" in
                      ld-linux*|libc.so.*|libm.so.*|libpthread.so.*|libdl.so.*|librt.so.*|libgcc_s.so.*) ;;
                      *) cp -Lf "$lib" "$AppDir/usr/lib/" ;;
                    esac
                  done
                  # libSDL2.so dlopens the X11/ALSA client libs at runtime
                  # (SDL_X11_SHARED / SDL_ALSA_SHARED) instead of listing them
                  # as DT_NEEDED, so ldd "$SdlLib" above can't see them. Copy
                  # the full X11/ALSA .so set explicitly, then pull the
                  # transitive closure of every bundled lib (libxcb ->
                  # libXau/libXdmcp, libXcursor -> libXrender, ...) via ldd.
                  for libdir in ${pkgs.lib.replaceStrings [ ":" ] [ " " ] (pkgs.lib.makeLibraryPath xorgLibs)}; do
                    for so in "$libdir"/lib*.so*; do
                      [ -f "$so" ] || continue
                      base="$(basename "$so")"
                      case "$base" in
                        ld-linux*|libc.so.*|libm.so.*|libpthread.so.*|libdl.so.*|librt.so.*|libgcc_s.so.*) ;;
                        *) [ -e "$AppDir/usr/lib/$base" ] || cp -Lf "$so" "$AppDir/usr/lib/" 2>/dev/null || true ;;
                      esac
                    done
                  done
                  for so in "$AppDir"/usr/lib/*.so*; do
                    for dep in $(ldd "$so" 2>/dev/null | awk '/=> \// {print $3}' | sort -u); do
                      base="$(basename "$dep")"
                      case "$base" in
                        ld-linux*|libc.so.*|libm.so.*|libpthread.so.*|libdl.so.*|librt.so.*|libgcc_s.so.*) ;;
                        *) [ -e "$AppDir/usr/lib/$base" ] || cp -Lf "$dep" "$AppDir/usr/lib/" 2>/dev/null || true ;;
                      esac
                    done
                  done
                  # Bundle glibc + the loader so the AppImage runs on any host
                  # glibc (building against the NixOS toolchain's glibc 2.42
                  # would otherwise require host glibc >= 2.38, excluding Debian
                  # 12 / Ubuntu 22.04 / RHEL 9 etc.). AppRun execs the bundled
                  # loader directly; the bundled libs resolve their deps via
                  # $ORIGIN and the binary's rpath. libgcc_s is bundled too —
                  # glibc dlopens it lazily for pthread cancellation.
                  GLIBC_BASE=${pkgs.glibc}/lib
                  for so in ld-linux-x86-64.so.2 libc.so.6 libm.so.6 libpthread.so.0 libdl.so.2 librt.so.1; do
                    cp -Lf "$GLIBC_BASE/$so" "$AppDir/usr/lib/"
                  done
                  cp -Lf ${pkgs.stdenv.cc.cc.lib}/lib/libgcc_s.so.1 "$AppDir/usr/lib/"
                  # Point libSDL2 and every bundled dep at their own dir so the
                  # bundle is self-contained (each lib resolves its own deps
                  # via $ORIGIN). The bundled glibc core is left untouched —
                  # patchelf'ing the loader is fragile and the core libs resolve
                  # through the binary's rpath / LD_LIBRARY_PATH instead.
                  for so in "$AppDir"/usr/lib/*.so*; do
                    [ -f "$so" ] || continue
                    case "$(basename "$so")" in
                      ld-linux*|libc.so.*|libm.so.*|libpthread.so.*|libdl.so.*|librt.so.*|libgcc_s.so.*) continue ;;
                    esac
                    chmod +w "$so"
                    patchelf --set-rpath '$ORIGIN' "$so"
                  done
                  chmod +w "$AppDir/usr/bin/VoiceTyper"
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
                  '#!/bin/sh' \
                  'dir="$(dirname "$(readlink -f "$0")")"' \
                  'export VOICETYPER_DATA_DIR="''${VOICETYPER_DATA_DIR:-''${XDG_DATA_HOME:-$HOME/.local/share}/voicetyper}"' \
                  'export LD_LIBRARY_PATH="$dir/usr/lib''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"' \
                  'exec "$dir/usr/lib/ld-linux-x86-64.so.2" "$dir/usr/bin/VoiceTyper" "$@"' \
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

                buildInputs = [
                  dynamicSDL2
                  cudaPackages.cuda_cudart
                  cudaPackages.libcublas
                ];

                cmakeBuildType = "Release";
                cmakeFlags = [
                  "-DVOICETYPER_CUDA=ON"
                  "-DVOICETYPER_APP_IPO=OFF"
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
                  chmod +w "$Binary"
                  patchelf --set-interpreter /lib64/ld-linux-x86-64.so.2 --remove-rpath "$Binary"

                  AppDir="$PWD/AppDir"
                  mkdir -p "$AppDir/usr/bin" "$AppDir/usr/lib"

                  cp -f "$Binary" "$AppDir/usr/bin/VoiceTyper"
                  chmod +x "$AppDir/usr/bin/VoiceTyper"

                  SdlLib=$(find ${dynamicSDL2}/lib -name 'libSDL2-2.0.so.0' | head -1)
                  if [ -n "$SdlLib" ]; then
                    cp -L "$SdlLib" "$AppDir/usr/lib/"
                    # Copy the X11/ALSA closure that libSDL2 needs (ldd
                    # resolves the full transitive DT_NEEDED closure). glibc
                    # core libs are copied separately below — they must be
                    # bundled so the app runs on any host glibc.
                    for lib in $(ldd "$SdlLib" 2>/dev/null | awk '/=> \// {print $3}' | sort -u); do
                      base="$(basename "$lib")"
                      case "$base" in
                        ld-linux*|libc.so.*|libm.so.*|libpthread.so.*|libdl.so.*|librt.so.*|libgcc_s.so.*) ;;
                        *) cp -Lf "$lib" "$AppDir/usr/lib/" ;;
                      esac
                    done
                  fi

                  # libSDL2.so dlopens the X11/ALSA client libs at runtime
                  # (SDL_X11_SHARED / SDL_ALSA_SHARED) instead of listing them
                  # as DT_NEEDED, so ldd "$SdlLib" above can't see them. Copy
                  # the full X11/ALSA .so set explicitly.
                  for libdir in ${pkgs.lib.replaceStrings [ ":" ] [ " " ] (pkgs.lib.makeLibraryPath xorgLibs)}; do
                    for so in "$libdir"/lib*.so*; do
                      [ -f "$so" ] || continue
                      base="$(basename "$so")"
                      case "$base" in
                        ld-linux*|libc.so.*|libm.so.*|libpthread.so.*|libdl.so.*|librt.so.*|libgcc_s.so.*) ;;
                        *) [ -e "$AppDir/usr/lib/$base" ] || cp -Lf "$so" "$AppDir/usr/lib/" 2>/dev/null || true ;;
                      esac
                    done
                  done

                  cp -L ${cudaPackages.cuda_cudart}/lib/libcudart.so.* "$AppDir/usr/lib/" 2>/dev/null || true
                  cp -L ${cudaPackages.libcublas.lib}/lib/libcublas.so.* "$AppDir/usr/lib/"
                  cp -L ${cudaPackages.libcublas.lib}/lib/libcublasLt.so.* "$AppDir/usr/lib/"

                  # Pull the transitive closure of every bundled lib (libxcb
                  # -> libXau/libXdmcp, libXcursor -> libXrender, ...) via ldd.
                  for so in "$AppDir"/usr/lib/*.so*; do
                    for dep in $(ldd "$so" 2>/dev/null | awk '/=> \// {print $3}' | sort -u); do
                      base="$(basename "$dep")"
                      case "$base" in
                        ld-linux*|libc.so.*|libm.so.*|libpthread.so.*|libdl.so.*|librt.so.*|libgcc_s.so.*) ;;
                        *) [ -e "$AppDir/usr/lib/$base" ] || cp -Lf "$dep" "$AppDir/usr/lib/" 2>/dev/null || true ;;
                      esac
                    done
                  done

                  # Bundle glibc + the loader so the AppImage runs on any host
                  # glibc (building against the NixOS toolchain's glibc 2.42
                  # would otherwise require host glibc >= 2.38, excluding Debian
                  # 12 / Ubuntu 22.04 / RHEL 9 etc.). AppRun execs the bundled
                  # loader directly; the bundled libs resolve their deps via
                  # $ORIGIN and the binary's rpath. libgcc_s is bundled too —
                  # glibc dlopens it lazily for pthread cancellation.
                  GLIBC_BASE=${pkgs.glibc}/lib
                  for so in ld-linux-x86-64.so.2 libc.so.6 libm.so.6 libpthread.so.0 libdl.so.2 librt.so.1; do
                    cp -Lf "$GLIBC_BASE/$so" "$AppDir/usr/lib/"
                  done
                  cp -Lf ${pkgs.stdenv.cc.cc.lib}/lib/libgcc_s.so.1 "$AppDir/usr/lib/"

                  # Point every bundled lib at its own directory so the bundle
                  # is self-contained (each lib resolves its own deps via
                  # $ORIGIN), then point the app at the lib dir. The bundled
                  # glibc core is left untouched — patchelf'ing the loader is
                  # fragile and the core libs resolve through the binary's
                  # rpath / LD_LIBRARY_PATH instead. The app also falls back to
                  # standard NVIDIA driver locations (which we deliberately do
                  # NOT bundle) so libcuda.so.1 is found at runtime.
                  for so in "$AppDir"/usr/lib/*.so*; do
                    [ -f "$so" ] || continue
                    case "$(basename "$so")" in
                      ld-linux*|libc.so.*|libm.so.*|libpthread.so.*|libdl.so.*|librt.so.*|libgcc_s.so.*) continue ;;
                    esac
                    chmod +w "$so"
                    patchelf --set-rpath '$ORIGIN' "$so"
                  done
                  chmod +w "$AppDir/usr/bin/VoiceTyper"
                  patchelf --set-rpath '$ORIGIN/../lib:/run/opengl-driver/lib:/usr/lib/x86_64-linux-gnu:/lib/x86_64-linux-gnu:/usr/lib64:/lib64:/usr/local/cuda/lib64' "$AppDir/usr/bin/VoiceTyper"

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
                    '#!/bin/sh' \
                    'dir="$(dirname "$(readlink -f "$0")")"' \
                    'export VOICETYPER_DATA_DIR="''${VOICETYPER_DATA_DIR:-''${XDG_DATA_HOME:-$HOME/.local/share}/voicetyper}"' \
                    'export LD_LIBRARY_PATH="$dir/usr/lib''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"' \
                    'exec "$dir/usr/lib/ld-linux-x86-64.so.2" "$dir/usr/bin/VoiceTyper" "$@"' \
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
