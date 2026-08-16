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
            libx11
            libxcursor
            libxext
            libxfixes
            libxi
            libxinerama
            libxrandr
          ];

          # X11/ALSA client libs that the portable x11 bundles ship next to the
          # bundled libSDL2.so. The dynamic SDL2 build loads them via dlopen
          # (SDL_X11_SHARED / SDL_ALSA_SHARED) rather than DT_NEEDED, so they
          # don't show up in ldd's closure of libSDL2 and must be copied
          # explicitly.
          xorgLibs = with pkgs; [
            libx11
            libxcursor
            libxext
            libxfixes
            libxi
            libxinerama
            libxrandr
            libxscrnsaver
            libxcb
            alsa-lib
          ];

          # Wayland/ALSA client libs for the portable wayland bundles. Same
          # dlopen story as xorgLibs (SDL_WAYLAND_SHARED / SDL_ALSA_SHARED):
          # libwayland-client/cursor/egl and libxkbcommon never appear in
          # ldd's closure of libSDL2 and must be copied explicitly.
          waylandLibs = with pkgs; [
            wayland
            libxkbcommon
            alsa-lib
          ];

          # nixpkgs's SDL2 attribute is sdl2-compat, which dlopens SDL3 at
          # runtime and therefore cannot be bundled in a self-contained way.
          # For the portable packages we instead build the real SDL2
          # (libsdl-org/SDL 2.32.x) from upstream source. buildSDL2
          # parameterizes the variants used across the flake:
          #   static = true   -> pkgsStatic (musl), SDL_STATIC=ON, X11-only
          #                      (currently unused by any package).
          #   static = false  -> glibc, SDL_SHARED=ON, for the portable
          #                      bundles that ship the libSDL2 .so closure
          #                      next to the app.
          #   wayland = false -> X11 video driver only (SDL_X11=ON).
          #   wayland = true  -> Wayland video driver only (SDL_WAYLAND=ON,
          #                      SDL_X11=OFF); used by the *-wayland bundles.
          # Only ALSA is enabled as the audio backend (see cmakeFlags below —
          # PipeWire and PulseAudio are OFF); ALSA is the lowest common
          # denominator and PipeWire/PulseAudio expose an ALSA compat shim, so
          # audio still works on those systems. ALSA only needs the kernel API
          # at runtime, so the resulting binary still runs on any Linux
          # (including NixOS) without system .so deps.
          buildSDL2 =
            {
              static ? false,
              wayland ? false,
            }:
            let
              spkgs = if static then pkgs.pkgsStatic else pkgs;
              xorgLibs = with spkgs; [
                libx11
                libxcursor
                libxext
                libxfixes
                libxi
                libxinerama
                libxrandr
                libxscrnsaver
                libxcb
                alsa-lib
              ];
              # The Wayland client libs + xkbcommon, which SDL's wayland video
              # driver requires at build time. wayland-egl / wayland-cursor /
              # wayland-scanner all ship inside the `wayland` derivation
              # (SDL's cmake finds the scanner through the wayland-scanner.pc
              # pkg-config variable); SDL2 2.32's wayland driver also hard
              # requires the `egl` pkg-config module, provided by glvnd
              # (libGL.dev), even though the app never uses GL. Explicit
              # spkgs.* paths: a `with spkgs;` here would leave the `wayland`
              # parameter shadowing the wayland package.
              waylandLibs = [
                spkgs.wayland
                spkgs.libxkbcommon
                spkgs.libGL
              ];
              videoLibs = if wayland then waylandLibs else xorgLibs;
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
              ]
              # wayland-scanner for the wayland video driver. It normally
              # resolves through the wayland-scanner.pc pkg-config variable,
              # but also putting the wayland bin output on PATH covers
              # find_program()-based lookup.
              ++ pkgs.lib.optionals (!static && wayland) [ spkgs.buildPackages.wayland ];
              # The static build pulls the video/ALSA client libs in at link
              # time via sdl2.pc's Libs.private, so they must be propagated to
              # downstream consumers. The shared build records them as
              # DT_NEEDED of libSDL2.so instead. Both variants still propagate
              # videoLibs so downstream apps can find the headers that
              # SDL_syswm.h includes.
              buildInputs = pkgs.lib.optionals (!static) videoLibs;
              propagatedBuildInputs = videoLibs;
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
                (if wayland then "-DSDL_X11=OFF" else "-DSDL_X11=ON")
                (if wayland then "-DSDL_WAYLAND=ON" else "-DSDL_WAYLAND=OFF")
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
              ]
              ++ pkgs.lib.optionals wayland [
                # SDL2 (>= 2.26) draws its own client-side decorations when
                # libdecor is unavailable; skipping it keeps libdecor's plugin
                # deps (gtk/cairo) out of the portable bundles.
                "-DSDL_WAYLAND_LIBDECOR=OFF"
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

          # glibc shared SDL2 for the portable bundles. The app links
          # libSDL2.so; each bundle ships the .so and its video/ALSA client
          # lib closure next to the binary.
          dynamicSDL2 = buildSDL2 { static = false; };
          dynamicSDL2Wayland = buildSDL2 {
            static = false;
            wayland = true;
          };

          # ---- ccache toggle (shared by every package) ----------------------
          # Reading $VOICETYPER_CCACHE / $CCACHE_DIR at eval time requires
          # --impure; tools/release.sh sets VOICETYPER_CCACHE=1 and opens
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
          # CCACHE_UMASK=000 so cache subdirs are world-writable — the cpu and
          # cuda portable derivations run in parallel as DIFFERENT nixbld users
          # sharing one CCACHE_DIR, so the default 0022 umask (mode 0755 dirs)
          # would let whichever nixbld creates a subdir lock out the other.
          ccacheEnv = pkgs.lib.optionalAttrs enableCcache {
            CCACHE_DIR = ccacheDir;
            CCACHE_NOHASHDIR = "true";
            CCACHE_COMPILERCHECK = "content";
            CCACHE_UMASK = "000";
          };

          # preConfigure fragment. Composed with `+` when a derivation already
          # has a preConfigure (e.g. cuda-static's LDFLAGS export).
          # CCACHE_TEMPDIR is pointed at $TMPDIR (unique per nix build) instead
          # of the default $CCACHE_DIR/tmp — the latter is created mode 0700 by
          # whichever nixbld user gets there first, denying the parallel build.
          ccachePreConfigure = pkgs.lib.optionalString enableCcache ''
            export CCACHE_BASEDIR="$PWD"
            export CCACHE_TEMPDIR="$TMPDIR/ccache-tmp"
          '';
        in
        # rec because the portable-* package entries instantiate the
        # mkPortableBundle factory defined alongside them below.
        rec {
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
                libx11
                libxcursor
                libxext
                libxfixes
                libxi
                libxinerama
                libxrandr
              ];

              cmakeBuildType = "Release";
              cmakeFlags = [
                "-DVOICETYPER_BUILD_CUDA_PLUGIN=OFF"
                "-DVOICETYPER_APP_IPO=OFF"
              ]
              ++ ccacheCmakeFlags { };

              preConfigure = ccachePreConfigure;

              # GGML_BACKEND_DL builds the CPU backend as a dlopened module
              # (libggml-cpu.so) that ggml_backend_load_all() discovers by
              # scanning the real executable's directory (/proc/self/exe). ggml
              # installs the module into bin/ next to the wrapped binary, so no
              # relocation is needed here. The DT_NEEDED shared libs
              # (libwhisper/libggml/libggml-base) stay in $out/lib and resolve
              # via the binary's nix-store rpath.
              postInstall = ''
                wrapProgram $out/bin/VoiceTyper \
                  --prefix LD_LIBRARY_PATH : ${pkgs.lib.makeLibraryPath runtimeLibs} \
                  --run 'export VOICETYPER_DATA_DIR="''${VOICETYPER_DATA_DIR:-''${XDG_DATA_HOME:-$HOME/.local/share}/voicetyper}"'
                # Clean up build artifacts from ggml/whisper cmake install targets
                rm -f $out/lib/*.a
                rm -rf $out/lib/cmake $out/lib/pkgconfig $out/include
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
                  libx11
                  libxcursor
                  libxext
                  libxfixes
                  libxi
                  libxinerama
                  libxrandr
                  cudaPackages.cuda_cudart
                  cudaPackages.libcublas
                ];

                cmakeBuildType = "Release";
                cmakeFlags = [
                  "-DVOICETYPER_BUILD_CUDA_PLUGIN=ON"
                  "-DVOICETYPER_APP_IPO=OFF"
                ]
                ++ ccacheCmakeFlags { cuda = true; };

                preConfigure = ccachePreConfigure;

                # Like `default`, relocate the dlopened backend modules next to
                # the wrapped binary. The CUDA plugin additionally goes in
                # bin/cuda/ — the app loads cuda/libggml-cuda.so explicitly
                # (system.h) rather than via ggml's exe-dir scan. The DT_NEEDED
                # shared libs stay in $out/lib and resolve via the nix rpath.
                postInstall = ''
                  # ggml installs the dlopened backend modules into bin/
                  # (CMAKE_INSTALL_BINDIR). libggml-cpu.so stays next to the
                  # binary (ggml scans the exe dir for it); the CUDA plugin
                  # goes in bin/cuda/ — the app loads cuda/libggml-cuda.so
                  # explicitly (system.h) rather than via ggml's exe-dir scan.
                  mkdir -p $out/bin/cuda
                  for so in $out/bin/libggml-cuda.so*; do
                    [ -e "$so" ] || continue
                    mv "$so" $out/bin/cuda/
                  done
                  wrapProgram $out/bin/VoiceTyper \
                    --prefix LD_LIBRARY_PATH : ${unfreePkgs.lib.makeLibraryPath (runtimeLibs ++ cudaRuntimeLibs)} \
                    --run 'export VOICETYPER_DATA_DIR="''${VOICETYPER_DATA_DIR:-''${XDG_DATA_HOME:-$HOME/.local/share}/voicetyper}"'
                  # Clean up build artifacts from ggml/whisper cmake install targets
                  rm -f $out/lib/*.a
                  rm -rf $out/lib/cmake $out/lib/pkgconfig $out/include
                '';
              }
              // ccacheEnv
            );

          # Portable bundle factory: a flat, Windows-style directory of
          # dynamically linked ELFs plus their full .so closure and a bundled
          # glibc ld-linux. Mirrors the Windows zip layout — the launcher, the
          # whisper/ggml shared libs (libwhisper / libggml / libggml-base /
          # libggml-cpu), and ld-linux all sit at the top level. The real
          # binary (VoiceTyper.elf) must stay at the top level because both
          # ggml_backend_load_all() and the app's cuda/ plugin probe resolve
          # libggml-cpu.so relative to /proc/self/exe. The top-level
          # `VoiceTyper` is a /bin/sh launcher that execs the bundled ld-linux
          # on VoiceTyper.elf, so the bundle runs on any distro — including
          # NixOS, where /lib64/ld-linux-x86-64.so.2 does not exist (the
          # launcher bypasses the kernel's PT_INTERP lookup entirely).
          #
          # { wayland }: selects the SDL2 video-driver flavour — false bundles
          # the X11 build (libSDL2 + its X11/ALSA client libs), true bundles
          # the Wayland build (libwayland-client/cursor/egl + libxkbcommon +
          # ALSA). The app itself is identical either way: it renders via
          # SDL's software renderer (imgui_impl_sdlrenderer2) and Linux text
          # insertion is clipboard-based, so no display-server-specific code
          # is compiled into the app.
          #
          # { cuda }: additionally bundles a cuda/ subdir holding the dlopened
          # CUDA backend (libggml-cuda.so) and its cublas/cudart runtime — the
          # same shape as the Windows cuda/ folder. libcuda.so.1 (the NVIDIA
          # driver) is deliberately NOT bundled: the host driver supplies it,
          # so cuda/libggml-cuda.so's rpath falls back to the standard driver
          # locations.
          mkPortableBundle =
            {
              cuda ? false,
              wayland ? false,
            }:
            let
              unfreePkgs = import nixpkgs {
                inherit system;
                config.allowUnfree = true;
              };
              cudaPackages = unfreePkgs.cudaPackages_13_0;
              stdenv = if cuda then cudaPackages.backendStdenv else pkgs.stdenv;
              cudaRuntimeLibs = [
                cudaPackages.cuda_cudart
                cudaPackages.libcublas
              ];
              sdl2 = if wayland then dynamicSDL2Wayland else dynamicSDL2;
              videoLibs = if wayland then waylandLibs else xorgLibs;
              buildDir = if cuda then "Release_cuda-plugin" else "Release_cpu";
            in
            stdenv.mkDerivation (
              {
                pname =
                  "voicetyper-"
                  + (if cuda then "cuda-portable" else "portable")
                  + (if wayland then "-wayland" else "-x11");
                version = pkgs.lib.strings.removeSuffix "\n" (builtins.readFile ./VERSION);
                src = ./.;

                nativeBuildInputs =
                  with unfreePkgs;
                  [
                    cmake
                    patchelf
                    pkg-config
                  ]
                  ++ pkgs.lib.optional cuda cudaPackages.cuda_nvcc
                  ++ pkgs.lib.optional enableCcache ccache;

                buildInputs = [ sdl2 ] ++ pkgs.lib.optionals cuda cudaRuntimeLibs;

                cmakeBuildType = "Release";
                cmakeFlags = [
                  "-DVOICETYPER_BUILD_CUDA_PLUGIN=${if cuda then "ON" else "OFF"}"
                  "-DVOICETYPER_APP_IPO=OFF"
                  "-DCMAKE_DISABLE_FIND_PACKAGE_OpenMP=TRUE"
                ]
                ++ ccacheCmakeFlags { cuda = cuda; };

                preConfigure = ccachePreConfigure;

                # The app + whisper/ggml shared libs and the libggml-cpu.so module
                # are written to <build-dir>/${buildDir}/ via the target output
                # overrides (CMAKE_RUNTIME_OUTPUT_DIRECTORY = the nix build dir).
                buildPhase = ''
                  runHook preBuild
                  cmake --build . --config Release --parallel $NIX_BUILD_CORES
                  runHook postBuild
                '';

                installPhase = ''
                  runHook preInstall

                  mkdir -p $out
                  ${pkgs.lib.optionalString cuda "mkdir -p $out/cuda"}
                  R="${buildDir}"
                  cp -f "$R/VoiceTyper" $out/VoiceTyper.elf
                  cp -Lf "$R/libwhisper.so.1" $out/libwhisper.so.1
                  cp -Lf "$R/libggml.so.0" $out/libggml.so.0
                  cp -Lf "$R/libggml-base.so.0" $out/libggml-base.so.0
                  cp -Lf "$R/libggml-cpu.so" $out/libggml-cpu.so
                  ${pkgs.lib.optionalString cuda ''
                    cp -Lf "$R/libggml-cuda.so" $out/cuda/libggml-cuda.so
                  ''}

                  cp -Lf ${sdl2}/lib/libSDL2-2.0.so.0 $out/

                  # libstdc++ / libgcc_s from the compiler runtime (the app is
                  # built against the toolchain libstdc++; bundling keeps older
                  # host libstdc++ from breaking it). The libstdc++.so.6 symlink
                  # dereferences (-L) to the real versioned .so, avoiding the
                  # -gdb.py pretty-printer that also matches a versioned glob.
                  cp -Lf ${stdenv.cc.cc.lib}/lib/libstdc++.so.6 $out/libstdc++.so.6
                  cp -Lf ${stdenv.cc.cc.lib}/lib/libgcc_s.so.1 $out/

                  # glibc core + the dynamic linker. Bundling these lets the
                  # launcher run the app under our own glibc regardless of the
                  # host libc (and is what makes NixOS work).
                  GLIBC_BASE=${pkgs.glibc}/lib
                  for so in ld-linux-x86-64.so.2 libc.so.6 libm.so.6 libpthread.so.0 libdl.so.2 librt.so.1; do
                    cp -Lf "$GLIBC_BASE/$so" $out/
                  done

                  ${pkgs.lib.optionalString cuda ''
                    # CUDA runtime libs, next to the plugin that dlopens/links
                    # them. libcuda.so.1 (the driver) is NOT bundled.
                    cp -Lf ${cudaPackages.cuda_cudart}/lib/libcudart.so.* $out/cuda/
                    cp -Lf ${cudaPackages.libcublas.lib}/lib/libcublas.so.* $out/cuda/
                    cp -Lf ${cudaPackages.libcublas.lib}/lib/libcublasLt.so.* $out/cuda/
                  ''}

                  # libSDL2.so dlopens the ${if wayland then "Wayland" else "X11"}/ALSA client
                  # libs at runtime (SDL_WAYLAND_SHARED / SDL_X11_SHARED /
                  # SDL_ALSA_SHARED) instead of listing them as DT_NEEDED, so
                  # ldd of the binary never sees them. Copy the full client .so
                  # set explicitly, then pull the transitive closure of
                  # everything in the bundle (libxcb -> libXau/libXdmcp,
                  # libxkbcommon, ...) via ldd.
                  for libdir in ${pkgs.lib.replaceStrings [ ":" ] [ " " ] (pkgs.lib.makeLibraryPath videoLibs)}; do
                    for so in "$libdir"/lib*.so*; do
                      [ -f "$so" ] || continue
                      base="$(basename "$so")"
                      case "$base" in
                        *.so | *.so.*[0-9]) ;;
                        *) continue ;;
                      esac
                      case "$base" in
                        ld-linux*|libc.so.*|libm.so.*|libpthread.so.*|libdl.so.*|librt.so.*|libgcc_s.so.*) ;;
                        *) [ -e "$out/$base" ] || cp -Lf "$so" $out/ 2>/dev/null || true ;;
                      esac
                    done
                  done
                  # Transitive closure of the root${pkgs.lib.optionalString cuda " AND cuda/"} libs. Deps
                  # that already live in cuda/ (cudart/cublas/cublasLt) are
                  # detected via the $out/cuda/ check so they are not
                  # duplicated into the root. libcuda.so.1 (the NVIDIA driver)
                  # is never bundled.
                  for so in $out/*.so* ${pkgs.lib.optionalString cuda "$out/cuda/*.so*"}; do
                    [ -f "$so" ] || continue
                    for dep in $(ldd "$so" 2>/dev/null | awk '/=> \// {print $3}' | sort -u); do
                      base="$(basename "$dep")"
                      case "$base" in
                        ld-linux*|libc.so.*|libm.so.*|libpthread.so.*|libdl.so.*|librt.so.*|libgcc_s.so.*) ;;
                        libcuda.so*) ;;
                        *) [ -e "$out/$base" ] || [ -e "$out/cuda/$base" ] || cp -Lf "$dep" $out/ 2>/dev/null || true ;;
                      esac
                    done
                  done

                  # /bin/sh launcher — present on every Linux including NixOS.
                  # Does NOT export VOICETYPER_DATA_DIR, so settings/models/cuda
                  # resolve to the bundle root (the real exe dir), matching the
                  # Windows zip's portable behaviour.
                  cat > $out/VoiceTyper <<'LAUNCHER'
                  #!/bin/sh
                  DIR="$(cd "$(dirname "$0")" && pwd)"
                  # Point the bundled glibc's lazy dlopens (libgcc_s for pthread
                  # cancellation, NSS modules) at the bundle first. Uses
                  # LD_LIBRARY_PATH (prepends, does not shadow) rather than
                  # ld-linux's --library-path (which would replace the search
                  # path). The bundled libs already carry an $ORIGIN rpath, so
                  # this is belt-and-suspenders for the loader's own lazy
                  # dlopens. $DIR/cuda lets the CUDA runtime libs resolve even
                  # if a sibling rpath misses (harmless when cuda/ is absent).
                  export LD_LIBRARY_PATH="$DIR:$DIR/cuda''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
                  exec "$DIR/ld-linux-x86-64.so.2" "$DIR/VoiceTyper.elf" "$@"
                  LAUNCHER
                  chmod +x $out/VoiceTyper

                  runHook postInstall
                '';

                # Apply rpaths in postFixup (after nixpkgs's shrink-rpath /
                # strip). Resolve runtime deps to the bundle itself. RUNPATH
                # (not RPATH) keeps LD_LIBRARY_PATH authoritative and lets
                # libdl / libpthread resolve to our bundled glibc. The FHS
                # interpreter path is set so the ELF also runs directly on
                # glibc distros; the bundled ld-linux launcher is what makes
                # NixOS work.
                postFixup = ''
                  patchelf --set-interpreter /lib64/ld-linux-x86-64.so.2 \
                           --set-rpath '$ORIGIN' \
                           $out/VoiceTyper.elf
                  for so in $out/*.so*; do
                    [ -f "$so" ] || continue
                    case "$(basename "$so")" in
                      ld-linux*|libc.so.*|libm.so.*|libpthread.so.*|libdl.so.*|librt.so.*|libgcc_s.so.*) continue ;;
                    esac
                    chmod +w "$so"
                    patchelf --set-rpath '$ORIGIN' "$so"
                  done
                  ${pkgs.lib.optionalString cuda ''
                    # cuda/ plugin + runtime libs: resolve ggml core libs from
                    # the bundle root ($ORIGIN/..), sibling cuda libs from
                    # $ORIGIN, then fall back to the standard NVIDIA driver
                    # locations for libcuda.so.1 (which we deliberately do not
                    # bundle).
                    for so in $out/cuda/*.so*; do
                      [ -f "$so" ] || continue
                      chmod +w "$so"
                      patchelf --set-rpath '$ORIGIN/..:$ORIGIN:/run/opengl-driver/lib:/usr/lib/x86_64-linux-gnu:/lib/x86_64-linux-gnu:/usr/lib64:/lib64:/usr/local/cuda/lib64' "$so"
                    done
                  ''}
                '';

                # We manage rpaths ourselves in postFixup (the bundle is
                # relocatable via $ORIGIN), so skip nixpkgs's shrink-rpath
                # step (the patchelf setup hook's patchELF), which would strip
                # the cuda/ libs' $ORIGIN/.. entry and the NVIDIA driver
                # fallbacks.
                dontPatchELF = true;
                # auditTmpdir is a fixupOutput hook and therefore runs BEFORE
                # postFixup, so it would see the pre-fix rpath (the build dir,
                # i.e. $TMPDIR) and fail with a forbidden-/build/ error. We
                # deliberately overwrite every rpath in postFixup, so skip it.
                noAuditTmpdir = true;
                # fixupPhase would rewrite the launcher's /bin/sh shebang to a
                # nix-store bash, which breaks the bundle on non-NixOS systems.
                dontPatchShebangs = true;
              }
              // ccacheEnv
            );

          portable-x11 = mkPortableBundle {
            cuda = false;
            wayland = false;
          };
          portable-wayland = mkPortableBundle {
            cuda = false;
            wayland = true;
          };
          cuda-portable-x11 = mkPortableBundle {
            cuda = true;
            wayland = false;
          };
          cuda-portable-wayland = mkPortableBundle {
            cuda = true;
            wayland = true;
          };
        }
        # The mingw-w64 cross target is only defined from an x86_64-linux
        # host (the only dev box that uses it).
        // nixpkgs.lib.optionalAttrs (system == "x86_64-linux") {
          # Windows CPU cross-compile (mingw-w64). Dev-only target for
          # iterating on the real Windows code path (imgui_impl_win32 +
          # imgui_impl_dx11, WASAPI capture, winhttp model downloader,
          # dbghelp crash dumps) from a Linux host - run the output under
          # wine64 or copy it to a Windows box. NOT a distributable: the
          # release Windows builds remain MSVC-only (tools/release.sh).
          # No buildInputs - the Windows code path links only system import
          # libs (d3d11/dxgi/dwmapi/...) that ship inside the mingw-w64
          # toolchain itself.
          windows-cpu =
            let
              mingwPkgs = import nixpkgs {
                inherit system;
                crossSystem = nixpkgs.lib.systems.examples.mingwW64;
                config.allowUnsupportedSystem = true;
              };
            in
            mingwPkgs.stdenv.mkDerivation (
              {
                pname = "voicetyper-windows-cpu";
                version = pkgs.lib.strings.removeSuffix "\n" (builtins.readFile ./VERSION);
                src = ./.;

                nativeBuildInputs =
                  with mingwPkgs.buildPackages;
                  [
                    cmake
                  ]
                  ++ pkgs.lib.optional enableCcache ccache;

                cmakeBuildType = "Release";
                cmakeFlags = [
                  "-DVOICETYPER_BUILD_CUDA_PLUGIN=OFF"
                  "-DVOICETYPER_APP_IPO=OFF"
                ]
                ++ ccacheCmakeFlags { };

                preConfigure = ccachePreConfigure;

                buildPhase = ''
                  runHook preBuild
                  cmake --build . --config Release --parallel $NIX_BUILD_CORES
                  runHook postBuild
                '';

                # The target output overrides put VoiceTyper.exe and the
                # whisper/ggml DLLs (incl. the dlopened ggml-cpu.dll
                # backend) into <build-dir>/Release_cpu/ - the same flat
                # layout as the Windows release build, runnable directly
                # under wine64. The mingw runtime DLLs the exe links
                # (libstdc++-6, libgcc_s_seh-1 from the cross gcc,
                # libmcfgthread-2 from its threading model) are bundled
                # too so $out is self-contained on a Windows box.
                installPhase = ''
                  runHook preInstall
                  mkdir -p $out
                  cp -f Release_cpu/VoiceTyper.exe $out/
                  cp -f Release_cpu/*.dll $out/
                  cp -f ${mingwPkgs.stdenv.cc.cc.lib}/x86_64-w64-mingw32/lib/libstdc++-6.dll $out/
                  cp -f ${mingwPkgs.stdenv.cc.cc.lib}/x86_64-w64-mingw32/lib/libgcc_s_seh-1.dll $out/
                  cp -f ${mingwPkgs.windows.mcfgthreads}/bin/libmcfgthread-2.dll $out/
                  runHook postInstall
                '';
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
              libx11
              libxcursor
              libxext
              libxfixes
              libxi
              libxinerama
              libxrandr
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
