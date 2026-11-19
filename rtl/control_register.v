`timescale 1ns / 1ps

// Reduced afs_v1 AXI4-Lite register bank. All logic uses S_AXI_ACLK.
module control_register (
    input                   S_AXI_ACLK,
    input                   S_AXI_ARESETN,
    input [127:0]           adc_data,
    input                   sample_done,
    input                   clear_done,
    output [31:0]           sample_interval,
    output reg [31:0]       sample_control,
    output reg [31:0]       sample_number,
    input [31:0]            S_AXI_AWADDR,
    input [2:0]             S_AXI_AWPROT,
    input                   S_AXI_AWVALID,
    output                  S_AXI_AWREADY,
    input [31:0]            S_AXI_WDATA,
    input [3:0]             S_AXI_WSTRB,
    input                   S_AXI_WVALID,
    output                  S_AXI_WREADY,
    output [1:0]            S_AXI_BRESP,
    output reg              S_AXI_BVALID,
    input                   S_AXI_BREADY,
    input [31:0]            S_AXI_ARADDR,
    input [2:0]             S_AXI_ARPROT,
    input                   S_AXI_ARVALID,
    output                  S_AXI_ARREADY,
    output reg [31:0]       S_AXI_RDATA,
    output [1:0]            S_AXI_RRESP,
    output reg              S_AXI_RVALID,
    input                   S_AXI_RREADY
);
localparam ADDR_SAMPLE_INTERVAL=8'hF0;
localparam ADDR_SAMPLE_CONTROL=8'hF1;
localparam ADDR_SAMPLE_NUMBER=8'hF2;
localparam ADDR_SAMPLE_DONE=8'hF3;
localparam ADDR_FIRMWARE_VERSION = 8'hF4;
// PD firmware 1.0.0: {reserved[7:0], major[7:0], minor[7:0], patch[7:0]}.
localparam [31:0] FIRMWARE_VERSION = 32'h00010000;
reg [31:0] interval_reg;
reg done_reg;
reg aw_pending;
reg w_pending;
reg [31:0] awaddr;
reg [31:0] wdata;
reg [3:0] wstrb;
wire address_valid = awaddr[31:10] == (32'h40000000 >> 10) && awaddr[1:0] == 0;
wire read_address_valid = S_AXI_ARADDR[31:10] == (32'h40000000 >> 10) && S_AXI_ARADDR[1:0] == 0;
wire [7:0] wr_addr = awaddr[9:2];
wire [7:0] rd_addr = S_AXI_ARADDR[9:2];
wire commit = aw_pending && w_pending && !S_AXI_BVALID;
assign sample_interval = interval_reg > 1000 ? interval_reg : 32'd1000;
assign S_AXI_AWREADY = !aw_pending && !S_AXI_BVALID;
assign S_AXI_WREADY = !w_pending && !S_AXI_BVALID;
assign S_AXI_ARREADY = !S_AXI_RVALID;
assign S_AXI_BRESP = 0;
assign S_AXI_RRESP = 0;
integer byte_index;

always @(posedge S_AXI_ACLK or negedge S_AXI_ARESETN)
    if (!S_AXI_ARESETN) begin
        interval_reg <= 1000;
        sample_control <= 0;
        sample_number <= 0;
        done_reg <= 0;
        aw_pending <= 0;
        w_pending <= 0;
        awaddr <= 0;
        wdata <= 0;
        wstrb <= 0;
        S_AXI_BVALID <= 0;
        S_AXI_RVALID <= 0;
        S_AXI_RDATA <= 0;
    end else begin
        sample_control[31:16] <= 0;
        if (sample_done) begin
            sample_control[0] <= 0;
            done_reg <= 1;
        end
        if (clear_done)
            sample_control[2] <= 0;
        if (S_AXI_AWVALID && S_AXI_AWREADY) begin
            aw_pending <= 1;
            awaddr <= S_AXI_AWADDR;
        end
        if (S_AXI_WVALID && S_AXI_WREADY) begin
            w_pending <= 1;
            wdata <= S_AXI_WDATA;
            wstrb <= S_AXI_WSTRB;
        end
        if (S_AXI_BVALID && S_AXI_BREADY)
            S_AXI_BVALID <= 0;
        if (commit) begin
            aw_pending <= 0;
            w_pending <= 0;
            S_AXI_BVALID <= 1;
            if (address_valid)
                for (byte_index=0; byte_index<4; byte_index=byte_index+1)
                    if (wstrb[byte_index])
                        case (wr_addr)
                            ADDR_SAMPLE_INTERVAL:
                                interval_reg[byte_index*8 +: 8] <= wdata[byte_index*8 +: 8];
                            ADDR_SAMPLE_CONTROL: begin
                                case (byte_index)
                                    0: begin
                                        sample_control[7:0] <= wdata[7:0] & 8'h07;
                                        if (wdata[0])
                                            done_reg <= 0;
                                    end
                                    1:
                                        sample_control[15:8] <= wdata[15:8] & 8'h03;
                                    default: ;
                                endcase
                            end
                            ADDR_SAMPLE_NUMBER:
                                sample_number[byte_index*8 +: 8] <= wdata[byte_index*8 +: 8];
                            default: ;
                        endcase
        end
        if (S_AXI_RVALID && S_AXI_RREADY)
            S_AXI_RVALID <= 0;
        if (S_AXI_ARVALID && S_AXI_ARREADY) begin
            S_AXI_RVALID <= 1;
            S_AXI_RDATA <= 0;
            if (read_address_valid) begin
                if (rd_addr < 8)
                    S_AXI_RDATA <= {16'd0, adc_data[rd_addr[2:0]*16 +: 16]};
                else
                    case (rd_addr)
                        ADDR_SAMPLE_INTERVAL:
                            S_AXI_RDATA <= interval_reg;
                        ADDR_SAMPLE_CONTROL:
                            S_AXI_RDATA <= sample_control;
                        ADDR_SAMPLE_NUMBER:
                            S_AXI_RDATA <= sample_number;
                        ADDR_SAMPLE_DONE:
                            S_AXI_RDATA <= {31'd0,done_reg};
                        ADDR_FIRMWARE_VERSION:
                            S_AXI_RDATA <= FIRMWARE_VERSION;
                        default: ;
                    endcase
            end
        end
    end
endmodule
