# System and FT232H source clocks.
create_clock -name sys_clk -period 20.000 [get_ports clk50M]
create_clock -name ftdi_clk -period 16.667 [get_ports usb_clk_60m]
# Only asynchronous FIFO pointer synchronizers and reset synchronizers cross domains.
# Bound every Gray pointer net to one fast-clock period; also bounds bus skew.
# Quartus legacy skew analysis obeys clock-group cuts; use independent net-delay constraints.
set_clock_groups -asynchronous -group {sys_clk} -group {ftdi_clk}
set_false_path -from [get_ports Rst_n]
set_net_delay -max -from [get_registers {*|wq_wptr_grey*}] -to [get_registers {*|rq1_wptr_grey*}] 16.667
set_net_delay -max -from [get_registers {*|rq_rptr_grey*}] -to [get_registers {*|wq1_rptr_grey*}] 16.667
# FT232H synchronous FIFO interface budgets, ns; board trace skew assumed <= 1 ns.
set_input_delay -clock ftdi_clk -max 10.0 [get_ports {usb_data[*] usb_rxf_n usb_txe_n}]
set_input_delay -clock ftdi_clk -min 0.0 [get_ports {usb_data[*] usb_rxf_n usb_txe_n}]
set_output_delay -clock ftdi_clk -max 8.5 [get_ports {usb_data[*] usb_oe_n usb_rd_n usb_wr_n}]
set_output_delay -clock ftdi_clk -min -1.0 [get_ports {usb_data[*] usb_oe_n usb_rd_n usb_wr_n}]
# SPI output is 25 MHz, falling edge launches AD7380 data; sample occurs 2 sys_clk cycles later.
create_generated_clock -name adc_sclk -source [get_ports clk50M] -divide_by 2 [get_registers {*|AD7380_spi_mode2_rw|sclk}]
set_input_delay -clock adc_sclk -clock_fall -max 12.0 [get_ports {spidiT[*] spidiB[*]}]
set_input_delay -clock adc_sclk -clock_fall -min 0.0 [get_ports {spidiT[*] spidiB[*]}]
# Receiver intentionally samples two 50MHz clocks after launch; applies only to input capture registers.
set_multicycle_path -setup 2 -from [get_ports {spidiT[*] spidiB[*]}] -to [get_registers {*|sro0*|datao[*]}]
set_multicycle_path -hold 1 -from [get_ports {spidiT[*] spidiB[*]}] -to [get_registers {*|sro0*|datao[*]}]
set_output_delay -clock adc_sclk -clock_fall -max 5.0 [get_ports {spics spido}]
set_output_delay -clock adc_sclk -clock_fall -min 0.0 [get_ports {spics spido}]
set_false_path -to [get_ports usb_siwu_n]
derive_clock_uncertainty
