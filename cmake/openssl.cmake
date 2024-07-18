# Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

SET(OPENSSL_SOURCE_DIR ${LIB_INSTALL_PREFIX})
SET(OPENSSL_INSTALL_DIR "${LIB_INSTALL_PREFIX}")
SET(OPENSSL_INCLUDE_DIR "${LIB_INCLUDE_DIR}" CACHE PATH "Openssl include directory." FORCE)

FILE(MAKE_DIRECTORY ${OPENSSL_INCLUDE_DIR})

ExternalProject_Add(
        OpenSSL
        #        SOURCE_DIR ${OPENSSL_SOURCE_DIR}
        URL https://github.com/openssl/openssl/archive/refs/tags/openssl-3.2.1.tar.gz
        URL_HASH SHA256=75cc6803ffac92625c06ea3c677fb32ef20d15a1b41ecc8dddbc6b9d6a2da84c
        USES_TERMINAL_DOWNLOAD TRUE
        CONFIGURE_COMMAND
        <SOURCE_DIR>/config
        --prefix=${OPENSSL_INSTALL_DIR}
        --openssldir=${OPENSSL_INSTALL_DIR}
        --libdir=${OPENSSL_INSTALL_DIR}/lib
        no-docs
        BUILD_COMMAND make -j${CPU_CORE}
        TEST_COMMAND ""
        INSTALL_COMMAND make install_sw
        INSTALL_DIR ${OPENSSL_INSTALL_DIR}
)

SET(OPENSSL_LIB "lib")
ADD_LIBRARY(ssl STATIC IMPORTED GLOBAL)
SET_PROPERTY(TARGET ssl PROPERTY IMPORTED_LOCATION ${OPENSSL_INSTALL_DIR}/${OPENSSL_LIB}/libssl.a)
SET_PROPERTY(TARGET ssl PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${OPENSSL_INCLUDE_DIR})
ADD_DEPENDENCIES(ssl OpenSSL)
SET(OPENSSL_SSL_LIBRARY ${OPENSSL_INSTALL_DIR}/${OPENSSL_LIB}/libssl.a)
list(APPEND LIBS ssl)

ADD_LIBRARY(crypto STATIC IMPORTED GLOBAL)
SET_PROPERTY(TARGET crypto PROPERTY IMPORTED_LOCATION ${OPENSSL_INSTALL_DIR}/${OPENSSL_LIB}/libcrypto.a)
SET_PROPERTY(TARGET crypto PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${OPENSSL_INCLUDE_DIR})
ADD_DEPENDENCIES(crypto OpenSSL)
SET(OPENSSL_CRYPTO_LIBRARY ${OPENSSL_INSTALL_DIR}/${OPENSSL_LIB}/libcrypto.a)

SET(OPENSSL_INCLUDE_DIR ${LIB_INCLUDE_DIR})
