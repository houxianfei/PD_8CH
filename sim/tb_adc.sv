`timescale 1ns / 1ps
module tb_adc;
reg clk=0, rst_n=0;
always #10 clk=~clk;
reg [7:0] spi_di=0;
wire spi_clk, spi_cs, spi_do, valid;
wire [127:0] adc_data;
Sensor_Collection #(.T_POWER_UP(20)) dut (.*);
reg [15:0] values[0:7];
integer bits=0, transactions=0, samples=0, lane, cycle=0, last_sample=0;
reg [15:0] command;
reg [127:0] previous_data;
always @(posedge clk) cycle <= cycle+1;
always @(negedge spi_cs) begin
    bits=0;
    command=0;
end
always @(negedge spi_clk) if (!spi_cs) begin
    if (bits >= 16) $fatal(1,"too many SPI clocks");
    for (lane=0;lane<8;lane=lane+1) spi_di[lane]=values[lane][15-bits];
    command={command[14:0],spi_do};
    bits=bits+1;
end
always @(posedge spi_cs) if (rst_n && bits != 0) begin
    if (bits != 16) $fatal(1,"SPI frame length %0d",bits);
    if (transactions==0 && command!==16'hA000) $fatal(1,"reset command %h",command);
    if (transactions==1 && command!==16'h9282) $fatal(1,"configuration command %h",command);
    if (transactions>=2 && command!==0) $fatal(1,"conversion command %h",command);
    transactions=transactions+1;
end
always @(negedge clk) if (rst_n) begin
    if (valid) begin
        if (transactions < 3) $fatal(1,"valid during initialization");
        for (integer k=0;k<8;k=k+1)
            if (adc_data[k*16 +:16] !== values[k])
                $fatal(1,"CH%0d expected %h got %h",k,values[k],adc_data[k*16 +:16]);
        if (samples>0 && cycle-last_sample!=50) $fatal(1,"sample spacing %0d",cycle-last_sample);
        last_sample=cycle;
        samples=samples+1;
    end else if (adc_data !== previous_data) $fatal(1,"ADC output changed without valid");
    previous_data=adc_data;
end
initial begin
    values[0]=16'h8001; values[1]=16'h1234; values[2]=16'hCAFE; values[3]=16'hFFFF;
    values[4]=16'h55AA; values[5]=16'hABCD; values[6]=16'h0000; values[7]=16'hF001;
    previous_data=0;
    repeat(4) @(negedge clk);
    rst_n=1;
    wait(samples==6);
    @(negedge clk); rst_n=0;
    #1;
    if (adc_data!==0 || valid!==0) $fatal(1,"ADC reset");
    transactions=0; samples=0; previous_data=0;
    repeat(4) @(negedge clk);
    rst_n=1;
    wait(samples==3);
    $display("PASS tb_adc: configuration, 8 lanes, 1 MSPS, atomic update, reset");
    $finish;
end
initial begin #200000; $fatal(1,"ADC timeout"); end
endmodule
