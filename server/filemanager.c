#include <jansson.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <syslog.h>

#ifndef ROOT
#define ROOT ""
#endif
#ifndef LOG
#define LOG ""
#endif

#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)(-1))
#endif

#define INITBUF 500

typedef struct strlist {
    char *value;
    struct strlist *next;
} strlist_t;

typedef struct context {
    char *root;
    char *log;
    char **query;
} context_t;

void logmsg(context_t *ctx, char *fmt, ...) {
    if (ctx->log) {
        char *buf = malloc(INITBUF);
        va_list va;
        va_start(va, fmt);
        int len = vsnprintf(buf, INITBUF - 2, fmt, va);
        va_end(va);
        if (len >= INITBUF - 2) {
            free(buf);
            buf = malloc(len + 5);
            va_start(va, fmt);
            len = vsnprintf(buf, len + 5, fmt, va);
            va_end(va);
        }
        if (!strcmp(ctx->log, "syslog")) {
            buf[len++] = 0;
            syslog(LOG_USER|LOG_INFO, "%s", buf);
        } else {
            buf[len++] = '\n';
            buf[len++] = 0;
            int fd = open(ctx->log, O_CREAT|O_WRONLY|O_APPEND, 0666);
            write(fd, buf, strlen(buf));
            close(fd);
        }
    }
}

void help(context_t *ctx) {
    printf("\nUsage: \"filemanager\" runs as a cgi-script.\n");
    printf("No REQUEST_METHOD environment variable detected, so this is not a CGI environment\n\n");
    printf("  --root <dir>           specify the root directory for files. Must be writable\n");
    printf("  --log <file|\"syslog\">  specify file to write log messages to, or syslog. optional\n");
    printf("  --path <dir>           (for non-CGI debugging) specify the PATH_INFO variable\n");
    printf("  --query <dir>          (for non-CGI debugging) specify the QUERY_STRING variable\n");
    printf("Also override root directory and log-file with ROOT and LOG environments variable\n");
    if (ctx->root) {
        printf("  Default root directory: \"%s\"\n", ctx->root);
    } else {
        printf("  Default root directory: unspecified\n");
    }
    if (ctx->log) {
        printf("  Default logfile: \"%s\"\n", ctx->log);
    } else {
        printf("  Default logfile: unspecified\n");
    }
    printf("\n");
    exit(0);
}

/**
 * Given a query string, return an array of strings [key,value,key,value,...,0]
 * each individual string should be freed when done, as should the return value
 */
char **parse_querystring(char *s) {
    char **out;
    int count = 0;
    if (s && *s) {
        count = 1;
        for (char *c=s;*c;c++) {
            if (*c == '&') {
                count++;
            }
        }
    }
    out = calloc(sizeof(char*), count * 2 + 1);
    if (count > 0) {
        char **z = out;
        char *in = s, *in0 = s;
        char boundary = '=';
        do {
            if (!*in || *in == boundary || *in == '&') {
                char *e = in;
                char *out = *z = malloc(in - in0 + 1);
                for (in=in0;in!=e;in++) {
                    if (*in == '+') {
                        *out++ = ' ';
                    } else if (*in == '%') {
                        int v = *(in + 1);
                        if (v >= '0' && v <= '9') {
                            v = v - '0';
                        } else if (v >= 'a' && v <= 'f') {
                            v = v - 'a' + 10;
                        } else if (v >= 'A' && v <= 'F') {
                            v = v - 'A' + 10;
                        } else {
                            *out++ = '%';
                            *out++ = *(++in);
                            continue;
                        }
                        int v1 = *(in + 2);
                        if (v1 >= '0' && v1 <= '9') {
                            v = (v << 4) | (v1 - '0');
                        } else if (v1 >= 'a' && v1 <= 'f') {
                            v = (v << 4) | (v1 - 'a' + 10);
                        } else if (v1 >= 'A' && v1 <= 'F') {
                            v = (v << 4) | (v1 - 'A' + 10);
                        } else {
                            *out++ = '%';
                            *out++ = *(++in);
                            *out++ = *(++in);
                            continue;
                        }
                        *out++ = v;
                        in += 2;
                    } else {
                        *out++ = *in;
                    }
                }
                if (boundary == '=' && (!*e || *e == '&')) { // Malformed, create a dummy value
                    z++;
                    *z = malloc(1);
                    **z = 0;
                }
                boundary = boundary == '=' ? '&' : '=';
                in0 = *e ? ++e : e;
                z++;
            }
            in++;
        } while (*in0);
    }
    return out;
}


