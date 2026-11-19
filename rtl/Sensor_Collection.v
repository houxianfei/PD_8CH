`timescale 1ns / 1ps

// afs_v1 ae1a285 Sensor_Collection: eight parallel AD7380 data lanes.
module Sensor_Collection #(
    parameter T_POWER_UP = 599999,
    parameter CONFIG_CYCLE = 2047
)(
    input                   clk,
    input                   rst_n,
    input       [7:0]       spi_di,
    output                  spi_clk,
    output                  spi_cs,
    output                  spi_do,
    output reg              valid,
    output reg [127:0]      adc_data
);
localparam DATA_WIDTH = 16;
localparam NUM_PORTS = 8;
localparam CYCLE_DELAY = 1;
localparam SAMPLING_DIV = 49;
reg [31:0] power_up_cnt;
reg [15:0] config_cnt;
reg [5:0] sampling_cnt;
reg go;
reg conversion;
reg [15:0] config_data;
wire busy;
wire done;
wire [127:0] datao;
wire [7:0] spi_do_all;
reg [CYCLE_DELAY:0] done_dn;
wire complete = done_dn[CYCLE_DELAY-1] && !done_dn[CYCLE_DELAY];

always @(posedge clk or negedge rst_n)
    if (!rst_n) begin
        power_up_cnt <= 0;
        config_cnt <= 0;
        sampling_cnt <= 0;
        config_data <= 16'hA000;
        go <= 0;
        conversion <= 0;
    end else begin
        go <= 0;
        if (power_up_cnt < T_POWER_UP)
            power_up_cnt <= power_up_cnt + 1'b1;
        else if (config_cnt < CONFIG_CYCLE) begin
            config_cnt <= config_cnt + 1'b1;
            config_data <= (config_cnt < CONFIG_CYCLE/2) ? 16'hA000 : 16'h9282;
            if (config_cnt == SAMPLING_DIV ||
                config_cnt == (CONFIG_CYCLE+1)/2 + SAMPLING_DIV)
                go <= 1;
        end else begin
            config_data <= 0;
            if (sampling_cnt == SAMPLING_DIV) begin
                sampling_cnt <= 0;
                go <= !busy;
                conversion <= 1;
            end else
                sampling_cnt <= sampling_cnt + 1'b1;
        end
    end

spi_master #(
    .DATA_WIDTH(DATA_WIDTH),
    .NUM_PORTS(NUM_PORTS),
    .CPOL(1),
    .CPHA(0),
    .CLK_DIVIDER(2),
    .SAMPLE_PHASE(0),
    .CYCLE_DELAY(CYCLE_DELAY)
) AD7380_spi_mode2_rw (
    .clk(clk),
    .resetb(rst_n),
    .go(go),
    .datai({NUM_PORTS{config_data}}),
    .datao(datao),
    .busy(busy),
    .done(done),
    .dout(spi_di),
    .din(spi_do_all),
    .csb(spi_cs),
    .sclk(spi_clk)
);
assign spi_do = spi_do_all[0];

always @(posedge clk or negedge rst_n)
    if (!rst_n) begin
        done_dn <= 0;
        valid <= 0;
        adc_data <= 0;
    end else begin
        done_dn <= {done_dn[CYCLE_DELAY-1:0], done};
        valid <= complete && conversion;
        if (complete && conversion)
            adc_data <= datao;
    end
endmodule
