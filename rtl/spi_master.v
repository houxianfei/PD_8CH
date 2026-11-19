`timescale 1ns / 1ps

module spi_master
#(
    parameter   DATA_WIDTH          =   16  ,
    parameter   NUM_PORTS           =   1   ,
    parameter   CPOL                =   0   ,
    parameter   CPHA                =   0   ,
    parameter   CLK_DIVIDER         =   2   ,
    parameter   SAMPLE_PHASE        =   0   ,
    parameter   CYCLE_DELAY         =   0
)
(
    input                               clk         ,
    input                               resetb      ,
    input                               go          ,
    input [(NUM_PORTS*DATA_WIDTH)-1:0]  datai       ,
    output [(NUM_PORTS*DATA_WIDTH)-1:0] datao       ,
    output reg                          busy        ,
    output reg                          done        ,
    input [NUM_PORTS-1:0]               dout        ,
    output [NUM_PORTS-1:0]              din         ,
    output reg                          csb         ,
    output reg                          sclk
);
localparam  LOG2_DATA_WIDTH = $clog2(DATA_WIDTH+1);
localparam  CLK_DIVIDER_WIDTH   =   8;
localparam [0:0] IDLE_STATE = 0,
            RUN_STATE   = 1;

reg     [CLK_DIVIDER_WIDTH-1:0]  clk_count;
reg     state;
reg     [LOG2_DATA_WIDTH:0] shift_count;
reg     stop_s;
wire [CLK_DIVIDER_WIDTH-1:0] next_clk_count = clk_count + 1'b1;
wire pulse = next_clk_count == (CLK_DIVIDER >> 1);
wire stop = shift_count >= 2*DATA_WIDTH-1;



generate if(CPHA)begin
    sro1 #(
        .DATA_WIDTH(DATA_WIDTH),
        .CYCLE_DELAY(CYCLE_DELAY)
    ) sro1[NUM_PORTS-1:0]
    (
        .clk(clk),
        .resetb(resetb),
        .shift(pulse && !csb && (shift_count[0] == SAMPLE_PHASE) && !stop_s),
        .dout(dout),
        .datao(datao)
    );
end else begin
    sro0 #(
        .DATA_WIDTH(DATA_WIDTH),
        .CYCLE_DELAY(CYCLE_DELAY)
    )
    sro0[NUM_PORTS-1:0]
    (
        .clk(clk),
        .resetb(resetb),
        .shift(pulse && !csb && (shift_count[0] == SAMPLE_PHASE) && !stop_s),
        .dout(dout),
        .datao(datao)
    );
end endgenerate

sri #(.DATA_WIDTH(DATA_WIDTH)) sri[NUM_PORTS-1:0]
(
    .clk(clk),
    .resetb(resetb),
    .datai(datai),
    .sample(go && (state == IDLE_STATE)), // we condition on state so that if the user holds 'go' high, this will sample only at the start of the transfer
    .shift(pulse && !csb && (shift_count[0] == 1) && !stop),
    .din(din)
);


always @(posedge clk or negedge resetb) begin
    if(!resetb) begin
        clk_count <= 0;
        shift_count <= 0;
        sclk  <= 1;
        csb   <= 1;
        state <= IDLE_STATE;
        busy  <= 0;
        done  <= 0;
        stop_s <= 0;
    end else begin
         // generate the pulse train
        if(pulse) begin
            clk_count <= 0;
            stop_s <= stop;
        end else begin
            clk_count <= next_clk_count;
        end

        // generate csb
        if(state == IDLE_STATE) begin
            csb  <= 1;
            shift_count <= 0;
            done <= 0;
            if(go && !busy) begin // the !busy condition here allows the user to hold go high and this will then run transactions back-to-back at maximum speed where busy drops at for at least one clock cycle but we stay in this idle state for two clock cycles. Staying in idle state for two cycles probably isn't a big deal since the serial clock is running slower anyway.
               state  <= RUN_STATE;
               busy   <= 1;
            end else begin
               busy   <= 0;
            end
        end else begin
            if(pulse) begin
                if(stop) begin
                    //csb <= 1;
                    if(done) begin
                        state <= IDLE_STATE;
                        done <= 0;
                        busy <= 0;
                    end else begin
                        done  <= 1;
                    end
                end else begin
                    csb <= 0;
                    if(!csb) begin
                        shift_count <= shift_count + 1'b1;
                    end
                end
            end
        end

        // generate sclk
        if(pulse) begin
           if((CPHA==1 && state==RUN_STATE && !stop) ||
              (CPHA==0 && !csb && !stop)) begin
              sclk <= !sclk;
           end else begin
              sclk <= (CPOL != 0);
           end
        end
    end
end
endmodule // spi_master

module sri
  // This is a shift register that sends data out to the di lines of
  // spi slaves.
  #(parameter DATA_WIDTH=16)
  (input clk,
   input resetb,
   input [DATA_WIDTH-1:0] datai,
   input sample,
   input shift,
   output din
   );

   reg [DATA_WIDTH-1:0] sr_reg;
   assign din = sr_reg[DATA_WIDTH-1];


    always @(posedge clk or negedge resetb) begin
        if(!resetb) begin
            sr_reg <= 0;
        end else begin
            if(sample) begin
                sr_reg <= datai;
            end else if(shift) begin
                sr_reg <= sr_reg << 1;
            end
        end
    end
endmodule

module sro0
  // This is a shift register that receives data on the dout lines
  // from spi slaves.
    #(
        parameter DATA_WIDTH=16,
        parameter CYCLE_DELAY = 0
    )
    (
        input clk,
        input resetb,
        input shift,
        input dout,
        output reg [DATA_WIDTH-1:0] datao
    );

    wire    [CYCLE_DELAY+1:0] shift_dn_1;
    reg [CYCLE_DELAY:0] shift_dn;

    generate if(CYCLE_DELAY>0)begin
        always @(posedge clk or negedge resetb)
            if(!resetb)
                shift_dn <= 0;
            else
                shift_dn <= {shift_dn[CYCLE_DELAY-1:0], shift};
        end else begin
        always @(posedge clk or negedge resetb)
            if(!resetb)
                shift_dn <= 0;
            else
                shift_dn <= shift;
        end
    endgenerate

    assign shift_dn_1 = {shift_dn, shift};

    always @(posedge clk or negedge resetb) begin

        if(!resetb) begin
            datao <= 0;
        end else begin
            if(shift_dn_1[CYCLE_DELAY]) begin
                datao <= { datao[DATA_WIDTH-2:0], dout };
            end
        end
    end
endmodule

module sro1
  // This is a shift register that receives data on the dout lines
  // from spi slaves.
    #(
        parameter DATA_WIDTH=16,
        parameter CYCLE_DELAY = 0
    )
    (
        input clk,
        input resetb,
        input shift,
        input dout,
        output reg [DATA_WIDTH-1:0] datao
    );


    wire    [CYCLE_DELAY+1:0] shift_dn_1;
    reg [CYCLE_DELAY:0] shift_dn;

    generate if(CYCLE_DELAY>0)begin
        always @(posedge clk or negedge resetb)
            if(!resetb)
                shift_dn <= 0;
            else
                shift_dn <= {shift_dn[CYCLE_DELAY-1:0], shift};
        end else begin
        always @(posedge clk or negedge resetb)
            if(!resetb)
                shift_dn <= 0;
            else
                shift_dn <= shift;
        end
    endgenerate

    assign shift_dn_1 = {shift_dn, shift};

    reg  dout_s;

    always @(posedge clk or negedge resetb) begin

        if(!resetb) begin
            dout_s <= 0;
            datao <= 0;
        end else begin
            dout_s <= dout;
            if(shift_dn_1[CYCLE_DELAY]) begin
                datao <= { datao[DATA_WIDTH-2:0], dout_s };
            end
        end
    end
endmodule
