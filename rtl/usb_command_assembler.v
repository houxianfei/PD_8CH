`timescale 1ns / 1ps

// USB transfer boundaries are not command boundaries. Accumulate eight bytes,
// including partial FT232H RX beats; retain the original afs_v1 command format.
module usb_command_assembler (
    input                   clk,
    input                   rst_n,
    input [63:0]            s_tdata,
    input [7:0]             s_tkeep,
    input                   s_tvalid,
    output                  s_tready,
    output reg [63:0]       m_tdata,
    output reg              m_tvalid,
    input                   m_tready
);
reg [63:0] input_data;
reg [63:0] assembled;
reg [7:0] input_keep;
reg [2:0] byte_index;
reg [2:0] count;
reg input_pending;
assign s_tready = !input_pending;

always @(posedge clk or negedge rst_n)
    if (!rst_n) begin
        input_data <= 0;
        input_keep <= 0;
        assembled <= 0;
        byte_index <= 0;
        count <= 0;
        input_pending <= 0;
        m_tdata <= 0;
        m_tvalid <= 0;
    end else begin
        if (m_tvalid && m_tready)
            m_tvalid <= 0;
        if (s_tvalid && s_tready) begin
            input_data <= s_tdata;
            input_keep <= s_tkeep;
            byte_index <= 0;
            input_pending <= 1;
        end else if (input_pending && !m_tvalid) begin
            input_data <= input_data >> 8;
            input_keep <= input_keep >> 1;
            byte_index <= byte_index + 1'b1;
            if (byte_index == 7)
                input_pending <= 0;
            if (input_keep[0]) begin
                assembled <= {input_data[7:0],assembled[63:8]};
                count <= count + 1'b1;
                if (count == 7) begin
                    m_tdata <= {input_data[7:0],assembled[63:8]};
                    m_tvalid <= 1;
                end
            end
        end
    end
endmodule
