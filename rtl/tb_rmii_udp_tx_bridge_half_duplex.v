`timescale 1ns / 1ps

module tb_rmii_udp_tx_bridge_half_duplex;
reg clk = 1'b0;
reg rst_n = 1'b0;
reg carrier_sense = 1'b0;
reg [7:0] payload_tdata = 8'h00;
reg       payload_tvalid = 1'b0;
wire      payload_tready;
reg       payload_tlast = 1'b0;
wire      eth_txen;
wire [1:0] eth_txd;
wire      packet_sent;
wire      payload_overflow;

integer i;

always #10 clk = ~clk; // 50 MHz

rmii_udp_tx_bridge #(
    .LOCAL_MAC(48'h02_00_00_00_00_01),
    .LOCAL_IP(32'hA9FE_F23C),
    .LOCAL_PORT(16'd50000),
    .REMOTE_MAC(48'h9c_69_d3_1f_21_5e),
    .REMOTE_IP(32'hA9FE_F23B),
    .REMOTE_PORT(16'd50000),
    .HALF_DUPLEX_MODE(1'b1)
) dut (
    .clk_50(clk),
    .rst_n(rst_n),
    .carrier_sense(carrier_sense),
    .remote_mac(48'h9c_69_d3_1f_21_5e),
    .remote_ip(32'hA9FE_F23B),
    .remote_port(16'd50000),
    .s_payload_tdata(payload_tdata),
    .s_payload_tvalid(payload_tvalid),
    .s_payload_tready(payload_tready),
    .s_payload_tlast(payload_tlast),
    .eth_txen(eth_txen),
    .eth_txd(eth_txd),
    .tx_busy(),
    .packet_sent(packet_sent),
    .payload_overflow(payload_overflow)
);

task send_payload_byte;
    input [7:0] b;
    input       last;
    begin
        @(negedge clk);
        payload_tdata <= b;
        payload_tlast <= last;
        payload_tvalid <= 1'b1;
        while (!payload_tready) @(negedge clk);
        @(negedge clk);
        payload_tvalid <= 1'b0;
        payload_tlast <= 1'b0;
    end
endtask

initial begin
    repeat (4) @(posedge clk);
    rst_n <= 1'b1;
    carrier_sense <= 1'b1;
    repeat (4) @(posedge clk);

    send_payload_byte("O", 1'b0);
    send_payload_byte("K", 1'b1);

    for (i = 0; i < 100; i = i + 1) begin
        @(posedge clk);
        if (eth_txen) $fatal(1, "TXEN asserted while carrier_sense was high");
    end

    carrier_sense <= 1'b0;

    for (i = 0; i < 47; i = i + 1) begin
        @(posedge clk);
        if (eth_txen) $fatal(1, "TXEN asserted before half-duplex defer gap expired");
    end

    wait (eth_txen);
    wait (packet_sent);
    repeat (8) @(posedge clk);

    if (payload_overflow) $fatal(1, "unexpected payload overflow");

    $display("PASS tb_rmii_udp_tx_bridge_half_duplex");
    $finish;
end

endmodule
