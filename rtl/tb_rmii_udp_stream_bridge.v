`timescale 1ns / 1ps

module tb_rmii_udp_stream_bridge;
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
wire tx_packet_sent;
wire tx_payload_overflow;

reg [7:0] rx_bytes [0:7];
reg [15:0] rx_count = 16'd0;

reg [1:0] tx_dibit_count = 2'd0;
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
    .REMOTE_PORT(16'd40000),
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
    .tx_packet_sent(tx_packet_sent),
    .tx_payload_overflow(tx_payload_overflow),
    .tx_busy(),
    .debug_tx_payload_tvalid(),
    .debug_tx_payload_tready(),
    .debug_tx_payload_tlast(),
    .debug_rx_byte_ge_6(),
    .debug_rx_byte_ge_12(),
    .debug_rx_byte_ge_14()
);

task send_rmii_byte;
    input [7:0] b;
    begin
        @(negedge clk); eth_rxd <= b[1:0];
        @(negedge clk); eth_rxd <= b[3:2];
        @(negedge clk); eth_rxd <= b[5:4];
        @(negedge clk); eth_rxd <= b[7:6];
    end
endtask

task send_axis_byte;
    input [7:0] b;
    input       last;
    begin
        @(negedge clk);
        s_axis_tdata <= b;
        s_axis_tlast <= last;
        s_axis_tvalid <= 1'b1;
        while (!s_axis_tready) @(negedge clk);
        @(negedge clk);
        s_axis_tvalid <= 1'b0;
        s_axis_tlast <= 1'b0;
    end
endtask

function [31:0] crc32_next;
    input [31:0] crc;
    input [7:0] data;
    integer i;
    reg [31:0] c;
    begin
        c = crc;
        for (i = 0; i < 8; i = i + 1) begin
            if ((c[0] ^ data[i]) != 1'b0) c = (c >> 1) ^ 32'hEDB88320;
            else c = (c >> 1);
        end
        crc32_next = c;
    end
endfunction

function [31:0] expected_tx_fcs;
    input dummy;
    integer i;
    reg [31:0] crc;
    begin
        crc = 32'hFFFF_FFFF;
        for (i = 8; i < 68; i = i + 1) begin
            crc = crc32_next(crc, tx_bytes[i]);
        end
        expected_tx_fcs = ~crc;
    end
endfunction

always @(posedge clk) begin
    if (m_axis_tvalid && m_axis_tready) begin
        rx_bytes[rx_count] <= m_axis_tdata;
        if (rx_count == 16'd3 && !m_axis_tlast) begin
            $fatal(1, "expected RX m_axis_tlast on fourth byte");
        end
        rx_count <= rx_count + 16'd1;
    end

    if (!eth_txen) begin
        tx_dibit_count <= 2'd0;
        tx_shift <= 8'h00;
    end else begin
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
    @(negedge clk); eth_rxd <= 2'b01;
    send_rmii_byte(8'h55);
    send_rmii_byte(8'h55);
    send_rmii_byte(8'h55);
    send_rmii_byte(8'h55);
    send_rmii_byte(8'h55);
    send_rmii_byte(8'h55);
    send_rmii_byte(8'h55);
    send_rmii_byte(8'hd5);

    send_rmii_byte(8'h02);
    send_rmii_byte(8'h00);
    send_rmii_byte(8'h00);
    send_rmii_byte(8'h00);
    send_rmii_byte(8'h00);
    send_rmii_byte(8'h01);
    send_rmii_byte(8'h9c);
    send_rmii_byte(8'h69);
    send_rmii_byte(8'hd3);
    send_rmii_byte(8'h1f);
    send_rmii_byte(8'h21);
    send_rmii_byte(8'h5e);
    send_rmii_byte(8'h08);
    send_rmii_byte(8'h00);

    send_rmii_byte(8'h45);
    send_rmii_byte(8'h00);
    send_rmii_byte(8'h00);
    send_rmii_byte(8'h20);
    send_rmii_byte(8'h00);
    send_rmii_byte(8'h00);
    send_rmii_byte(8'h40);
    send_rmii_byte(8'h00);
    send_rmii_byte(8'h40);
    send_rmii_byte(8'h11);
    send_rmii_byte(8'h00);
    send_rmii_byte(8'h00);
    send_rmii_byte(8'ha9);
    send_rmii_byte(8'hfe);
    send_rmii_byte(8'hf2);
    send_rmii_byte(8'h3b);
    send_rmii_byte(8'ha9);
    send_rmii_byte(8'hfe);
    send_rmii_byte(8'hf2);
    send_rmii_byte(8'h3c);

    send_rmii_byte(8'h9c);
    send_rmii_byte(8'h40);
    send_rmii_byte(8'hc3);
    send_rmii_byte(8'h50);
    send_rmii_byte(8'h00);
    send_rmii_byte(8'h0c);
    send_rmii_byte(8'h00);
    send_rmii_byte(8'h00);

    send_rmii_byte("J");
    send_rmii_byte("O");
    send_rmii_byte("I");
    send_rmii_byte("N");
    send_rmii_byte(8'hde);
    send_rmii_byte(8'had);
    send_rmii_byte(8'hbe);
    send_rmii_byte(8'hef);
    @(negedge clk);
    eth_crsdv <= 1'b0;
    eth_rxd <= 2'b00;

    wait (rx_count == 16'd4);
    repeat (8) @(posedge clk);

    if (rx_packet_dropped || rx_parser_error) $fatal(1, "RX packet drop/error");
    if (rx_bytes[0] != "J" || rx_bytes[1] != "O" || rx_bytes[2] != "I" || rx_bytes[3] != "N") begin
        $fatal(1, "RX stream mismatch");
    end

    send_axis_byte("O", 1'b0);
    send_axis_byte("K", 1'b1);

    wait (tx_packet_sent);
    repeat (24) @(posedge clk);

    if (tx_payload_overflow) $fatal(1, "TX payload overflow");
    if (tx_byte_count != 16'd72) $fatal(1, "expected 72 TX bytes, got %0d", tx_byte_count);
    if (tx_bytes[0] != 8'h55 || tx_bytes[7] != 8'hd5) $fatal(1, "bad TX preamble");
    if (tx_bytes[50] != "O" || tx_bytes[51] != "K") $fatal(1, "bad TX payload");
    if (tx_bytes[46] != 8'h00 || tx_bytes[47] != 8'h0a) $fatal(1, "bad TX UDP length");
    if ({tx_bytes[71], tx_bytes[70], tx_bytes[69], tx_bytes[68]} != expected_tx_fcs(1'b0)) begin
        $fatal(1, "bad TX FCS");
    end

    $display("PASS tb_rmii_udp_stream_bridge");
    $finish;
end

endmodule
