`timescale 1ns / 1ps

// afs_v1 packet wire format; groups 0/1 now carry CH0..3 / CH4..7.
module packet (
    input                   clk,
    input                   rst_n,
    input [127:0]           adc_data,
    input [31:0]            sample_interval,
    input [31:0]            sample_control,
    input [31:0]            sample_number,
    input                   pause,
    input [8:0]             free_words,
    output reg              sample_done,
    output                  busy,
    input                   m_tready,
    output                  m_tvalid,
    output [63:0]           m_tdata,
    output [7:0]            m_tkeep,
    output                  m_tlast
);
localparam [3:0] IDLE=0;
localparam [3:0] WAIT_SAMPLE=1;
localparam [3:0] CRC_HEADER=2;
localparam [3:0] CRC_GROUP0=3;
localparam [3:0] CRC_GROUP1=4;
localparam [3:0] SEND_HEADER=5;
localparam [3:0] SEND_GROUP0=6;
localparam [3:0] SEND_GROUP1=7;
reg [3:0] current_state;
reg [31:0] interval_counter;
reg [31:0] timestamp_counter;
reg [31:0] frame_count;
reg [31:0] frame_timestamp;
reg [1:0] config_mode;
reg [63:0] group0;
reg [63:0] group1;
reg [7:0] crc_result;
wire [63:0] header_zero = {8'hAA, 6'd0, config_mode, frame_timestamp, 16'd0};
wire [63:0] header = {8'hAA, 6'd0, config_mode, frame_timestamp, crc_result, 8'd0};
wire transfer = m_tvalid && m_tready;
wire final_transfer = transfer && m_tlast;
wire interval_due = interval_counter >= sample_interval;
assign busy = current_state >= CRC_HEADER;
assign m_tvalid = current_state >= SEND_HEADER;
assign m_tdata = (current_state == SEND_HEADER) ? header :
                 (current_state == SEND_GROUP0) ? group0 : group1;
assign m_tkeep = 8'hFF;
assign m_tlast = (current_state == SEND_HEADER && config_mode == 0) ||
                (current_state == SEND_GROUP0 && !config_mode[1]) ||
                current_state == SEND_GROUP1;

function [7:0] calculate_crc8;
    input [7:0] current_crc;
    input [63:0] data;
    integer i;
    reg [7:0] crc;
    begin
        crc = current_crc;
        for (i=0; i<64; i=i+1)
            if (crc[7] ^ data[63-i])
                crc = {crc[6:0],1'b0} ^ 8'h07;
            else
                crc = {crc[6:0],1'b0};
        calculate_crc8 = crc;
    end
endfunction

always @(posedge clk or negedge rst_n)
    if (!rst_n) begin
        current_state <= IDLE;
        interval_counter <= 0;
        timestamp_counter <= 0;
        frame_timestamp <= 0;
        frame_count <= 0;
        config_mode <= 0;
        group0 <= 0;
        group1 <= 0;
        crc_result <= 0;
        sample_done <= 0;
    end else begin
        sample_done <= 0;
        if (current_state != IDLE) begin
            if (interval_due) begin
                interval_counter <= 0;
                timestamp_counter <= timestamp_counter + 1'b1;
            end
            else
                interval_counter <= interval_counter + 1'b1;
        end
        case (current_state)
            IDLE: begin
                frame_count <= 0;
                timestamp_counter <= 0;
                interval_counter <= 0;
                if (sample_control[0] && (sample_control[1] || sample_number != 0))
                    current_state <= WAIT_SAMPLE;
            end
            WAIT_SAMPLE: begin
                if (!sample_control[0]) begin
                    current_state <= IDLE;
                    sample_done <= 1;
                end else if (!pause && free_words >= 3 &&
                             (frame_count == 0 || interval_due)) begin
                    // One atomic eight-channel snapshot; first channel is MSW on wire.
                    group0 <= {
                        adc_data[15:0],
                        adc_data[31:16],
                        adc_data[47:32],
                        adc_data[63:48]
                    };
                    group1 <= {
                        adc_data[79:64],
                        adc_data[95:80],
                        adc_data[111:96],
                        adc_data[127:112]
                    };
                    config_mode <= sample_control[9:8];
                    frame_timestamp <= timestamp_counter;
                    current_state <= CRC_HEADER;
                end
            end
            CRC_HEADER: begin
                crc_result <= calculate_crc8(0, header_zero);
                current_state <= CRC_GROUP0;
            end
            CRC_GROUP0: begin
                if (config_mode[0])
                    crc_result <= calculate_crc8(crc_result, group0);
                current_state <= CRC_GROUP1;
            end
            CRC_GROUP1: begin
                if (config_mode[1])
                    crc_result <= calculate_crc8(crc_result, group1);
                current_state <= SEND_HEADER;
            end
            SEND_HEADER:
                if (transfer)
                    current_state <= config_mode[0] ? SEND_GROUP0 : SEND_GROUP1;
            SEND_GROUP0:
                if (transfer)
                    current_state <= SEND_GROUP1;
            SEND_GROUP1: ;
            default:
                current_state <= IDLE;
        endcase
        if (final_transfer) begin
            frame_count <= frame_count + 1'b1;
            if (!sample_control[0] || (!sample_control[1] && frame_count + 1'b1 >= sample_number)) begin
                current_state <= IDLE;
                sample_done <= 1;
            end
            else
                current_state <= WAIT_SAMPLE;
        end
    end
endmodule
