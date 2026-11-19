`timescale 1ns / 1ps
// Byte-level FT232H model exercises the complete controller at unrelated 50/60MHz clocks.
module tb_ft232h;
reg clk50M=0, usb_clk_60m=0, Rst_n=0;
always #10 clk50M=~clk50M;
always #8.333 usb_clk_60m=~usb_clk_60m;
wire usb_rxf_n;
reg usb_txe_n=0;
wire usb_oe_n,usb_rd_n,usb_wr_n,usb_siwu_n;
tri [7:0] usb_data;
wire spics;
wire spiclk;
wire spido;
reg [7:0] lanes=0;
AFS dut (
    .clk50M(clk50M),
    .usb_clk_60m(usb_clk_60m),
    .Rst_n(Rst_n),
    .usb_rxf_n(usb_rxf_n),
    .usb_txe_n(usb_txe_n),
    .usb_oe_n(usb_oe_n),
    .usb_rd_n(usb_rd_n),
    .usb_wr_n(usb_wr_n),
    .usb_siwu_n(usb_siwu_n),
    .usb_data(usb_data),
    .spics(spics),
    .spiclk(spiclk),
    .spido(spido),
    .spidiT(lanes[3:0]),
    .spidiB(lanes[7:4])
);
defparam dut.i_Sensor_Collection.T_POWER_UP=20;
reg [7:0] to_fpga[0:2047],from_fpga[0:2047];
integer rx_head=0,rx_tail=0,tx_count=0;
assign usb_rxf_n = rx_head==rx_tail;
assign usb_data = !usb_oe_n && !usb_rxf_n ? to_fpga[rx_head] : 8'hZZ;
integer seed=32'h709123;
always @(negedge usb_clk_60m) usb_txe_n=($random(seed)&7)==0;
always @(posedge usb_clk_60m) if(Rst_n) begin
    if(!usb_rd_n&&!usb_rxf_n) begin
        if(usb_oe_n) $fatal(1,"FT232H read without OE");
        rx_head<=rx_head+1;
    end
    if(!usb_wr_n&&!usb_txe_n) begin
        if(!usb_oe_n || (^usb_data)===1'bX) $fatal(1,"USB contention/invalid write");
        from_fpga[tx_count]=usb_data;
        tx_count=tx_count+1;
    end
end
reg [15:0] values[0:7];
integer bit_index=0;
always @(negedge spics) bit_index=0;
always @(negedge spiclk) if(!spics) begin
    for(integer k=0;k<8;k=k+1) lanes[k]=values[k][15-bit_index];
    bit_index=bit_index+1;
end
`include "reference_vectors.vh"
task bytes(input [63:0] value,input integer offset,input integer length);
    begin
        @(negedge usb_clk_60m);
        for(integer k=offset;k<offset+length;k=k+1) begin
            to_fpga[rx_tail]=value[k*8 +:8];
            rx_tail=rx_tail+1;
        end
        wait(rx_head==rx_tail);
    end
endtask
task expect_word(input integer start,input [63:0] expected);
    begin
        wait(tx_count>=start+8);
        for(integer k=0;k<8;k=k+1)
            if(from_fpga[start+k]!==expected[k*8 +:8]) $fatal(1,"USB byte %0d got %h expected %h",start+k,from_fpga[start+k],expected[k*8 +:8]);
    end
endtask
initial begin
    for(integer k=0;k<8;k=k+1) values[k]=16'h1111*k;
    repeat(8) @(negedge clk50M); Rst_n=1;
    wait(dut.adc_valid);
    bytes(REF_READ_CH7,0,2);
    repeat(50) @(negedge usb_clk_60m);
    bytes(REF_READ_CH7,2,6);
    expect_word(0,64'h0000777700007777);
    bytes(REF_WRITE_INTERVAL,0,8);
    bytes(REF_READ_INTERVAL,0,8);
    expect_word(8,64'h000003E8000003E8);
    // Finite one-frame stream, both channel groups.
    // WRITE F2=1: CRC generated from the exact reference function in the test runner.
    bytes(REF_WRITE_NUMBER,0,8);
    bytes(REF_WRITE_START,0,8);
    expect_word(16,REF_HEADER_3);
    expect_word(24,64'h0000111122223333);
    expect_word(32,64'h4444555566667777);
    repeat(5000) @(negedge usb_clk_60m);
    if(tx_count!=40) $fatal(1,"finite frame byte count %0d",tx_count);
    @(negedge clk50M); Rst_n=0;
    repeat(8) @(negedge clk50M); Rst_n=1;
    wait(dut.adc_valid);
    bytes(REF_READ_CH7,0,8);
    expect_word(40,64'h0000777700007777);
    $display("PASS tb_ft232h: real 8bit FIFO, 50/60MHz CDC, split USB command, replies, sample bytes, reset");
    $finish;
end
initial begin #1000000; $fatal(1,"FT232H timeout rx=%0d/%0d tx=%0d",rx_head,rx_tail,tx_count); end
endmodule
