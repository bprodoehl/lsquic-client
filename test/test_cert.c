/* Copyright (c) 2017 LiteSpeed Technologies Inc.  See LICENSE. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/ssl.h>

#include "lsquic_types.h"
#include "lsquic.h"
#include "../src/liblsquic/lsquic_logger.h"
#include "../src/liblsquic/lsquic_hash.h"

#include "test_cert.h"

int
load_cert (struct lsquic_hash *certs, const char *optarg)
{
    int rv = -1;
    char *sni, *cert_file, *key_file;
    struct server_cert *cert = NULL;
    EVP_PKEY *pkey = NULL;
    FILE *f = NULL;

    sni = strdup(optarg);
    cert_file = strchr(sni, ',');
    if (!cert_file)
        goto end;
    *cert_file = '\0';
    ++cert_file;
    key_file = strchr(cert_file, ',');
    if (!key_file)
        goto end;
    *key_file = '\0';
    ++key_file;

    cert = malloc(sizeof(*cert));
    cert->ce_sni = strdup(sni);

    cert->ce_ssl_ctx = SSL_CTX_new(SSLv23_server_method());
    if (!cert->ce_ssl_ctx)
    {
        LSQ_ERROR("SSL_CTX_new failed");
        goto end;
    }
    if (1 != SSL_CTX_use_certificate_chain_file(cert->ce_ssl_ctx, cert_file))
    {
        LSQ_ERROR("SSL_CTX_use_certificate_chain_file failed: %s", cert_file);
        goto end;
    }

    if (strstr(key_file, ".pkcs8"))
    {
        f = fopen(key_file, "r");
        if (!f)
        {
            LSQ_ERROR("fopen(%s) failed: %s", cert_file, strerror(errno));
            goto end;
        }
        pkey = d2i_PrivateKey_fp(f, NULL);
        fclose(f);
        f = NULL;
        if (!pkey)
        {
            LSQ_ERROR("Reading private key from %s failed", key_file);
            goto end;
        }
        if (!SSL_CTX_use_PrivateKey(cert->ce_ssl_ctx, pkey))
        {
            LSQ_ERROR("SSL_CTX_use_PrivateKey failed");
            goto end;
        }
    }
    else if (1 != SSL_CTX_use_PrivateKey_file(cert->ce_ssl_ctx, key_file,
                                                            SSL_FILETYPE_PEM))
    {
        LSQ_ERROR("SSL_CTX_use_PrivateKey_file failed");
        goto end;
    }

    lsquic_hash_insert(certs, cert->ce_sni, strlen(cert->ce_sni), cert);
    rv = 0;

  end:
    free(sni);
    if (f)
        fclose(f);
    if (rv != 0)
    {   /* Error: free cert and its components */
        if (cert)
        {
            free(cert->ce_sni);
            free(cert);
        }
    }
    return rv;
}

struct ssl_ctx_st *
lookup_cert (void *cert_lu_ctx, const struct sockaddr *sa_UNUSED,
             const char *sni)
{
    if (!cert_lu_ctx)
        return NULL;
    struct lsquic_hash_elem *el = lsquic_hash_find(cert_lu_ctx, sni, strlen(sni));
    struct server_cert *server_cert = NULL;
    if (el)
    {
        server_cert = lsquic_hashelem_getdata(el);
        if (server_cert)
            return server_cert->ce_ssl_ctx;
    }
    return NULL;
}


void
delete_certs (struct lsquic_hash *certs)
{
    struct lsquic_hash_elem *el;
    struct server_cert *cert;

    for (el = lsquic_hash_first(certs); el; el = lsquic_hash_next(certs))
    {
        cert = lsquic_hashelem_getdata(el);
        SSL_CTX_free(cert->ce_ssl_ctx);
        free(cert->ce_sni);
        free(cert);
    }
    lsquic_hash_destroy(certs);
}
