#!/bin/bash

set -e

SCRIPT=$PWD/scripted_gmx_docker_builds.py
PYTHON=${PYTHON:-$(which python3)}

# Note: All official GROMACS CI images are built
# with openmpi on. That reduces the total number of
# images needed, because the same one can test library,
# thread and no MPI configurations.

args[${#args[@]}]="--llvm 12"
args[${#args[@]}]="--gcc 11 --clfft --mpi openmpi --rocm"
args[${#args[@]}]="--gcc 11 --cuda 11.4.1 --clfft --mpi openmpi --nvhpcsdk 22.5"
args[${#args[@]}]="--gcc 9 --cuda 11.0.3 --clfft --mpi openmpi --heffte v2.2.0"
args[${#args[@]}]="--gcc 9 --mpi openmpi --cp2k 8.2"
args[${#args[@]}]="--gcc 9 --mpi openmpi --cp2k 9.1"
args[${#args[@]}]="--llvm 11 --cuda 11.4.1"
args[${#args[@]}]="--llvm 11 --tsan"
args[${#args[@]}]="--llvm 9 --cuda 11.0.3 --clfft --mpi openmpi"
args[${#args[@]}]="--llvm 13 --clfft --mpi openmpi --rocm"
# Note that oneAPI currently only supports Ubuntu 20.04
args[${#args[@]}]="--oneapi 2022.1.0 --ubuntu 20.04"
# Note that oneAPI currently only supports Ubuntu 20.04
args[${#args[@]}]="--oneapi 2022.1.0 --intel-compute-runtime --ubuntu 20.04"
args[${#args[@]}]="--llvm --doxygen --mpi openmpi --venvs 3.7.7 3.9.13"
args[${#args[@]}]="--llvm 14 --cuda 11.4.3 --hipsycl 03de8f8 --rocm 5.1 --mpi mpich"
args[${#args[@]}]="--intel-llvm 2022-06 --cuda 11.4.3"

echo
echo "Consider pulling the following images for build layer cache."
echo
for arg_string in "${args[@]}"; do
  # shellcheck disable=SC2086
  echo "docker pull $($PYTHON -m utility $arg_string)"
done
echo
echo

echo "To build with cache hints:"
echo
for arg_string in "${args[@]}"; do
  # shellcheck disable=SC2086
  tag=$($PYTHON -m utility $arg_string)
  tags[${#tags[@]}]=$tag
  # shellcheck disable=SC2086
  echo "$PYTHON $SCRIPT $arg_string | docker build -t $tag --cache-from $tag -"
done
unset tags
echo
echo

echo "To build without extra cache hints:"
echo
for arg_string in "${args[@]}"; do
  # shellcheck disable=SC2086
  tag=$($PYTHON -m utility $arg_string)
  tags[${#tags[@]}]=$tag
  # shellcheck disable=SC2086
  echo "$PYTHON $SCRIPT $arg_string | docker build -t $tag -"
done
unset tags
echo
echo

echo "Run the following to upload the updated images."
echo "docker login registry.gitlab.com -u <token name> -p <hash>"
echo
for arg_string in "${args[@]}"; do
  # shellcheck disable=SC2086
  tag=$($PYTHON -m utility $arg_string)
  tags[${#tags[@]}]=$tag
  echo "docker push $tag"
done

# Check whether built images are used and whether used images are built.
# Note: Let used images with a ':latest' tag match images without explicit tags.
for tag in "${tags[@]}"; do
  image=$(basename $tag)
  # Checking whether $image is used.
  grep -qR -e "${image}\(:latest\)*$" ../gitlab-ci || echo Warning: Image $image appears unused.
done
list=$(grep -R 'image: ' ../gitlab-ci/ |awk '{print $3}' |sort -u)
$PYTHON << EOF
from os.path import basename
built="""${tags[@]}"""
built=set(basename(image.rstrip()) for image in built.split())
in_use="""${list[@]}"""
in_use=[basename(image.rstrip()) for image in in_use.split()]
for tag in in_use:
  if tag.endswith(':latest'):
    tag = tag.split(':')[0]
  if not tag in built:
    print(f'Warning: {tag} is not being built.')
EOF