void send(context_t *ctx, int code, json_t *json) {
    char *s = json ? json_dumps(json, 0) : NULL;
    logmsg(ctx, "tx %d %s", code, s);
    printf("Status: %d\r\n", code);
    if (s) {
        printf("Content-Type: application/json\r\n");
        printf("Content-Length: %lu\r\n", (long unsigned int)strlen(s));
        fputs("\r\n", stdout);
        fputs(s, stdout);
        free(s);
    } else {
        fputs("\r\n", stdout);
    }
}

void sendmsg(context_t *ctx, int code, char *fmt, ...) {
    json_t *j = json_object();
    json_object_set_new(j, "ok", json_boolean(code >= 200 && code < 300));
    if (fmt) {
        char *buf = malloc(INITBUF);
        va_list va; 
        va_start(va, fmt);
        int len = vsnprintf(buf, INITBUF, fmt, va);
        va_end(va);
        if (len > INITBUF) {
            free(buf);
            buf = malloc(len + 2);
            va_start(va, fmt);
            vsnprintf(buf, len + 2, fmt, va);
            va_end(va);
        }
        json_object_set_new(j, "msg", json_string(buf));
    }
    send(ctx, code, j);
    json_decref(j);
}

void info(context_t *ctx) {
    json_t *a = json_array();
    struct stat sb;
    int found = 0;
    for (char **q=ctx->query;*q;) {
        char *qkey = *q++;
        char *qval = *q++;
        if (!strcmp(qkey, "path")) {
            found = 1;
            char *path = calloc(strlen(ctx->root) + strlen(qval) + 2, 1);
            if (strlen(qval)) {
                sprintf(path, "%s/%s", ctx->root, qval[0] == '/' ? qval+1 : qval);
            } else {
                strcpy(path, ctx->root);
            }
            if (!strstr(path, "/.") && !stat(path, &sb)) {
                DIR *dir;
                json_t *j = NULL;
                if (S_ISDIR(sb.st_mode) && (dir=opendir(path)) != NULL) {
                    j = json_object();
                    json_object_set_new(j, "path", json_string(qval));
                    json_object_set_new(j, "type", json_string("dir"));
                    if (access(path, W_OK)) {
                        json_object_set_new(j, "readonly", json_true());
                    }
                    json_object_set_new(j, "ctime", json_integer((json_int_t)sb.st_ctime));
                    json_object_set_new(j, "mtime", json_integer((json_int_t)sb.st_mtime));
                    json_t *c = json_array();
                    json_object_set_new(j, "kids", c);
                    struct dirent *dp;
                    int path2len = 100;
                    char *path2 = calloc(strlen(path) + path2len + 3, 1);
                    while ((dp = readdir(dir))) {
                        if (dp->d_name[0] != '.') {
                            if (strlen(dp->d_name) > path2len) {
                                free(path2);
                                path2len = strlen(dp->d_name);
                                path2 = calloc(strlen(path) + path2len + 3, 1);
                            }
                            sprintf(path2, "%s/%s", path, dp->d_name);
                            if (!stat(path2, &sb) && !access(path2, S_ISDIR(sb.st_mode) ? R_OK|X_OK : R_OK)) {
                                json_array_append_new(c, json_string(dp->d_name));
                            }
                        }
                    }
                    closedir(dir);
                    free(path2);
                } else if (S_ISREG(sb.st_mode) && !access(path, R_OK)) {
                    j = json_object();
                    json_object_set_new(j, "path", json_string(qval));
                    json_object_set_new(j, "type", json_string("file"));
                    if (access(path, W_OK)) {
                        json_object_set_new(j, "readonly", json_true());
                    }
                    json_object_set_new(j, "ctime", json_integer((json_int_t)sb.st_ctime));
                    json_object_set_new(j, "ctime", json_integer((json_int_t)sb.st_mtime));
                    json_object_set_new(j, "length", json_integer((json_int_t)sb.st_size));
                }
                if (j) {
                    json_array_append_new(a, j);
                }
            }
        }
    }
    if (!found) {
        json_decref(a);
        void *old = ctx->query;
        ctx->query = parse_querystring("path=");
        info(ctx);
        for (char **q=ctx->query;*q;q++) {
            free(*q);
        }
        free(ctx->query);
        ctx->query = old;
    } else {
        json_t *o = json_object();
        json_object_set_new(o, "ok", json_true());
        json_object_set_new(o, "paths", a);
        send(ctx, 200, o);
        json_decref(o);
    }
}

