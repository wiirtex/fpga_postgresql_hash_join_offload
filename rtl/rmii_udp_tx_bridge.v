`timescale 1ns / 1ps
//
// rmii_udp_tx_bridge.v
// UDP/IP/Ethernet transmit path followed by RMII byte serialization.
//
module rmii_udp_tx_bridge #(
    parameter [47:0] LOCAL_MAC   = 48'h02_00_00_00_00_01,
    parameter [31:0] LOCAL_IP    = 32'hA9FE_F23C,
    parameter [15:0] LOCAL_PORT  = 16'd50000,
    parameter [47:0] REMOTE_MAC  = 48'h9c_69_d3_1f_21_5e,
    parameter [31:0] REMOTE_IP   = 32'hA9FE_F23B,
    parameter [15:0] REMOTE_PORT = 16'd50000,
    parameter integer MAX_PAYLOAD_BYTES = 1472,
    parameter        TEN_MBIT_MODE = 1'b0,
    parameter        HALF_DUPLEX_MODE = 1'b0
) (
    input  wire       clk_50,
    input  wire       rst_n,
    input  wire       carrier_sense,

    input  wire [47:0] remote_mac,
    input  wire [31:0] remote_ip,
    input  wire [15:0] remote_port,

    input  wire [7:0] s_payload_tdata,
    input  wire       s_payload_tvalid,
    output wire       s_payload_tready,
    input  wire       s_payload_tlast,

    output wire       eth_txen,
    output wire [1:0] eth_txd,

    output wire       tx_busy,
    output wire       packet_sent,
    output wire       payload_overflow
);

wire [7:0] frame_tdata;
wire       frame_tvalid;
wire       frame_tready;
wire       frame_tlast;
wire       framer_busy;
wire       serializer_busy;

udp_ipv4_tx #(
    .LOCAL_MAC(LOCAL_MAC),
    .LOCAL_IP(LOCAL_IP),
    .LOCAL_PORT(LOCAL_PORT),
    .REMOTE_MAC(REMOTE_MAC),
    .REMOTE_IP(REMOTE_IP),
    .REMOTE_PORT(REMOTE_PORT),
    .MAX_PAYLOAD_BYTES(MAX_PAYLOAD_BYTES)
) udp_ipv4_tx_i (
    .clk(clk_50),
    .rst_n(rst_n),
    .remote_mac(remote_mac),
    .remote_ip(remote_ip),
    .remote_port(remote_port),
    .s_payload_tdata(s_payload_tdata),
    .s_payload_tvalid(s_payload_tvalid),
    .s_payload_tready(s_payload_tready),
    .s_payload_tlast(s_payload_tlast),
    .m_frame_tdata(frame_tdata),
    .m_frame_tvalid(frame_tvalid),
    .m_frame_tready(frame_tready),
    .m_frame_tlast(frame_tlast),
    .tx_busy(framer_busy),
    .packet_sent(packet_sent),
    .payload_overflow(payload_overflow)
);

rmii_tx_bytes #(
    .TEN_MBIT_MODE(TEN_MBIT_MODE),
    .HALF_DUPLEX_MODE(HALF_DUPLEX_MODE)
) rmii_tx_bytes_i (
    .clk_50(clk_50),
    .rst_n(rst_n),
    .carrier_sense(carrier_sense),
    .s_frame_tdata(frame_tdata),
    .s_frame_tvalid(frame_tvalid),
    .s_frame_tready(frame_tready),
    .s_frame_tlast(frame_tlast),
    .eth_txen(eth_txen),
    .eth_txd(eth_txd),
    .tx_active(serializer_busy)
);

assign tx_busy = framer_busy | serializer_busy;

endmodule
