`timescale 1ns / 1ps
//
// Sticky LEDs for the physical RMII TX pins.
//
module eth_tx_debug_sticky (
    input  wire       clk_50,
    input  wire       rst_n,

    input  wire       eth_txen,
    input  wire [1:0] eth_txd,

    output reg        led_txen_seen,
    output reg        led_txd0_seen,
    output reg        led_txd1_seen
);

always @(posedge clk_50) begin
    if (!rst_n) begin
        led_txen_seen <= 1'b0;
        led_txd0_seen <= 1'b0;
        led_txd1_seen <= 1'b0;
    end else if (eth_txen) begin
        led_txen_seen <= 1'b1;
        if (eth_txd[0]) led_txd0_seen <= 1'b1;
        if (eth_txd[1]) led_txd1_seen <= 1'b1;
    end
end

endmodule
