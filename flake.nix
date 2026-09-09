{
  description = "hosq: a C++20-native command line parser";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-parts.url = "github:hercules-ci/flake-parts";
  };

  outputs =
    inputs@{
      flake-parts,
      ...
    }:
    flake-parts.lib.mkFlake { inherit inputs; } {
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
      ];

      perSystem =
        { pkgs, ... }:
        let
          version = builtins.replaceStrings [ "\n" ] [ "" ] (builtins.readFile ./VERSION);
          clangTools = pkgs.clang-tools.override { enableLibcxx = true; };
          gtest = pkgs.gtest.override { stdenv = pkgs.libcxxStdenv; };
          developmentTools = [
            pkgs.llvmPackages.libcxxClang
            clangTools
            pkgs.cmake
            pkgs.ninja
            gtest
          ];
          formatter = pkgs.writeShellApplication {
            name = "hosq-fmt";
            runtimeInputs = [
              pkgs.findutils
              pkgs.git
              pkgs.nixfmt
              clangTools
            ];
            text = ''
              if [ "''${1:-}" = "--ci" ]; then
                shift
                git ls-files -z -- '*.nix' | xargs -0 -r nixfmt --check
                git ls-files -z -- \
                  '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hh' '*.hpp' '*.hxx' \
                  ':(exclude)include/hosq/third_party/**' \
                  ':(exclude)bench/comparison/results/**' \
                  | xargs -0 -r clang-format --dry-run --Werror
              else
                git ls-files -z -- '*.nix' | xargs -0 -r nixfmt
                git ls-files -z -- \
                  '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hh' '*.hpp' '*.hxx' \
                  ':(exclude)include/hosq/third_party/**' \
                  ':(exclude)bench/comparison/results/**' \
                  | xargs -0 -r clang-format -i
              fi
            '';
          };
        in
        {
          inherit formatter;

          devShells = rec {
            default = pkgs.mkShellNoCC {
              packages = developmentTools;
            };
            ci = default;
          };

          packages.default = pkgs.stdenvNoCC.mkDerivation {
            pname = "hosq";
            inherit version;
            src = ./.;
            nativeBuildInputs = [
              pkgs.llvmPackages.libcxxClang
              clangTools
              pkgs.cmake
              pkgs.ninja
            ];
            buildInputs = [ gtest ];
            cmakeFlags = [
              "-G Ninja"
              "-DCMAKE_BUILD_TYPE=Release"
              "-DCXX_HOSQ_ENABLE_TEST=ON"
              "-DCXX_HOSQ_ENABLE_CLANG_TIDY=ON"
              "-DCXX_HOSQ_ENABLE_SANITIZERS=OFF"
            ];
            doCheck = true;
            checkPhase = ''
              runHook preCheck
              ctest --output-on-failure
              runHook postCheck
            '';
          };
        };
    };
}
