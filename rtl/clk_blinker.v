// clk_blinker.v — 27-bit counter driving a visible blink on 'blink' output.
// At ~81 MHz (ui_clk from MIG): period = 2^27 / 81.25e6 ≈ 1.65 s → ~0.6 Hz
// At ~65 MHz (clk_out3):        period = 2^27 / 65e6    ≈ 2.07 s → ~0.5 Hz
module clk_blinker (
    input  wire clk,
    input  wire rstn,   // active-low reset (tie to peripheral_aresetn)
    output wire blink
);
    reg [26:0] cnt;
    always @(posedge clk or negedge rstn)
        if (!rstn) cnt <= 27'd0;
        else       cnt <= cnt + 1'b1;
    assign blink = cnt[26];
endmodule
