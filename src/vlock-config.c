/* vlock-config.c -- read the vlock JSON config and emit shell-eval'able
 * `set_default VLOCK_FOO 'bar'` lines that the vlock wrapper applies.
 *
 * This is the C replacement for the jq dependency that used to do the
 * same translation; mirrors the original jq filter:
 *   general.<key>          -> VLOCK_<KEY>
 *   modules.<name>.<key>   -> VLOCK_<NAME>_<KEY>
 * Keys are upper-cased and non-[A-Z0-9_] characters become '_'.
 * Values are POSIX-shell single-quoted.
 *
 * Usage:  vlock-config <path-to-config.json>
 * On unreadable file: exit 0 silently (nothing to emit).
 * On invalid JSON:    warn to stderr, exit 0 (caller proceeds with defaults).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

static char *
slurp(const char *path)
{
    FILE   *f;
    long    size;
    char   *buf;
    size_t  got;

    f = fopen(path, "rb");
    if(f == NULL) return NULL;
    if(fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    size = ftell(f);
    if(size < 0)                    { fclose(f); return NULL; }
    rewind(f);

    buf = malloc((size_t)size + 1);
    if(buf == NULL)                 { fclose(f); return NULL; }

    got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if(got != (size_t)size)         { free(buf); return NULL; }
    buf[size] = '\0';
    return buf;
}

/* Write `prefix` verbatim followed by `key` upper-cased with any
   character outside [A-Z0-9_] replaced by '_'. */
static void
print_env_name(const char *prefix, const char *key)
{
    const char *p;

    fputs(prefix, stdout);
    for(p = key; *p != '\0'; p++)
    {
        unsigned char c = (unsigned char)*p;
        if(c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
        if(!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
            c = '_';
        putchar((int)c);
    }
}

/* POSIX-portable single-quote.  Embedded ' becomes '\''. */
static void
print_shquote(const char *s)
{
    const char *p;

    putchar('\'');
    for(p = s; *p != '\0'; p++)
    {
        if(*p == '\'') fputs("'\\''", stdout);
        else           putchar(*p);
    }
    putchar('\'');
}

static int
is_scalar(const cJSON *v)
{
    return cJSON_IsString(v) || cJSON_IsBool(v) || cJSON_IsNumber(v);
}

/* Emit the shell-quoted form of a JSON scalar. */
static void
print_scalar(const cJSON *v)
{
    if(cJSON_IsString(v))
    {
        print_shquote(v->valuestring);
        return;
    }
    if(cJSON_IsBool(v))
    {
        fputs(cJSON_IsTrue(v) ? "'true'" : "'false'", stdout);
        return;
    }
    if(cJSON_IsNumber(v))
    {
        /* Integers vs floats: cJSON stores both in valuedouble, with
           valueint clamped to int range.  Treat values whose double
           form is exactly the int as integers so "info_box":10 emits
           '10' rather than '10.000000'. */
        char    buf[64];
        double  d = v->valuedouble;

        if((double)v->valueint == d)
            snprintf(buf, sizeof buf, "'%d'", v->valueint);
        else
            snprintf(buf, sizeof buf, "'%g'", d);
        fputs(buf, stdout);
        return;
    }
    /* not a scalar -- emit empty string just to be safe */
    fputs("''", stdout);
}

/* Iterate a JSON object and emit `set_default <PREFIX><KEY> <VAL>`
   for every scalar child.  Non-scalar children (nested objects /
   arrays / null) are silently skipped, matching the jq filter. */
static void
emit_object(const char *name_prefix, const cJSON *obj)
{
    const cJSON *child;

    for(child = obj->child; child != NULL; child = child->next)
    {
        if(child->string == NULL) continue;
        if(!is_scalar(child))     continue;

        fputs("set_default ", stdout);
        print_env_name(name_prefix, child->string);
        putchar(' ');
        print_scalar(child);
        putchar('\n');
    }
}

/* Build "VLOCK_<MODNAME>_" into `out` (size `cap`), upper-casing and
   sanitising the module name the same way print_env_name does. */
static void
build_module_prefix(char *out, size_t cap, const char *modname)
{
    static const char head[] = "VLOCK_";
    size_t i = 0;
    const char *p;

    for(p = head; *p != '\0' && i + 1 < cap; p++)
        out[i++] = *p;

    for(p = modname; *p != '\0' && i + 1 < cap; p++)
    {
        unsigned char c = (unsigned char)*p;
        if(c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
        if(!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
            c = '_';
        out[i++] = (char)c;
    }

    if(i + 1 < cap) out[i++] = '_';
    out[i] = '\0';
}

int
main(int argc, char *argv[])
{
    char        *text;
    cJSON       *root, *general, *modules, *mod;

    if(argc < 2)
    {
        fprintf(stderr, "usage: %s <config.json>\n", argv[0]);
        return 2;
    }

    text = slurp(argv[1]);
    if(text == NULL) return 0;          /* missing/unreadable: nothing to emit */

    root = cJSON_Parse(text);
    free(text);
    if(root == NULL)
    {
        fprintf(stderr,
                "vlock: warning: %s is not valid JSON; ignoring it\n",
                argv[1]);
        return 0;
    }

    general = cJSON_GetObjectItemCaseSensitive(root, "general");
    if(cJSON_IsObject(general))
        emit_object("VLOCK_", general);

    modules = cJSON_GetObjectItemCaseSensitive(root, "modules");
    if(cJSON_IsObject(modules))
    {
        for(mod = modules->child; mod != NULL; mod = mod->next)
        {
            char prefix[256];

            if(mod->string == NULL)   continue;
            if(!cJSON_IsObject(mod))  continue;

            build_module_prefix(prefix, sizeof prefix, mod->string);
            emit_object(prefix, mod);
        }
    }

    cJSON_Delete(root);
    return 0;
}
