#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <jwt.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    ngx_flag_t enable;
    ngx_str_t env_path;
    ngx_str_t secret;
} ngx_http_jwt_loc_conf_t;

static ngx_int_t ngx_http_jwt_handler(ngx_http_request_t *r);
static void *ngx_http_jwt_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_jwt_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);
static ngx_int_t ngx_http_jwt_init(ngx_conf_t *cf);
static char *read_env_secret(ngx_pool_t *pool, const char *env_path);

static ngx_command_t ngx_http_jwt_commands[] = {
    {
        ngx_string("jwt_auth"),
        NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
        ngx_conf_set_flag_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_jwt_loc_conf_t, enable),
        NULL
    },
    {
        ngx_string("jwt_env_path"),
        NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_jwt_loc_conf_t, env_path),
        NULL
    },
    ngx_null_command
};

static ngx_http_module_t ngx_http_jwt_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_jwt_init,                    /* postconfiguration */
    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */
    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */
    ngx_http_jwt_create_loc_conf,        /* create location configuration */
    ngx_http_jwt_merge_loc_conf          /* merge location configuration */
};

ngx_module_t ngx_http_jwt_module = {
    NGX_MODULE_V1,
    &ngx_http_jwt_module_ctx,             /* module context */
    ngx_http_jwt_commands,                /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};

/* Read JWT secret from .env file */
static char *read_env_secret(ngx_pool_t *pool, const char *env_path) {
    FILE *fp;
    char line[1024];
    char *secret = NULL;

    fp = fopen(env_path, "r");
    if (fp == NULL) {
        return NULL;
    }

    while (fgets(line, sizeof(line), fp)) {
        /* Remove newline */
        line[strcspn(line, "\r\n")] = 0;

        /* Look for JWT_SECRET= */
        if (strncmp(line, "JWT_SECRET=", 11) == 0) {
            size_t secret_len = strlen(line + 11);
            secret = ngx_palloc(pool, secret_len + 1);
            if (secret) {
                ngx_memcpy(secret, line + 11, secret_len);
                secret[secret_len] = '\0';
            }
            break;
        }
    }

    fclose(fp);
    return secret;
}

/* Extract JWT token from Authorization header */
static ngx_int_t get_jwt_token(ngx_http_request_t *r, ngx_str_t *token) {
    ngx_list_part_t *part;
    ngx_table_elt_t *header;
    ngx_uint_t i;

    part = &r->headers_in.headers.part;
    header = part->elts;

    for (i = 0; /* void */; i++) {
        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }
            part = part->next;
            header = part->elts;
            i = 0;
        }

        if (header[i].key.len == 13 &&
            ngx_strncasecmp(header[i].key.data, (u_char *)"Authorization", 13) == 0) {

            /* Check for "Bearer " prefix */
            if (header[i].value.len > 7 &&
                ngx_strncasecmp(header[i].value.data, (u_char *)"Bearer ", 7) == 0) {
                token->data = header[i].value.data + 7;
                token->len = header[i].value.len - 7;
                return NGX_OK;
            }
        }
    }

    return NGX_DECLINED;
}

