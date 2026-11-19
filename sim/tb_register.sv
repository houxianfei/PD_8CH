`timescale 1ns / 1ps
module tb_register;
reg S_AXI_ACLK=0,S_AXI_ARESETN=0;
always #10 S_AXI_ACLK=~S_AXI_ACLK;
reg [127:0] adc_data=128'h77776666555544443333222211110000;
reg sample_done=0,clear_done=0;
wire [31:0] sample_interval,sample_control,sample_number;
reg [31:0] S_AXI_AWADDR=0,S_AXI_WDATA=0,S_AXI_ARADDR=0;
reg [2:0] S_AXI_AWPROT=0,S_AXI_ARPROT=0;
reg [3:0] S_AXI_WSTRB=0;
reg S_AXI_AWVALID=0,S_AXI_WVALID=0,S_AXI_BREADY=0,S_AXI_ARVALID=0,S_AXI_RREADY=0;
wire S_AXI_AWREADY,S_AXI_WREADY,S_AXI_BVALID,S_AXI_ARREADY,S_AXI_RVALID;
wire [1:0] S_AXI_BRESP,S_AXI_RRESP;
wire [31:0] S_AXI_RDATA;
control_register dut (.*);
task aw(input [31:0] address);
    begin
        @(negedge S_AXI_ACLK); S_AXI_AWADDR=address; S_AXI_AWVALID=1;
        do @(posedge S_AXI_ACLK); while(!S_AXI_AWREADY);
        @(negedge S_AXI_ACLK); S_AXI_AWVALID=0;
    end
endtask
task wd(input [31:0] value,input [3:0] strobe);
    begin
        @(negedge S_AXI_ACLK); S_AXI_WDATA=value; S_AXI_WSTRB=strobe; S_AXI_WVALID=1;
        do @(posedge S_AXI_ACLK); while(!S_AXI_WREADY);
        @(negedge S_AXI_ACLK); S_AXI_WVALID=0;
    end
endtask
task response;
    begin
        wait(S_AXI_BVALID);
        repeat(5) begin
            @(negedge S_AXI_ACLK);
            if(!S_AXI_BVALID || S_AXI_BRESP!==0) $fatal(1,"write response not held");
        end
        S_AXI_BREADY=1;
        @(negedge S_AXI_ACLK); S_AXI_BREADY=0;
    end
endtask
task rd(input [31:0] address,input [31:0] expected);
    begin
        @(negedge S_AXI_ACLK); S_AXI_ARADDR=address; S_AXI_ARVALID=1;
        do @(posedge S_AXI_ACLK); while(!S_AXI_ARREADY);
        @(negedge S_AXI_ACLK); S_AXI_ARVALID=0;
        repeat(5) begin
            @(negedge S_AXI_ACLK);
            if(!S_AXI_RVALID || S_AXI_RDATA!==expected || S_AXI_RRESP!==0) $fatal(1,"AXI read %h: %h expected %h",address,S_AXI_RDATA,expected);
        end
        S_AXI_RREADY=1;
        @(negedge S_AXI_ACLK); S_AXI_RREADY=0;
    end
endtask
initial begin
    repeat(4) @(negedge S_AXI_ACLK); S_AXI_ARESETN=1;
    rd(32'h400003D0,32'h00010000);
    aw(32'h400003D0);
    wd(32'hFFFFFFFF,4'hF);
    response();
    rd(32'h400003D0,32'h00010000);
    // Independent AW/W ordering and byte enables.
    aw(32'h400003C0); repeat(4) @(negedge S_AXI_ACLK);
    wd(32'hAABBCCDD,4'b0101); response(); rd(32'h400003C0,32'h00BB03DD);
    wd(32'h12345678,4'b1010); repeat(4) @(negedge S_AXI_ACLK);
    aw(32'h400003C0); response(); rd(32'h400003C0,32'h12BB56DD);
    aw(32'h40000004); wd(32'hFFFF,4'hF); response(); rd(32'h40000004,32'h1111);
    aw(32'h400003C4); wd(32'hFFFFFFFF,4'hF); response(); rd(32'h400003C4,32'h307);
    @(negedge S_AXI_ACLK); sample_done=1; clear_done=1;
    @(negedge S_AXI_ACLK); sample_done=0; clear_done=0;
    rd(32'h400003C4,32'h302); rd(32'h400003CC,1);
    rd(32'h400003C1,0); rd(32'h500003C0,0);
    @(negedge S_AXI_ACLK); S_AXI_ARESETN=0;
    #1;
    if(S_AXI_RVALID || S_AXI_BVALID || sample_control!=0) $fatal(1,"AXI asynchronous reset");
    @(negedge S_AXI_ACLK);
    S_AXI_ARESETN=1;
    rd(32'h400003D0,32'h00010000);
    $display("PASS tb_register: separate AXI channels, byte strobes, stalled responses, invalid addresses, reset");
    $finish;
end
initial begin #100000; $fatal(1,"AXI timeout"); end
endmodule
