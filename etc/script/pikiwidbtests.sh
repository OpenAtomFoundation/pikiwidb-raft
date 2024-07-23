#!/bin/bash

# clear the log file
function cleanup() {
    rm -rf ./logs*
    rm -rf ./db*
    rm -rf dbsync/
    rm src/redis-server
}

# check if tcl is installed
function check_tcl {
    if [ -z "$(which tclsh)" ]; then
        echo "tclsh is not installed"
        exit 1
    fi
}

# handle different build directories.
function setup_build_dir {
    BUILD_DIR="./bin"
    echo "BUILD_DIR: $BUILD_DIR"
}

# setup pikiwidb bin and conf
function setup_pikiwidb_bin {
    PIKIWIDB_BIN="./$BUILD_DIR/pikiwidb"
    if [ ! -f "$PIKIWIDB_BIN" ]; then
        echo "pikiwidb bin not found"
        exit 1
    fi
    cp $PIKIWIDB_BIN src/redis-server
    cp ./etc/conf/pikiwidb.conf tests/assets/default.conf
}


cleanup

check_tcl

setup_build_dir

setup_pikiwidb_bin

echo "run pikiwidb tests $1"

if [ "$1" == "all" ]; then
    tclsh tests/test_helper.tcl --clients 1
else
    tclsh tests/test_helper.tcl --clients 1 --single unit/$1
fi

if [ $? -ne 0 ]; then
    echo "pikiwidb tests failed"
    cleanup
    exit 1
fi

# You can use './pikiwidb.sh all clean 'to ensure that the
# data can be deleted immediately after the test
if [ "$2" == "clean" ]; then
   cleanup
fi