/*                      _             _
**  _ __ ___   ___   __| |    ___ ___| |  mod_ssl
** | '_ ` _ \ / _ \ / _` |   / __/ __| |  Apache Interface to OpenSSL
** | | | | | | (_) | (_| |   \__ \__ \ |  www.modssl.org
** |_| |_| |_|\___/ \__,_|___|___/___/_|  ftp.modssl.org
**                      |_____|
**  ssl_engine_init.c
**  Initialization of Servers
*/

/* ====================================================================
 * The Apache Software License, Version 1.1
 *
 * Copyright (c) 2000-2002 The Apache Software Foundation.  All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The end-user documentation included with the redistribution,
 *    if any, must include the following acknowledgment:
 *       "This product includes software developed by the
 *        Apache Software Foundation (http://www.apache.org/)."
 *    Alternately, this acknowledgment may appear in the software itself,
 *    if and wherever such third-party acknowledgments normally appear.
 *
 * 4. The names "Apache" and "Apache Software Foundation" must
 *    not be used to endorse or promote products derived from this
 *    software without prior written permission. For written
 *    permission, please contact apache@apache.org.
 *
 * 5. Products derived from this software may not be called "Apache",
 *    nor may "Apache" appear in their name, without prior written
 *    permission of the Apache Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE APACHE SOFTWARE FOUNDATION OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * ====================================================================
 */
                             /* ``Recursive, adj.;
                                  see Recursive.''
                                        -- Unknown   */
#include "mod_ssl.h"

/*  _________________________________________________________________
**
**  Module Initialization
**  _________________________________________________________________
*/

static char *ssl_add_version_component(apr_pool_t *p,
                                       server_rec *s,
                                       char *name)
{
    char *val = ssl_var_lookup(p, s, NULL, NULL, name);

    if (val && *val) {
        ap_add_version_component(p, val);
    }

    return val;
}

static char *version_components[] = {
    "SSL_VERSION_PRODUCT",
    "SSL_VERSION_INTERFACE",
    "SSL_VERSION_LIBRARY",
    NULL
};

static void ssl_add_version_components(apr_pool_t *p,
                                       server_rec *s)
{
    char *vals[sizeof(version_components)/sizeof(char *)];
    int i;

    for (i=0; version_components[i]; i++) {
        vals[i] = ssl_add_version_component(p, s,
                                            version_components[i]);
    }

    ssl_log(s, SSL_LOG_INFO,
            "Server: %s, Interface: %s, Library: %s",
            AP_SERVER_BASEVERSION,
            vals[1],  /* SSL_VERSION_INTERFACE */
            vals[2]); /* SSL_VERSION_LIBRARY */
}


/*
 *  Initialize SSL library
 */
static void ssl_init_SSLLibrary(server_rec *s)
{
    ssl_log(s, SSL_LOG_INFO,
            "Init: Initializing %s library", SSL_LIBRARY_NAME);

    CRYPTO_malloc_init();
    SSL_load_error_strings();
    SSL_library_init();
}

/*
 * Handle the Temporary RSA Keys and DH Params
 */

#define MODSSL_TMP_KEY_FREE(mc, type, idx) \
    if (mc->pTmpKeys[idx]) { \
        type##_free((type *)mc->pTmpKeys[idx]); \
        mc->pTmpKeys[idx] = NULL; \
    }

