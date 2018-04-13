#!/bin/sh -e

nix-channel --update
nix-env -u
nix-env -iA \
    nixpkgs.curlFull \
    nixpkgs.nix-serve \
    nixpkgs.unzip

cd /tmp
curl -L https://github.com/TerrorJack/asterius-nixpkgs/archive/$NIXPKGS_REV.zip -o asterius-nixpkgs-$NIXPKGS_REV.zip
unzip -q asterius-nixpkgs-$NIXPKGS_REV.zip
cd asterius-nixpkgs-$NIXPKGS_REV
nix-build -j4 -A haskell.compiler.ghcAsterius
nix-env -f . -iA haskell.compiler.ghcAsterius
cd /root

nix-env --uninstall \
    curl \
    unzip
nix-collect-garbage -d
rm -rf \
    /tmp/bootstrap.sh \
    /tmp/asterius-nixpkgs-$NIXPKGS_REV \
    /tmp/asterius-nixpkgs-$NIXPKGS_REV.zip
