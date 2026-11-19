`timescale 1ns / 1ps

// System-clock domain integration; FTDI transport remains a separate clock domain.
module AFS_core (
    input                   clk,
    input                   rst_n,
    input [127:0]           adc_data,
    input [63:0]            rx_tdata,
    input [7:0]             rx_tkeep,
    input                   rx_tvalid,
    output                  rx_tready,
    output [63:0]           tx_tdata,
    output [7:0]            tx_tkeep,
    output                  tx_tvalid,
    output                  tx_tlast,
    input                   tx_tready
);
wire [63:0] command_data;
wire command_valid;
wire command_ready;
usb_command_assembler i_command_assembler (
    .clk(clk),
    .rst_n(rst_n),
    .s_tdata(rx_tdata),
    .s_tkeep(rx_tkeep),
    .s_tvalid(rx_tvalid),
    .s_tready(rx_tready),
    .m_tdata(command_data),
    .m_tvalid(command_valid),
    .m_tready(command_ready)
);
wire [63:0] ctrl_data;
wire ctrl_valid;
wire ctrl_last;
wire ctrl_ready;
wire [31:0] sample_interval;
wire [31:0] sample_control;
wire [31:0] sample_number;
wire sample_done;
wire clear_done;
wire [31:0]  axi_AWADDR;
wire [2:0]  axi_AWPROT;
wire  axi_AWVALID;
wire  axi_AWREADY;
wire [31:0]  axi_WDATA;
wire [3:0]  axi_WSTRB;
wire  axi_WVALID;
wire  axi_WREADY;
wire [1:0]  axi_BRESP;
wire  axi_BVALID;
wire  axi_BREADY;
wire [31:0]  axi_ARADDR;
wire [2:0]  axi_ARPROT;
wire  axi_ARVALID;
wire  axi_ARREADY;
wire [31:0]  axi_RDATA;
wire [1:0]  axi_RRESP;
wire  axi_RVALID;
wire  axi_RREADY;
USB_axi4lite i_USB_axi4lite (
    .clk(clk),
    .rst_n(rst_n),
    .s_tdata(command_data),
    .s_tkeep(8'hFF),
    .s_tvalid(command_valid),
    .s_tready(command_ready),
    .s_tlast(1'b1),
    .m_tdata(ctrl_data),
    .m_tvalid(ctrl_valid),
    .m_tlast(ctrl_last),
    .m_tready(ctrl_ready),
    .m_tkeep(),
    .ERROR(),
    .TXN_DONE(),
    .M_AXI_ACLK(clk),
    .M_AXI_ARESETN(rst_n),
    .M_AXI_AWADDR(axi_AWADDR),
    .M_AXI_AWPROT(axi_AWPROT),
    .M_AXI_AWVALID(axi_AWVALID),
    .M_AXI_AWREADY(axi_AWREADY),
    .M_AXI_WDATA(axi_WDATA),
    .M_AXI_WSTRB(axi_WSTRB),
    .M_AXI_WVALID(axi_WVALID),
    .M_AXI_WREADY(axi_WREADY),
    .M_AXI_BRESP(axi_BRESP),
    .M_AXI_BVALID(axi_BVALID),
    .M_AXI_BREADY(axi_BREADY),
    .M_AXI_ARADDR(axi_ARADDR),
    .M_AXI_ARPROT(axi_ARPROT),
    .M_AXI_ARVALID(axi_ARVALID),
    .M_AXI_ARREADY(axi_ARREADY),
    .M_AXI_RDATA(axi_RDATA),
    .M_AXI_RRESP(axi_RRESP),
    .M_AXI_RVALID(axi_RVALID),
    .M_AXI_RREADY(axi_RREADY)
);
control_register i_control_register (
    .S_AXI_ACLK(clk),
    .S_AXI_ARESETN(rst_n),
    .adc_data(adc_data),
    .sample_done(sample_done),
    .clear_done(clear_done),
    .sample_interval(sample_interval),
    .sample_control(sample_control),
    .sample_number(sample_number),
    .S_AXI_AWADDR(axi_AWADDR),
    .S_AXI_AWPROT(axi_AWPROT),
    .S_AXI_AWVALID(axi_AWVALID),
    .S_AXI_AWREADY(axi_AWREADY),
    .S_AXI_WDATA(axi_WDATA),
    .S_AXI_WSTRB(axi_WSTRB),
    .S_AXI_WVALID(axi_WVALID),
    .S_AXI_WREADY(axi_WREADY),
    .S_AXI_BRESP(axi_BRESP),
    .S_AXI_BVALID(axi_BVALID),
    .S_AXI_BREADY(axi_BREADY),
    .S_AXI_ARADDR(axi_ARADDR),
    .S_AXI_ARPROT(axi_ARPROT),
    .S_AXI_ARVALID(axi_ARVALID),
    .S_AXI_ARREADY(axi_ARREADY),
    .S_AXI_RDATA(axi_RDATA),
    .S_AXI_RRESP(axi_RRESP),
    .S_AXI_RVALID(axi_RVALID),
    .S_AXI_RREADY(axi_RREADY)
);
wire [63:0] packet_data;
wire [63:0] sample_data;
wire packet_valid;
wire packet_last;
wire packet_ready;
wire packet_busy;
wire sample_valid;
wire sample_last;
wire sample_ready;
wire sample_busy;
wire [8:0] free_words;
wire clear_request = sample_control[2];
// Stop starting new frames, finish any already exposed frame, then discard queued samples.
assign clear_done = clear_request && !packet_busy && !sample_busy;
packet i_packet (
    .clk(clk),
    .rst_n(rst_n),
    .adc_data(adc_data),
    .sample_interval(sample_interval),
    .sample_control(sample_control),
    .sample_number(sample_number),
    .sample_done(sample_done),
    .pause(clear_request),
    .free_words(free_words),
    .busy(packet_busy),
    .m_tdata(packet_data),
    .m_tkeep(),
    .m_tvalid(packet_valid),
    .m_tlast(packet_last),
    .m_tready(packet_ready)
);
axis_fifo i_packet_fifo (
    .clk(clk),
    .rst_n(rst_n),
    .clear(clear_done),
    .s_tdata(packet_data),
    .s_tvalid(packet_valid),
    .s_tlast(packet_last),
    .s_tready(packet_ready),
    .m_tdata(sample_data),
    .m_tvalid(sample_valid),
    .m_tlast(sample_last),
    .m_tready(sample_ready),
    .free_words(free_words)
);
axis_frame_arbiter i_tx_arbiter (
    .clk(clk),
    .rst_n(rst_n),
    .pause_sample(clear_request),
    .ctrl_data(ctrl_data),
    .ctrl_valid(ctrl_valid),
    .ctrl_last(ctrl_last),
    .ctrl_ready(ctrl_ready),
    .sample_data(sample_data),
    .sample_valid(sample_valid),
    .sample_last(sample_last),
    .sample_ready(sample_ready),
    .sample_busy(sample_busy),
    .m_tdata(tx_tdata),
    .m_tvalid(tx_tvalid),
    .m_tlast(tx_tlast),
    .m_tready(tx_tready)
);
assign tx_tkeep = 8'hFF;
endmodule