#define MODSSL_TMP_KEYS_FREE(mc, type) \
    MODSSL_TMP_KEY_FREE(mc, type, SSL_TMP_KEY_##type##_512); \
    MODSSL_TMP_KEY_FREE(mc, type, SSL_TMP_KEY_##type##_1024)

static void ssl_tmp_keys_free(server_rec *s)
{
    SSLModConfigRec *mc = myModConfig(s);

    MODSSL_TMP_KEYS_FREE(mc, RSA);
    MODSSL_TMP_KEYS_FREE(mc, DH);
}

static void ssl_tmp_key_init_rsa(server_rec *s,
                                 int bits, int idx)
{
    SSLModConfigRec *mc = myModConfig(s);

    if (!(mc->pTmpKeys[idx] =
          RSA_generate_key(bits, RSA_F4, NULL, NULL)))
    {
        ssl_log(s, SSL_LOG_ERROR,
                "Init: Failed to generate temporary "
                "%d bit RSA private key", bits);
        ssl_die();
    }

}

static void ssl_tmp_key_init_dh(server_rec *s,
                                int bits, int idx)
{
    SSLModConfigRec *mc = myModConfig(s);

    if (!(mc->pTmpKeys[idx] =
          ssl_dh_GetTmpParam(bits)))
    {
        ssl_log(s, SSL_LOG_ERROR,
                "Init: Failed to generate temporary "
                "%d bit DH parameters", bits);
        ssl_die();
    }
}

#define MODSSL_TMP_KEY_INIT_RSA(s, bits) \
    ssl_tmp_key_init_rsa(s, bits, SSL_TMP_KEY_RSA_##bits)

#define MODSSL_TMP_KEY_INIT_DH(s, bits) \
    ssl_tmp_key_init_dh(s, bits, SSL_TMP_KEY_DH_##bits)

static void ssl_tmp_keys_init(server_rec *s)
{
    ssl_log(s, SSL_LOG_INFO,
            "Init: Generating temporary RSA private keys (512/1024 bits)");

    MODSSL_TMP_KEY_INIT_RSA(s, 512);
    MODSSL_TMP_KEY_INIT_RSA(s, 1024);

    ssl_log(s, SSL_LOG_INFO,
            "Init: Generating temporary DH parameters (512/1024 bits)");

    MODSSL_TMP_KEY_INIT_DH(s, 512);
    MODSSL_TMP_KEY_INIT_DH(s, 1024);
}

/*
 *  Per-module initialization
 */
int ssl_init_Module(apr_pool_t *p, apr_pool_t *plog,
                    apr_pool_t *ptemp,
                    server_rec *base_server)
{
    SSLModConfigRec *mc = myModConfig(base_server);
    SSLSrvConfigRec *sc;
    server_rec *s;

    /*
     * Let us cleanup on restarts and exists
     */
    apr_pool_cleanup_register(p, base_server,
                              ssl_init_ModuleKill,
                              apr_pool_cleanup_null);

    /*
     * Any init round fixes the global config
     */
    ssl_config_global_create(base_server); /* just to avoid problems */
    ssl_config_global_fix(mc);

    /*
     *  try to fix the configuration and open the dedicated SSL
     *  logfile as early as possible
     */
    for (s = base_server; s; s = s->next) {
        sc = mySrvConfig(s);

        /*
         * Create the server host:port string because we need it a lot
         */
        sc->szVHostID = ssl_util_vhostid(p, s);
        sc->nVHostID_length = strlen(sc->szVHostID);

        /* Fix up stuff that may not have been set */
        if (sc->bEnabled == UNSET) {
            sc->bEnabled = FALSE;
        }

        if (sc->nSessionCacheTimeout == UNSET) {
            sc->nSessionCacheTimeout = SSL_SESSION_CACHE_TIMEOUT;
        }

        if (sc->nPassPhraseDialogType == SSL_PPTYPE_UNSET) {
            sc->nPassPhraseDialogType = SSL_PPTYPE_BUILTIN;
        }

        /* Open the dedicated SSL logfile */
        ssl_log_open(base_server, s, p);
    }

    ssl_init_SSLLibrary(base_server);

#if APR_HAS_THREADS
    ssl_util_thread_setup(base_server, p);
#endif

    /*
     * Seed the Pseudo Random Number Generator (PRNG)
     * only need ptemp here; nothing inside allocated from the pool
     * needs to live once we return from ssl_rand_seed().
     */
    ssl_rand_seed(base_server, ptemp, SSL_RSCTX_STARTUP, "Init: ");

    /*
     * read server private keys/public certs into memory.
     * decrypting any encrypted keys via configured SSLPassPhraseDialogs
     * anything that needs to live longer than ptemp needs to also survive
     * restarts, in which case they'll live inside s->process->pool.
     */
    ssl_pphrase_Handle(base_server, ptemp);

    ssl_tmp_keys_init(base_server);

    /*
     * SSL external crypto device ("engine") support
     */
#ifdef SSL_EXPERIMENTAL_ENGINE
    ssl_init_Engine(base_server, p);
#endif

    /*
     * initialize the mutex handling
     */
    if (!ssl_mutex_init(base_server, p)) {
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /*
     * initialize session caching
     */
    ssl_scache_init(base_server, p);

    /*
     *  initialize servers
     */
    ssl_log(base_server, SSL_LOG_INFO,
            "Init: Initializing (virtual) servers for SSL");

    for (s = base_server; s; s = s->next) {
        sc = mySrvConfig(s);
        /*
         * Either now skip this server when SSL is disabled for
         * it or give out some information about what we're
         * configuring.
         */
        if (!sc->bEnabled) {
            continue;
        }

        ssl_log(s, SSL_LOG_INFO|SSL_INIT,
                "Configuring server for SSL protocol");

        /*
         * Read the server certificate and key
         */
        ssl_init_ConfigureServer(s, p, ptemp, sc);
    }

    /*
     * Configuration consistency checks
     */
    ssl_init_CheckServers(base_server, ptemp);

    /*
     *  Announce mod_ssl and SSL library in HTTP Server field
     *  as ``mod_ssl/X.X.X OpenSSL/X.X.X''
     */
    ssl_add_version_components(p, base_server);

    SSL_init_app_data2_idx(); /* for SSL_get_app_data2() at request time */

    return OK;
}

/*
 * Support for external a Crypto Device ("engine"), usually
 * a hardware accellerator card for crypto operations.
 */
#ifdef SSL_EXPERIMENTAL_ENGINE
void ssl_init_Engine(server_rec *s, apr_pool_t *p)
{
    SSLModConfigRec *mc = myModConfig(s);
    ENGINE *e;

    if (mc->szCryptoDevice) {
        if (!(e = ENGINE_by_id(mc->szCryptoDevice))) {
            ssl_log(s, SSL_LOG_ERROR,
                    "Init: Failed to load Crypto Device API `%s'",
                    mc->szCryptoDevice);
            ssl_die();
        }

        if (strEQ(mc->szCryptoDevice, "chil")) {
            ENGINE_ctrl(e, ENGINE_CTRL_CHIL_SET_FORKCHECK, 1, 0, 0);
        }

        if (!ENGINE_set_default(e, ENGINE_METHOD_ALL)) {
            ssl_log(s, SSL_LOG_ERROR,
                    "Init: Failed to enable Crypto Device API `%s'",
                    mc->szCryptoDevice);
            ssl_die();
        }

        ENGINE_free(e);
    }
}
#endif

static void ssl_init_check_server(server_rec *s,
                                  apr_pool_t *p,
                                  apr_pool_t *ptemp,
                                  SSLSrvConfigRec *sc)
{
    /*
     * check for important parameters and the
     * possibility that the user forgot to set them.
     */
    if (!sc->szPublicCertFiles[0]) {
        ssl_log(s, SSL_LOG_ERROR|SSL_INIT,
                "No SSL Certificate set [hint: SSLCertificateFile]");
        ssl_die();
    }

    /*
     *  Check for problematic re-initializations
     */
    if (sc->pPublicCert[SSL_AIDX_RSA] ||
        sc->pPublicCert[SSL_AIDX_DSA])
    {
        ssl_log(s, SSL_LOG_ERROR|SSL_INIT,
                "Illegal attempt to re-initialise SSL for server "
                "(theoretically shouldn't happen!)");
        ssl_die();
    }
}

static SSL_CTX *ssl_init_ctx(server_rec *s,
                             apr_pool_t *p,
                             apr_pool_t *ptemp,
                             SSLSrvConfigRec *sc)
{
    SSL_CTX *ctx = NULL;
    char *cp;
    int protocol = sc->nProtocol;

    /*
     *  Create the new per-server SSL context
     */
    if (protocol == SSL_PROTOCOL_NONE) {
        ssl_log(s, SSL_LOG_ERROR|SSL_INIT,
                "No SSL protocols available [hint: SSLProtocol]");
        ssl_die();
    }

    cp = apr_pstrcat(p,
                     (protocol & SSL_PROTOCOL_SSLV2 ? "SSLv2, " : ""),
                     (protocol & SSL_PROTOCOL_SSLV3 ? "SSLv3, " : ""),
                     (protocol & SSL_PROTOCOL_TLSV1 ? "TLSv1, " : ""),
                     NULL);
    cp[strlen(cp)-2] = NUL;

    ssl_log(s, SSL_LOG_TRACE|SSL_INIT,
            "Creating new SSL context (protocols: %s)", cp);

    if (protocol == SSL_PROTOCOL_SSLV2) {
        ctx = SSL_CTX_new(SSLv2_server_method());  /* only SSLv2 is left */
    }
    else {
        ctx = SSL_CTX_new(SSLv23_server_method()); /* be more flexible */
    }

    sc->pSSLCtx = ctx;

    SSL_CTX_set_options(ctx, SSL_OP_ALL);

    if (!(protocol & SSL_PROTOCOL_SSLV2)) {
        SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2);
    }

    if (!(protocol & SSL_PROTOCOL_SSLV3)) {
        SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv3);
    }

    if (!(protocol & SSL_PROTOCOL_TLSV1)) {
        SSL_CTX_set_options(ctx, SSL_OP_NO_TLSv1);
    }

    SSL_CTX_set_app_data(ctx, s);

    /*
     * Configure additional context ingredients
     */
    SSL_CTX_set_options(ctx, SSL_OP_SINGLE_DH_USE);

    return ctx;
}

static void ssl_init_session_cache_ctx(server_rec *s,
                                       apr_pool_t *p,
                                       apr_pool_t *ptemp,
                                       SSLSrvConfigRec *sc)
{
    SSL_CTX *ctx = sc->pSSLCtx;
    SSLModConfigRec *mc = myModConfig(s);
    long cache_mode = SSL_SESS_CACHE_OFF;

    if (mc->nSessionCacheMode != SSL_SCMODE_NONE) {
        /* SSL_SESS_CACHE_NO_INTERNAL_LOOKUP will force OpenSSL
         * to ignore process local-caching and
         * to always get/set/delete sessions using mod_ssl's callbacks.
         */
        cache_mode = SSL_SESS_CACHE_SERVER|SSL_SESS_CACHE_NO_INTERNAL_LOOKUP;
    }

    SSL_CTX_set_session_cache_mode(ctx, cache_mode);

    SSL_CTX_sess_set_new_cb(ctx,    ssl_callback_NewSessionCacheEntry);
    SSL_CTX_sess_set_get_cb(ctx,    ssl_callback_GetSessionCacheEntry);
    SSL_CTX_sess_set_remove_cb(ctx, ssl_callback_DelSessionCacheEntry);
}

static void ssl_init_verify(server_rec *s,
                            apr_pool_t *p,
                            apr_pool_t *ptemp,
                            SSLSrvConfigRec *sc)
{
    SSL_CTX *ctx = sc->pSSLCtx;

    int verify = SSL_VERIFY_NONE;
    STACK_OF(X509_NAME) *ca_list;

    if (sc->nVerifyClient == SSL_CVERIFY_UNSET) {
        sc->nVerifyClient = SSL_CVERIFY_NONE;
    }

    if (sc->nVerifyDepth == UNSET) {
        sc->nVerifyDepth = 1;
    }

    /*
     *  Configure callbacks for SSL context
     */
    if (sc->nVerifyClient == SSL_CVERIFY_REQUIRE) {
        verify |= SSL_VERIFY_PEER_STRICT;
    }

    if ((sc->nVerifyClient == SSL_CVERIFY_OPTIONAL) ||
        (sc->nVerifyClient == SSL_CVERIFY_OPTIONAL_NO_CA))
    {
        verify |= SSL_VERIFY_PEER;
    }

    SSL_CTX_set_verify(ctx, verify,  ssl_callback_SSLVerify);

    /*
     * Configure Client Authentication details
     */
    if (sc->szCACertificateFile || sc->szCACertificatePath) {
        ssl_log(s, SSL_LOG_TRACE|SSL_INIT,
                "Configuring client authentication");

        if (!SSL_CTX_load_verify_locations(ctx,
                                           sc->szCACertificateFile,
                                           sc->szCACertificatePath))
        {
            ssl_log(s, SSL_LOG_ERROR|SSL_ADD_SSLERR|SSL_INIT,
                    "Unable to configure verify locations "
                    "for client authentication");
            ssl_die();
        }

        ca_list = ssl_init_FindCAList(s, ptemp,
                                      sc->szCACertificateFile,
                                      sc->szCACertificatePath);
        if (!ca_list) {
            ssl_log(s, SSL_LOG_ERROR|SSL_INIT,
                    "Unable to determine list of available "
                    "CA certificates for client authentication");
            ssl_die();
        }

        SSL_CTX_set_client_CA_list(ctx, (STACK *)ca_list);
    }

    /*
     * Give a warning when no CAs were configured but client authentication
     * should take place. This cannot work.
     */
    if (sc->nVerifyClient == SSL_CVERIFY_REQUIRE) {
        ca_list = (STACK_OF(X509_NAME) *)SSL_CTX_get_client_CA_list(ctx);

        if (sk_X509_NAME_num(ca_list) == 0) {
            ssl_log(s, SSL_LOG_WARN,
                    "Init: Oops, you want to request client authentication, "
                    "but no CAs are known for verification!? "
                    "[Hint: SSLCACertificate*]");
        }
    }
}

static void ssl_init_cipher_suite(server_rec *s,
                                  apr_pool_t *p,
                                  apr_pool_t *ptemp,
                                  SSLSrvConfigRec *sc)
{
    SSL_CTX *ctx = sc->pSSLCtx;
    const char *suite = sc->szCipherSuite;

    /*
     *  Configure SSL Cipher Suite
     */
    if (!suite) {
        return;
    }

    ssl_log(s, SSL_LOG_TRACE|SSL_INIT,
            "Configuring permitted SSL ciphers [%s]", 
            suite);

    if (!SSL_CTX_set_cipher_list(ctx, suite)) {
        ssl_log(s, SSL_LOG_ERROR|SSL_ADD_SSLERR|SSL_INIT,
                "Unable to configure permitted SSL ciphers");
        ssl_die();
    }
}

static void ssl_init_crl(server_rec *s,
                         apr_pool_t *p,
                         apr_pool_t *ptemp,
                         SSLSrvConfigRec *sc)
{
    /*
     * Configure Certificate Revocation List (CRL) Details
     */

    if (!(sc->szCARevocationFile || sc->szCARevocationPath)) {
        return;
    }

    ssl_log(s, SSL_LOG_TRACE|SSL_INIT,
            "Configuring certificate revocation facility");

    sc->pRevocationStore =
        SSL_X509_STORE_create((char *)sc->szCARevocationFile,
                              (char *)sc->szCARevocationPath);

    if (!sc->pRevocationStore) {
        ssl_log(s, SSL_LOG_ERROR|SSL_ADD_SSLERR|SSL_INIT,
                "Unable to configure X.509 CRL storage "
                "for certificate revocation");
        ssl_die();
    }
}

static void ssl_init_cert_chain(server_rec *s,
                                apr_pool_t *p,
                                apr_pool_t *ptemp,
                                SSLSrvConfigRec *sc)
{
    BOOL skip_first = TRUE;
    int i, n;
    const char *chain = sc->szCertificateChain;

    /* 
     * Optionally configure extra server certificate chain certificates.
     * This is usually done by OpenSSL automatically when one of the
     * server cert issuers are found under SSLCACertificatePath or in
     * SSLCACertificateFile. But because these are intended for client
     * authentication it can conflict. For instance when you use a
     * Global ID server certificate you've to send out the intermediate
     * CA certificate, too. When you would just configure this with
     * SSLCACertificateFile and also use client authentication mod_ssl
     * would accept all clients also issued by this CA. Obviously this
     * isn't what we want in this situation. So this feature here exists
     * to allow one to explicity configure CA certificates which are
     * used only for the server certificate chain.
     */
    if (!chain) {
        return;
    }

    for (i = 0; (i < SSL_AIDX_MAX) && sc->szPublicCertFiles[i]; i++) {
        if (strEQ(sc->szPublicCertFiles[i], chain)) {
            skip_first = TRUE;
            break;
        }
    }

    n = SSL_CTX_use_certificate_chain(sc->pSSLCtx,
                                      (char *)chain, 
                                      skip_first, NULL);
    if (n < 0) {
        ssl_log(s, SSL_LOG_ERROR|SSL_INIT,
                "Failed to configure CA certificate chain!");
        ssl_die();
    }

    ssl_log(s, SSL_LOG_TRACE|SSL_INIT,
            "Configuring server certificate chain "
            "(%d CA certificate%s)",
            n, n == 1 ? "" : "s");
}

static int ssl_server_import_cert(server_rec *s,
                                  SSLSrvConfigRec *sc,
                                  const char *id,
                                  int idx)
{
    SSLModConfigRec *mc = myModConfig(s);
    ssl_asn1_t *asn1;
    unsigned char *ptr;
    const char *type = ssl_asn1_keystr(idx);
    X509 *cert;

    if (!(asn1 = ssl_asn1_table_get(mc->tPublicCert, id))) {
        return FALSE;
    }

    ssl_log(s, SSL_LOG_TRACE|SSL_INIT,
            "Configuring %s server certificate", type);

    ptr = asn1->cpData;
    if (!(cert = d2i_X509(NULL, &ptr, asn1->nData))) {
        ssl_log(s, SSL_LOG_ERROR|SSL_ADD_SSLERR|SSL_INIT,
                "Unable to import %s server certificate", type);
        ssl_die();
    }

    if (SSL_CTX_use_certificate(sc->pSSLCtx, cert) <= 0) {
        ssl_log(s, SSL_LOG_ERROR|SSL_ADD_SSLERR|SSL_INIT,
                "Unable to configure %s server certificate", type);
        ssl_die();
    }

    sc->pPublicCert[idx] = cert;

    return TRUE;
}

static int ssl_server_import_key(server_rec *s,
                                 SSLSrvConfigRec *sc,
                                 const char *id,
                                 int idx)
{
    SSLModConfigRec *mc = myModConfig(s);
    ssl_asn1_t *asn1;
    unsigned char *ptr;
    const char *type = ssl_asn1_keystr(idx);
    int pkey_type = (idx == SSL_AIDX_RSA) ? EVP_PKEY_RSA : EVP_PKEY_DSA;
    EVP_PKEY *pkey;

    if (!(asn1 = ssl_asn1_table_get(mc->tPrivateKey, id))) {
        return FALSE;
    }

    ssl_log(s, SSL_LOG_TRACE|SSL_INIT,
            "Configuring %s server private key", type);

    ptr = asn1->cpData;
    if (!(pkey = d2i_PrivateKey(pkey_type, NULL, &ptr, asn1->nData)))
    {
        ssl_log(s, SSL_LOG_ERROR|SSL_ADD_SSLERR|SSL_INIT,
                "Unable to import %s server private key", type);
        ssl_die();
    }

    if (SSL_CTX_use_PrivateKey(sc->pSSLCtx, pkey) <= 0) {
        ssl_log(s, SSL_LOG_ERROR|SSL_ADD_SSLERR|SSL_INIT,
                "Unable to configure %s server private key", type);
        ssl_die();
    }

    sc->pPrivateKey[idx] = pkey;

    return TRUE;
}

static void ssl_check_public_cert(server_rec *s,
                                  apr_pool_t *ptemp,
                                  X509 *cert,
                                  int type)
{
    int is_ca, pathlen;
    char *cn;

    if (!cert) {
        return;
    }

    /*
     * Some information about the certificate(s)
     */

    if (SSL_X509_isSGC(cert)) {
        ssl_log(s, SSL_LOG_INFO|SSL_INIT,
                "%s server certificate enables "
                "Server Gated Cryptography (SGC)", 
                ssl_asn1_keystr(type));
    }

    if (SSL_X509_getBC(cert, &is_ca, &pathlen)) {
        if (is_ca) {
            ssl_log(s, SSL_LOG_WARN|SSL_INIT,
                    "%s server certificate is a CA certificate "
                    "(BasicConstraints: CA == TRUE !?)",
                    ssl_asn1_keystr(type));
        }

        if (pathlen > 0) {
            ssl_log(s, SSL_LOG_WARN|SSL_INIT,
                    "%s server certificate is not a leaf certificate "
                    "(BasicConstraints: pathlen == %d > 0 !?)",
                    ssl_asn1_keystr(type), pathlen);
        }
    }

    if (SSL_X509_getCN(ptemp, cert, &cn)) {
        int fnm_flags = FNM_PERIOD|FNM_CASE_BLIND;

        if (apr_is_fnmatch(cn) &&
            (apr_fnmatch(cn, s->server_hostname,
                         fnm_flags) == FNM_NOMATCH))
        {
            ssl_log(s, SSL_LOG_WARN|SSL_INIT,
                    "%s server certificate wildcard CommonName (CN) `%s' "
                    "does NOT match server name!?",
                    ssl_asn1_keystr(type), cn);
        }
        else if (strNE(s->server_hostname, cn)) {
            ssl_log(s, SSL_LOG_WARN|SSL_INIT,
                    "%s server certificate CommonName (CN) `%s' "
                    "does NOT match server name!?",
                    ssl_asn1_keystr(type), cn);
        }
    }
}

/*
 * Configure a particular server
 */
void ssl_init_ConfigureServer(server_rec *s,
                              apr_pool_t *p,
                              apr_pool_t *ptemp,
                              SSLSrvConfigRec *sc)
{
    const char *rsa_id, *dsa_id;
    const char *vhost_id = sc->szVHostID;
    EVP_PKEY *pkey;
    SSL_CTX *ctx;
    int i;
    int have_rsa, have_dsa;

    ssl_init_check_server(s, p, ptemp, sc);

    ctx = ssl_init_ctx(s, p, ptemp, sc);

    ssl_init_session_cache_ctx(s, p, ptemp, sc);

    ssl_init_verify(s, p, ptemp, sc);

    ssl_init_cipher_suite(s, p, ptemp, sc);

    ssl_init_crl(s, p, ptemp, sc);

    ssl_init_cert_chain(s, p, ptemp, sc);

    SSL_CTX_set_tmp_rsa_callback(ctx, ssl_callback_TmpRSA);
    SSL_CTX_set_tmp_dh_callback(ctx,  ssl_callback_TmpDH);

    if (sc->nLogLevel >= SSL_LOG_INFO) {
        /* this callback only logs if SSLLogLevel >= info */
        SSL_CTX_set_info_callback(ctx, ssl_callback_LogTracingState);
    }

    /*
     *  Configure server certificate(s)
     */
    rsa_id = ssl_asn1_table_keyfmt(ptemp, vhost_id, SSL_AIDX_RSA);
    dsa_id = ssl_asn1_table_keyfmt(ptemp, vhost_id, SSL_AIDX_DSA);

    have_rsa = ssl_server_import_cert(s, sc, rsa_id, SSL_AIDX_RSA);
    have_dsa = ssl_server_import_cert(s, sc, dsa_id, SSL_AIDX_DSA);

    if (!(have_rsa || have_dsa)) {
        ssl_log(s, SSL_LOG_ERROR|SSL_INIT,
                "Oops, no RSA or DSA server certificate found?!");
        ssl_log(s, SSL_LOG_ERROR|SSL_INIT,
                "You have to perform a *full* server restart "
                "when you added or removed a certificate and/or key file");
        ssl_die();
    }

    for (i = 0; i < SSL_AIDX_MAX; i++) {
        ssl_check_public_cert(s, ptemp, sc->pPublicCert[i], i);
    }

    have_rsa = ssl_server_import_key(s, sc, rsa_id, SSL_AIDX_RSA);
    have_dsa = ssl_server_import_key(s, sc, dsa_id, SSL_AIDX_DSA);

    if (!(have_rsa || have_dsa)) {
        ssl_log(s, SSL_LOG_ERROR|SSL_INIT,
                "Oops, no RSA or DSA server private key found?!");
        ssl_die();
    }

    /*
     * Optionally copy DSA parameters for certificate from private key
     * (see http://www.psy.uq.edu.au/~ftp/Crypto/ssleay/TODO.html)
     */
    if (sc->pPublicCert[SSL_AIDX_DSA] &&
        sc->pPrivateKey[SSL_AIDX_DSA])
    {
        pkey = X509_get_pubkey(sc->pPublicCert[SSL_AIDX_DSA]);

        if (pkey && (EVP_PKEY_key_type(pkey) == EVP_PKEY_DSA) &&
            EVP_PKEY_missing_parameters(pkey))
        {
            EVP_PKEY_copy_parameters(pkey,
                                     sc->pPrivateKey[SSL_AIDX_DSA]);
        }
    }
}

void ssl_init_CheckServers(server_rec *base_server, apr_pool_t *p)
{
    server_rec *s, *ps;
    SSLSrvConfigRec *sc;
    apr_hash_t *table;
    const char *key;
    apr_ssize_t klen;

    BOOL conflict = FALSE;

    /*
     * Give out warnings when a server has HTTPS configured 
     * for the HTTP port or vice versa
     */
    for (s = base_server; s; s = s->next) {
        sc = mySrvConfig(s);

        if (sc->bEnabled && (s->port == DEFAULT_HTTP_PORT)) {
            ssl_log(base_server, SSL_LOG_WARN,
                    "Init: (%s) You configured HTTPS(%d) "
                    "on the standard HTTP(%d) port!",
                    ssl_util_vhostid(p, s),
                    DEFAULT_HTTPS_PORT, DEFAULT_HTTP_PORT);
        }

        if (!sc->bEnabled && (s->port == DEFAULT_HTTPS_PORT)) {
            ssl_log(base_server, SSL_LOG_WARN,
                    "Init: (%s) You configured HTTP(%d) "
                    "on the standard HTTPS(%d) port!",
                    ssl_util_vhostid(p, s),
                    DEFAULT_HTTP_PORT, DEFAULT_HTTPS_PORT);
        }
    }

    /*
     * Give out warnings when more than one SSL-aware virtual server uses the
     * same IP:port. This doesn't work because mod_ssl then will always use
     * just the certificate/keys of one virtual host (which one cannot be said
     * easily - but that doesn't matter here).
     */
    table = apr_hash_make(p);

    for (s = base_server; s; s = s->next) {
        sc = mySrvConfig(s);

        if (!sc->bEnabled) {
            continue;
        }

        key = apr_psprintf(p, "%pA:%u",
                           &s->addrs->host_addr, s->addrs->host_port);
        klen = strlen(key);

        if ((ps = (server_rec *)apr_hash_get(table, key, klen))) {
            ssl_log(base_server, SSL_LOG_WARN,
                    "Init: SSL server IP/port conflict: "
                    "%s (%s:%d) vs. %s (%s:%d)",
                    ssl_util_vhostid(p, s), 
                    (s->defn_name ? s->defn_name : "unknown"),
                    s->defn_line_number,
                    ssl_util_vhostid(p, ps),
                    (ps->defn_name ? ps->defn_name : "unknown"), 
                    ps->defn_line_number);
            conflict = TRUE;
            continue;
        }

        apr_hash_set(table, key, klen, s);
    }

    if (conflict) {
        ssl_log(base_server, SSL_LOG_WARN,
                "Init: You should not use name-based "
                "virtual hosts in conjunction with SSL!!");
    }
}

static int ssl_init_FindCAList_X509NameCmp(X509_NAME **a, X509_NAME **b)
{
    return(X509_NAME_cmp(*a, *b));
}

static void ssl_init_PushCAList(STACK_OF(X509_NAME) *ca_list,
                                server_rec *s, const char *file)
{
    int n;
    STACK_OF(X509_NAME) *sk;

    sk = (STACK_OF(X509_NAME) *)SSL_load_client_CA_file(file);

    if (!sk) {
        return;
    }

    for (n = 0; n < sk_X509_NAME_num(sk); n++) {
        char name_buf[256];
        X509_NAME *name = sk_X509_NAME_value(sk, n);

        ssl_log(s, SSL_LOG_TRACE,
                "CA certificate: %s",
                X509_NAME_oneline(name, name_buf, sizeof(name_buf)));

        /*
         * note that SSL_load_client_CA_file() checks for duplicates,
         * but since we call it multiple times when reading a directory
         * we must also check for duplicates ourselves.
         */

        if (sk_X509_NAME_find(ca_list, name) < 0) {
            /* this will be freed when ca_list is */
            sk_X509_NAME_push(ca_list, name);
        }
        else {
            /* need to free this ourselves, else it will leak */
            X509_NAME_free(name);
        }
    }

    sk_X509_NAME_free(sk);
}

STACK_OF(X509_NAME) *ssl_init_FindCAList(server_rec *s,
                                         apr_pool_t *ptemp,
                                         const char *ca_file,
                                         const char *ca_path)
{
    STACK_OF(X509_NAME) *ca_list;

    /*
     * Start with a empty stack/list where new
     * entries get added in sorted order.
     */
    ca_list = sk_X509_NAME_new(ssl_init_FindCAList_X509NameCmp);

    /*
     * Process CA certificate bundle file
     */
    if (ca_file) {
        ssl_init_PushCAList(ca_list, s, ca_file);
    }

    /*
     * Process CA certificate path files
     */
    if (ca_path) {
        apr_dir_t *dir;
        apr_finfo_t direntry;
        apr_int32_t finfo_flags = APR_FINFO_MIN|APR_FINFO_NAME;

        if (apr_dir_open(&dir, ca_path, ptemp) != APR_SUCCESS) {
            ssl_log(s, SSL_LOG_ERROR|SSL_ADD_ERRNO|SSL_INIT,
                    "Failed to open SSLCACertificatePath `%s'",
                    ca_path);
            ssl_die();
        }

        while ((apr_dir_read(&direntry, finfo_flags, dir)) == APR_SUCCESS) {
            const char *file;
            if (direntry.filetype == APR_DIR) {
                continue; /* don't try to load directories */
            }
            file = apr_pstrcat(ptemp, ca_path, "/", direntry.name, NULL);
            ssl_init_PushCAList(ca_list, s, file);
        }

        apr_dir_close(dir);
    }

    /*
     * Cleanup
     */
    sk_X509_NAME_set_cmp_func(ca_list, NULL);

    return ca_list;
}

void ssl_init_Child(apr_pool_t *p, server_rec *s)
{
    SSLModConfigRec *mc = myModConfig(s);
    mc->pid = getpid(); /* only call getpid() once per-process */

    /* XXX: there should be an ap_srand() function */
    srand((unsigned int)time(NULL));

    /* open the mutex lockfile */
    ssl_mutex_reinit(s, p);
}

#define MODSSL_CFG_ITEM_FREE(func, item) \
    if (item) { \
        func(item); \
        item = NULL; \
    }

apr_status_t ssl_init_ModuleKill(void *data)
{
    SSLSrvConfigRec *sc;
    server_rec *base_server = (server_rec *)data;
    server_rec *s;

    /*
     * Drop the session cache and mutex
     */
    ssl_scache_kill(base_server);

    /* 
     * Destroy the temporary keys and params
     */
    ssl_tmp_keys_free(base_server);

    /*
     * Free the non-pool allocated structures
     * in the per-server configurations
     */
    for (s = base_server; s; s = s->next) {
        int i;
        sc = mySrvConfig(s);

        for (i=0; i < SSL_AIDX_MAX; i++) {
            MODSSL_CFG_ITEM_FREE(X509_free,
                                 sc->pPublicCert[i]);

            MODSSL_CFG_ITEM_FREE(EVP_PKEY_free,
                                 sc->pPrivateKey[i]);
        }

        MODSSL_CFG_ITEM_FREE(X509_STORE_free,
                             sc->pRevocationStore);

        MODSSL_CFG_ITEM_FREE(SSL_CTX_free,
                             sc->pSSLCtx);
    }

    /*
     * Try to kill the internals of the SSL library.
     */
    ERR_free_strings();
    ERR_remove_state(0);
    EVP_cleanup();

    return APR_SUCCESS;
}

