{
  description = "gattn - attention hub for GNOME";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
      # ponytail: rebuild vte from source — nixpkgs ships it with gtk4=false
      vte-gtk4 = pkgs.vte.overrideAttrs (old: {
        buildInputs = old.buildInputs ++ [ pkgs.gtk4 ];
        mesonFlags = map
          (f: if f == "-Dgtk4=false" then "-Dgtk4=true" else f)
          old.mesonFlags;
      });
    in {
      devShells.${system}.default = pkgs.mkShell {
        packages = with pkgs; [
          gcc
          pkg-config
          gtk4
          libadwaita
          vte-gtk4
          meson
          ninja
        ];
      };
    };
}
