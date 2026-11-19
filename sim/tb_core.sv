`timescale 1ns / 1ps
module tb_core;
`include "reference_vectors.vh"
reg clk=0,rst_n=0;
always #10 clk=~clk;
reg [127:0] adc_data=128'h77776666555544443333222211110000;
reg [63:0] rx_tdata=0;
reg [7:0] rx_tkeep=0;
reg rx_tvalid=0;
wire rx_tready;
wire [63:0] tx_tdata;
wire [7:0] tx_tkeep;
wire tx_tvalid,tx_tlast;
reg tx_tready=0;
AFS_core dut (.*);
reg [63:0] records[0:4095];
reg lasts[0:4095];
integer received=0, frames=0, replies=0;
integer seed=32'h73501234;
reg random_ready=1;
always @(negedge clk) if (random_ready) tx_tready=($random(seed)&3)!=0;
reg stalled=0;
reg [63:0] held;
reg held_last;
integer remaining=0;
reg [7:0] crc, expected_crc;
reg [1:0] groups;
function [7:0] crc64(input [7:0] initial_crc,input [63:0] value);
    reg [7:0] c;
    begin
        c=initial_crc;
        for(integer i=63;i>=0;i=i-1) c={c[6:0],1'b0} ^ ((c[7]^value[i])?8'h07:8'h00);
        crc64=c;
    end
endfunction
always @(posedge clk) if (rst_n) begin
    if (stalled && (!tx_tvalid || tx_tdata!==held || tx_tlast!==held_last)) $fatal(1,"TX changed under backpressure");
    stalled=tx_tvalid&&!tx_tready; held=tx_tdata; held_last=tx_tlast;
    if(tx_tvalid&&tx_tready) begin
        if(tx_tkeep!==8'hFF) $fatal(1,"TX keep");
        records[received]=tx_tdata; lasts[received]=tx_tlast; received=received+1;
        if(remaining==0) begin
            if(tx_tdata[63:56]==8'hAA) begin
                groups=tx_tdata[49:48];
                remaining=groups[0]+{1'b0,groups[1]};
                expected_crc=tx_tdata[15:8];
                crc=crc64(0,{tx_tdata[63:16],16'd0});
                if(tx_tlast !== (remaining==0)) $fatal(1,"header last");
                if(remaining==0) begin
                    if(crc!==expected_crc) $fatal(1,"header-only CRC");
                    frames=frames+1;
                end
            end else begin
                if(!tx_tlast || tx_tdata[63:32]!==tx_tdata[31:0]) $fatal(1,"invalid/interleaved register reply %h",tx_tdata);
                replies=replies+1;
            end
        end else begin
            if(groups[0]) begin
                if(tx_tdata!==64'h0000111122223333) $fatal(1,"group0 %h",tx_tdata);
                groups[0]=0;
            end else if(groups[1]) begin
                if(tx_tdata!==64'h4444555566667777) $fatal(1,"group1 %h",tx_tdata);
                groups[1]=0;
            end
            crc=crc64(crc,tx_tdata);
            remaining=remaining-1;
            if(tx_tlast !== (remaining==0)) $fatal(1,"payload last");
            if(remaining==0) begin
                if(crc!==expected_crc) $fatal(1,"sample CRC got %h expected %h",crc,expected_crc);
                frames=frames+1;
            end
        end
    end
end
task beat(input [63:0] data,input [7:0] keep);
    begin
        @(negedge clk); rx_tdata=data; rx_tkeep=keep; rx_tvalid=1;
        do @(posedge clk); while(!rx_tready);
        @(negedge clk); rx_tvalid=0;
    end
endtask
function [63:0] command(input [7:0] op,input [7:0] addr,input [31:0] data);
    reg [63:0] value;
    begin
        value={8'd0,data,addr,op,8'hAA};
        command={crc64(0,value),value[55:0]};
    end
endfunction
task wr(input [7:0] addr,input [31:0] data);
    begin beat(command(1,addr,data),8'hFF); end
endtask
task rd(input [7:0] addr,input [31:0] expected);
    integer before_replies;
    begin
        before_replies=replies;
        beat(command(2,addr,0),8'hFF);
        wait(replies>before_replies);
        if(records[received-1]!=={expected,expected}) $fatal(1,"read %h: got %h expected %h",addr,records[received-1],expected);
    end
endtask
integer n,before_count,before_frames;
reg [63:0] split_command;
reg [63:0] second_command;
initial begin
    if(command(1,8'hF0,1000)!==REF_WRITE_INTERVAL || command(2,7,0)!==REF_READ_CH7 ||
       command(1,8'hF1,32'h301)!==REF_WRITE_START) $fatal(1,"afs_v1 golden command mismatch");
    repeat(4) @(negedge clk); rst_n=1;
    for(n=0;n<8;n=n+1) rd(n,n*16'h1111);
    rd(8'h80,0);
    wr(0,32'hDEADBEEF); rd(0,0);
    wr(8'h80,32'hDEADBEEF); rd(8'h80,0);
    wr(8'hF0,32'd2); rd(8'hF0,2);
    if(dut.sample_interval!==1000) $fatal(1,"minimum interval");
    wr(8'hF0,1000); rd(8'hF0,1000);
    beat(command(1,8'hF0,2000)^64'h0100000000000000,8'hFF);
    rd(8'hF0,1000);
    // Two commands spanning USB beats: 5 bytes, then 3+5, then 3 bytes.
    split_command=command(1,8'hF0,1500);
    second_command=command(2,8'hF0,0);
    before_count=replies;
    beat(split_command,8'h1F);
    beat((split_command>>40)|(second_command<<24),8'hFF);
    beat(second_command>>40,8'h07);
    wait(replies==before_count+1);
    if(records[received-1]!==64'h000005DC000005DC) $fatal(1,"cross-command byte preservation");
    wr(8'hF0,1000); rd(8'hF0,1000);
    // A command fragmented over three FT232H RX beats, with idle USB gaps.
    split_command=command(1,8'hF2,3);
    beat(split_command,8'h03); repeat(5) @(negedge clk);
    beat(split_command>>16,8'h07); beat(split_command>>40,8'h07);
    rd(8'hF2,3);
    wr(8'hF1,32'h301);
    wait(frames==3); repeat(30) @(negedge clk);
    rd(8'hF1,32'h300); rd(8'hF3,1);
    wr(8'hF2,2); wr(8'hF1,32'h101);
    wait(frames==5); rd(8'hF3,1);
    wr(8'hF2,1); wr(8'hF1,32'h201);
    wait(frames==6); rd(8'hF3,1);
    // Header-only frames and masked unsupported group bits.
    wr(8'hF1,32'hFC01); wait(frames==7); rd(8'hF1,0);
    // Continuous acquisition with register traffic interleaved at frame boundaries.
    wr(8'hF1,32'h303);
    for(n=0;n<12;n=n+1) rd(3,32'h3333);
    before_frames=frames;
    wait(frames>=before_frames+3);
    wr(8'hF1,32'h300); rd(8'hF1,32'h300);
    repeat(2000) @(negedge clk);
    before_frames=frames; repeat(2000) @(negedge clk);
    if(frames!=before_frames) $fatal(1,"stream did not stop");
    // Saturate output FIFO; packet creation pauses without changing in-flight data.
    random_ready=0; @(negedge clk); tx_tready=0;
    wr(8'hF1,32'h303);
    repeat(100000) @(negedge clk);
    if(dut.free_words>=3) $fatal(1,"FIFO full scenario not reached");
    before_count=dut.i_packet.frame_count;
    repeat(4000) @(negedge clk);
    if(dut.i_packet.frame_count!=before_count) $fatal(1,"count advanced while full");
    // Stop and request clear while downstream stalls. Clear must wait for exposed frame.
    wr(8'hF1,32'h304);
    repeat(100) @(negedge clk);
    random_ready=1;
    wait(dut.sample_control[2]==0);
    repeat(100) @(negedge clk);
    if(dut.i_packet_fifo.m_tvalid) $fatal(1,"FIFO not cleared");
    rd(8'hF1,32'h300);
    wr(8'hF2,2); wr(8'hF1,32'h301);
    before_frames=frames; wait(frames==before_frames+2);
    rd(8'hF3,1);
    $display("PASS tb_core: reads/writes, CRC, split commands, finite/continuous modes, backpressure, clear, restart (%0d frames)",frames);
    $finish;
end
initial begin #10000000; $fatal(1,"core timeout frames=%0d replies=%0d state=%0d",frames,replies,dut.i_packet.current_state); end
endmodule
