`timescale 1ns / 1ps
//
// Fixed-configuration wrapper for a forced 10 Mb/s LAN8720A/RMII link.
//
// Vivado module-reference parameter propagation is easy to get wrong in batch
// Tcl flows, so the hardware bring-up script instantiates this wrapper when the
// host NIC is forced to 10 Mbps Full Duplex.
//
module rmii_udp_stream_bridge_10m #(
    parameter HALF_DUPLEX_MODE = 1'b0,
    parameter ENABLE_RX_DIAG_ECHO = 1'b1
) (
    input  wire       clk_50,
    input  wire       rst_n,

    input  wire       eth_crsdv,
    input  wire       eth_rxerr,
    input  wire [1:0] eth_rxd,
    output wire       eth_txen,
    output wire [1:0] eth_txd,

    output wire [7:0] m_axis_tdata,
    output wire       m_axis_tvalid,
    input  wire       m_axis_tready,
    output wire       m_axis_tlast,

    input  wire [7:0] s_axis_tdata,
    input  wire       s_axis_tvalid,
    output wire       s_axis_tready,
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

rmii_udp_stream_bridge #(
    .TEN_MBIT_MODE(1'b1),
    .HALF_DUPLEX_MODE(HALF_DUPLEX_MODE),
    .ENABLE_RX_DIAG_ECHO(ENABLE_RX_DIAG_ECHO)
) bridge_i (
    .clk_50(clk_50),
    .rst_n(rst_n),
    .eth_crsdv(eth_crsdv),
    .eth_rxerr(eth_rxerr),
    .eth_rxd(eth_rxd),
    .eth_txen(eth_txen),
    .eth_txd(eth_txd),
    .m_axis_tdata(m_axis_tdata),
    .m_axis_tvalid(m_axis_tvalid),
    .m_axis_tready(m_axis_tready),
    .m_axis_tlast(m_axis_tlast),
    .s_axis_tdata(s_axis_tdata),
    .s_axis_tvalid(s_axis_tvalid),
    .s_axis_tready(s_axis_tready),
    .s_axis_tlast(s_axis_tlast),
    .rx_sfd_seen(rx_sfd_seen),
    .rx_packet_seen(rx_packet_seen),
    .rx_packet_dropped(rx_packet_dropped),
    .rx_parser_error(rx_parser_error),
    .tx_packet_sent(tx_packet_sent),
    .tx_payload_overflow(tx_payload_overflow),
    .tx_busy(tx_busy),
    .debug_tx_payload_tvalid(debug_tx_payload_tvalid),
    .debug_tx_payload_tready(debug_tx_payload_tready),
    .debug_tx_payload_tlast(debug_tx_payload_tlast),
    .debug_crsdv_seen(debug_crsdv_seen),
    .debug_rxerr_seen(debug_rxerr_seen),
    .debug_rxd0_seen(debug_rxd0_seen),
    .debug_rxd1_seen(debug_rxd1_seen),
    .debug_rmii_rx_dv_seen(debug_rmii_rx_dv_seen),
    .debug_rx_frame_start_seen(debug_rx_frame_start_seen),
    .debug_rx_byte_seen(debug_rx_byte_seen),
    .debug_rx_frame_end_seen(debug_rx_frame_end_seen),
    .debug_rx_problem_seen(debug_rx_problem_seen),
    .debug_rx_byte_ge_6(debug_rx_byte_ge_6),
    .debug_rx_byte_ge_12(debug_rx_byte_ge_12),
    .debug_rx_byte_ge_14(debug_rx_byte_ge_14)
);

endmodule
