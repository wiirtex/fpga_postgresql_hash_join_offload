`timescale 1ns / 1ps
//
// eth_led_mux.v
// Switch-selectable LED pages for Ethernet/RMII/MDIO bring-up.
//
module eth_led_pages_v3 (
    input  wire       page0,
    input  wire       page1,
    input  wire       page2,
    input  wire       page3,

    input  wire       heartbeat,
    input  wire       eth_rstn,
    input  wire       eth_crsdv,
    input  wire       eth_rxerr,
    input  wire [1:0] eth_rxd,
    input  wire       eth_txen,
    input  wire [1:0] eth_txd,
    input  wire       eth_intn,

    input  wire       rx_crsdv_seen,
    input  wire       rx_rxerr_seen,
    input  wire       rx_rxd0_seen,
    input  wire       rx_rxd1_seen,
    input  wire       rx_sfd_seen,
    input  wire       rx_byte_ge_6,
    input  wire       rx_byte_ge_14,
    input  wire       rx_packet_seen,
    input  wire       rx_packet_dropped,
    input  wire       rx_parser_error,

    input  wire       tx_pin_txen_seen,
    input  wire       tx_pin_txd0_seen,
    input  wire       tx_pin_txd1_seen,
    input  wire       tx_activity_seen,
    input  wire       tx_busy_seen,
    input  wire       tx_packet_sent_seen,
    input  wire       tx_overflow_seen,
    input  wire       tx_busy,
    input  wire       tx_packet_sent,
    input  wire       tx_payload_overflow,
    input  wire       dbg_valid,
    input  wire       dbg_ready,
    input  wire       dbg_last,

    input  wire [15:0] bmcr,
    input  wire [15:0] bmsr,
    input  wire [15:0] anar,
    input  wire [15:0] anlpar,
    input  wire [15:0] special_status,
    input  wire        regs_valid,
    input  wire        mdio_active,
    input  wire        link_up,
    input  wire        auto_neg_complete,
    input  wire        speed_100,
    input  wire        full_duplex,

    input  wire [15:0] tx_cap_frame_count,
    input  wire [15:0] tx_cap_packet_sent_count,
    input  wire [15:0] tx_cap_overflow_count,
    input  wire [15:0] tx_cap_last_txen_clocks,
    input  wire [15:0] tx_cap_active_txen_clocks,
    input  wire [15:0] tx_cap_last_tx_dibits,
    input  wire [15:0] tx_cap_active_tx_dibits,
    input  wire [31:0] tx_cap_first16_dibits,
    input  wire [15:0] tx_cap_status_flags,

    output reg        led0,
    output reg        led1,
    output reg        led2,
    output reg        led3,
    output reg        led4,
    output reg        led5,
    output reg        led6,
    output reg        led7,
    output reg        led8,
    output reg        led9,
    output reg        led10,
    output reg        led11,
    output reg        led12,
    output reg        led13,
    output reg        led14,
    output reg        led15
);

wire [3:0] page = {page3, page2, page1, page0};

wire [15:0] summary_page = {
    heartbeat,
    ~eth_intn,
    eth_txd[1],
    eth_txd[0],
    eth_txen,
    eth_rxd[1],
    eth_rxd[0],
    eth_rxerr,
    eth_crsdv,
    full_duplex,
    speed_100,
    auto_neg_complete,
    link_up,
    regs_valid,
    mdio_active,
    eth_rstn
};

wire [15:0] sticky_page = {
    heartbeat,
    dbg_last,
    dbg_ready,
    dbg_valid,
    tx_overflow_seen,
    tx_packet_sent_seen,
    tx_busy_seen,
    tx_activity_seen,
    tx_pin_txd1_seen,
    tx_pin_txd0_seen,
    tx_pin_txen_seen,
    rx_packet_seen,
    rx_byte_ge_14,
    rx_byte_ge_6,
    rx_sfd_seen,
    rx_crsdv_seen
};

wire [15:0] error_page = {
    heartbeat,
    1'b0,
    tx_payload_overflow,
    tx_packet_sent,
    tx_busy,
    rx_parser_error,
    rx_packet_dropped,
    rx_rxerr_seen,
    eth_rxerr,
    eth_crsdv,
    rx_rxd1_seen,
    rx_rxd0_seen,
    tx_pin_txd1_seen,
    tx_pin_txd0_seen,
    tx_pin_txen_seen,
    regs_valid
};

wire [15:0] selected =
    (page == 4'h0) ? summary_page :
    (page == 4'h1) ? sticky_page :
    (page == 4'h2) ? bmcr :
    (page == 4'h3) ? bmsr :
    (page == 4'h4) ? anar :
    (page == 4'h5) ? anlpar :
    (page == 4'h6) ? special_status :
    (page == 4'h7) ? error_page :
    (page == 4'h8) ? tx_cap_status_flags :
    (page == 4'h9) ? tx_cap_last_txen_clocks :
    (page == 4'ha) ? tx_cap_last_tx_dibits :
    (page == 4'hb) ? tx_cap_frame_count :
    (page == 4'hc) ? tx_cap_packet_sent_count :
    (page == 4'hd) ? tx_cap_first16_dibits[15:0] :
    (page == 4'he) ? tx_cap_first16_dibits[31:16] :
    (page == 4'hf) ? {tx_cap_overflow_count[7:0], tx_cap_active_tx_dibits[7:0]} :
                     {4'hE, page, 8'h7E};

always @* begin
    {led15, led14, led13, led12, led11, led10, led9, led8,
     led7, led6, led5, led4, led3, led2, led1, led0} = selected;
end

endmodule