/* Add JWT claims as headers */
static ngx_int_t add_claims_to_headers(ngx_http_request_t *r, jwt_t *jwt) {
    json_t *grants;
    const char *key;
    json_t *value;
    void *iter;
    ngx_table_elt_t *h;
    char header_name[256];

    grants = jwt_get_grants_json(jwt, NULL);
    if (grants == NULL) {
        return NGX_ERROR;
    }

    iter = json_object_iter(grants);
    while (iter) {
        key = json_object_iter_key(iter);
        value = json_object_iter_value(iter);

        /* Create header name: X-JWT-Claim-<key> */
        ngx_snprintf((u_char *)header_name, sizeof(header_name), "X-JWT-Claim-%s", key);

        h = ngx_list_push(&r->headers_in.headers);
        if (h == NULL) {
            json_decref(grants);
            return NGX_ERROR;
        }

        /* Set header key */
        h->key.len = ngx_strlen(header_name);
        h->key.data = ngx_palloc(r->pool, h->key.len);
        if (h->key.data == NULL) {
            json_decref(grants);
            return NGX_ERROR;
        }
        ngx_memcpy(h->key.data, header_name, h->key.len);

        /* Set header value */
        if (json_is_string(value)) {
            const char *str_val = json_string_value(value);
            h->value.len = ngx_strlen(str_val);
            h->value.data = ngx_palloc(r->pool, h->value.len + 1);
            if (h->value.data == NULL) {
                json_decref(grants);
                return NGX_ERROR;
            }
            ngx_memcpy(h->value.data, str_val, h->value.len);
            h->value.data[h->value.len] = '\0';
        } else if (json_is_integer(value)) {
            long long int_val = json_integer_value(value);
            h->value.data = ngx_palloc(r->pool, 32);
            if (h->value.data == NULL) {
                json_decref(grants);
                return NGX_ERROR;
            }
            h->value.len = ngx_snprintf(h->value.data, 32, "%lld", int_val) - h->value.data;
        } else if (json_is_real(value)) {
            double real_val = json_real_value(value);
            h->value.data = ngx_palloc(r->pool, 32);
            if (h->value.data == NULL) {
                json_decref(grants);
                return NGX_ERROR;
            }
            h->value.len = ngx_snprintf(h->value.data, 32, "%f", real_val) - h->value.data;
        } else if (json_is_boolean(value)) {
            const char *bool_val = json_is_true(value) ? "true" : "false";
            h->value.len = ngx_strlen(bool_val);
            h->value.data = ngx_palloc(r->pool, h->value.len + 1);
            if (h->value.data == NULL) {
                json_decref(grants);
                return NGX_ERROR;
            }
            ngx_memcpy(h->value.data, bool_val, h->value.len);
            h->value.data[h->value.len] = '\0';
        } else {
            /* For complex types, convert to JSON string */
            char *json_str = json_dumps(value, JSON_COMPACT);
            if (json_str) {
                h->value.len = ngx_strlen(json_str);
                h->value.data = ngx_palloc(r->pool, h->value.len + 1);
                if (h->value.data == NULL) {
                    free(json_str);
                    json_decref(grants);
                    return NGX_ERROR;
                }
                ngx_memcpy(h->value.data, json_str, h->value.len);
                h->value.data[h->value.len] = '\0';
                free(json_str);
            }
        }

        h->hash = 1;

        iter = json_object_iter_next(grants, iter);
    }

    json_decref(grants);
    return NGX_OK;
}

/* Main JWT verification handler */
static ngx_int_t ngx_http_jwt_handler(ngx_http_request_t *r) {
    ngx_http_jwt_loc_conf_t *jlcf;
    ngx_str_t token;
    jwt_t *jwt = NULL;
    int ret;
    char *token_str;

    jlcf = ngx_http_get_module_loc_conf(r, ngx_http_jwt_module);

    if (!jlcf->enable) {
        return NGX_DECLINED;
    }

    /* Load secret from .env if not already loaded */
    if (jlcf->secret.len == 0) {
        const char *env_path = jlcf->env_path.len > 0 ?
            (char *)jlcf->env_path.data : ".env";

        char *secret = read_env_secret(r->pool, env_path);
        if (secret == NULL) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                         "JWT: Failed to read secret from %s", env_path);
            return NGX_HTTP_UNAUTHORIZED;
        }

        jlcf->secret.data = (u_char *)secret;
        jlcf->secret.len = ngx_strlen(secret);
    }

    /* Extract JWT token from Authorization header */
    if (get_jwt_token(r, &token) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                     "JWT: No valid Authorization header found");
        return NGX_HTTP_UNAUTHORIZED;
    }

    /* Null-terminate the token */
    token_str = ngx_palloc(r->pool, token.len + 1);
    if (token_str == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    ngx_memcpy(token_str, token.data, token.len);
    token_str[token.len] = '\0';

    /* Decode JWT */
    ret = jwt_decode(&jwt, token_str, (unsigned char *)jlcf->secret.data, jlcf->secret.len);
    if (ret != 0 || jwt == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                     "JWT: Token verification failed");
        if (jwt) {
            jwt_free(jwt);
        }
        return NGX_HTTP_UNAUTHORIZED;
    }

    /* Add claims to headers */
    if (add_claims_to_headers(r, jwt) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                     "JWT: Failed to add claims to headers");
        jwt_free(jwt);
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    jwt_free(jwt);
    return NGX_DECLINED;
}

static void *ngx_http_jwt_create_loc_conf(ngx_conf_t *cf) {
    ngx_http_jwt_loc_conf_t *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_jwt_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->enable = NGX_CONF_UNSET;
    conf->secret.len = 0;
    conf->secret.data = NULL;
    conf->env_path.len = 0;
    conf->env_path.data = NULL;

    return conf;
}

static char *ngx_http_jwt_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child) {
    ngx_http_jwt_loc_conf_t *prev = parent;
    ngx_http_jwt_loc_conf_t *conf = child;

    ngx_conf_merge_value(conf->enable, prev->enable, 0);
    ngx_conf_merge_str_value(conf->env_path, prev->env_path, ".env");

    return NGX_CONF_OK;
}

static ngx_int_t ngx_http_jwt_init(ngx_conf_t *cf) {
    ngx_http_handler_pt *h;
    ngx_http_core_main_conf_t *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_ACCESS_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_jwt_handler;

    return NGX_OK;
}
