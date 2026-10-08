# Bish

> the best shell around, if you want to know the truth about it, bish

## What is it?
Bish is Tcl adopted for shell scripting.
The implementation is a thin wrapper around Tcl 9.

## Why?
Bash is bad.

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
