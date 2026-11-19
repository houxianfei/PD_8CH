`timescale 1ns / 1ps

module PD (
    input                   usb_clk_60m,
    input                   clk50M,
    input                   Rst_n,
    input                   usb_rxf_n,
    input                   usb_txe_n,
    output                  usb_oe_n,
    output                  usb_rd_n,
    output                  usb_wr_n,
    output                  usb_siwu_n,
    inout [7:0]             usb_data,
	
	output                  spics,
	output                  spiclk,
	output                  spido,
	// 上层板4通道
	input [3:0]             spidiT,
	// 下层板4通道
	input [3:0]             spidiB
);
wire rst_n;
resetn_sync i_reset_sys (
    .rstn_async(Rst_n),
    .clk(clk50M),
    .rstn(rst_n)
);
wire [127:0] adc_data;
wire adc_valid;

// 上下层板共用 SPI 控制线，CH0~CH3 为上层板，CH4~CH7 为下层板。
Sensor_Collection i_Sensor_Collection (
    .clk(clk50M),
    .rst_n(rst_n),
    .spi_di({spidiB, spidiT}),
    .spi_cs(spics),
    .spi_clk(spiclk),
    .spi_do(spido),
    .valid(adc_valid),
    .adc_data(adc_data)
);
wire [63:0] rx_tdata;
wire [63:0] tx_tdata;
wire [7:0] rx_tkeep;
wire [7:0] tx_tkeep;
wire rx_tvalid;
wire rx_tready;
wire tx_tvalid;
wire tx_tready;
wire tx_tlast;
PD_core i_core (
    .clk(clk50M),
    .rst_n(rst_n),
    .adc_data(adc_data),
    .rx_tdata(rx_tdata),
    .rx_tkeep(rx_tkeep),
    .rx_tvalid(rx_tvalid),
    .rx_tready(rx_tready),
    .tx_tdata(tx_tdata),
    .tx_tkeep(tx_tkeep),
    .tx_tvalid(tx_tvalid),
    .tx_tlast(tx_tlast),
    .tx_tready(tx_tready)
);
ftdi_245fifo_top #(
    .TX_EW(3),
    .TX_EA(8),
    .RX_EW(3),
    .RX_EA(8),
    .CHIP_TYPE("FTx232H"),
    .SIMULATION(0)
) i_usb (
    .rstn_async(Rst_n),
    .tx_clk(clk50M),
    .tx_tready(tx_tready),
    .tx_tvalid(tx_tvalid),
    .tx_tdata(tx_tdata),
    .tx_tkeep(tx_tkeep),
    .tx_tlast(tx_tlast),
    .rx_clk(clk50M),
    .rx_tready(rx_tready),
    .rx_tvalid(rx_tvalid),
    .rx_tdata(rx_tdata),
    .rx_tkeep(rx_tkeep),
    .rx_tlast(),
    .ftdi_clk(usb_clk_60m),
    .ftdi_rxf_n(usb_rxf_n),
    .ftdi_txe_n(usb_txe_n),
    .ftdi_oe_n(usb_oe_n),
    .ftdi_rd_n(usb_rd_n),
    .ftdi_wr_n(usb_wr_n),
    .ftdi_data(usb_data),
    .ftdi_be()
);
assign usb_siwu_n = 1'b1;
endmodule
