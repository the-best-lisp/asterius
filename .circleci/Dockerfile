FROM terrorjack/vanilla:latest

ARG NIXPKGS_REV
ADD bootstrap.sh /tmp/
RUN NIXPKGS_REV=${NIXPKGS_REV} sh -e /tmp/bootstrap.sh
ENTRYPOINT ["nix-serve"]
