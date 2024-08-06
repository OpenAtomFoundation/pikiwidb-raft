#!/bin/bash

#color code
C_RED="\033[31m"
C_GREEN="\033[32m"

C_END="\033[0m"

BUILD_TIME=$(git log -1 --format=%ai)
BUILD_TIME=${BUILD_TIME: 0: 10}

COMMIT_ID=$(git rev-parse HEAD)
SHORT_COMMIT_ID=${COMMIT_ID: 0: 8}

BUILD_TYPE=Release
VERBOSE=0
CMAKE_FLAGS=""
MAKE_FLAGS=""
PREFIX="cmake-build"

PWD=`pwd`
PROJECT_HOME="${PWD}/"
CONF="${PROJECT_HOME}/etc/conf/pikiwidb.conf"

function build() {
  if [ ! -f "/proc/cpuinfo" ];then
    CPU_CORE=$(sysctl -n hw.ncpu)
  else
    CPU_CORE=$(cat /proc/cpuinfo| grep "processor"| wc -l)
  fi
  if [ ${CPU_CORE} -eq 0 ]; then
    CPU_CORE=1
  fi

  echo "cpu core ${CPU_CORE}"

  echo "BUILD_TYPE:" $BUILD_TYPE
  echo "CMAKE_FLAGS:" $CMAKE_FLAGS
  echo "MAKE_FLAGS:" $MAKE_FLAGS

  if [ "${BUILD_TYPE}" == "Release" ]; then
    PREFIX="${PREFIX}-release"
  else
    PREFIX="${PREFIX}-debug"
  fi

  cmake -DCMAKE_BUILD_TYPE=${BUILD_TYPE} ${CMAKE_FLAGS} -S . -B ${PREFIX}
  cmake --build ${PREFIX} -- ${MAKE_FLAGS} -j ${CPU_CORE}

  if [ $? -eq 0 ]; then
    # echo -e "pikiwidb compile complete, output file ${C_GREEN} ${PREFIX}/bin/pikiwidb ${C_END}"
    echo -e "pikiwidb compile complete, output file ${C_GREEN} ./bin/pikiwidb ${C_END}"
  else
    echo -e "${C_RED} pikiwidb compile fail ${C_END}"
    exit 1
  fi
}

# ":?" makes sure the bash var is not empty.
function clear() {
  rm -rf ${PROJECT_HOME:?}/deps-debug
  rm -rf ${PROJECT_HOME:?}/deps-release
  rm -rf ${PROJECT_HOME:?}/cmake-build-debug
  rm -rf ${PROJECT_HOME:?}/cmake-build-release
  rm -rf ${PROJECT_HOME:?}/cmake-build-release-release
  rm -rf ${PROJECT_HOME:?}/build
  rm -rf ${PROJECT_HOME:?}/build-release
  rm -rf ${PROJECT_HOME:?}/build-debug
  rm -rf ${PROJECT_HOME:?}/bin
  exit 0
}

function show_help() {
  echo "
  sh $0 --debug     compile with debug
  sh $0 --clear     clear compilation directory
  sh $0 -h|--help   show help
  sh $0 --prefix    compile output path
  sh $0 --verbose   compile with verbose
  "
  exit 0
}

# ARGS=`getopt -a -o h -l help,debug,verbose,prefix: -- "$@"`
# Convert the parsed arguments into an array
# eval set -- "$ARGS"

while true; do
  # echo "hello first arg $1"
  case "$1" in
  -c|--clear)
    clear
    shift ;;

  --debug)
    BUILD_TYPE=debug
    ;;

  -h|--help)
    show_help
    ;;

  --prefix)
    if [[ -n $2 ]]; then
      PREFIX=$2
    else
      echo "you should provide a value for --prefix as output path"
      exit 0
    fi
    shift;;

  --verbose)
    CMAKE_FLAGS="${CMAKE_FLAGS} -DCMAKE_VERBOSE_MAKEFILE:BOOL=ON"
    MAKE_FLAGS="${MAKE_FLAGS} VERBOSE=1"
    ;;

  --)
    shift
    break;;

  *)
    break;;
  esac
  shift
done

build
