/*
 * getopt.c -- `getopt` command for ticklish, a thin wrapper around GNU
 * getopt_long(3).
 *
 * NOTE: global state
 * ------------------
 * glibc's getopt keeps its parser state in globals (optind, the pointer into
 * the current short-option bundle, the permutation bookkeeping).  This
 * command deliberately does NOT reinitialize that state (optind is never
 * reset), so:
 *   - a nested getopt inside a handler body will clobber the outer parse;
 *   - a second getopt call in the same process continues from wherever the
 *     previous one left optind, rather than starting at the first argument.
 * If that ever needs to change, set `optind = 0;` right before the parse loop
 * (glibc treats 0 as "full reinitialization").
 *
 * Syntax
 * ------
 *     getopt options argv body
 *
 * options   list of definitions; each definition is a list of aliases.
 *           A one-character alias is a short option, longer ones are long
 *           options.  A trailing ':' on ANY alias of a definition means the
 *           option takes an argument (the whole definition does).  Optional
 *           arguments ("::") are not supported.
 *               {{h help} {v version} {o: out} {dump-data} {c set-color set-colour}}
 *
 * argv      the list of arguments to parse (argv[0] is NOT included; $argv0
 *           is used as the program name in diagnostics).
 *
 * body      a list of key/script pairs, like [switch].  A key is any alias of
 *           a definition (naming two aliases of the same definition is an
 *           error), or one of the specials:
 *               ?   unknown option (also missing argument when there is no
 *                   ':' handler).  Presence of this handler turns off
 *                   getopt's built-in error messages.
 *               :   option is missing its required argument.
 *           Every key must refer to a defined option, and every defined
 *           option must have a handler (both are errors raised before any
 *           parsing).  '?' and ':' handlers are optional.
 *
 * Variables set in the invoking scope:
 *     opt      name of the option being handled, without dashes, as typed
 *              (valid or not).  Set before each handler runs.
 *     optarg   the option's argument; UNSET when there is none.
 *     optrem   list of remaining (non-option) arguments, set once parsing
 *              stops (end of options, `--`, or `break`).  Not set while
 *              handlers run.
 *
 * Handlers run in the caller's scope; break stops parsing, continue goes to
 * the next option, error/return propagate.  Arguments are permuted the GNU
 * way (options may follow non-options; POSIXLY_CORRECT is honored by glibc).
 *
 * Build (standalone loadable, Tcl 8.6 or 9):
 *     gcc -shared -fPIC -o libgetopt.so getopt.c $(pkg-config --cflags --libs tcl)
 *     load ./libgetopt.so Getopt
 * To embed it in ticklish, just call Getopt_Init(interp) from the plugin
 * loader.
 */

#include <tcl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if TCL_MAJOR_VERSION >= 9
typedef Tcl_Size GoSize;
#else
typedef int GoSize;
#endif

typedef struct {
    int         has_arg;
    Tcl_Obj    *handler;     /* NULL when the body has no entry for it */
    const char *handlerKey;  /* key that named it, for diagnostics */
} OptDef;

#define FAIL(...) \
    do { Tcl_SetObjResult(interp, Tcl_ObjPrintf(__VA_ARGS__)); goto done; } while (0)

/*
 * Validate one alias such as "o", "o:", "help", "out:".  Returns an error
 * string, or NULL and fills in the bare name length and whether it ended in
 * ':'.
 */
static const char *
ParseAlias(const char *s, GoSize len, GoSize *nameLen, int *takesArg)
{
    GoSize n = len, i;

    *takesArg = 0;
    if (n > 0 && s[n - 1] == ':') {
        *takesArg = 1;
        n--;
        if (n > 0 && s[n - 1] == ':') {
            return "optional arguments (\"::\") are not supported";
        }
    }
    if (n == 0) {
        return "empty option name";
    }
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char) s[i];
        if (c <= 0x20 || c >= 0x7f) {
            return "option names must be printable ASCII without spaces";
        }
    }
    if (n == 1) {
        if (strchr("?:;-", s[0]) != NULL) {
            return "reserved character cannot be an option name";
        }
    } else {
        if (s[0] == '-') {
            return "long option names cannot start with '-'";
        }
        if (memchr(s, '=', n) != NULL || memchr(s, ':', n) != NULL) {
            return "long option names cannot contain '=' or ':'";
        }
    }
    *nameLen = n;
    return NULL;
}

