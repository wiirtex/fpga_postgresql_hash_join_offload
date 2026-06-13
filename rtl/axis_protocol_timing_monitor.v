`timescale 1ns / 1ps

// Passive protocol timing monitor for the byte-wide PostgreSQL <-> FPGA stream.
//
// The monitor does not drive or modify AXI-Stream data. It observes accepted
// bytes on both directions and latches timestamps from a real free-running
// cycle counter. It is intended to provide true board-side phase timing, unlike
// HLS-local estimated counters.

module axis_protocol_timing_monitor #(
    parameter [31:0] CLOCK_HZ = 32'd70000000
) (
    input  wire       clk,
    input  wire       rst_n,
    input  wire       clear,

    // Host -> kernel stream.
    input  wire [7:0] rx_tdata,
    input  wire       rx_tvalid,
    input  wire       rx_tready,

    // Kernel -> host stream.
    input  wire [7:0] tx_tdata,
    input  wire       tx_tvalid,
    input  wire       tx_tready,

    output reg [63:0] cycle_counter,
    output reg        timing_valid,

    output reg [31:0] inner_frames,
    output reg [31:0] outer_frames,
    output reg [31:0] result_frames,
    output reg [31:0] ack_frames,
    output reg [31:0] debug_frames,
    output reg [31:0] status_frames,
    output reg [31:0] timing_frames,
    output reg [31:0] bytes_rx,
    output reg [31:0] bytes_tx,

    output reg [63:0] ts_config_first,
    output reg [63:0] ts_config_ack,
    output reg [63:0] ts_build_first,
    output reg [63:0] ts_build_last,
    output reg [63:0] ts_build_ack_last,
    output reg [63:0] ts_probe_first,
    output reg [63:0] ts_probe_last,
    output reg [63:0] ts_probe_ack_last,
    output reg [63:0] ts_result_first,
    output reg [63:0] ts_result_last,
    output reg [63:0] ts_timing_first,
    output reg [63:0] ts_done_status,

    output wire [63:0] cycles_config_to_build_done,
    output wire [63:0] cycles_config_to_probe_done,
    output wire [63:0] cycles_config_to_status_done,
    output wire [31:0] clock_hz
);

localparam [7:0] MSG_CONFIGURE  = 8'h01;
localparam [7:0] MSG_INNER_DATA = 8'h02;
localparam [7:0] MSG_OUTER_DATA = 8'h03;
localparam [7:0] MSG_RESULT     = 8'h04;
localparam [7:0] MSG_ACK        = 8'h05;
localparam [7:0] MSG_STATUS     = 8'h06;
localparam [7:0] MSG_ERROR      = 8'h07;
localparam [7:0] MSG_RESET      = 8'h08;
localparam [7:0] MSG_DEBUG      = 8'h09;
localparam [7:0] MSG_TIMING     = 8'h0A;

localparam [7:0] PHASE_BUILDING = 8'h01;
localparam [7:0] PHASE_PROBING  = 8'h02;
localparam [7:0] PHASE_DONE     = 8'h03;

localparam [7:0] KEY_INT32 = 8'h01;
localparam [7:0] KEY_INT64 = 8'h02;

assign clock_hz = CLOCK_HZ;
assign cycles_config_to_build_done =
    (ts_config_first != 64'd0 && ts_build_ack_last != 64'd0)
        ? (ts_build_ack_last - ts_config_first) : 64'd0;
wire [63:0] probe_done_ts =
    (ts_probe_ack_last != 64'd0) ? ts_probe_ack_last : ts_done_status;
assign cycles_config_to_probe_done =
    (ts_config_first != 64'd0 && probe_done_ts != 64'd0)
        ? (probe_done_ts - ts_config_first) : 64'd0;
assign cycles_config_to_status_done =
    (ts_config_first != 64'd0 && ts_done_status != 64'd0)
        ? (ts_done_status - ts_config_first) : 64'd0;

wire rx_fire = rx_tvalid && rx_tready;
wire tx_fire = tx_tvalid && tx_tready;

reg [1:0]  rx_hdr_idx;
reg [7:0]  rx_msg;
reg [15:0] rx_count;
reg [31:0] rx_payload_remaining;
reg [31:0] rx_payload_index;
reg [7:0]  key_type;

reg [1:0]  tx_hdr_idx;
reg [7:0]  tx_msg;
reg [15:0] tx_count;
reg [31:0] tx_payload_remaining;
reg [31:0] tx_payload_index;
reg [7:0]  tx_phase;

function [31:0] rx_payload_len;
    input [7:0] msg;
    input [15:0] count;
    input [7:0] key_t;
    begin
        case (msg)
        MSG_CONFIGURE:  rx_payload_len = (count == 16'd1) ? 32'd12 : 32'd0;
        MSG_INNER_DATA,
        MSG_OUTER_DATA: rx_payload_len = count * ((key_t == KEY_INT64) ? 32'd14 : 32'd10);
        MSG_RESET:      rx_payload_len = 32'd0;
        default:        rx_payload_len = 32'd0;
        endcase
    end
endfunction

function [31:0] tx_payload_len;
    input [7:0] msg;
    input [15:0] count;
    begin
        case (msg)
        MSG_ACK,
        MSG_STATUS: tx_payload_len = (count == 16'd1) ? 32'd12 : 32'd0;
        MSG_RESULT: tx_payload_len = count * 32'd12;
        MSG_ERROR:  tx_payload_len = 32'd1;
        MSG_DEBUG:  tx_payload_len = 32'd7;
        MSG_TIMING: tx_payload_len = 32'd200;
        default:    tx_payload_len = 32'd0;
        endcase
    end
endfunction

task reset_measurements;
    begin
        timing_valid <= 1'b0;
        inner_frames <= 32'd0;
        outer_frames <= 32'd0;
        result_frames <= 32'd0;
        ack_frames <= 32'd0;
        debug_frames <= 32'd0;
        status_frames <= 32'd0;
        timing_frames <= 32'd0;
        bytes_rx <= 32'd0;
        bytes_tx <= 32'd0;
        ts_config_first <= 64'd0;
        ts_config_ack <= 64'd0;
        ts_build_first <= 64'd0;
        ts_build_last <= 64'd0;
        ts_build_ack_last <= 64'd0;
        ts_probe_first <= 64'd0;
        ts_probe_last <= 64'd0;
        ts_probe_ack_last <= 64'd0;
        ts_result_first <= 64'd0;
        ts_result_last <= 64'd0;
        ts_timing_first <= 64'd0;
        ts_done_status <= 64'd0;
        key_type <= KEY_INT32;
        rx_hdr_idx <= 2'd0;
        rx_msg <= 8'd0;
        rx_count <= 16'd0;
        rx_payload_remaining <= 32'd0;
        rx_payload_index <= 32'd0;
        tx_hdr_idx <= 2'd0;
        tx_msg <= 8'd0;
        tx_count <= 16'd0;
        tx_payload_remaining <= 32'd0;
        tx_payload_index <= 32'd0;
        tx_phase <= 8'd0;
    end
endtask

always @(posedge clk) begin
    if (!rst_n) begin
        cycle_counter <= 64'd0;
        reset_measurements();
    end else begin
        cycle_counter <= cycle_counter + 64'd1;
        if (clear) begin
            reset_measurements();
        end else begin
            if (rx_fire) begin
                bytes_rx <= bytes_rx + 32'd1;

                if (rx_hdr_idx == 2'd0) begin
                    rx_msg <= rx_tdata;
                    rx_hdr_idx <= 2'd1;
                    if (rx_tdata == MSG_CONFIGURE && ts_config_first == 64'd0)
                        ts_config_first <= cycle_counter;
                    if (rx_tdata == MSG_INNER_DATA && ts_build_first == 64'd0)
                        ts_build_first <= cycle_counter;
                    if (rx_tdata == MSG_OUTER_DATA && ts_probe_first == 64'd0)
                        ts_probe_first <= cycle_counter;
                end else if (rx_hdr_idx == 2'd1) begin
                    rx_count[7:0] <= rx_tdata;
                    rx_hdr_idx <= 2'd2;
                end else if (rx_hdr_idx == 2'd2) begin
                    rx_count[15:8] <= rx_tdata;
                    rx_payload_remaining <= rx_payload_len(
                        rx_msg, {rx_tdata, rx_count[7:0]}, key_type);
                    rx_payload_index <= 32'd0;
                    if (rx_payload_len(rx_msg, {rx_tdata, rx_count[7:0]}, key_type) == 32'd0) begin
                        rx_hdr_idx <= 2'd0;
                        if (rx_msg == MSG_INNER_DATA) begin
                            inner_frames <= inner_frames + 32'd1;
                            ts_build_last <= cycle_counter;
                        end else if (rx_msg == MSG_OUTER_DATA) begin
                            outer_frames <= outer_frames + 32'd1;
                            ts_probe_last <= cycle_counter;
                        end
                    end else begin
                        rx_hdr_idx <= 2'd3;
                    end
                end else begin
                    if (rx_msg == MSG_CONFIGURE && rx_payload_index == 32'd1)
                        key_type <= rx_tdata;

                    if (rx_payload_remaining <= 32'd1) begin
                        if (rx_msg == MSG_INNER_DATA) begin
                            inner_frames <= inner_frames + 32'd1;
                            ts_build_last <= cycle_counter;
                        end else if (rx_msg == MSG_OUTER_DATA) begin
                            outer_frames <= outer_frames + 32'd1;
                            ts_probe_last <= cycle_counter;
                        end
                        rx_payload_remaining <= 32'd0;
                        rx_hdr_idx <= 2'd0;
                    end else begin
                        rx_payload_remaining <= rx_payload_remaining - 32'd1;
                    end
                    rx_payload_index <= rx_payload_index + 32'd1;
                end
            end

            if (tx_fire) begin
                bytes_tx <= bytes_tx + 32'd1;

                if (tx_hdr_idx == 2'd0) begin
                    tx_msg <= tx_tdata;
                    tx_hdr_idx <= 2'd1;
                    if (tx_tdata == MSG_RESULT && ts_result_first == 64'd0)
                        ts_result_first <= cycle_counter;
                    if (tx_tdata == MSG_TIMING && ts_timing_first == 64'd0)
                        ts_timing_first <= cycle_counter;
                end else if (tx_hdr_idx == 2'd1) begin
                    tx_count[7:0] <= tx_tdata;
                    tx_hdr_idx <= 2'd2;
                end else if (tx_hdr_idx == 2'd2) begin
                    tx_count[15:8] <= tx_tdata;
                    tx_payload_remaining <= tx_payload_len(
                        tx_msg, {tx_tdata, tx_count[7:0]});
                    tx_payload_index <= 32'd0;
                    tx_phase <= 8'd0;
                    if (tx_payload_len(tx_msg, {tx_tdata, tx_count[7:0]}) == 32'd0)
                        tx_hdr_idx <= 2'd0;
                    else
                        tx_hdr_idx <= 2'd3;
                end else begin
                    if ((tx_msg == MSG_ACK || tx_msg == MSG_STATUS) &&
                        tx_payload_index == 32'd0)
                        tx_phase <= tx_tdata;

                    if (tx_payload_remaining <= 32'd1) begin
                        if (tx_msg == MSG_RESULT) begin
                            result_frames <= result_frames + 32'd1;
                            ts_result_last <= cycle_counter;
                        end else if (tx_msg == MSG_TIMING) begin
                            timing_frames <= timing_frames + 32'd1;
                        end else if (tx_msg == MSG_DEBUG) begin
                            debug_frames <= debug_frames + 32'd1;
                        end else if (tx_msg == MSG_ACK) begin
                            ack_frames <= ack_frames + 32'd1;
                            if (tx_phase == PHASE_BUILDING || tx_payload_index == 32'd0) begin
                                if (ts_config_ack == 64'd0)
                                    ts_config_ack <= cycle_counter;
                                ts_build_ack_last <= cycle_counter;
                            end
                            if (tx_phase == PHASE_PROBING)
                                ts_probe_ack_last <= cycle_counter;
                        end else if (tx_msg == MSG_STATUS) begin
                            status_frames <= status_frames + 32'd1;
                            if (tx_phase == PHASE_DONE || tx_payload_index == 32'd0) begin
                                ts_done_status <= cycle_counter;
                                timing_valid <= 1'b1;
                            end
                        end
                        tx_payload_remaining <= 32'd0;
                        tx_hdr_idx <= 2'd0;
                    end else begin
                        tx_payload_remaining <= tx_payload_remaining - 32'd1;
                    end
                    tx_payload_index <= tx_payload_index + 32'd1;
                end
            end
        end
    end
end

endmodule
