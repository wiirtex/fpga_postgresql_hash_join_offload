`timescale 1ns / 1ps

module tb_rmii_udp_stream_bridge_protocol;
reg clk = 1'b0;
reg rst_n = 1'b0;

reg       eth_crsdv = 1'b0;
reg       eth_rxerr = 1'b0;
reg [1:0] eth_rxd = 2'b00;
wire      eth_txen;
wire [1:0] eth_txd;

wire [7:0] m_axis_tdata;
wire       m_axis_tvalid;
reg        m_axis_tready = 1'b1;
wire       m_axis_tlast;

reg [7:0] s_axis_tdata = 8'h00;
reg       s_axis_tvalid = 1'b0;
wire      s_axis_tready;
reg       s_axis_tlast = 1'b0;

wire rx_packet_seen;
wire rx_packet_dropped;
wire rx_parser_error;
wire tx_payload_overflow;

reg [7:0] payload [0:2047];
reg [15:0] payload_len = 16'd0;

reg [7:0] expected [0:8191];
reg [15:0] expected_len = 16'd0;
reg [15:0] expected_last [0:31];
reg [4:0] expected_last_count = 5'd0;

reg [7:0] received [0:8191];
reg [15:0] received_len = 16'd0;
reg [15:0] received_last [0:31];
reg [4:0] received_last_count = 5'd0;
reg [15:0] packet_seen_count = 16'd0;

integer i;

always #10 clk = ~clk; // 50 MHz, 100BASE-TX RMII reference clock.

rmii_udp_stream_bridge #(
    .LOCAL_MAC(48'h02_00_00_00_00_01),
    .LOCAL_IP(32'hA9FE_F23C),
    .LOCAL_PORT(16'd50000),
    .REMOTE_MAC(48'h9c_69_d3_1f_21_5e),
    .REMOTE_IP(32'hA9FE_F23B),
    .REMOTE_PORT(16'd50000),
    .TEN_MBIT_MODE(1'b0),
    .ENABLE_RX_DIAG_ECHO(1'b0),
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
    .m_axis_tready(m_axis_tready),
    .m_axis_tlast(m_axis_tlast),
    .s_axis_tdata(s_axis_tdata),
    .s_axis_tvalid(s_axis_tvalid),
    .s_axis_tready(s_axis_tready),
    .s_axis_tlast(s_axis_tlast),
    .rx_sfd_seen(),
    .rx_packet_seen(rx_packet_seen),
    .rx_packet_dropped(rx_packet_dropped),
    .rx_parser_error(rx_parser_error),
    .tx_packet_sent(),
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

task put_expected;
    input [7:0] b;
    input       last;
    begin
        expected[expected_len] = b;
        if (last) begin
            expected_last[expected_last_count] = expected_len;
            expected_last_count = expected_last_count + 5'd1;
        end
        expected_len = expected_len + 16'd1;
    end
endtask

task set_payload_pattern;
    input [15:0] len;
    input [7:0] seed;
    begin
        payload_len = len;
        for (i = 0; i < len; i = i + 1) begin
            payload[i] = seed + i[7:0];
            put_expected(payload[i], i == len - 1);
        end
    end
endtask

task send_rmii_byte;
    input [7:0] b;
    begin
        @(negedge clk); eth_rxd <= b[1:0];
        @(negedge clk); eth_rxd <= b[3:2];
        @(negedge clk); eth_rxd <= b[5:4];
        @(negedge clk); eth_rxd <= b[7:6];
    end
endtask

task send_host_udp_frame;
    integer j;
    reg [15:0] ip_total_len;
    reg [15:0] udp_total_len;
    begin
        ip_total_len = payload_len + 16'd28;
        udp_total_len = payload_len + 16'd8;

        repeat (12) @(negedge clk);
        eth_crsdv <= 1'b1;

        send_rmii_byte(8'h55);
        send_rmii_byte(8'h55);
        send_rmii_byte(8'h55);
        send_rmii_byte(8'h55);
        send_rmii_byte(8'h55);
        send_rmii_byte(8'h55);
        send_rmii_byte(8'h55);
        send_rmii_byte(8'hd5);

        // Ethernet II: dst FPGA MAC, src ASIX MAC, ethertype IPv4.
        send_rmii_byte(8'h02); send_rmii_byte(8'h00); send_rmii_byte(8'h00);
        send_rmii_byte(8'h00); send_rmii_byte(8'h00); send_rmii_byte(8'h01);
        send_rmii_byte(8'h9c); send_rmii_byte(8'h69); send_rmii_byte(8'hd3);
        send_rmii_byte(8'h1f); send_rmii_byte(8'h21); send_rmii_byte(8'h5e);
        send_rmii_byte(8'h08); send_rmii_byte(8'h00);

        // IPv4 header. Header checksum is ignored by the parser.
        send_rmii_byte(8'h45);
        send_rmii_byte(8'h00);
        send_rmii_byte(ip_total_len[15:8]);
        send_rmii_byte(ip_total_len[7:0]);
        send_rmii_byte(8'h00); send_rmii_byte(8'h00);
        send_rmii_byte(8'h40); send_rmii_byte(8'h00);
        send_rmii_byte(8'h40); send_rmii_byte(8'h11);
        send_rmii_byte(8'h00); send_rmii_byte(8'h00);
        send_rmii_byte(8'ha9); send_rmii_byte(8'hfe);
        send_rmii_byte(8'hf2); send_rmii_byte(8'h3b);
        send_rmii_byte(8'ha9); send_rmii_byte(8'hfe);
        send_rmii_byte(8'hf2); send_rmii_byte(8'h3c);

        // UDP header: src 50000, dst 50000.
        send_rmii_byte(8'hc3); send_rmii_byte(8'h50);
        send_rmii_byte(8'hc3); send_rmii_byte(8'h50);
        send_rmii_byte(udp_total_len[15:8]);
        send_rmii_byte(udp_total_len[7:0]);
        send_rmii_byte(8'h00); send_rmii_byte(8'h00);

        for (j = 0; j < payload_len; j = j + 1) begin
            send_rmii_byte(payload[j]);
        end

        // Fake FCS bytes; udp_ipv4_rx stops at UDP length.
        send_rmii_byte(8'hde);
        send_rmii_byte(8'had);
        send_rmii_byte(8'hbe);
        send_rmii_byte(8'hef);

        @(negedge clk);
        eth_crsdv <= 1'b0;
        eth_rxd <= 2'b00;
    end
endtask

task set_payload_reset;
    begin
        payload_len = 16'd3;
        payload[0] = 8'h08; payload[1] = 8'h00; payload[2] = 8'h00;
        put_expected(8'h08, 1'b0);
        put_expected(8'h00, 1'b0);
        put_expected(8'h00, 1'b1);
    end
endtask

task set_payload_configure;
    begin
        payload_len = 16'd15;
        payload[0]  = 8'h01; // MSG_CONFIGURE
        payload[1]  = 8'h01; payload[2]  = 8'h00; // count = 1
        payload[3]  = 8'h00; // algorithm A
        payload[4]  = 8'h01; // KEY_INT32
        payload[5]  = 8'h00; payload[6]  = 8'h01; // rx_buf_hint = 256
        payload[7]  = 8'h02; payload[8]  = 8'h00; payload[9]  = 8'h00; payload[10] = 8'h00; // inner=2
        payload[11] = 8'h03; payload[12] = 8'h00; payload[13] = 8'h00; payload[14] = 8'h00; // outer=3
        for (i = 0; i < 15; i = i + 1) put_expected(payload[i], i == 14);
    end
endtask

task set_payload_inner;
    begin
        payload_len = 16'd23;
        payload[0] = 8'h02; payload[1] = 8'h02; payload[2] = 8'h00; // MSG_INNER_DATA count=2
        payload[3] = 8'h01; payload[4] = 8'h00; payload[5] = 8'h00; payload[6] = 8'h00;
        payload[7] = 8'h01; payload[8] = 8'h00; payload[9] = 8'h00; payload[10] = 8'h00; payload[11] = 8'h01; payload[12] = 8'h00;
        payload[13] = 8'h02; payload[14] = 8'h00; payload[15] = 8'h00; payload[16] = 8'h00;
        payload[17] = 8'h01; payload[18] = 8'h00; payload[19] = 8'h00; payload[20] = 8'h00; payload[21] = 8'h02; payload[22] = 8'h00;
        for (i = 0; i < 23; i = i + 1) put_expected(payload[i], i == 22);
    end
endtask

task set_payload_outer;
    begin
        payload_len = 16'd33;
        payload[0] = 8'h03; payload[1] = 8'h03; payload[2] = 8'h00; // MSG_OUTER_DATA count=3
        payload[3] = 8'h02; payload[4] = 8'h00; payload[5] = 8'h00; payload[6] = 8'h00;
        payload[7] = 8'h02; payload[8] = 8'h00; payload[9] = 8'h00; payload[10] = 8'h00; payload[11] = 8'h01; payload[12] = 8'h00;
        payload[13] = 8'h03; payload[14] = 8'h00; payload[15] = 8'h00; payload[16] = 8'h00;
        payload[17] = 8'h02; payload[18] = 8'h00; payload[19] = 8'h00; payload[20] = 8'h00; payload[21] = 8'h02; payload[22] = 8'h00;
        payload[23] = 8'h99; payload[24] = 8'h00; payload[25] = 8'h00; payload[26] = 8'h00;
        payload[27] = 8'h02; payload[28] = 8'h00; payload[29] = 8'h00; payload[30] = 8'h00; payload[31] = 8'h03; payload[32] = 8'h00;
        for (i = 0; i < 33; i = i + 1) put_expected(payload[i], i == 32);
    end
endtask

always @(posedge clk) begin
    if (rx_packet_seen) packet_seen_count <= packet_seen_count + 16'd1;
    if (m_axis_tvalid && m_axis_tready) begin
        received[received_len] <= m_axis_tdata;
        if (m_axis_tlast) begin
            received_last[received_last_count] <= received_len;
            received_last_count <= received_last_count + 5'd1;
        end
        received_len <= received_len + 16'd1;
    end
end

initial begin
    repeat (4) @(posedge clk);
    rst_n <= 1'b1;
    repeat (4) @(posedge clk);

    set_payload_reset();
    send_host_udp_frame();
    set_payload_configure();
    send_host_udp_frame();
    set_payload_inner();
    send_host_udp_frame();
    set_payload_outer();
    send_host_udp_frame();
    set_payload_pattern(16'd1198, 8'h40);
    send_host_udp_frame();
    set_payload_pattern(16'd1419, 8'h80);
    send_host_udp_frame();
    set_payload_pattern(16'd1468, 8'hc0);
    send_host_udp_frame();

    repeat (3000) @(posedge clk);

    if (rx_packet_dropped || rx_parser_error) $fatal(1, "RX packet drop/error");
    if (tx_payload_overflow) $fatal(1, "unexpected TX payload overflow");
    if (packet_seen_count != 16'd7) $fatal(1, "expected 7 packets, got %0d", packet_seen_count);
    if (received_len != expected_len) $fatal(1, "expected %0d bytes, got %0d", expected_len, received_len);
    if (received_last_count != expected_last_count) begin
        $fatal(1, "expected %0d TLASTs, got %0d", expected_last_count, received_last_count);
    end
    for (i = 0; i < expected_len; i = i + 1) begin
        if (received[i] != expected[i]) $fatal(1, "byte %0d mismatch: got %02x expected %02x", i, received[i], expected[i]);
    end
    for (i = 0; i < expected_last_count; i = i + 1) begin
        if (received_last[i] != expected_last[i]) begin
            $fatal(1, "TLAST %0d mismatch: got byte %0d expected byte %0d", i, received_last[i], expected_last[i]);
        end
    end

    $display("PASS tb_rmii_udp_stream_bridge_protocol");
    $finish;
end

endmodule