static int
SetVar(Tcl_Interp *interp, const char *name, Tcl_Obj *val)
{
    Tcl_Obj *r;

    Tcl_IncrRefCount(val);
    r = Tcl_SetVar2Ex(interp, name, NULL, val, TCL_LEAVE_ERR_MSG);
    Tcl_DecrRefCount(val);
    return r != NULL ? TCL_OK : TCL_ERROR;
}

/*
 * After getopt_long returned '?' or ':', work out which option was at fault.
 * Long options (and short options that ended their bundle) advance optind, so
 * the offending word is argv[optind-1].  A short option in the middle of a
 * bundle leaves optind alone; there, optopt is the character.
 */
static int
ErrorName(Tcl_DString *out, char **argv, int prev, int cur)
{
    Tcl_DStringSetLength(out, 0);
    if (cur != prev && cur > 0) {
        const char *a = argv[cur - 1];
        if (a[0] == '-' && a[1] == '-' && a[2] != '\0') {
            const char *eq = strchr(a + 2, '=');
            Tcl_DStringAppend(out, a + 2, eq ? (GoSize) (eq - (a + 2)) : -1);
            return 1;
        }
    }
    {
        char ch = (char) optopt;
        Tcl_DStringAppend(out, &ch, 1);
    }
    return 0;
}

