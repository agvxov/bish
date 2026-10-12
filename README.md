# Bish

> the best shell around, if you want to know the truth about it, bish

## What is it?
Bish is Tcl adopted for shell scripting.
The implementation is a thin wrapper around Tcl 9.

## Why?
Bash is bad.

## Design notes

### Readonly
Bish has no equivalent of `readonly`.

First thing first, if it had, it would be called `const`.

Secondly, its a damned if you do, damned if you don't:
* if you don't add it, someone will want it sooner or later
* if you do add it, it may turn out that most people don't use it,
  or if they do, they will have illusions about that it actually does

Tcl is incapable of optimizing around constants,
in fact, they would be marginally slower to operate on,
since it would have to be a runtime assertion.

Tcl also allows you to redefine anything into anything,
so there is no sense of safety added.

Const can used as a readability tool, to entail intent,
however if your shell code is so complex that you require const,
you probably should be using Perl at that point.

## Features
For trivial scripts,
Bish looks identiacal to Bourne derivatives.
```tcl
#!/bin/bish
# Usage: git-is-my-package-manager-and-i-love-the-antichrist.bish

git fetch --all
git submodule update --init --recursive
git lfs fetch --all
git lfs checkout
git submodule foreach 'git lfs fetch --all; git lfs checkout'
```

But to have commands make noise,
you have to be explicit:
```tcl
# Same script, but program output shows up
emit {
    git fetch --all
    git submodule update --init --recursive
    git lfs fetch --all
    git lfs checkout
    git submodule foreach 'git lfs fetch --all; git lfs checkout'
}
```

## Builtins
* emit cmd - print the return value of all commands in the argument; not recursive
* which    - get the path of a command or "" if its missing

## TODO
* powershell like variable manipulation
* relevant builtin variables
* relevant builtin functions
* ::sys and ::tcl namespace to replace command/builtin
* make sure set -euo pipefail is the default behaviour
