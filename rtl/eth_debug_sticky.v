`timescale 1ns / 1ps
//
// eth_debug_sticky.v
// Sticky LED helper for Ethernet/RMII bring-up.
//
// This module is intentionally separate from rmii_udp_stream_bridge so Vivado
// block-design debug mappings can change without perturbing the transport path.
//
module eth_debug_sticky (
    input  wire       clk_50,
    input  wire       rst_n,

    input  wire       eth_crsdv,
    input  wire       eth_rxerr,
    input  wire [1:0] eth_rxd,

    input  wire       rx_sfd_seen,
    input  wire       rx_packet_seen,
    input  wire       rx_packet_dropped,
    input  wire       rx_parser_error,
    input  wire       debug_rx_byte_ge_6,
    input  wire       debug_rx_byte_ge_12,
    input  wire       debug_rx_byte_ge_14,

    input  wire       tx_packet_sent,
    input  wire       tx_busy,

    output reg        led_crsdv_seen,
    output reg        led_rxerr_seen,
    output reg        led_rxd0_seen,
    output reg        led_rxd1_seen,
    output reg        led_rmii_rx_dv_seen,
    output reg        led_sfd_seen,
    output reg        led_byte_ge_6,
    output reg        led_byte_ge_14,
    output reg        led_rx_problem_seen,
    output reg        led_tx_seen
);

reg crsdv_d1;

always @(posedge clk_50) begin
    if (!rst_n) begin
        crsdv_d1            <= 1'b0;
        led_crsdv_seen      <= 1'b0;
        led_rxerr_seen      <= 1'b0;
        led_rxd0_seen       <= 1'b0;
        led_rxd1_seen       <= 1'b0;
        led_rmii_rx_dv_seen <= 1'b0;
        led_sfd_seen        <= 1'b0;
        led_byte_ge_6       <= 1'b0;
        led_byte_ge_14      <= 1'b0;
        led_rx_problem_seen <= 1'b0;
        led_tx_seen         <= 1'b0;
    end else begin
        crsdv_d1 <= eth_crsdv;

        if (eth_crsdv) begin
            led_crsdv_seen <= 1'b1;
            if (eth_rxd[0]) led_rxd0_seen <= 1'b1;
            if (eth_rxd[1]) led_rxd1_seen <= 1'b1;
        end
        if (eth_crsdv | crsdv_d1) led_rmii_rx_dv_seen <= 1'b1;
        if (eth_rxerr) led_rxerr_seen <= 1'b1;

        if (rx_sfd_seen) led_sfd_seen <= 1'b1;
        if (debug_rx_byte_ge_6) led_byte_ge_6 <= 1'b1;
        if (debug_rx_byte_ge_14 || debug_rx_byte_ge_12) led_byte_ge_14 <= 1'b1;
        if (rx_packet_seen) led_rx_problem_seen <= 1'b1;
        if (tx_packet_sent || tx_busy) led_tx_seen <= 1'b1;
    end
end

endmodule
