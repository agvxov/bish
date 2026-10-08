# --- Variable standins
namespace export home lines cols

proc home {{name ""}} {
    file normalize ~$name
}

proc lines {} {
    set size [invoke stty [resolve stty] size]
    lindex $size 0
}

proc cols {} {
    set size [invoke stty [resolve stty] size]
    lindex $size 1
}

# --- Bournewave
# Legend:
#  x : covered
#    : missing
#  ? : maybe should cover in the future
# Coverage:
# [x] :
# [x] .
# [x] break
# [x] continue
# [x] eval
# [x] exec
# [x] exit
# [ ] export // ?!
# [ ] readonly
# [x] return
# [x] set
# [ ] shift
# [-] times
# [x] trap
# [x] unset
# [x] alias
# [?] bg
# [?] bind
# [ ] builtin
# [-] caller
# [ ] command
# [?] compgen
# [?] complete
# [?] compopt
# [-] declare
# [x] dirs
# [x] disown
# [-] enable
# [?] fc
# [?] fg
# [ ] getopts
# [?] hash
# [?] help
# [?] history
# [?] jobs
# [ ] kill
# [-] let
# [-] local
# [-] logout
# [-] popd
# [-] printf
# [-] pushd
# [x] pwd
# [x] read
# [?] readarray
# [x] source
# [ ] suspend
# [-] test
# [ ] type
# [-] typeset
# [-] ulimit
# [-] umask
# [-] unalias
# [ ] wait
namespace export alias cd pop exec

proc alias {name value} {
    proc ::$name {args} [string map [list @NAME@ $name @VALUE@ $value] {
        if {$::tcl_interactive} {
            puts stderr [::bish::paint yellow {alias @NAME@ -> @VALUE@}]
        }
        tailcall @VALUE@ {*}$args
    }]
}

rename ::cd ::tcl_cd
set ::dirs [list [pwd]]

proc cd {{directory ""}} {
    if {$directory eq ""} {
        set directory $::env(HOME)
    }
    ::tcl_cd $directory
    lappend ::dirs [pwd]
    return
}

proc pop {{n 1}} {
    set max [expr {[llength $::dirs] - 1}]
    if {$n > $max} {
        set n $max
    }
    set ::dirs [lrange $::dirs 0 end-$n]
    ::tcl_cd [lindex $::dirs end]
    return
}

rename ::exec ::tcl_exec
load [file join [file dirname [file normalize [info script]]] libbishc.so] Bishc

proc spawn {args} {
    ::tcl_exec setsid {*}$args < /dev/null &
}

# --- Should always have been a builtin
namespace export which slurp

proc which {name} {
    if {[string first / $name] >= 0} {
        return [expr {[file isfile $name] && [file executable $name] ? $name : ""}]
    }

    if {![info exists ::env(PATH)]} {
        return {}
    }
    foreach dir [split $::env(PATH) :] {
        if {$dir eq ""} continue
        set candidate [file join $dir $name]
        if {[file isfile $candidate] && [file executable $candidate]} {
            return $candidate
        }
    }
    return {}
}

proc slurp {path} {
    set f [open $path r]
    try {
        read $f
    } finally {
        close $f
    }
}
