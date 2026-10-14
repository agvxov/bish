eval namespace export report

proc paint {color text} {
    variable colors
    variable use_color
    if {!$use_color} {
        return $text
    }
    return "\033\[[dict get $colors $color]m$text\033\[0m"
}

proc loc {file line} {
    return "[paint purple $file]:[paint cyan $line]"
}

# $mark is already painted; every line of $text gets it.
proc margin {mark text} {
    return "$mark [string map [list \n "\n$mark "] $text]"
}

# XXX broken state, half this implementation half previous
# preseve the behaviour, but feel free to rewrite
proc report {msg trace} {
    # XXX error reporting must be merged in the future
    set hash [paint red #]
    set bar  [paint yellow |]

    set out {}
    foreach item [lrange $trace 0 end-1] {
        lassign $item _ file line text
        lappend out [margin $hash "[paint aqua "from:"] [loc $file $line]"] \
                    [margin $bar $text]
    }
    lassign [lindex $trace end] _ file line text
    lappend out [margin $hash "[paint red "error:"] [loc $file $line]: $msg"] \
                [margin $bar $text]
    return [join $out \n]
}
