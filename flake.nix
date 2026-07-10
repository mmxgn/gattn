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
      packages.${system}.default = pkgs.stdenv.mkDerivation {
        pname = "gattn";
        version = "0.1.0";
        src = pkgs.lib.cleanSource ./.;
        nativeBuildInputs = with pkgs; [ meson ninja pkg-config wrapGAppsHook4 ];
        buildInputs = [ pkgs.gtk4 pkgs.libadwaita vte-gtk4 pkgs.gtksourceview5 ];
      };

      devShells.${system}.default = pkgs.mkShell {
        packages = with pkgs; [
          gcc
          pkg-config
          gtk4
          libadwaita
          vte-gtk4
          gtksourceview5
          meson
          ninja
          meld
        ];
      };
    };
}
