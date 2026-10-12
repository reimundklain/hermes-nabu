{
  description = "hermes-nabu: thin-client firmware for the Home Assistant Voice PE (ESP32-S3)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.11";
    esp-dev.url = "github:mirrexagon/nixpkgs-esp-dev";
  };

  outputs =
    {
      self,
      nixpkgs,
      esp-dev,
    }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "aarch64-darwin"
      ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
      formatter = forAllSystems (system: nixpkgs.legacyPackages.${system}.nixfmt-rfc-style);

      devShells = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          espIdf = esp-dev.packages.${system}.esp-idf-xtensa;
        in
        {
          default = pkgs.mkShell {
            name = "hermes-nabu";

            packages = [
              espIdf
              pkgs.nixfmt-rfc-style
            ];
          };
        }
      );
    };
}
