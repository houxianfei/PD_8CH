# Invoked by Quartus full-flow compile, including GUI compilation.
# Analysis & Synthesis (quartus_map) intentionally remains available without board pins.
set script_dir [file dirname [file normalize [info script]]]
set qsf [file join $script_dir .. par AFS.qsf]
set fh [open $qsf r]
set text [read $fh]
close $fh
set missing {}
set ports {clk50M Rst_n usb_clk_60m usb_rxf_n usb_txe_n usb_oe_n usb_rd_n usb_wr_n usb_siwu_n spics spiclk spido}
for {set i 0} {$i < 8} {incr i} { lappend ports "usb_data\[$i\]" }
for {set i 0} {$i < 4} {incr i} {
    lappend ports "spidiT\[$i\]"
    lappend ports "spidiB\[$i\]"
}
set pin_map [dict create]
foreach line [split $text "\n"] {
    if {[regexp {^\s*set_location_assignment\s+(PIN_[A-Za-z0-9]+)\s+-to\s+\{?([^\s\}]+)\}?\s*$} $line -> pin port]} {
        dict set pin_map $port $pin
    }
}
foreach port $ports {
    if {![dict exists $pin_map $port]} {lappend missing $port}
}
if {[llength $missing]} {
    error "Board build blocked: missing pin assignments: [join $missing {, }]. Run synthesis only until actual board pins are supplied."
}
set unique_pins [lsort -unique [dict values $pin_map]]
if {[llength $unique_pins] != [dict size $pin_map]} {error "Board build blocked: duplicate FPGA pin assignments"}
puts "Board pin check passed."
