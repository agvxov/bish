namespace export pipe gunt ->

# XXX
proc ::bish::is_system {name} {
    expr {[namespace which -command $name] eq ""}
}

proc reap_channel {chan} {
    set ::err 0
    try {
        close $chan
    } trap CHILDSTATUS {msg opts} {
        set ::err [lindex [dict get $opts -errorcode] 2]
    } trap CHILDKILLED {msg opts} - trap CHILDSUSP {msg opts} {
        set ::err [expr {128 + [signal-number [lindex [dict get $opts -errorcode] 2]]}]
    }
}

proc run_group {group data have} {
    set cmdline {}
    foreach c $group {
        if {[llength $cmdline]} {
            lappend cmdline |
        }
        lappend cmdline {*}$c
    }
    if {$have} {
        if {$data ne ""} {
            append data \n
        }
        lappend cmdline << $data
    } else {
        lappend cmdline <@ stdin
    }
    lappend cmdline 2>@1

    flush stdout
    flush stderr
    set chan [open |$cmdline r]
    fconfigure $chan -translation lf
    set out [read $chan]
    reap_channel $chan

    if {[string index $out end] eq "\n"} {
        set out [string range $out 0 end-1]
    }
    return $out
}

proc pipe {args} {
    set data {}
    set have 0
    set split 0
    set group {}
    set gdata {}
    set ghave 0

    foreach stage $args {
        if {$stage eq "*"} {
            # Finish any pending OS pipeline to get the output to split.
            if {[llength $group]} {
                set data [run_group $group $gdata $ghave]
                set have 1
                set group {}
            }
            if {!$have} {
                return -code error "pipe: * must follow a stage"
            }
            set split 1
            continue
        }

        set name [lindex $stage 0]
        set argv [lrange $stage 1 end]

        if {[is_system $name]} {
            if {$split} {
                # The output becomes arguments, so it can't feed stdin too.
                lappend argv {*}$data
                set split 0
                set gdata {}
                set ghave 0
            } elseif {![llength $group]} {
                set gdata $data
                set ghave $have
            }
            lappend group [list [resolve $name] {*}$argv]
            continue
        }

        if {[llength $group]} {
            set data [run_group $group $gdata $ghave]
            set have 1
            set group {}
        }
        if {$have} {
            if {$split} {
                lappend argv {*}$data
            } else {
                lappend argv $data
            }
        }
        set split 0
        set data [uplevel 1 [list $name {*}$argv]]
        set have 1
    }

    if {$split} {
        return -code error "pipe: * must be followed by a stage"
    }
    if {[llength $group]} {
        set data [run_group $group $gdata $ghave]
    }
    return $data
}

# Turn one operator-free run of words into a pipe stage.
proc gunt_parse_stage {seg} {
    if {![llength $seg]} {
        return -code error "gunt: missing command near operator"
    }
    if {[llength $seg] > 1} {
        return $seg
    }
    # A lone word is either a plain command ("ls", "echo hi")
    # or a nested expression ("a && b").
    set sub [lindex $seg 0]
    foreach t $sub {
        if {$t in {| && ||}} {
            return [list ::bish::gunt_group $sub]
        }
    }
    return $sub
}

# Stage wrapper for a nested group used inside a pipeline.
# Any piped-in data is accepted but ignored.
proc gunt_group {sub args} {
    uplevel 1 [list ::bish::gunt {*}$sub]
}

proc gunt {args} {
    # Parse: pipelines separated by && / ||, stages separated by |
    set seg {}
    set stages {}
    set pipelines {}
    set ops {}

    foreach tok $args {
        switch -exact -- $tok {
            | {
                lappend stages [gunt_parse_stage $seg]
                set seg {}
            }
            && - || {
                lappend stages [gunt_parse_stage $seg]
                set seg {}
                lappend pipelines $stages
                set stages {}
                lappend ops $tok
            }
            default {
                lappend seg $tok
            }
        }
    }
    lappend stages [gunt_parse_stage $seg]
    lappend pipelines $stages

    # Evaluate left to right, && and || with equal precedence
    # (like sh), skipping lazily.
    set failed 0
    set out {}
    set opts {}
    set i 0
    foreach p $pipelines {
        if {$i > 0} {
            set op [lindex $ops [expr {$i - 1}]]
            if {($op eq "&&" && $failed) || ($op eq "||" && !$failed)} {
                incr i
                continue
            }
        }
        set code [catch {uplevel 1 [list ::bish::pipe {*}$p]} out opts]
        set failed [expr {$code != 0}]
        incr i
    }

    if {$failed} {
        return -options $opts $out
    }
    return $out
}

interp alias {} -> {} gunt
