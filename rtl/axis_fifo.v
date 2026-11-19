`timescale 1ns / 1ps

// Synchronous 256-word packet FIFO. Registered RAM read maps to Cyclone IV M9K.
module axis_fifo #(
    parameter EA = 8
)(
    input                   clk,
    input                   rst_n,
    input                   clear,
    input [63:0]            s_tdata,
    input                   s_tlast,
    input                   s_tvalid,
    output                  s_tready,
    output [63:0]           m_tdata,
    output                  m_tlast,
    output                  m_tvalid,
    input                   m_tready,
    output [EA:0]           free_words
);
localparam [EA:0] DEPTH = {1'b1,{EA{1'b0}}};
reg [64:0] memory [0:(1<<EA)-1];
reg [EA:0] wr_ptr;
reg [EA:0] rd_ptr;
reg [64:0] read_data;
reg read_valid;
wire [EA:0] used = wr_ptr - rd_ptr;
wire pop = m_tvalid && m_tready;
wire load = (!read_valid || pop) && (used != 0);
assign s_tready = used < DEPTH && !clear;
assign free_words = DEPTH - used;
assign m_tdata = read_data[63:0];
assign m_tlast = read_data[64];
assign m_tvalid = read_valid;

always @(posedge clk) begin
    if (s_tvalid && s_tready)
        memory[wr_ptr[EA-1:0]] <= {s_tlast, s_tdata};
    if (load)
        read_data <= memory[rd_ptr[EA-1:0]];
end

always @(posedge clk or negedge rst_n)
    if (!rst_n) begin
        wr_ptr <= 0;
        rd_ptr <= 0;
        read_valid <= 0;
    end else if (clear) begin
        wr_ptr <= 0;
        rd_ptr <= 0;
        read_valid <= 0;
    end else begin
        if (s_tvalid && s_tready)
            wr_ptr <= wr_ptr + 1'b1;
        if (load)
            rd_ptr <= rd_ptr + 1'b1;
        if (load)
            read_valid <= 1;
        else if (pop)
            read_valid <= 0;
    end
endmodule
