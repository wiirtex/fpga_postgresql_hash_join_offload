`timescale 1ns / 1ps
//
// rmii_refclk_forward.v
// Forward a fabric/MMCM RMII reference clock through an output DDR register.
//
// Keeping the PHY REF_CLK launch in the output structure reduces clock routing
// ambiguity relative to TXEN/TXD IOB registers.
//
module rmii_refclk_forward (
    input  wire clk_50,
    output wire eth_refclk
);

ODDR #(
    .DDR_CLK_EDGE("SAME_EDGE"),
    .INIT(1'b0),
    .SRTYPE("SYNC")
) oddr_i (
    .Q(eth_refclk),
    .C(clk_50),
    .CE(1'b1),
    .D1(1'b0),
    .D2(1'b1),
    .R(1'b0),
    .S(1'b0)
);

endmodule
