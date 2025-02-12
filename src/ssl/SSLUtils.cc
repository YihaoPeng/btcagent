/*
 Mining Pool Agent

 Copyright (C) 2016  BTC.COM

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
 #include <pthread.h>
#endif

#include <openssl/opensslv.h>
#include <openssl/ssl.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <glog/logging.h>

#include "SSLUtils.h"

/*
 * Some code copied from LibEvent's document
 * http://www.wangafu.net/~nickm/libevent-book/Ref6a_advanced_bufferevents.html
 *
 * The built in threading mechanisms of Libevent do not cover OpenSSL locking.
 * Since OpenSSL uses a myriad of global variables, you must still configure
 * OpenSSL to be thread safe. While this process is outside the scope of
 * Libevent, this topic comes up enough to warrant discussion.
 *
 * Example: A very simple example of how to enable thread safe OpenSSL
 */

#if OPENSSL_VERSION_NUMBER < 0x10100000L

// These two functions do not exist in OpenSSL 1.0.2
#define TLS_client_method TLSv1_2_client_method
#define TLS_server_method TLSv1_2_server_method

static pthread_mutex_t *ssl_locks;
static int ssl_num_locks;

/* Implements a thread-ID function as requied by openssl */
static unsigned long get_thread_id_cb() {
  return (unsigned long)pthread_self();
}

static void thread_lock_cb(int mode, int which, const char *f, int l) {
  if (which < ssl_num_locks) {
    if (mode & CRYPTO_LOCK) {
      pthread_mutex_lock(&(ssl_locks[which]));
    } else {
      pthread_mutex_unlock(&(ssl_locks[which]));
    }
  }
}

void init_ssl_locking() {
  static bool inited = false;
  if (inited) {
    return;
  }

  ssl_num_locks = CRYPTO_num_locks();
  ssl_locks = new pthread_mutex_t[ssl_num_locks];

  for (int i = 0; i < ssl_num_locks; i++) {
    pthread_mutex_init(&(ssl_locks[i]), NULL);
  }

  CRYPTO_set_id_callback(get_thread_id_cb);
  CRYPTO_set_locking_callback(thread_lock_cb);

  inited = true;
  return;
}

#else

void init_ssl_locking() {
  // OpenSSL 1.1 is built-in thread safe, so we don't need to do anything
}

#endif

std::string get_ssl_err_string() {
  std::string errmsg;
  errmsg.resize(1024);
  ERR_error_string_n(ERR_get_error(), (char *)errmsg.data(), errmsg.size());
  return errmsg.c_str(); // strip padding '\0'
}

SSL_CTX *get_client_SSL_CTX() {
  SSL_CTX *sslCTX = nullptr;

  /* Initialize the OpenSSL library */
  SSL_load_error_strings();
  SSL_library_init();
  OpenSSL_add_all_algorithms();
  init_ssl_locking();

  /* We MUST have entropy, or else there's no point to crypto. */
  if (!RAND_poll()) {
    LOG(FATAL) << "RAND_poll failed: " << get_ssl_err_string();
  }

  sslCTX = SSL_CTX_new(TLS_client_method());
  if (sslCTX == nullptr) {
    LOG(FATAL) << "SSL_CTX init failed: " << get_ssl_err_string();
  }

  SSL_CTX_set_verify(sslCTX, SSL_VERIFY_NONE, NULL);

  return sslCTX;
}

SSL_CTX *get_client_SSL_CTX_With_Cache() {
  static SSL_CTX *sslCTX = nullptr;

  if (sslCTX == nullptr) {
    sslCTX = get_client_SSL_CTX();
  }

  return sslCTX;
}

SSL_CTX *
get_server_SSL_CTX(const std::string &certFile, const std::string &keyFile) {
  SSL_CTX *sslCTX = nullptr;

  /* Initialize the OpenSSL library */
  SSL_load_error_strings();
  SSL_library_init();
  OpenSSL_add_all_algorithms();
  init_ssl_locking();

  /* We MUST have entropy, or else there's no point to crypto. */
  if (!RAND_poll()) {
    LOG(FATAL) << "RAND_poll failed: " << get_ssl_err_string();
  }

  sslCTX = SSL_CTX_new(TLS_server_method());
  if (sslCTX == nullptr) {
    LOG(FATAL) << "SSL_CTX init failed: " << get_ssl_err_string();
  }

  if (!SSL_CTX_use_certificate_chain_file(sslCTX, certFile.c_str()) ||
      !SSL_CTX_use_PrivateKey_file(sslCTX, keyFile.c_str(), SSL_FILETYPE_PEM)) {
    LOG(FATAL) << "Couldn't read '" << certFile << "' or '" << keyFile
               << "' file.\n"
                  "To generate a key and self-signed certificate, run:\n"
                  "  openssl genrsa -out "
               << keyFile
               << " 2048\n"
                  "  openssl req -new -key "
               << keyFile << " -out " << certFile
               << ".req\n"
                  "  openssl x509 -req -days 365 -in "
               << certFile << ".req -signkey " << keyFile << " -out "
               << certFile;
  }

  return sslCTX;
}
