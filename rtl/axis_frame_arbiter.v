`timescale 1ns / 1ps

// Register replies have priority between frames. Ownership also locks while stalled.
module axis_frame_arbiter (
    input                   clk,
    input                   rst_n,
    input                   pause_sample,
    input [63:0]            ctrl_data,
    input                   ctrl_valid,
    input                   ctrl_last,
    output                  ctrl_ready,
    input [63:0]            sample_data,
    input                   sample_valid,
    input                   sample_last,
    output                  sample_ready,
    output                  sample_busy,
    output [63:0]           m_tdata,
    output                  m_tvalid,
    output                  m_tlast,
    input                   m_tready
);
reg locked;
reg owner_ctrl;
wire select_ctrl = locked ? owner_ctrl : ctrl_valid;
wire select_sample = locked ? !owner_ctrl : (!ctrl_valid && !pause_sample);
assign m_tdata = select_ctrl ? ctrl_data : sample_data;
assign m_tvalid = select_ctrl ? ctrl_valid : (select_sample && sample_valid);
assign m_tlast = select_ctrl ? ctrl_last : sample_last;
assign ctrl_ready = m_tready && select_ctrl;
assign sample_ready = m_tready && select_sample;
assign sample_busy = locked && !owner_ctrl;

always @(posedge clk or negedge rst_n)
    if (!rst_n) begin
        locked <= 0;
        owner_ctrl <= 0;
    end else if (m_tvalid) begin
        if (m_tready && m_tlast)
            locked <= 0;
        else begin
            locked <= 1;
            owner_ctrl <= select_ctrl;
        end
    end
endmodule