void get(context_t *ctx) {
    struct stat sb;
    for (char **q=ctx->query;*q;) {
        char *qkey = *q++;
        char *qval = *q++;
        if (!strcmp(qkey, "path")) {
            const char *name = qval;
            if (*name == '/') {
                name++;
            }
            if (name[0] == 0 || name[0] == '.' || strstr(name, "/.")) {
                sendmsg(ctx, 400, "invalid path \"%s\"", name);
                return;
            } else {
                char *path = calloc(strlen(ctx->root) + strlen(name) + 2, 1);
                sprintf(path, "%s/%s", ctx->root, name);
                if (stat(path, &sb)) {
                    logmsg(ctx, "get stat \"%s\": %s", path, strerror(errno));
                    sendmsg(ctx, 404, "get stat \"%s\": %s", name, strerror(errno));
                    return;
                } else {
                    int fd = open(path, O_RDONLY);
                    if (fd < 0) {
                        logmsg(ctx, "get open \"%s\": %s", path, strerror(errno));
                        sendmsg(ctx, 404, "get open: %s", strerror(errno));
                        return;
                    } else {
                        printf("Content-Type: application/octet-stream\r\n");
                        printf("Content-Length: %lu\r\n", sb.st_size);
                        fputs("\r\n", stdout);
                        char buf[32768];
                        int l;
                        while ((l=read(fd, buf, sizeof(buf))) > 0) {
                            write(STDOUT_FILENO, buf, l);
                        }
                        close(fd);
                    }
                    return;
                }
                free(path);
            }
        }
    }
    sendmsg(ctx, 400, "missing path");
}

