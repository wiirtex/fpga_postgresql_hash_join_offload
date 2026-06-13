`timescale 1ns / 1ps
//
// eth_tx_pipeline_sticky.v
// Sticky LEDs for short TX pipeline events that are too narrow to see raw.
//
module eth_tx_pipeline_sticky (
    input  wire clk_50,
    input  wire rst_n,

    input  wire tx_busy,
    input  wire tx_packet_sent,
    input  wire tx_payload_overflow,

    output reg  led_tx_busy_seen,
    output reg  led_tx_packet_sent_seen,
    output reg  led_tx_overflow_seen
);

always @(posedge clk_50) begin
    if (!rst_n) begin
        led_tx_busy_seen        <= 1'b0;
        led_tx_packet_sent_seen <= 1'b0;
        led_tx_overflow_seen    <= 1'b0;
    end else begin
        if (tx_busy) led_tx_busy_seen <= 1'b1;
        if (tx_packet_sent) led_tx_packet_sent_seen <= 1'b1;
        if (tx_payload_overflow) led_tx_overflow_seen <= 1'b1;
    end
end

endmodule
