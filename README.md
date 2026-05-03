# CS5544_Project

## Environment Setup:

To pull docker container: `docker pull priyatam19/compiler-opts-lli-stats:llvm21`

To start development environment within the container: `docker run --rm -it -v "$PWD":/work -w /work priyatam19/compiler-opts-lli-stats:llvm21 /bin/bash`

## To Run:

Run `make` in the `/` directory

Run `./run_benchmarks` in the `/tests/` directory