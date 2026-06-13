`timescale 1ns / 1ps

module tb_rmii_udp_rx_bridge;
reg clk = 1'b0;
reg rst_n = 1'b0;
reg eth_crsdv = 1'b0;
reg eth_rxerr = 1'b0;
reg [1:0] eth_rxd = 2'b00;

wire [7:0] payload_data;
wire       payload_valid;
wire       payload_last;
wire       sfd_seen;
wire       packet_seen;
wire       packet_dropped;
wire       parser_error;
wire       debug_dst_mac_accepted;
wire       debug_ethertype_hi_accepted;
wire       debug_eth_accepted;
wire       debug_ip_accepted;
wire       debug_rx_byte_ge_6;
wire       debug_rx_byte_ge_12;
wire       debug_rx_byte_ge_14;

reg [31:0] received = 32'd0;
reg [7:0]  payload [0:3];
reg        saw_sfd = 1'b0;

always #10 clk = ~clk; // 50 MHz

rmii_udp_rx_bridge #(
    .LOCAL_MAC(48'h02_00_00_00_00_01),
    .LOCAL_IP(32'hA9FE_F23C),
    .LOCAL_PORT(16'd50000)
) dut (
    .clk_50(clk),
    .rst_n(rst_n),
    .eth_crsdv(eth_crsdv),
    .eth_rxerr(eth_rxerr),
    .eth_rxd(eth_rxd),
    .m_payload_tdata(payload_data),
    .m_payload_tvalid(payload_valid),
    .m_payload_tready(1'b1),
    .m_payload_tlast(payload_last),
    .sfd_seen(sfd_seen),
    .packet_seen(packet_seen),
    .packet_dropped(packet_dropped),
    .parser_error(parser_error),
    .debug_dst_mac_accepted(debug_dst_mac_accepted),
    .debug_ethertype_hi_accepted(debug_ethertype_hi_accepted),
    .debug_eth_accepted(debug_eth_accepted),
    .debug_ip_accepted(debug_ip_accepted),
    .debug_rx_byte_ge_6(debug_rx_byte_ge_6),
    .debug_rx_byte_ge_12(debug_rx_byte_ge_12),
    .debug_rx_byte_ge_14(debug_rx_byte_ge_14)
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

always @(posedge clk) begin
    if (sfd_seen) begin
        saw_sfd <= 1'b1;
    end
    if (payload_valid) begin
        payload[received] <= payload_data;
        received <= received + 32'd1;
        if (received == 32'd3 && !payload_last) begin
            $fatal(1, "expected tlast on fourth payload byte");
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

    // Ethernet destination MAC: broadcast.
    send_rmii_byte(8'hff);
    send_rmii_byte(8'hff);
    send_rmii_byte(8'hff);
    send_rmii_byte(8'hff);
    send_rmii_byte(8'hff);
    send_rmii_byte(8'hff);
    // Source MAC.
    send_rmii_byte(8'h9c);
    send_rmii_byte(8'h69);
    send_rmii_byte(8'hd3);
    send_rmii_byte(8'h1f);
    send_rmii_byte(8'h21);
    send_rmii_byte(8'h5e);
    // Ethertype IPv4.
    send_rmii_byte(8'h08);
    send_rmii_byte(8'h00);

    // IPv4 header.
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
    send_rmii_byte(8'hff);
    send_rmii_byte(8'hff);

    // UDP header: src 40000, dst 50000, len 12.
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

    repeat (16) @(posedge clk);

    if (!saw_sfd) $fatal(1, "SFD was not observed");
    if (received != 32'd4) $fatal(1, "expected 4 payload bytes, got %0d", received);
    if (payload[0] != "J" || payload[1] != "O" || payload[2] != "I" || payload[3] != "N") begin
        $fatal(1, "payload mismatch");
    end
    if (packet_dropped || parser_error) $fatal(1, "unexpected drop/error");

    $display("PASS tb_rmii_udp_rx_bridge");
    $finish;
end

endmodule
