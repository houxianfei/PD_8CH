# Validate SDC endpoints against the synthesized netlist. This is not routed timing sign-off.
package require ::quartus::project
package require ::quartus::sta
project_open AFS
create_timing_netlist -post_map
read_sdc
update_timing_netlist
foreach pattern {*|wq_wptr_grey* *|rq1_wptr_grey* *|rq_rptr_grey* *|wq1_rptr_grey*} {
    set count [get_collection_size [get_registers $pattern]]
    if {$count != 18} {error "Unexpected CDC endpoint count: $pattern = $count (expected 18)"}
    puts "CDC endpoint check: $pattern = $count"
}
set count [get_collection_size [get_registers {*|sro0*|datao*}]]
if {$count != 128} {error "Expected 128 ADC capture registers, found $count"}
foreach name {sys_clk ftdi_clk adc_sclk} {
    if {[get_collection_size [get_clocks $name]] != 1} {error "Missing clock $name"}
}
report_clocks -file output_files/AFS.clocks.rpt
report_net_delay -nworst 100 -file output_files/AFS.cdc_nets.rpt
puts "PASS: clocks, Gray pointer constraints and 128 ADC capture endpoints resolved."
project_close
