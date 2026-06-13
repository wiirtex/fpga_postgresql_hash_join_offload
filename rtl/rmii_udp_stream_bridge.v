`timescale 1ns / 1ps
//
// rmii_udp_stream_bridge.v
// RMII Ethernet/IPv4/UDP <-> byte AXI4-Stream bridge for the hash-join kernel.
//
// Data flow:
//   Host UDP payload -> RMII RX -> m_axis_* -> hash_join_kernel slave (rx)
//   hash_join_kernel master (tx) -> s_axis_* -> RMII TX -> Host UDP payload
//
// The bridge is intentionally UDP-datagram based.  Kernel response bytes are
// grouped into one UDP packet when TLAST is provided by the upstream stream, or
// after TX_FLUSH_CYCLES of input idleness.
//
module rmii_udp_stream_bridge #(
    parameter [47:0] LOCAL_MAC   = 48'h02_00_00_00_00_01,
    parameter [31:0] LOCAL_IP    = 32'hA9FE_F23C,
    parameter [15:0] LOCAL_PORT  = 16'd50000,
    parameter [47:0] REMOTE_MAC  = 48'h9c_69_d3_1f_21_5e,
    parameter [31:0] REMOTE_IP   = 32'hA9FE_F23B,
    parameter [15:0] REMOTE_PORT = 16'd50000,
    parameter        ACCEPT_ANY_DEST_MAC = 1'b0,
    parameter        TEN_MBIT_MODE = 1'b0,
    parameter        HALF_DUPLEX_MODE = 1'b0,
    parameter        ENABLE_RX_DIAG_ECHO = 1'b0,
    parameter integer MAX_TX_PAYLOAD_BYTES = 1472,
    parameter integer TX_FLUSH_CYCLES = 512
) (
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 clk_50 CLK" *)
    (* X_INTERFACE_PARAMETER = "FREQ_HZ 50000000, ASSOCIATED_BUSIF m_axis:s_axis, ASSOCIATED_RESET rst_n" *)
    input  wire       clk_50,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 rst_n RST" *)
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire       rst_n,

    input  wire       eth_crsdv,
    input  wire       eth_rxerr,
    input  wire [1:0] eth_rxd,
    output wire       eth_txen,
    output wire [1:0] eth_txd,

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TDATA" *)
    (* X_INTERFACE_PARAMETER = "FREQ_HZ 50000000, TDATA_NUM_BYTES 1, HAS_TLAST 1, HAS_TKEEP 0, HAS_TSTRB 0" *)
    output wire [7:0] m_axis_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TVALID" *)
    output wire       m_axis_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TREADY" *)
    input  wire       m_axis_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TLAST" *)
    output wire       m_axis_tlast,

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_axis TDATA" *)
    (* X_INTERFACE_PARAMETER = "FREQ_HZ 50000000, TDATA_NUM_BYTES 1, HAS_TLAST 1, HAS_TKEEP 0, HAS_TSTRB 0" *)
    input  wire [7:0] s_axis_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_axis TVALID" *)
    input  wire       s_axis_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_axis TREADY" *)
    output wire       s_axis_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_axis TLAST" *)
    input  wire       s_axis_tlast,

    output wire       rx_sfd_seen,
    output wire       rx_packet_seen,
    output wire       rx_packet_dropped,
    output wire       rx_parser_error,
    output wire       tx_packet_sent,
    output wire       tx_payload_overflow,
    output wire       tx_busy,
    output wire       debug_tx_payload_tvalid,
    output wire       debug_tx_payload_tready,
    output wire       debug_tx_payload_tlast,
    output wire       debug_crsdv_seen,
    output wire       debug_rxerr_seen,
    output wire       debug_rxd0_seen,
    output wire       debug_rxd1_seen,
    output wire       debug_rmii_rx_dv_seen,
    output wire       debug_rx_frame_start_seen,
    output wire       debug_rx_byte_seen,
    output wire       debug_rx_frame_end_seen,
    output wire       debug_rx_problem_seen,
    output wire       debug_rx_byte_ge_6,
    output wire       debug_rx_byte_ge_12,
    output wire       debug_rx_byte_ge_14
);

wire [7:0] kernel_tx_payload_tdata;
wire       kernel_tx_payload_tvalid;
wire       kernel_tx_payload_tready;
wire       kernel_tx_payload_tlast;
wire [7:0] diag_tx_payload_tdata;
wire       diag_tx_payload_tvalid;
wire       diag_tx_payload_tready;
wire       diag_tx_payload_tlast;
wire [7:0] tx_payload_tdata;
wire       tx_payload_tvalid;
wire       tx_payload_tready;
wire       tx_payload_tlast;
wire       tx_framer_overflow;
wire       debug_dst_mac_accepted_i;
wire       debug_ethertype_hi_accepted_i;
wire       debug_eth_accepted_i;
wire       debug_ip_accepted_i;
wire [47:0] rx_remote_mac;
wire [31:0] rx_remote_ip;
wire [15:0] rx_remote_port;
wire       debug_rx_byte_ge_6_raw;
wire       debug_rx_byte_ge_12_raw;
wire       debug_rx_byte_ge_14_raw;

localparam [1:0] TX_MUX_NONE   = 2'd0;
localparam [1:0] TX_MUX_DIAG   = 2'd1;
localparam [1:0] TX_MUX_KERNEL = 2'd2;

reg [1:0] tx_mux_owner;
wire [1:0] tx_mux_selected =
    (tx_mux_owner != TX_MUX_NONE) ? tx_mux_owner :
    diag_tx_payload_tvalid        ? TX_MUX_DIAG :
    kernel_tx_payload_tvalid      ? TX_MUX_KERNEL :
                                    TX_MUX_NONE;
wire tx_payload_fire = tx_payload_tvalid && tx_payload_tready;

rmii_udp_rx_bridge #(
    .LOCAL_MAC(LOCAL_MAC),
    .LOCAL_IP(LOCAL_IP),
    .LOCAL_PORT(LOCAL_PORT),
    .ACCEPT_ANY_DEST_MAC(ACCEPT_ANY_DEST_MAC),
    .TEN_MBIT_MODE(TEN_MBIT_MODE)
) rmii_udp_rx_bridge_i (
    .clk_50(clk_50),
    .rst_n(rst_n),
    .eth_crsdv(eth_crsdv),
    .eth_rxerr(eth_rxerr),
    .eth_rxd(eth_rxd),
    .m_payload_tdata(m_axis_tdata),
    .m_payload_tvalid(m_axis_tvalid),
    .m_payload_tready(m_axis_tready),
    .m_payload_tlast(m_axis_tlast),
    .sfd_seen(rx_sfd_seen),
    .packet_seen(rx_packet_seen),
    .packet_dropped(rx_packet_dropped),
    .parser_error(rx_parser_error),
    .debug_dst_mac_accepted(debug_dst_mac_accepted_i),
    .debug_ethertype_hi_accepted(debug_ethertype_hi_accepted_i),
    .debug_eth_accepted(debug_eth_accepted_i),
    .debug_ip_accepted(debug_ip_accepted_i),
    .remote_mac(rx_remote_mac),
    .remote_ip(rx_remote_ip),
    .remote_port(rx_remote_port),
    .debug_crsdv_seen(debug_crsdv_seen),
    .debug_rxerr_seen(debug_rxerr_seen),
    .debug_rxd0_seen(debug_rxd0_seen),
    .debug_rxd1_seen(debug_rxd1_seen),
    .debug_rmii_rx_dv_seen(debug_rmii_rx_dv_seen),
    .debug_rx_frame_start_seen(debug_rx_frame_start_seen),
    .debug_rx_byte_seen(debug_rx_byte_seen),
    .debug_rx_frame_end_seen(debug_rx_frame_end_seen),
    .debug_rx_problem_seen(debug_rx_problem_seen),
    .debug_rx_byte_ge_6(debug_rx_byte_ge_6_raw),
    .debug_rx_byte_ge_12(debug_rx_byte_ge_12_raw),
    .debug_rx_byte_ge_14(debug_rx_byte_ge_14_raw)
);

// Diagnostic build: keep the module-ref port list stable for Vivado BD, but
// expose parser-stage progress through the existing debug pins.
assign debug_rx_byte_ge_6  = debug_dst_mac_accepted_i;
assign debug_rx_byte_ge_12 = debug_eth_accepted_i;
assign debug_rx_byte_ge_14 = debug_ip_accepted_i | rx_packet_seen;

// The HLS kernel already marks every protocol frame with TLAST.  Feeding that
// stream directly into the UDP framer avoids buffering the same payload twice
// and removes a large register array from the RMII TX path.
assign kernel_tx_payload_tdata  = s_axis_tdata;
assign kernel_tx_payload_tvalid = s_axis_tvalid;
assign kernel_tx_payload_tlast  = s_axis_tlast;
assign s_axis_tready           = kernel_tx_payload_tready;

udp_rx_diag_echo udp_rx_diag_echo_i (
    .clk(clk_50),
    .rst_n(rst_n),
    .stage_udp_header(ENABLE_RX_DIAG_ECHO ? debug_dst_mac_accepted_i : 1'b0),
    .stage_udp_port(ENABLE_RX_DIAG_ECHO ? debug_eth_accepted_i : 1'b0),
    .stage_udp_length(ENABLE_RX_DIAG_ECHO ? debug_ip_accepted_i : 1'b0),
    .stage_udp_accept(ENABLE_RX_DIAG_ECHO ? rx_packet_seen : 1'b0),
    .m_payload_tdata(diag_tx_payload_tdata),
    .m_payload_tvalid(diag_tx_payload_tvalid),
    .m_payload_tready(diag_tx_payload_tready),
    .m_payload_tlast(diag_tx_payload_tlast)
);

assign tx_payload_tdata =
    (tx_mux_selected == TX_MUX_DIAG)   ? diag_tx_payload_tdata :
    (tx_mux_selected == TX_MUX_KERNEL) ? kernel_tx_payload_tdata :
                                         8'h00;
assign tx_payload_tvalid =
    (tx_mux_selected == TX_MUX_DIAG)   ? diag_tx_payload_tvalid :
    (tx_mux_selected == TX_MUX_KERNEL) ? kernel_tx_payload_tvalid :
                                         1'b0;
assign tx_payload_tlast =
    (tx_mux_selected == TX_MUX_DIAG)   ? diag_tx_payload_tlast :
    (tx_mux_selected == TX_MUX_KERNEL) ? kernel_tx_payload_tlast :
                                         1'b0;
assign diag_tx_payload_tready    = tx_payload_tready && (tx_mux_selected == TX_MUX_DIAG);
assign kernel_tx_payload_tready  = tx_payload_tready && (tx_mux_selected == TX_MUX_KERNEL);
assign debug_tx_payload_tvalid   = tx_payload_tvalid;
assign debug_tx_payload_tready   = tx_payload_tready;
assign debug_tx_payload_tlast    = tx_payload_tlast;

always @(posedge clk_50) begin
    if (!rst_n) begin
        tx_mux_owner <= TX_MUX_NONE;
    end else if (tx_payload_fire) begin
        if (tx_payload_tlast) begin
            tx_mux_owner <= TX_MUX_NONE;
        end else if (tx_mux_owner == TX_MUX_NONE) begin
            tx_mux_owner <= tx_mux_selected;
        end
    end
end

rmii_udp_tx_bridge #(
    .LOCAL_MAC(LOCAL_MAC),
    .LOCAL_IP(LOCAL_IP),
    .LOCAL_PORT(LOCAL_PORT),
    .REMOTE_MAC(REMOTE_MAC),
    .REMOTE_IP(REMOTE_IP),
    .REMOTE_PORT(REMOTE_PORT),
    .MAX_PAYLOAD_BYTES(MAX_TX_PAYLOAD_BYTES),
    .TEN_MBIT_MODE(TEN_MBIT_MODE),
    .HALF_DUPLEX_MODE(HALF_DUPLEX_MODE)
) rmii_udp_tx_bridge_i (
    .clk_50(clk_50),
    .rst_n(rst_n),
    .carrier_sense(eth_crsdv),
    .remote_mac((rx_remote_mac == 48'd0) ? REMOTE_MAC : rx_remote_mac),
    .remote_ip((rx_remote_ip == 32'd0) ? REMOTE_IP : rx_remote_ip),
    .remote_port((rx_remote_port == 16'd0) ? REMOTE_PORT : rx_remote_port),
    .s_payload_tdata(tx_payload_tdata),
    .s_payload_tvalid(tx_payload_tvalid),
    .s_payload_tready(tx_payload_tready),
    .s_payload_tlast(tx_payload_tlast),
    .eth_txen(eth_txen),
    .eth_txd(eth_txd),
    .tx_busy(tx_busy),
    .packet_sent(tx_packet_sent),
    .payload_overflow(tx_framer_overflow)
);

assign tx_payload_overflow = tx_framer_overflow;

endmodule

module udp_rx_diag_echo (
    input  wire       clk,
    input  wire       rst_n,

    input  wire       stage_udp_header,
    input  wire       stage_udp_port,
    input  wire       stage_udp_length,
    input  wire       stage_udp_accept,

    output reg  [7:0] m_payload_tdata,
    output reg        m_payload_tvalid,
    input  wire       m_payload_tready,
    output reg        m_payload_tlast
);

localparam [1:0] ST_IDLE = 2'd0;
localparam [1:0] ST_SEND = 2'd1;

reg [1:0] state;
reg [3:0] pending_stage_bits;
reg [3:0] send_stage_bits;
reg [3:0] send_index;
reg       stage_diag_sent;

wire [3:0] incoming_stage_bits = {
    stage_udp_accept,
    stage_udp_length,
    stage_udp_port,
    stage_udp_header
};
wire output_fire = m_payload_tvalid && m_payload_tready;

function [7:0] debug_byte;
    input [3:0] idx;
    input [3:0] stage_bits;
    begin
        case (idx)
            4'd0: debug_byte = 8'h09;       // MSG_DEBUG
            4'd1: debug_byte = 8'h01;       // count = 1, little-endian
            4'd2: debug_byte = 8'h00;
            4'd3: debug_byte = 8'h00;       // DbgLevel::DEBUG
            4'd4: debug_byte = 8'he0;       // diagnostic code 0x00e0
            4'd5: debug_byte = 8'h00;
            4'd6: debug_byte = {4'h0, stage_bits};
            4'd7: debug_byte = 8'h00;
            4'd8: debug_byte = 8'h00;
            default: debug_byte = 8'h00;
        endcase
    end
endfunction

always @(posedge clk) begin
    if (!rst_n) begin
        state              <= ST_IDLE;
        pending_stage_bits <= 4'h0;
        send_stage_bits    <= 4'h0;
        send_index         <= 4'd0;
        stage_diag_sent    <= 1'b0;
        m_payload_tdata    <= 8'h00;
        m_payload_tvalid   <= 1'b0;
        m_payload_tlast    <= 1'b0;
    end else begin
        pending_stage_bits <= pending_stage_bits | incoming_stage_bits;

        case (state)
            ST_IDLE: begin
                m_payload_tvalid <= 1'b0;
                m_payload_tlast  <= 1'b0;
                send_index       <= 4'd0;
                if (stage_udp_accept || (stage_udp_length && !stage_diag_sent)) begin
                    send_stage_bits <= pending_stage_bits | incoming_stage_bits;
                    pending_stage_bits <= 4'h0;
                    m_payload_tdata  <= debug_byte(4'd0, pending_stage_bits | incoming_stage_bits);
                    m_payload_tvalid <= 1'b1;
                    m_payload_tlast  <= 1'b0;
                    stage_diag_sent  <= 1'b1;
                    state <= ST_SEND;
                end
            end

            ST_SEND: begin
                if (output_fire) begin
                    if (send_index == 4'd9) begin
                        m_payload_tvalid <= 1'b0;
                        m_payload_tlast  <= 1'b0;
                        state <= ST_IDLE;
                    end else begin
                        send_index <= send_index + 4'd1;
                        m_payload_tdata <= debug_byte(send_index + 4'd1, send_stage_bits);
                        m_payload_tlast <= (send_index == 4'd8);
                    end
                end
            end

            default: state <= ST_IDLE;
        endcase
    end
end

endmodule

module udp_stream_packetizer #(
    parameter integer MAX_PAYLOAD_BYTES = 1472,
    parameter integer FLUSH_CYCLES = 512
) (
    input  wire       clk,
    input  wire       rst_n,

    input  wire [7:0] s_axis_tdata,
    input  wire       s_axis_tvalid,
    output wire       s_axis_tready,
    input  wire       s_axis_tlast,

    output reg  [7:0] m_payload_tdata,
    output reg        m_payload_tvalid,
    input  wire       m_payload_tready,
    output reg        m_payload_tlast,

    output reg        packet_overflow
);

localparam [1:0] ST_IDLE = 2'd0;
localparam [1:0] ST_RECV = 2'd1;
localparam [1:0] ST_SEND = 2'd2;

reg [1:0] state;
reg [15:0] stored_len;
reg [15:0] send_index;
reg [31:0] idle_count;
reg [7:0] payload_mem [0:MAX_PAYLOAD_BYTES-1];

assign s_axis_tready = (state == ST_IDLE || state == ST_RECV) && (stored_len < MAX_PAYLOAD_BYTES);

wire input_fire = s_axis_tvalid && s_axis_tready;
wire flush_due = (state == ST_RECV) && (stored_len != 16'd0) && (idle_count >= FLUSH_CYCLES);
wire full_due = (state == ST_RECV) && (stored_len == MAX_PAYLOAD_BYTES);

always @(posedge clk) begin
    if (!rst_n) begin
        state            <= ST_IDLE;
        stored_len       <= 16'd0;
        send_index       <= 16'd0;
        idle_count       <= 32'd0;
        m_payload_tdata  <= 8'h00;
        m_payload_tvalid <= 1'b0;
        m_payload_tlast  <= 1'b0;
        packet_overflow  <= 1'b0;
    end else begin
        case (state)
            ST_IDLE: begin
                stored_len       <= 16'd0;
                send_index       <= 16'd0;
                idle_count       <= 32'd0;
                m_payload_tvalid <= 1'b0;
                m_payload_tlast  <= 1'b0;
                if (input_fire) begin
                    payload_mem[0] <= s_axis_tdata;
                    stored_len <= 16'd1;
                    if (s_axis_tlast) begin
                        state <= ST_SEND;
                        m_payload_tdata <= s_axis_tdata;
                        m_payload_tvalid <= 1'b1;
                        m_payload_tlast <= 1'b1;
                    end else begin
                        state <= ST_RECV;
                    end
                end
            end

            ST_RECV: begin
                if (input_fire) begin
                    payload_mem[stored_len] <= s_axis_tdata;
                    stored_len <= stored_len + 16'd1;
                    idle_count <= 32'd0;
                    if (s_axis_tlast) begin
                        state <= ST_SEND;
                        send_index <= 16'd0;
                        m_payload_tdata <= payload_mem[0];
                        m_payload_tvalid <= 1'b1;
                        m_payload_tlast <= (stored_len == 16'd0);
                    end
                end else if (s_axis_tvalid && !s_axis_tready) begin
                    packet_overflow <= 1'b1;
                end else if (flush_due || full_due) begin
                    state <= ST_SEND;
                    send_index <= 16'd0;
                    m_payload_tdata <= payload_mem[0];
                    m_payload_tvalid <= 1'b1;
                    m_payload_tlast <= (stored_len == 16'd1);
                end else begin
                    idle_count <= idle_count + 32'd1;
                end
            end

            ST_SEND: begin
                if (m_payload_tvalid && m_payload_tready) begin
                    if (send_index == stored_len - 16'd1) begin
                        state <= ST_IDLE;
                        m_payload_tvalid <= 1'b0;
                        m_payload_tlast <= 1'b0;
                    end else begin
                        send_index <= send_index + 16'd1;
                        m_payload_tdata <= payload_mem[send_index + 16'd1];
                        m_payload_tlast <= (send_index + 16'd1 == stored_len - 16'd1);
                    end
                end
            end

            default: state <= ST_IDLE;
        endcase
    end
end

endmodule
