`timescale 1 ns / 1 ps

module USB_axi4lite #
(
    // The master requires a target slave base address.
    parameter  C_M_TARGET_SLAVE_BASE_ADDR    = 32'h40000000,
    // Width of M_AXI address bus. 
    parameter integer C_M_AXI_ADDR_WIDTH    = 32,
    // Width of M_AXI data bus. 
    parameter integer C_M_AXI_DATA_WIDTH    = 32
)
(
    // Users to add ports here
    input            clk,
    input            rst_n,
    // USB -> FPGA
    output           s_tready,
    input            s_tvalid,
    input    [63:0]  s_tdata,
    input    [7:0]   s_tkeep,
    input            s_tlast,
    // FPGA -> USB
    input            m_tready,
    output           m_tvalid,
    output   [63:0]  m_tdata,
    output   [7:0]   m_tkeep,
    output           m_tlast,

    // Asserts when ERROR is detected
    output reg  ERROR,
    // Asserts when AXI transactions is complete
    output reg  TXN_DONE,
    // AXI clock signal
    input wire  M_AXI_ACLK,
    // AXI active low reset signal
    input wire  M_AXI_ARESETN,
    // Master Interface Write Address Channel ports. Write address (issued by master)
    output wire [C_M_AXI_ADDR_WIDTH-1 : 0] M_AXI_AWADDR,
    // Write channel Protection type.
    output wire [2 : 0] M_AXI_AWPROT,
    // Write address valid. 
    output reg  M_AXI_AWVALID,
    // Write address ready. 
    input wire  M_AXI_AWREADY,
    // Master Interface Write Data Channel ports. Write data (issued by master)
    output wire [C_M_AXI_DATA_WIDTH-1 : 0] M_AXI_WDATA,
    // Write strobes. 
    output wire [C_M_AXI_DATA_WIDTH/8-1 : 0] M_AXI_WSTRB,
    // Write valid. This signal indicates that valid write data and strobes are available.
    output reg  M_AXI_WVALID,
    // Write ready. This signal indicates that the slave can accept the write data.
    input wire  M_AXI_WREADY,
    // Master Interface Write Response Channel ports. 
    input wire [1 : 0] M_AXI_BRESP,
    // Write response valid. 
    input wire  M_AXI_BVALID,
    // Response ready. This signal indicates that the master can accept a write response.
    output reg  M_AXI_BREADY,
    // Master Interface Read Address Channel ports. Read address (issued by master)
    output wire [C_M_AXI_ADDR_WIDTH-1 : 0] M_AXI_ARADDR,
    // Protection type. 
    output wire [2 : 0] M_AXI_ARPROT,
    // Read address valid. 
    output reg  M_AXI_ARVALID,
    // Read address ready. 
    input wire  M_AXI_ARREADY,
    // Master Interface Read Data Channel ports. Read data (issued by slave)
    input wire [C_M_AXI_DATA_WIDTH-1 : 0] M_AXI_RDATA,
    // Read response. This signal indicates the status of the read transfer.
    input wire [1 : 0] M_AXI_RRESP,
    // Read valid. This signal indicates that the channel is signaling the required read data.
    input wire  M_AXI_RVALID,
    // Read ready. This signal indicates that the master can accept the read data and response information.
    output reg  M_AXI_RREADY
);

// 地址对齐
localparam integer ADDR_LSB = (C_M_AXI_DATA_WIDTH/32) + 1; 

// 协议定义
localparam [7:0] FRAME_HEADER = 8'hAA;
localparam [7:0] CMD_WRITE = 8'h01;
localparam [7:0] CMD_READ  = 8'h02;

// 状态定义
localparam [2:0] IDLE        = 3'b000;
localparam [2:0] PARSE_CMD   = 3'b001;
localparam [2:0] WRITE_START = 3'b010;
localparam [2:0] WRITE_WAIT  = 3'b011;
localparam [2:0] READ_START  = 3'b100;
localparam [2:0] READ_WAIT   = 3'b101;
localparam [2:0] SEND_RESP   = 3'b110;

// 内部寄存器
(* keep = "true" *)reg [2:0] current_state, next_state;
(* keep = "true" *)reg [31:0] tx_data;
(* keep = "true" *)reg [7:0] cmd_reg;
(* keep = "true" *)reg [7:0] addr_reg;
(* keep = "true" *)reg [31:0] data_reg;
(* keep = "true" *)reg packet_valid;
(* keep = "true" *)reg tx_valid;

// AXI4LITE信号
(* keep = "true" *)reg [C_M_AXI_ADDR_WIDTH-1 : 0] axi_awaddr;
(* keep = "true" *)reg [C_M_AXI_ADDR_WIDTH-1 : 0] axi_araddr;

// 控制信号
(* keep = "true" *)reg start_single_write;
(* keep = "true" *)reg start_single_read;
(* keep = "true" *)reg write_issued;
(* keep = "true" *)reg read_issued;

// 响应监控
(* keep = "true" *)reg write_resp_error;
(* keep = "true" *)reg read_resp_error;

// 时钟和复位同步
(* keep = "true" *)wire aclk = M_AXI_ACLK;
(* keep = "true" *)wire aresetn = M_AXI_ARESETN;

// CRC8计算函数
function [7:0] calculate_crc8;
    input [7:0] current_crc;
    input [63:0] data;
    integer i;
    reg [7:0] crc;
    begin
        crc = current_crc;
        for (i = 0; i < 64; i = i + 1) begin
            if ((crc[7] ^ data[63-i]) == 1'b1) begin
                crc = {crc[6:0], 1'b0} ^ 8'h07;
            end else begin
                crc = {crc[6:0], 1'b0};
            end
        end
        calculate_crc8 = crc;
    end
endfunction

// 数据包解析和验证
always @(posedge aclk or negedge aresetn) begin
	if (!aresetn) begin
		packet_valid <= 1'b0;
		cmd_reg <= 8'h0;
		addr_reg <= 8'h0;
		data_reg <= 32'h0;
	end else if (s_tvalid && s_tready) begin
		// 检查帧头
		if (s_tdata[7:0] == FRAME_HEADER && s_tkeep == 8'hFF) begin
			cmd_reg <= s_tdata[15:8];
			addr_reg <= s_tdata[23:16];

			if (s_tdata[15:8] == CMD_WRITE) begin
				data_reg <= s_tdata[55:24];  // 4字节数据
				// 验证CRC (计算命令+地址+数据的CRC)
				packet_valid <= (calculate_crc8(8'h00, {s_tdata[55:0]}) == s_tdata[63:56]);
			end else if (s_tdata[15:8] == CMD_READ) begin
				data_reg <= 32'h0;
				// 验证CRC (计算命令+地址的CRC)
				packet_valid <= (calculate_crc8(8'h00, {32'h0, s_tdata[23:0]}) == s_tdata[63:56]);
			end else begin
				packet_valid <= 1'b0;
			end
		end else begin
			packet_valid <= 1'b0;
		end
	end else begin
		packet_valid <= 1'b0;
	end
end

// 主状态机
always @(posedge aclk or negedge aresetn) begin
	if (!aresetn) begin
		current_state <= IDLE;
	end else begin
		current_state <= next_state;
	end
end

// 状态转换逻辑
always @(*) begin
	next_state = current_state;
	case (current_state)
		IDLE: begin
			if (s_tvalid && s_tready) begin
				next_state = PARSE_CMD;
			end
		end
		PARSE_CMD: begin
			if(packet_valid)begin
				if (cmd_reg == CMD_WRITE) begin
					next_state = WRITE_START;
				end else if (cmd_reg == CMD_READ) begin
					next_state = READ_START;
				end else begin
					next_state = IDLE;
				end
			end else begin
				next_state = IDLE;
			end
		end
		WRITE_START: begin
			if (start_single_write) begin
				next_state = WRITE_WAIT;
			end
		end
		WRITE_WAIT: begin
			if (M_AXI_BVALID && M_AXI_BREADY) begin
				next_state = IDLE;
			end
		end
		READ_START: begin
			if (start_single_read) begin
				next_state = READ_WAIT;
			end
		end
		READ_WAIT: begin
			if (M_AXI_RVALID && M_AXI_RREADY) begin
				next_state = SEND_RESP;
			end
		end
		SEND_RESP: begin
			if (m_tvalid && m_tready) begin
				next_state = IDLE;
			end
		end
		default: next_state = IDLE;
	endcase
end

// AXI控制信号

// 写地址通道
always @(posedge aclk or negedge aresetn) begin
	if (!aresetn) begin
		M_AXI_AWVALID <= 1'b0;
		axi_awaddr <= 32'h0;
	end else begin
		if (start_single_write) begin
			M_AXI_AWVALID <= 1'b1;
			axi_awaddr <= {24'h0, addr_reg}; // 8-bit到32-bit地址
		end else if (M_AXI_AWREADY && M_AXI_AWVALID) begin
			M_AXI_AWVALID <= 1'b0;
		end
	end
end

// 写数据通道  
always @(posedge aclk or negedge aresetn) begin
	if (!aresetn) begin
		M_AXI_WVALID <= 1'b0;
	end else begin
		if (start_single_write) begin
			M_AXI_WVALID <= 1'b1;
		end else if (M_AXI_WREADY && M_AXI_WVALID) begin
			M_AXI_WVALID <= 1'b0;
		end
	end
end

// 写响应通道
always @(posedge aclk or negedge aresetn) begin
	if (!aresetn) begin
		M_AXI_BREADY <= 1'b0;
	end else begin
		if (M_AXI_BVALID && !M_AXI_BREADY) begin
			M_AXI_BREADY <= 1'b1;
		end else if (M_AXI_BREADY) begin
			M_AXI_BREADY <= 1'b0;
		end
	end
end

// 读地址通道
always @(posedge aclk or negedge aresetn) begin
	if (!aresetn) begin
		M_AXI_ARVALID <= 1'b0;
		axi_araddr <= 32'h0;
	end else begin
		if (start_single_read) begin
			M_AXI_ARVALID <= 1'b1;
			axi_araddr <= {24'h0, addr_reg}; // 8-bit到32-bit地址
		end else if (M_AXI_ARREADY && M_AXI_ARVALID) begin
			M_AXI_ARVALID <= 1'b0;
		end
	end
end

// 读数据通道
always @(posedge aclk or negedge aresetn) begin
	if (!aresetn) begin
		M_AXI_RREADY <= 1'b0;
	end else begin
		if (M_AXI_RVALID && !M_AXI_RREADY) begin
			M_AXI_RREADY <= 1'b1;
		end else if (M_AXI_RREADY) begin
			M_AXI_RREADY <= 1'b0;
		end
	end
end

// 事务控制信号
always @(posedge aclk or negedge aresetn) begin
	if (!aresetn) begin
		start_single_write <= 1'b0;
		start_single_read <= 1'b0;
		write_issued <= 1'b0;
		read_issued <= 1'b0;
	end else begin
		// 生成单次写脉冲
		if (current_state == WRITE_START && !M_AXI_AWVALID && !M_AXI_WVALID && 
			!M_AXI_BVALID && !write_issued) begin
			start_single_write <= 1'b1;
			write_issued <= 1'b1;
		end else begin
			start_single_write <= 1'b0;
		end
		
		// 生成单次读脉冲  
		if (current_state == READ_START && !M_AXI_ARVALID && !M_AXI_RVALID && 
			!read_issued) begin
			start_single_read <= 1'b1;
			read_issued <= 1'b1;
		end else begin
			start_single_read <= 1'b0;
		end
		
		// 清除issued标志
		if (M_AXI_BVALID && M_AXI_BREADY) begin
			write_issued <= 1'b0;
		end
		if (M_AXI_RVALID && M_AXI_RREADY) begin
			read_issued <= 1'b0;
		end
	end
end

// 响应数据生成
always @(posedge aclk or negedge aresetn) begin
	if (!aresetn) begin
		tx_valid <= 1'b0;
		tx_data <= 32'h0;
	end else if (current_state == READ_WAIT && M_AXI_RVALID && M_AXI_RREADY) begin
		tx_data <= M_AXI_RDATA;
		tx_valid <= 1'b1;
	end else if (m_tvalid && m_tready) begin
		tx_valid <= 1'b0;
	end
end

// 常量信号分配
assign s_tready = (current_state == IDLE);

assign M_AXI_AWADDR = C_M_TARGET_SLAVE_BASE_ADDR + (axi_awaddr << ADDR_LSB);
assign M_AXI_AWPROT = 3'b000;
assign M_AXI_WDATA = data_reg;
assign M_AXI_WSTRB = 4'b1111;
assign M_AXI_ARADDR = C_M_TARGET_SLAVE_BASE_ADDR + (axi_araddr << ADDR_LSB);
assign M_AXI_ARPROT = 3'b001;

assign m_tvalid = tx_valid;
assign m_tdata = {tx_data, tx_data};
assign m_tkeep = 8'hFF;
assign m_tlast = tx_valid;

// 事务完成和错误检测
always @(posedge aclk or negedge aresetn) begin
	if (!aresetn) begin
		TXN_DONE <= 1'b0;
		ERROR <= 1'b0;
		write_resp_error <= 1'b0;
		read_resp_error <= 1'b0;
	end else begin
		// 事务完成标志
		TXN_DONE <= (current_state != IDLE) && (next_state == IDLE);
		
		// 错误检测
		write_resp_error <= (M_AXI_BREADY & M_AXI_BVALID & M_AXI_BRESP[1]);
		read_resp_error <= (M_AXI_RREADY & M_AXI_RVALID & M_AXI_RRESP[1]);
		
		// CRC错误检测
		if (current_state == PARSE_CMD && !packet_valid) begin
			ERROR <= 1'b1;
		end else if (write_resp_error || read_resp_error) begin
			ERROR <= 1'b1;
		end else if (current_state == IDLE) begin
			ERROR <= 1'b0; // 新事务开始时清除错误
		end
	end
end

endmodule
