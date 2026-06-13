`timescale 1ns / 1ps
//
// rmii_link_probe.v
// Minimal Nexys A7 LAN8720A RMII bring-up helper.
//
// This is not the final UDP bridge.  It only drives the PHY reset, forwards a
// 50 MHz RMII reference clock to the PHY, keeps TX idle, and exposes simple RX
// activity/error indicators.  Use it to validate Ethernet pins, clocking, and
// link/activity before replacing the UART stream bridge with a real UDP bridge.
//
module rmii_link_probe #(
    parameter integer RESET_HOLD_CYCLES = 5_000_000,
    parameter integer ACTIVITY_HOLD_CYCLES = 5_000_000
) (
    input  wire       clk_50,
    input  wire       rst_n,

    // Nexys A7 / LAN8720A RMII pins.
    output wire       eth_refclk,
    output reg        eth_rstn,
    input  wire       eth_crsdv,
    input  wire       eth_rxerr,
    input  wire [1:0] eth_rxd,
    output wire       eth_txen,
    output wire [1:0] eth_txd,
    output wire       eth_mdc,
    inout  wire       eth_mdio,
    input  wire       eth_intn,

    // Optional board/debug indicators.
    output reg        link_activity,
    output reg        rx_error_seen,
    output wire       phy_reset_released,
    output wire       clk_heartbeat,
    output reg        crsdv_seen,
    output reg        udp_packet_seen,
    output reg  [7:0] rx_sample
);

assign eth_refclk = clk_50;
assign eth_txen   = 1'b0;
assign eth_txd    = 2'b00;

// Keep MDIO idle until we add a management interface.
assign eth_mdc  = 1'b0;
assign eth_mdio = 1'bz;

reg [31:0] reset_counter;
reg [31:0] activity_counter;
reg [31:0] heartbeat_counter;
reg        eth_intn_seen_low;

assign phy_reset_released = eth_rstn;
assign clk_heartbeat = heartbeat_counter[25];

wire [7:0] udp_payload_data;
wire       udp_payload_valid;
wire       udp_payload_last;
wire       udp_sfd_seen;
wire       udp_packet_pulse;
wire       udp_packet_dropped;
wire       udp_parser_error;
wire       udp_dst_mac_accepted;
wire       udp_ethertype_hi_accepted;
wire       udp_eth_accepted;
wire       udp_ip_accepted;
wire       udp_rx_byte_ge_6;
wire       udp_rx_byte_ge_12;
wire       udp_rx_byte_ge_14;

rmii_udp_rx_bridge #(
    .LOCAL_MAC(48'h02_00_00_00_00_01),
    .LOCAL_IP(32'hA9FE_F23C),
    .LOCAL_PORT(16'd50000),
    .ACCEPT_ANY_DEST_MAC(1'b1)
) udp_rx_bridge_i (
    .clk_50(clk_50),
    .rst_n(rst_n && eth_rstn),
    .eth_crsdv(eth_crsdv),
    .eth_rxerr(eth_rxerr),
    .eth_rxd(eth_rxd),
    .m_payload_tdata(udp_payload_data),
    .m_payload_tvalid(udp_payload_valid),
    .m_payload_tready(1'b1),
    .m_payload_tlast(udp_payload_last),
    .sfd_seen(udp_sfd_seen),
    .packet_seen(udp_packet_pulse),
    .packet_dropped(udp_packet_dropped),
    .parser_error(udp_parser_error),
    .debug_dst_mac_accepted(udp_dst_mac_accepted),
    .debug_ethertype_hi_accepted(udp_ethertype_hi_accepted),
    .debug_eth_accepted(udp_eth_accepted),
    .debug_ip_accepted(udp_ip_accepted),
    .debug_rx_byte_ge_6(udp_rx_byte_ge_6),
    .debug_rx_byte_ge_12(udp_rx_byte_ge_12),
    .debug_rx_byte_ge_14(udp_rx_byte_ge_14)
);

always @(posedge clk_50) begin
    if (!rst_n) begin
        reset_counter    <= 32'd0;
        activity_counter <= 32'd0;
        heartbeat_counter <= 32'd0;
        eth_rstn         <= 1'b0;
        link_activity    <= 1'b0;
        rx_error_seen    <= 1'b0;
        crsdv_seen       <= 1'b0;
        udp_packet_seen  <= 1'b0;
        rx_sample        <= 8'h00;
        eth_intn_seen_low <= 1'b0;
    end else begin
        heartbeat_counter <= heartbeat_counter + 32'd1;

        if (reset_counter < RESET_HOLD_CYCLES) begin
            reset_counter <= reset_counter + 32'd1;
            eth_rstn      <= 1'b0;
        end else begin
            eth_rstn <= 1'b1;
        end

        if (!eth_intn) begin
            eth_intn_seen_low <= 1'b1;
            rx_sample[5] <= 1'b1;
        end

        if (eth_crsdv) begin
            activity_counter <= ACTIVITY_HOLD_CYCLES;
        end else if (activity_counter != 32'd0) begin
            activity_counter <= activity_counter - 32'd1;
        end

        if (udp_sfd_seen) begin
            rx_sample[2] <= 1'b1;
            rx_error_seen <= 1'b1;
        end
        if (udp_rx_byte_ge_6) begin
            rx_sample[3] <= 1'b1;
        end
        if (udp_rx_byte_ge_12) begin
            rx_sample[4] <= 1'b1;
        end
        if (udp_rx_byte_ge_14) begin
            rx_sample[0] <= 1'b1;
        end
        if (udp_packet_pulse) begin
            crsdv_seen <= 1'b1;
            udp_packet_seen <= 1'b1;
            rx_sample[6] <= 1'b1;
        end

        link_activity <= (activity_counter != 32'd0);
        rx_sample[1] <= rx_sample[1] | eth_rxerr | udp_packet_dropped | udp_parser_error;
        rx_sample[7] <= rx_sample[7] | eth_rxerr;
    end
end

endmodule
