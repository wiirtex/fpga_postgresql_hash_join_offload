`timescale 1ns / 1ps
//
// rmii_tx_bytes.v
// Byte-to-RMII transmit serializer.  It prepends Ethernet preamble/SFD and
// serializes each byte least-significant dibit first.
//
module rmii_tx_bytes #(
    parameter TEN_MBIT_MODE = 1'b0,
    parameter HALF_DUPLEX_MODE = 1'b0
) (
    input  wire       clk_50,
    input  wire       rst_n,
    input  wire       carrier_sense,

    input  wire [7:0] s_frame_tdata,
    input  wire       s_frame_tvalid,
    output wire       s_frame_tready,
    input  wire       s_frame_tlast,

    output reg        eth_txen,
    output reg  [1:0] eth_txd,
    output wire       tx_active
);

localparam [1:0] ST_IDLE     = 2'd0;
localparam [1:0] ST_PREAMBLE = 2'd1;
localparam [1:0] ST_DATA     = 2'd2;
localparam [1:0] ST_IFG      = 2'd3;
localparam [9:0] IFG_COUNT_INIT = TEN_MBIT_MODE ? 10'd479 : 10'd47;

reg [1:0] state;
reg [1:0] dibit_count;
reg [3:0] preamble_index;
reg [3:0] tick_count;
reg [9:0] ifg_count;
reg [7:0] current_byte;
reg       current_last;
reg       have_byte;
reg       eth_txen_core;
reg [1:0] eth_txd_core;

wire advance_now = (!TEN_MBIT_MODE) || (tick_count == 4'd0);
wire defer_active = HALF_DUPLEX_MODE && carrier_sense;
wire idle_ready = (state == ST_IDLE) && !defer_active && (ifg_count == 10'd0);
wire load_ready = idle_ready || ((state == ST_DATA) && !have_byte && advance_now);

assign s_frame_tready = load_ready;
assign tx_active = (state != ST_IDLE);

function [1:0] byte_dibit;
    input [7:0] b;
    input [1:0] idx;
    begin
        case (idx)
            2'd0: byte_dibit = b[1:0];
            2'd1: byte_dibit = b[3:2];
            2'd2: byte_dibit = b[5:4];
            default: byte_dibit = b[7:6];
        endcase
    end
endfunction

function [7:0] preamble_byte;
    input [3:0] idx;
    begin
        preamble_byte = (idx == 4'd7) ? 8'hd5 : 8'h55;
    end
endfunction

task arm_next_dibit;
    begin
        tick_count <= TEN_MBIT_MODE ? 4'd9 : 4'd0;
    end
endtask

// Keep the protocol state machine on the RMII logic clock, but launch the
// external RMII TX pins on the opposite edge.  LAN8720A samples TXEN/TXD on
// REF_CLK rising edges, so this gives the PHY close to half a cycle of setup
// time instead of depending on fabric/MMCM phase luck.
always @(negedge clk_50 or negedge rst_n) begin
    if (!rst_n) begin
        eth_txen <= 1'b0;
        eth_txd  <= 2'b00;
    end else begin
        eth_txen <= eth_txen_core;
        eth_txd  <= eth_txd_core;
    end
end

always @(posedge clk_50) begin
    if (!rst_n) begin
        state          <= ST_IDLE;
        dibit_count    <= 2'd0;
        preamble_index <= 4'd0;
        tick_count     <= 4'd0;
        ifg_count      <= 10'd0;
        current_byte   <= 8'h00;
        current_last   <= 1'b0;
        have_byte      <= 1'b0;
        eth_txen_core  <= 1'b0;
        eth_txd_core   <= 2'b00;
    end else begin
        if (TEN_MBIT_MODE && tick_count != 4'd0) begin
            tick_count <= tick_count - 4'd1;
        end else begin
            case (state)
                ST_IDLE: begin
                    eth_txen_core  <= 1'b0;
                    eth_txd_core   <= 2'b00;
                    dibit_count    <= 2'd0;
                    preamble_index <= 4'd0;
                    tick_count     <= 4'd0;
                    ifg_count      <= 10'd0;
                    have_byte      <= 1'b0;
                    current_last   <= 1'b0;

                    if (defer_active) begin
                        ifg_count <= IFG_COUNT_INIT;
                    end else if (ifg_count != 10'd0) begin
                        ifg_count <= ifg_count - 10'd1;
                    end else if (s_frame_tvalid) begin
                        state        <= ST_PREAMBLE;
                        eth_txen_core <= 1'b1;
                        eth_txd_core  <= byte_dibit(8'h55, 2'd0);
                        dibit_count  <= 2'd1;
                        current_byte <= s_frame_tdata;
                        current_last <= s_frame_tlast;
                        have_byte    <= 1'b1;
                        arm_next_dibit();
                    end
                end

                ST_PREAMBLE: begin
                    eth_txen_core <= 1'b1;
                    eth_txd_core  <= byte_dibit(preamble_byte(preamble_index), dibit_count);
                    arm_next_dibit();

                    if (dibit_count == 2'd3) begin
                        dibit_count <= 2'd0;
                        if (preamble_index == 4'd7) begin
                            state <= ST_DATA;
                        end else begin
                            preamble_index <= preamble_index + 4'd1;
                        end
                    end else begin
                        dibit_count <= dibit_count + 2'd1;
                    end
                end

                ST_DATA: begin
                    eth_txen_core <= 1'b1;
                    arm_next_dibit();

                    if (!have_byte) begin
                        if (s_frame_tvalid) begin
                            current_byte <= s_frame_tdata;
                            current_last <= s_frame_tlast;
                            have_byte    <= 1'b1;
                            eth_txd_core <= byte_dibit(s_frame_tdata, 2'd0);
                            dibit_count  <= 2'd1;
                        end else begin
                            eth_txd_core <= 2'b00;
                        end
                    end else begin
                        eth_txd_core <= byte_dibit(current_byte, dibit_count);
                        if (dibit_count == 2'd3) begin
                            dibit_count <= 2'd0;
                            have_byte   <= 1'b0;
                            if (current_last) begin
                                state <= ST_IFG;
                                ifg_count <= IFG_COUNT_INIT;
                            end
                        end else begin
                            dibit_count <= dibit_count + 2'd1;
                        end
                    end
                end

                ST_IFG: begin
                    eth_txen_core <= 1'b0;
                    eth_txd_core  <= 2'b00;
                    dibit_count <= 2'd0;
                    preamble_index <= 4'd0;
                    tick_count <= 4'd0;
                    have_byte <= 1'b0;
                    current_last <= 1'b0;
                    if (ifg_count == 10'd0) begin
                        state <= ST_IDLE;
                    end else begin
                        ifg_count <= ifg_count - 10'd1;
                    end
                end

                default: state <= ST_IDLE;
            endcase
        end
    end
end

endmodule
