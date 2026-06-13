`timescale 1ns / 1ps
//
// rmii_udp_rx_bridge.v
// Receive-only RMII Ethernet/IPv4/UDP bridge.
//
// This module is the first functional Ethernet transport slice.  It is not yet
// connected to the hash-join kernel in the Vivado block design.
//
module rmii_udp_rx_bridge #(
    parameter [47:0] LOCAL_MAC  = 48'h02_00_00_00_00_01,
    parameter [31:0] LOCAL_IP   = 32'hA9FE_F23C,
    parameter [15:0] LOCAL_PORT = 16'd50000,
    parameter        ACCEPT_ANY_DEST_MAC = 1'b0,
    parameter        TEN_MBIT_MODE = 1'b0
) (
    input  wire       clk_50,
    input  wire       rst_n,

    input  wire       eth_crsdv,
    input  wire       eth_rxerr,
    input  wire [1:0] eth_rxd,

    output wire [7:0] m_payload_tdata,
    output wire       m_payload_tvalid,
    input  wire       m_payload_tready,
    output wire       m_payload_tlast,

    output wire       sfd_seen,
    output wire       packet_seen,
    output wire       packet_dropped,
    output wire       parser_error,
    output wire       debug_dst_mac_accepted,
    output wire       debug_ethertype_hi_accepted,
    output wire       debug_eth_accepted,
    output wire       debug_ip_accepted,
    output wire [47:0] remote_mac,
    output wire [31:0] remote_ip,
    output wire [15:0] remote_port,
    output reg        debug_crsdv_seen,
    output reg        debug_rxerr_seen,
    output reg        debug_rxd0_seen,
    output reg        debug_rxd1_seen,
    output reg        debug_rmii_rx_dv_seen,
    output reg        debug_rx_frame_start_seen,
    output reg        debug_rx_byte_seen,
    output reg        debug_rx_frame_end_seen,
    output reg        debug_rx_problem_seen,
    output reg        debug_rx_byte_ge_6,
    output reg        debug_rx_byte_ge_12,
    output reg        debug_rx_byte_ge_14
);

wire [7:0] rx_byte;
wire       rx_byte_valid;
wire       rx_frame_start;
wire       rx_frame_end;
wire       rx_frame_error;
reg [5:0]  debug_rx_byte_count;
reg        crsdv_d1;

rmii_rx_bytes #(
    .TEN_MBIT_MODE(TEN_MBIT_MODE)
) rmii_rx_bytes_i (
    .clk_50(clk_50),
    .rst_n(rst_n),
    .eth_crsdv(eth_crsdv),
    .eth_rxerr(eth_rxerr),
    .eth_rxd(eth_rxd),
    .rx_byte(rx_byte),
    .rx_byte_valid(rx_byte_valid),
    .rx_frame_start(rx_frame_start),
    .rx_frame_end(rx_frame_end),
    .rx_frame_error(rx_frame_error),
    .sfd_seen(sfd_seen)
);

always @(posedge clk_50) begin
    if (!rst_n) begin
        debug_rx_byte_count <= 6'd0;
        debug_crsdv_seen    <= 1'b0;
        debug_rxerr_seen    <= 1'b0;
        debug_rxd0_seen     <= 1'b0;
        debug_rxd1_seen     <= 1'b0;
        debug_rmii_rx_dv_seen <= 1'b0;
        debug_rx_frame_start_seen <= 1'b0;
        debug_rx_byte_seen  <= 1'b0;
        debug_rx_frame_end_seen <= 1'b0;
        debug_rx_problem_seen <= 1'b0;
        debug_rx_byte_ge_6  <= 1'b0;
        debug_rx_byte_ge_12 <= 1'b0;
        debug_rx_byte_ge_14 <= 1'b0;
        crsdv_d1 <= 1'b0;
    end else begin
        crsdv_d1 <= eth_crsdv;
        if (eth_crsdv) begin
            debug_crsdv_seen <= 1'b1;
            if (eth_rxd[0]) debug_rxd0_seen <= 1'b1;
            if (eth_rxd[1]) debug_rxd1_seen <= 1'b1;
        end
        if (eth_crsdv | crsdv_d1) begin
            debug_rmii_rx_dv_seen <= 1'b1;
        end
        if (eth_rxerr) begin
            debug_rxerr_seen <= 1'b1;
        end
        if (rx_frame_error || parser_error || packet_dropped) begin
            debug_rx_problem_seen <= 1'b1;
        end
        if (rx_frame_start) begin
            debug_rx_byte_count <= 6'd0;
            debug_rx_frame_start_seen <= 1'b1;
        end else if (rx_frame_end) begin
            debug_rx_byte_count <= 6'd0;
            debug_rx_frame_end_seen <= 1'b1;
        end else if (rx_byte_valid) begin
            debug_rx_byte_seen <= 1'b1;
            if (debug_rx_byte_count != 6'h3f) begin
                debug_rx_byte_count <= debug_rx_byte_count + 6'd1;
            end
            if (debug_rx_byte_count >= 6'd5)  debug_rx_byte_ge_6  <= 1'b1;
            if (debug_rx_byte_count >= 6'd11) debug_rx_byte_ge_12 <= 1'b1;
            if (debug_rx_byte_count >= 6'd13) debug_rx_byte_ge_14 <= 1'b1;
        end
    end
end

udp_ipv4_rx #(
    .LOCAL_MAC(LOCAL_MAC),
    .LOCAL_IP(LOCAL_IP),
    .LOCAL_PORT(LOCAL_PORT),
    .ACCEPT_ANY_DEST_MAC(ACCEPT_ANY_DEST_MAC)
) udp_ipv4_rx_i (
    .clk(clk_50),
    .rst_n(rst_n),
    .rx_byte(rx_byte),
    .rx_byte_valid(rx_byte_valid),
    .rx_frame_start(rx_frame_start),
    .rx_frame_end(rx_frame_end),
    .rx_frame_error(rx_frame_error),
    .m_payload_tdata(m_payload_tdata),
    .m_payload_tvalid(m_payload_tvalid),
    .m_payload_tready(m_payload_tready),
    .m_payload_tlast(m_payload_tlast),
    .packet_seen(packet_seen),
    .packet_dropped(packet_dropped),
    .parser_error(parser_error),
    .debug_dst_mac_accepted(debug_dst_mac_accepted),
    .debug_ethertype_hi_accepted(debug_ethertype_hi_accepted),
    .debug_eth_accepted(debug_eth_accepted),
    .debug_ip_accepted(debug_ip_accepted),
    .remote_mac(remote_mac),
    .remote_ip(remote_ip),
    .remote_port(remote_port)
);

endmodule
