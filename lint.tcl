namespace export lint

# Features:
# - [ ] brace balancing
# - [ ] $ followed by non-variable (e.g. $. or $foo-bar)
# - [ ] $ in proc arguments
# - [ ] $ in set
# - [ ] # in a command line without ;
# - [ ] space after \
# - [ ] unused var?
# - [ ] possible variable misspelling?

proc ::bish::find_unterminated {script} {
    if {[info complete $script]} {
        return {}
    }

    # Walk line by line; a new command starts whenever the previous
    # candidate completes. The last start that never completes is the culprit.
    set lines [split $script \n]
    set start 0
    for {set i 0} {$i < [llength $lines]} {incr i} {
        set cand [join [lrange $lines $start $i] \n]
        regexp {\\*$} $cand bs
        if {[info complete $cand] && [string length $bs] % 2 == 0} {
            set start [expr {$i + 1}]
        }
    }

    # Which closer, appended to the unfinished tail, would complete it?
    set tail [join [lrange $lines $start end] \n]
    set reason {unterminated command}
    foreach {closer why} [list \
        "\n\}" {missing close-brace} \
        "\n\]" {missing close-bracket} \
        "\n\"" {missing quote}] {
        if {[info complete $tail$closer]} {
            set reason $why
            break
        }
    }
    return [list [expr {$start + 1}] $reason]
}

proc ::bish::lint {path} {
    set f [open $path r]
    try {
        set script [read $f]
    } finally {
        close $f
    }

    set found [find_unterminated $script]
    if {![llength $found]} {
        return
    }
    lassign $found line why

    set lines [split $script \n]
    set file [file normalize $path]
    set hash [paint red #]
    set bar  [paint yellow |]

    # The faulty line, with two lines of context on each side.
    set from [expr {max($line - 3, 0)}]
    set to   [expr {min($line + 1, [llength $lines] - 1)}]

    set out [list [margin $hash "[paint aqua from:] [loc $file $line]"]]
    foreach text [lrange $lines $from $to] {
        lappend out [margin $bar $text]
    }
    lappend out [margin $hash "[paint red error:] $why"]
    return -code error -errorcode {BISH LINT} [join $out \n]
}
