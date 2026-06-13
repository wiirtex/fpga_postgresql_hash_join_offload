`timescale 1ns / 1ps

module tb_rmii_udp_bridge_loopback;
reg clk = 1'b0;
reg rst_n = 1'b0;

reg       rx_crsdv = 1'b0;
reg       rxerr = 1'b0;
reg [1:0] rxd = 2'b00;

wire [7:0] rx_payload_data;
wire       rx_payload_valid;
wire       rx_payload_last;
wire       rx_packet_seen;
wire       rx_packet_dropped;
wire       rx_parser_error;

reg [7:0] tx_payload_data = 8'h00;
reg       tx_payload_valid = 1'b0;
wire      tx_payload_ready;
reg       tx_payload_last = 1'b0;
wire      txen;
wire [1:0] txd;
wire      tx_packet_sent;
wire      tx_overflow;

reg [7:0] rx_payload [0:7];
reg [15:0] rx_payload_count = 16'd0;
reg        rx_done = 1'b0;

reg [1:0] tx_dibit_count = 2'd0;
reg [7:0] tx_shift = 8'h00;
reg [7:0] tx_bytes [0:127];
reg [15:0] tx_byte_count = 16'd0;

always #10 clk = ~clk; // 50 MHz

rmii_udp_rx_bridge #(
    .LOCAL_MAC(48'h02_00_00_00_00_01),
    .LOCAL_IP(32'hA9FE_F23C),
    .LOCAL_PORT(16'd50000),
    .ACCEPT_ANY_DEST_MAC(1'b0)
) rx_bridge (
    .clk_50(clk),
    .rst_n(rst_n),
    .eth_crsdv(rx_crsdv),
    .eth_rxerr(rxerr),
    .eth_rxd(rxd),
    .m_payload_tdata(rx_payload_data),
    .m_payload_tvalid(rx_payload_valid),
    .m_payload_tready(1'b1),
    .m_payload_tlast(rx_payload_last),
    .sfd_seen(),
    .packet_seen(rx_packet_seen),
    .packet_dropped(rx_packet_dropped),
    .parser_error(rx_parser_error),
    .debug_dst_mac_accepted(),
    .debug_ethertype_hi_accepted(),
    .debug_eth_accepted(),
    .debug_ip_accepted(),
    .remote_mac(),
    .remote_ip(),
    .remote_port(),
    .debug_rx_byte_ge_6(),
    .debug_rx_byte_ge_12(),
    .debug_rx_byte_ge_14()
);

rmii_udp_tx_bridge #(
    .LOCAL_MAC(48'h02_00_00_00_00_01),
    .LOCAL_IP(32'hA9FE_F23C),
    .LOCAL_PORT(16'd50000),
    .REMOTE_MAC(48'h9c_69_d3_1f_21_5e),
    .REMOTE_IP(32'hA9FE_F23B),
    .REMOTE_PORT(16'd40000)
) tx_bridge (
    .clk_50(clk),
    .rst_n(rst_n),
    .carrier_sense(1'b0),
    .remote_mac(48'h9c_69_d3_1f_21_5e),
    .remote_ip(32'hA9FE_F23B),
    .remote_port(16'd40000),
    .s_payload_tdata(tx_payload_data),
    .s_payload_tvalid(tx_payload_valid),
    .s_payload_tready(tx_payload_ready),
    .s_payload_tlast(tx_payload_last),
    .eth_txen(txen),
    .eth_txd(txd),
    .tx_busy(),
    .packet_sent(tx_packet_sent),
    .payload_overflow(tx_overflow)
);

task send_rmii_byte;
    input [7:0] b;
    begin
        @(negedge clk); rxd <= b[1:0];
        @(negedge clk); rxd <= b[3:2];
        @(negedge clk); rxd <= b[5:4];
        @(negedge clk); rxd <= b[7:6];
    end
endtask

task send_payload_byte_to_tx;
    input [7:0] b;
    input       last;
    begin
        @(negedge clk);
        tx_payload_data <= b;
        tx_payload_last <= last;
        tx_payload_valid <= 1'b1;
        while (!tx_payload_ready) @(negedge clk);
        @(negedge clk);
        tx_payload_valid <= 1'b0;
        tx_payload_last <= 1'b0;
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
    if (rx_payload_valid) begin
        rx_payload[rx_payload_count] <= rx_payload_data;
        rx_payload_count <= rx_payload_count + 16'd1;
        if (rx_payload_last) begin
            rx_done <= 1'b1;
        end
    end

    if (!txen) begin
        tx_dibit_count <= 2'd0;
        tx_shift <= 8'h00;
    end else begin
        tx_shift <= {txd, tx_shift[7:2]};
        if (tx_dibit_count == 2'd3) begin
            tx_bytes[tx_byte_count] <= {txd, tx_shift[7:2]};
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

    rx_crsdv <= 1'b1;
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
    rx_crsdv <= 1'b0;
    rxd <= 2'b00;

    wait (rx_done);
    repeat (4) @(posedge clk);

    if (rx_packet_dropped || rx_parser_error) $fatal(1, "RX parser drop/error");
    if (rx_payload_count != 16'd4) $fatal(1, "expected 4 RX bytes, got %0d", rx_payload_count);
    if (rx_payload[0] != "J" || rx_payload[1] != "O" || rx_payload[2] != "I" || rx_payload[3] != "N") begin
        $fatal(1, "RX payload mismatch");
    end

    send_payload_byte_to_tx(rx_payload[0], 1'b0);
    send_payload_byte_to_tx(rx_payload[1], 1'b0);
    send_payload_byte_to_tx(rx_payload[2], 1'b0);
    send_payload_byte_to_tx(rx_payload[3], 1'b1);

    wait (tx_packet_sent);
    repeat (24) @(posedge clk);

    if (tx_overflow) $fatal(1, "TX overflow");
    if (tx_byte_count != 16'd72) $fatal(1, "expected 72 TX bytes, got %0d", tx_byte_count);
    if (tx_bytes[0] != 8'h55 || tx_bytes[7] != 8'hd5) $fatal(1, "bad TX preamble");
    if (tx_bytes[50] != "J" || tx_bytes[51] != "O" || tx_bytes[52] != "I" || tx_bytes[53] != "N") begin
        $fatal(1, "TX payload mismatch");
    end
    if ({tx_bytes[71], tx_bytes[70], tx_bytes[69], tx_bytes[68]} != expected_tx_fcs(1'b0)) begin
        $fatal(1, "bad TX FCS");
    end

    $display("PASS tb_rmii_udp_bridge_loopback");
    $finish;
end

endmodule