void put(context_t *ctx) {
    size_t off = SIZE_MAX;
    char *path = NULL;
    struct stat sb;
    memset(&sb, 0, sizeof(sb));
    char buf[65536];

    for (char **q=ctx->query;*q;) {
        char *qkey = *q++;
        char *qval = *q++;
        if (!strcmp(qkey, "path") && !path) {
            const char *name = qval;
            if (*name == '/') {
                name++;
            }
            if (name[0] == 0 || name[0] == '.' || strstr(name, "/.")) {
                sendmsg(ctx, 400, "invalid path \"%s\"", name);
                return;
            } else {
                path = calloc(strlen(ctx->root) + strlen(name) + 2, 1);
                sprintf(path, "%s/%s", ctx->root, name);
            }
        } else if (!strcmp(qkey, "off") && *(qval) && off == SIZE_MAX) {
            char *c;
            off = strtoul(qval, &c, 10);
            if (*c) {
                sendmsg(ctx, 400, "invalid off \"%s\"", qval);
                return;
            }
        }
    }
    int fd = 0;
    if (!path) {
        sendmsg(ctx, 400, "missing path");
    } else if ((access(path, F_OK) || !access(path, W_OK)) && (off == SIZE_MAX || off == 0)) {
        fd = O_CREAT|O_WRONLY|O_TRUNC;
    } else if (access(path, W_OK)) {
        logmsg(ctx, "put access \"%s\": not writable", path);
        sendmsg(ctx, 403, "not writable: %s", strerror(errno));
    } else if (stat(path, &sb)) {
        logmsg(ctx, "put stat \"%s\": %s", path, strerror(errno));
        sendmsg(ctx, 500, "put stat: %s", strerror(errno));
    } else if (!S_ISREG(sb.st_mode)) {
        sendmsg(ctx, 403, "not a file");
    } else if (off != sb.st_size) {
        sendmsg(ctx, 400, "offset %lu should be %lu", off, sb.st_size);
    } else {
        fd = O_APPEND|O_WRONLY;
    }
    if (fd) {
        for (char *c=path;*c;c++) {
            if (*c == '/' && c != path) {
                *c = 0;
                mkdir(path, 0777);
                *c = '/';
            }
        }
        fd = open(path, fd, 0666);
        if (fd < 0) {
            sendmsg(ctx, 403, "put open: %s", strerror(errno));
        } else {
            int l;
            size_t count = 0;
            while ((l=read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
                write(fd, buf, l);
                count += l;
            }
            close(fd);
            sendmsg(ctx, 200, "wrote %d bytes", count);
        }
    }
}

void domkdir(context_t *ctx) {
    for (char **q=ctx->query;*q;) {
        char *qkey = *q++;
        char *qval = *q++;
        if (!strcmp(qkey, "path") && qval) {
            const char *name = qval;
            if (name[0] == '/') {
                name++;
            }
            if (name[0] == 0 || name[0] == '.' || strstr(name, "/.")) {
                sendmsg(ctx, 400, "invalid path \"%s\"", name);
                return;
            } else {
                char *path = calloc(strlen(ctx->root) + strlen(name) + 2, 1);
                sprintf(path, "%s/%s", ctx->root, name);
                if (!strstr(path, "/.")) {
                    sendmsg(ctx, 400, "invalid path");
                } else if (!access(path, F_OK)) {
                    logmsg(ctx, "mkdir \"%s\": path exists", path);
                    sendmsg(ctx, 403, "mkdir: path exists");
                } else if (mkdir(path, 0777)) {
                    logmsg(ctx, "mkdir \"%s\": %s", path, strerror(errno));
                    sendmsg(ctx, 403, "mkdir: %s", strerror(errno));
                } else {
                    sendmsg(ctx, 200, "mkdir \"%s\"", name);
                }
                return;
            }
        }
    }
    sendmsg(ctx, 400, "missing path");
}


static strlist_t *free_strlist(strlist_t *n) {
    if (n) {
        strlist_t *start = n;
        do {
            free(n->value);
            strlist_t *next = n->next;
            free(n);
            n = next;
        } while (n && n != start);
    }
    return NULL;
}

/**
 * Traverse everything under "path" and add to end of list,
 * allocating if necessary. Files ending in "/" are directories.
 * If anything can't be traversed, send an error message and return null
 * returned list is a loop
 */
void traverse(context_t *ctx, const char *path, strlist_t **start, strlist_t **end) {
    struct stat sb;
    char *buf = calloc(strlen(ctx->root) + strlen(path) + 3, 1);
    if (*path == 0) {
        sprintf(buf, "%s", ctx->root);
    } else {
        sprintf(buf, "%s/%s", ctx->root, path);
    }
    if (stat(buf, &sb)) {
        logmsg(ctx, "traverse stat \"%s\": %s", path, strerror(errno));
        sendmsg(ctx, 500, "traverse stat \"%s\": %s", path, strerror(errno));
        *start = *end = free_strlist(*start);
    } else {
        if (S_ISDIR(sb.st_mode)) {
            DIR *dir = opendir(buf);
            if (!dir) {
                logmsg(ctx, "opendir \"%s\": %s", path, strerror(errno));
                sendmsg(ctx, 500, "opendir \"%s\": %s", path, strerror(errno));
                *start = *end = free_strlist(*start);
            } else {
                struct dirent *dp;
                while ((dp = readdir(dir))) {
                    if (strcmp(dp->d_name, ".") && strcmp(dp->d_name, "..")) {
                        char *buf2 = calloc(strlen(path) + strlen(dp->d_name) + 2, 1);
                        sprintf(buf2, "%s/%s", path, dp->d_name);
                        traverse(ctx, buf2, start, end);
                        free(buf2);
                        if (!*end) {
                            return;
                        }
                    }
                }
            }
            buf[strlen(buf)] = '/';
        }
        strlist_t *next = calloc(sizeof(strlist_t), 1);
        next->value = buf;
        if (!*end) {
            *start = *end = next;
        } else {
            (*end)->next = next;
            *end = next;
        }
    }
}

void delete(context_t *ctx) {
    strlist_t *names = NULL, *names_end = NULL;

    for (char **q=ctx->query;*q;) {
        char *qkey = *q++;
        char *qval = *q++;
        if (!strcmp(qkey, "path") && qval) {
            const char *name = qval;
            if (name[0] == '/') {
                name++;
            }
            if (name[0] == 0 || name[0] == '.' || strstr(name, "/.")) {
                sendmsg(ctx, 400, "invalid path \"%s\"", name);
                return;
            } else {
                traverse(ctx, name, &names, &names_end);
            }
        }
    }
    for (strlist_t *n=names;n;n=n->next) {
        char *path = n->value;
        char *relpath = path + strlen(ctx->root) + 1;
        if (path[strlen(path) - 1] == '/') {    // directory
            path[strlen(path) - 1] = 0;
            if (access(path, R_OK|X_OK|W_OK)) {
                logmsg(ctx, "delete access dir \"%s\": not writable", path);
                sendmsg(ctx, 403, "delete not writable \"%s\"", relpath);
                names = free_strlist(names);
                break;
            }
            path[strlen(path)] = '/';
        } else if (strstr(relpath, "/.")) {     // directory contains .file
            *(rindex(relpath, '/')) = 0;
            sendmsg(ctx, 400, "directory not empty \"%s\"", relpath);
            names = free_strlist(names);
            break;
        } else if (access(path, R_OK|W_OK)) {
            logmsg(ctx, "delete access file \"%s\": not writable", path);
            sendmsg(ctx, 400, "delete not writable \"%s\"", relpath);
            names = free_strlist(names);
            break;
        }
    }
    if (names) {
        json_t *a = json_array();
        for (strlist_t *n=names;n;n=n->next) {
            char *path = n->value;
            char *relpath = path + strlen(ctx->root) + 1;
            if (path[strlen(path) - 1] == '/') {    // directory
                if (rmdir(path)) {
                    logmsg(ctx, "rmdir \"%s\": %s", path, strerror(errno));
                    sendmsg(ctx, 500, "rmdir \"%s\": %s", relpath, strerror(errno));
                    names = free_strlist(names);
                    json_decref(a);
                    break;
                } else {
                    json_array_append_new(a, json_string(relpath));
                }
            } else {
                if (unlink(path)) {
                    logmsg(ctx, "unlink \"%s\": %s", path, strerror(errno));
                    sendmsg(ctx, 500, "unlink \"%s\": %s", relpath, strerror(errno));
                    names = free_strlist(names);
                    json_decref(a);
                    break;
                } else {
                    json_array_append_new(a, json_string(relpath));
                }
            }
        }
        if (names) {
            json_t *o = json_object();
            json_object_set_new(o, "ok", json_true());
            json_object_set_new(o, "paths", a);
            send(ctx, 200, o);
            names = free_strlist(names);
            json_decref(o);
        }
    }
}

int main(int argc, char **argv) {
    context_t *ctx = calloc(sizeof(context_t), 1);
    ctx->root = ROOT "\0";
    ctx->log = LOG "\0";
    if (ctx->root && !*ctx->root) {
        ctx->root = NULL;
    }
    if (ctx->log && !*ctx->log) {
        ctx->log = NULL;
    }
    if (getenv("ROOT")) {
        ctx->root = getenv("ROOT");
    }
    if (getenv("LOG")) {
        ctx->log = getenv("LOG");
    }
    char *method = getenv("REQUEST_METHOD");
    char *path = getenv("PATH_INFO");
    char *querystring = getenv("QUERY_STRING");
    struct stat sb;
//    fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);

    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i], "--root") && i + 1 < argc) {
            ctx->root = argv[++i];
        } else if (!strcmp(argv[i], "--log") && i + 1 < argc) {
            ctx->log = argv[++i];
        } else if (!strcmp(argv[i], "--method") && i + 1 < argc) {
            method = argv[++i];
        } else if (!strcmp(argv[i], "--path") && i + 1 < argc) {
            path = argv[++i];
        } else if (!strcmp(argv[i], "--query") && i + 1 < argc) {
            querystring = argv[++i];
        } else {
            sendmsg(ctx, 500, "unknown argument \"%s\"", argv[i]);
            return 0;
        }
    }
    if (!method) {
        help(ctx);
    } else if (!ctx->root) {
        sendmsg(ctx, 500, "root directory not specified");
        return 0;
    } else if (!path) {
        sendmsg(ctx, 500, "path not specified");
        return 0;
    } else {
        logmsg(ctx, "rx: path=%s query=%s", path, querystring);
    }
    ctx->query = parse_querystring(querystring);
    if (ctx->root[strlen(ctx->root) - 1] == '/') {
        ctx->root[strlen(ctx->root) - 1] = 0;
    }
    if (!chroot(ctx->root)) {
        ctx->root = "";     // chroot if we can
    }
    if (stat(ctx->root, &sb)) {
        logmsg(ctx, "init stat \"%s\": %s", path, strerror(errno));
        sendmsg(ctx, 500, "root stat: \"%s\"", strerror(errno));
        return 0;
    } else if (!S_ISDIR(sb.st_mode)) {
        sendmsg(ctx, 500, "root directory \"%s\" is not a directory", ctx->root);
        return 0;
    }

    if (strcmp("GET", method) && strcmp("POST", method)) {
        sendmsg(ctx, 405, "method \"%s\" invalid for \"%s\"", method, path + 1);
        return 0;
    } else if (!strcmp("/info", path)) {
        info(ctx);
    } else if (!strcmp("/get", path)) {
        get(ctx);
    } else if (!strcmp("/mkdir", path) && !strcmp("POST", method)) {
        domkdir(ctx);
    } else if (!strcmp("/delete", path)) {
        delete(ctx);
    } else if (!strcmp("/put", path) && !strcmp("POST", method)) {
        if (!strcmp("POST", method)) {
            put(ctx);
        } else {
            sendmsg(ctx, 405, "method \"%s\" invalid for \"%s\"", method, path + 1);
        }
    } else {
        sendmsg(ctx, 404, "invalid script path \"%s\"", path);
    }
    for (char **q=ctx->query;*q;q++) {
        free(*q);
    }
    free(ctx->query);
    free(ctx);
}