int
GetoptObjCmd(ClientData unused, Tcl_Interp *interp, int objc,
             Tcl_Obj *const objv[])
{
    int result = TCL_ERROR;
    GoSize ndefs, nbody, nargs, i, j, na, total = 0, nlong = 0, os = 1, k;
    Tcl_Obj **defv, **bodyv, **argvv, **av;
    Tcl_Obj *qHandler = NULL, *cHandler = NULL;
    OptDef *defs = NULL;
    struct option *longopts = NULL;
    char *optstring = NULL, **cargv = NULL;
    const char *ostr, *prog;
    int shortdef[256];
    int cargc, savedOpterr = opterr, namesInit = 0;
    Tcl_HashTable names;
    Tcl_DString ds, errName;
 
    (void) unused;
    Tcl_DStringInit(&ds);
    Tcl_DStringInit(&errName);
 
    if (objc != 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "options argv body");
        goto done;
    }
    for (i = 0; i < 256; i++) {
        shortdef[i] = -1;
    }
 
    /* ---- option definitions ---- */
    if (Tcl_ListObjGetElements(interp, objv[1], &ndefs, &defv) != TCL_OK) {
        goto done;
    }
    for (i = 0; i < ndefs; i++) {
        if (Tcl_ListObjGetElements(interp, defv[i], &na, &av) != TCL_OK) {
            goto done;
        }
        total += na;
    }
    defs = (OptDef *) Tcl_Alloc(sizeof(OptDef) * (ndefs + 1));
    memset(defs, 0, sizeof(OptDef) * (ndefs + 1));
    longopts = (struct option *) Tcl_Alloc(sizeof(struct option) * (total + 1));
    memset(longopts, 0, sizeof(struct option) * (total + 1));
    optstring = Tcl_Alloc(total * 2 + 2);   /* [0] reserved for ':' */
    Tcl_InitHashTable(&names, TCL_STRING_KEYS);
    namesInit = 1;
 
    for (i = 0; i < ndefs; i++) {
        int hasArg = 0, shortc = -1;
 
        Tcl_ListObjGetElements(interp, defv[i], &na, &av);
        if (na == 0) {
            FAIL("getopt: empty option definition (element %d)", (int) i);
        }
        for (j = 0; j < na; j++) {
            const char *s, *err;
            GoSize len, nl;
            int arg, isNew;
            Tcl_HashEntry *e;
 
            s = Tcl_GetStringFromObj(av[j], &len);
            err = ParseAlias(s, len, &nl, &arg);
            if (err != NULL) {
                FAIL("getopt: bad option \"%s\": %s", s, err);
            }
            Tcl_DStringSetLength(&ds, 0);
            Tcl_DStringAppend(&ds, s, nl);
            e = Tcl_CreateHashEntry(&names, Tcl_DStringValue(&ds), &isNew);
            if (!isNew) {
                FAIL("getopt: duplicate option name \"%s\"",
                     Tcl_DStringValue(&ds));
            }
            Tcl_SetHashValue(e, (ClientData) (size_t) (i + 1));
            if (arg) {
                hasArg = 1;
            }
            if (nl == 1 && shortc < 0) {
                shortc = (unsigned char) s[0];
            }
        }
        defs[i].has_arg = hasArg;
 
        for (j = 0; j < na; j++) {
            const char *s;
            GoSize len, nl;
            int arg;
 
            s = Tcl_GetStringFromObj(av[j], &len);
            ParseAlias(s, len, &nl, &arg);       /* already validated */
            if (nl == 1) {
                shortdef[(unsigned char) s[0]] = (int) i;
                optstring[os++] = s[0];
                if (hasArg) {
                    optstring[os++] = ':';
                }
            } else {
                char *nm = Tcl_Alloc(nl + 1);
                memcpy(nm, s, nl);
                nm[nl] = '\0';
                longopts[nlong].name = nm;
                longopts[nlong].has_arg = hasArg ? required_argument : no_argument;
                longopts[nlong].flag = NULL;
                longopts[nlong].val = shortc >= 0 ? shortc : 256 + (int) i;
                nlong++;
            }
        }
    }
    optstring[os] = '\0';
 
    /* ---- body: key/script pairs ---- */
    if (Tcl_ListObjGetElements(interp, objv[3], &nbody, &bodyv) != TCL_OK) {
        goto done;
    }
    if (nbody % 2 != 0) {
        FAIL("getopt: body must be a list of key/script pairs");
    }
    for (k = 0; k < nbody; k += 2) {
        const char *key = Tcl_GetString(bodyv[k]);
        Tcl_Obj *script = bodyv[k + 1];
 
        if (strcmp(key, "?") == 0) {
            if (qHandler != NULL) {
                FAIL("getopt: duplicate handler for \"?\"");
            }
            qHandler = script;
        } else if (strcmp(key, ":") == 0) {
            if (cHandler != NULL) {
                FAIL("getopt: duplicate handler for \":\"");
            }
            cHandler = script;
        } else {
            Tcl_HashEntry *e = Tcl_FindHashEntry(&names, key);
            size_t d;
 
            if (e == NULL) {
                FAIL("getopt: handler for undefined option \"%s\"", key);
            }
            d = (size_t) Tcl_GetHashValue(e) - 1;
            if (defs[d].handler != NULL) {
                FAIL("getopt: duplicate handler for option \"%s\" "
                     "(already handled by \"%s\")", key, defs[d].handlerKey);
            }
            defs[d].handler = script;
            defs[d].handlerKey = key;
        }
    }
 
    /* every defined option must have a handler ('?' and ':' stay optional) */
    for (i = 0; i < ndefs; i++) {
        if (defs[i].handler == NULL) {
            FAIL("getopt: no handler for option \"%s\"",
                 Tcl_GetString(defv[i]));
        }
    }
 
    /* ---- argv -> C ---- */
    if (Tcl_ListObjGetElements(interp, objv[2], &nargs, &argvv) != TCL_OK) {
        goto done;
    }
    prog = Tcl_GetVar2(interp, "argv0", NULL, TCL_GLOBAL_ONLY);
    cargv = (char **) Tcl_Alloc(sizeof(char *) * (nargs + 2));
    cargv[0] = (char *) (prog != NULL ? prog : "getopt");
    for (i = 0; i < nargs; i++) {
        cargv[i + 1] = Tcl_GetString(argvv[i]);
    }
    cargv[nargs + 1] = NULL;
    cargc = (int) nargs + 1;
 
    /*
     * Error handling: a leading ':' in the optstring makes getopt return ':'
     * for a missing argument (and silences it).  The built-in messages are
     * only left on when the script handles neither '?' nor ':'.
     */
    if (cHandler != NULL) {
        optstring[0] = ':';
        ostr = optstring;
    } else {
        ostr = optstring + 1;
    }
    opterr = (qHandler != NULL || cHandler != NULL) ? 0 : 1;
 
    /* ---- parse loop ---- */
    optind = 0;                      /* full glibc reinitialization */
    for (;;) {
        int longindex = -1, prev = optind, c, rc, isLong = 0;
        Tcl_Obj *handler = NULL;
        const char *optName = NULL, *optval = NULL;
        char chbuf[2];
 
        c = getopt_long(cargc, cargv, ostr, longopts, &longindex);
        if (c == -1) {
            break;
        }
 
        if (c == '?' || c == ':') {
            isLong = ErrorName(&errName, cargv, prev, optind);
            optName = Tcl_DStringValue(&errName);
            handler = (c == ':') ? cHandler : qHandler;
            if (handler == NULL) {
                /* only the ':' handler exists and getopt went quiet:
                 * keep the built-in behavior for unknown options. */
                if (c == '?' && cHandler != NULL) {
                    if (isLong) {
                        fprintf(stderr, "%s: unrecognized option '--%s'\n",
                                cargv[0], optName);
                    } else {
                        fprintf(stderr, "%s: invalid option -- '%s'\n",
                                cargv[0], optName);
                    }
                }
                continue;
            }
        } else {
            int d = (c >= 256) ? c - 256 : shortdef[c & 0xff];
 
            handler = defs[d].handler;
            if (handler == NULL) {
                continue;
            }
            if (longindex >= 0) {
                optName = longopts[longindex].name;
            } else {
                chbuf[0] = (char) c;
                chbuf[1] = '\0';
                optName = chbuf;
            }
            optval = optarg;
        }
 
        if (SetVar(interp, "opt", Tcl_NewStringObj(optName, -1)) != TCL_OK) {
            goto done;
        }
        if (optval != NULL) {
            if (SetVar(interp, "optarg", Tcl_NewStringObj(optval, -1))
                    != TCL_OK) {
                goto done;
            }
        } else {
            Tcl_UnsetVar2(interp, "optarg", NULL, 0);
        }
 
        rc = Tcl_EvalObjEx(interp, handler, 0);
        if (rc == TCL_OK || rc == TCL_CONTINUE) {
            continue;
        }
        if (rc == TCL_BREAK) {
            break;
        }
        result = rc;                 /* error / return / custom: propagate */
        goto done;
    }
 
    /* ---- optrem ---- */
    {
        Tcl_Obj *rem = Tcl_NewListObj(0, NULL);
        int ix;
 
        for (ix = optind; ix < cargc; ix++) {
            Tcl_ListObjAppendElement(NULL, rem,
                                     Tcl_NewStringObj(cargv[ix], -1));
        }
        if (SetVar(interp, "optrem", rem) != TCL_OK) {
            goto done;
        }
    }
    Tcl_ResetResult(interp);
    result = TCL_OK;
 
done:
    opterr = savedOpterr;
    optind = 0;                      /* drop pointers into cargv before it is freed */
    if (longopts != NULL) {
        for (k = 0; k < nlong; k++) {
            Tcl_Free((char *) longopts[k].name);
        }
        Tcl_Free((char *) longopts);
    }
    if (namesInit) {
        Tcl_DeleteHashTable(&names);
    }
    if (defs != NULL) Tcl_Free((char *) defs);
    if (optstring != NULL) Tcl_Free(optstring);
    if (cargv != NULL) Tcl_Free((char *) cargv);
    Tcl_DStringFree(&ds);
    Tcl_DStringFree(&errName);
    return result;
}
