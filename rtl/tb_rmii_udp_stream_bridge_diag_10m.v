`timescale 1ns / 1ps

module tb_rmii_udp_stream_bridge_diag_10m;
reg clk = 1'b0;
reg rst_n = 1'b0;

reg       eth_crsdv = 1'b0;
reg       eth_rxerr = 1'b0;
reg [1:0] eth_rxd = 2'b00;
wire      eth_txen;
wire [1:0] eth_txd;

wire [7:0] m_axis_tdata;
wire       m_axis_tvalid;
wire       m_axis_tlast;

wire rx_packet_seen;
wire rx_packet_dropped;
wire rx_parser_error;
wire tx_packet_sent;
wire tx_payload_overflow;

reg saw_rx_packet = 1'b0;
reg saw_tx_packet = 1'b0;
reg [1:0] tx_dibit_count = 2'd0;
reg [3:0] tx_sample_gap = 4'd0;
reg [7:0] tx_shift = 8'h00;
reg [7:0] tx_bytes [0:127];
reg [15:0] tx_byte_count = 16'd0;

always #10 clk = ~clk; // 50 MHz

rmii_udp_stream_bridge #(
    .LOCAL_MAC(48'h02_00_00_00_00_01),
    .LOCAL_IP(32'hA9FE_F23C),
    .LOCAL_PORT(16'd50000),
    .REMOTE_MAC(48'h9c_69_d3_1f_21_5e),
    .REMOTE_IP(32'hA9FE_F23B),
    .REMOTE_PORT(16'd50000),
    .TEN_MBIT_MODE(1'b1),
    .ENABLE_RX_DIAG_ECHO(1'b1),
    .TX_FLUSH_CYCLES(8)
) dut (
    .clk_50(clk),
    .rst_n(rst_n),
    .eth_crsdv(eth_crsdv),
    .eth_rxerr(eth_rxerr),
    .eth_rxd(eth_rxd),
    .eth_txen(eth_txen),
    .eth_txd(eth_txd),
    .m_axis_tdata(m_axis_tdata),
    .m_axis_tvalid(m_axis_tvalid),
    .m_axis_tready(1'b1),
    .m_axis_tlast(m_axis_tlast),
    .s_axis_tdata(8'h00),
    .s_axis_tvalid(1'b0),
    .s_axis_tready(),
    .s_axis_tlast(1'b0),
    .rx_sfd_seen(),
    .rx_packet_seen(rx_packet_seen),
    .rx_packet_dropped(rx_packet_dropped),
    .rx_parser_error(rx_parser_error),
    .tx_packet_sent(tx_packet_sent),
    .tx_payload_overflow(tx_payload_overflow),
    .tx_busy(),
    .debug_tx_payload_tvalid(),
    .debug_tx_payload_tready(),
    .debug_tx_payload_tlast(),
    .debug_crsdv_seen(),
    .debug_rxerr_seen(),
    .debug_rxd0_seen(),
    .debug_rxd1_seen(),
    .debug_rmii_rx_dv_seen(),
    .debug_rx_frame_start_seen(),
    .debug_rx_byte_seen(),
    .debug_rx_frame_end_seen(),
    .debug_rx_problem_seen(),
    .debug_rx_byte_ge_6(),
    .debug_rx_byte_ge_12(),
    .debug_rx_byte_ge_14()
);

task send_rmii_dibit_10m;
    input [1:0] d;
    integer i;
    begin
        eth_rxd <= d;
        for (i = 0; i < 10; i = i + 1) begin
            @(negedge clk);
        end
    end
endtask

task send_rmii_byte_10m;
    input [7:0] b;
    begin
        send_rmii_dibit_10m(b[1:0]);
        send_rmii_dibit_10m(b[3:2]);
        send_rmii_dibit_10m(b[5:4]);
        send_rmii_dibit_10m(b[7:6]);
    end
endtask

always @(posedge clk) begin
    if (rx_packet_seen) saw_rx_packet <= 1'b1;
    if (tx_packet_sent) saw_tx_packet <= 1'b1;

    if (!eth_txen) begin
        tx_dibit_count <= 2'd0;
        tx_sample_gap <= 4'd5;
        tx_shift <= 8'h00;
    end else if (tx_sample_gap != 4'd0) begin
        tx_sample_gap <= tx_sample_gap - 4'd1;
    end else begin
        tx_sample_gap <= 4'd9;
        tx_shift <= {eth_txd, tx_shift[7:2]};
        if (tx_dibit_count == 2'd3) begin
            tx_bytes[tx_byte_count] <= {eth_txd, tx_shift[7:2]};
            tx_byte_count <= tx_byte_count + 16'd1;
            tx_dibit_count <= 2'd0;
        end else begin
            tx_dibit_count <= tx_dibit_count + 2'd1;
        end
    end
end

initial begin
    repeat (4) @(posedge clk);
    rst_n <= 1'b1;
    repeat (2) @(posedge clk);

    eth_crsdv <= 1'b1;
    send_rmii_byte_10m(8'h55);
    send_rmii_byte_10m(8'h55);
    send_rmii_byte_10m(8'h55);
    send_rmii_byte_10m(8'h55);
    send_rmii_byte_10m(8'h55);
    send_rmii_byte_10m(8'h55);
    send_rmii_byte_10m(8'h55);
    send_rmii_byte_10m(8'hd5);

    send_rmii_byte_10m(8'h02);
    send_rmii_byte_10m(8'h00);
    send_rmii_byte_10m(8'h00);
    send_rmii_byte_10m(8'h00);
    send_rmii_byte_10m(8'h00);
    send_rmii_byte_10m(8'h01);
    send_rmii_byte_10m(8'h9c);
    send_rmii_byte_10m(8'h69);
    send_rmii_byte_10m(8'hd3);
    send_rmii_byte_10m(8'h1f);
    send_rmii_byte_10m(8'h21);
    send_rmii_byte_10m(8'h5e);
    send_rmii_byte_10m(8'h08);
    send_rmii_byte_10m(8'h00);

    send_rmii_byte_10m(8'h45);
    send_rmii_byte_10m(8'h00);
    send_rmii_byte_10m(8'h00);
    send_rmii_byte_10m(8'h1f);
    send_rmii_byte_10m(8'h00);
    send_rmii_byte_10m(8'h00);
    send_rmii_byte_10m(8'h40);
    send_rmii_byte_10m(8'h00);
    send_rmii_byte_10m(8'h40);
    send_rmii_byte_10m(8'h11);
    send_rmii_byte_10m(8'h42);
    send_rmii_byte_10m(8'h59);
    send_rmii_byte_10m(8'ha9);
    send_rmii_byte_10m(8'hfe);
    send_rmii_byte_10m(8'hf2);
    send_rmii_byte_10m(8'h3b);
    send_rmii_byte_10m(8'ha9);
    send_rmii_byte_10m(8'hfe);
    send_rmii_byte_10m(8'hf2);
    send_rmii_byte_10m(8'h3c);

    send_rmii_byte_10m(8'hc3);
    send_rmii_byte_10m(8'h51);
    send_rmii_byte_10m(8'hc3);
    send_rmii_byte_10m(8'h50);
    send_rmii_byte_10m(8'h00);
    send_rmii_byte_10m(8'h0b);
    send_rmii_byte_10m(8'h00);
    send_rmii_byte_10m(8'h00);
    send_rmii_byte_10m(8'h08);
    send_rmii_byte_10m(8'h00);
    send_rmii_byte_10m(8'h00);

    @(negedge clk);
    eth_crsdv <= 1'b0;
    eth_rxd <= 2'b00;

    wait (saw_rx_packet);
    wait (saw_tx_packet);
    repeat (3200) @(posedge clk);

    if (rx_packet_dropped || rx_parser_error) $fatal(1, "RX parser drop/error");
    if (tx_payload_overflow) $fatal(1, "TX payload overflow");
    if (tx_byte_count != 16'd72) $fatal(1, "expected 72 TX bytes, got %0d", tx_byte_count);
    if (tx_bytes[0] != 8'h55 || tx_bytes[7] != 8'hd5) $fatal(1, "bad TX preamble");
    if (tx_bytes[8] != 8'h9c || tx_bytes[13] != 8'h5e) $fatal(1, "bad TX destination MAC");
    if (tx_bytes[34] != 8'ha9 || tx_bytes[37] != 8'h3c) $fatal(1, "bad TX source IP");
    if (tx_bytes[38] != 8'ha9 || tx_bytes[41] != 8'h3b) $fatal(1, "bad TX destination IP");
    if (tx_bytes[42] != 8'hc3 || tx_bytes[43] != 8'h50 || tx_bytes[44] != 8'hc3 || tx_bytes[45] != 8'h50) begin
        $fatal(1, "bad TX UDP ports");
    end
    if (tx_bytes[46] != 8'h00 || tx_bytes[47] != 8'h12) $fatal(1, "bad TX UDP length");
    if (tx_bytes[50] != 8'h09 || tx_bytes[51] != 8'h01 || tx_bytes[52] != 8'h00 ||
        tx_bytes[53] != 8'h00 || tx_bytes[54] != 8'he0 || tx_bytes[55] != 8'h00) begin
        $fatal(1, "bad diagnostic payload header");
    end
    if (tx_bytes[56][3:0] != 4'hf) $fatal(1, "expected all diagnostic stage bits set");

    $display("PASS tb_rmii_udp_stream_bridge_diag_10m");
    $finish;
end

endmodule
