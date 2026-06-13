`timescale 1ns / 1ps

module tb_udp_ipv4_rx;
reg clk = 1'b0;
reg rst_n = 1'b0;

reg [7:0] rx_byte = 8'h00;
reg       rx_byte_valid = 1'b0;
reg       rx_frame_start = 1'b0;
reg       rx_frame_end = 1'b0;
reg       rx_frame_error = 1'b0;

wire [7:0] payload_data;
wire       payload_valid;
wire       payload_last;
wire       packet_seen;
wire       packet_dropped;
wire       parser_error;
wire       debug_dst_mac_accepted;
wire       debug_ethertype_hi_accepted;
wire       debug_eth_accepted;
wire       debug_ip_accepted;
wire [47:0] remote_mac;
wire [31:0] remote_ip;
wire [15:0] remote_port;

reg        payload_ready = 1'b1;
reg [31:0] received = 32'd0;
reg [7:0]  payload [0:3];

always #5 clk = ~clk;

udp_ipv4_rx #(
    .LOCAL_MAC(48'h02_00_00_00_00_01),
    .LOCAL_IP(32'hA9FE_F23C),
    .LOCAL_PORT(16'd50000)
) dut (
    .clk(clk),
    .rst_n(rst_n),
    .rx_byte(rx_byte),
    .rx_byte_valid(rx_byte_valid),
    .rx_frame_start(rx_frame_start),
    .rx_frame_end(rx_frame_end),
    .rx_frame_error(rx_frame_error),
    .m_payload_tdata(payload_data),
    .m_payload_tvalid(payload_valid),
    .m_payload_tready(payload_ready),
    .m_payload_tlast(payload_last),
    .packet_seen(packet_seen),
    .packet_dropped(packet_dropped),
    .parser_error(parser_error),
    .debug_dst_mac_accepted(debug_dst_mac_accepted),
    .debug_ethertype_hi_accepted(debug_ethertype_hi_accepted),
    .debug_eth_accepted(debug_eth_accepted),
    .debug_ip_accepted(debug_ip_accepted),
    .remote_mac(remote_mac),
    .remote_ip(remote_ip),
    .remote_port(remote_port)
);

task start_frame;
    begin
        @(posedge clk);
        rx_frame_start <= 1'b1;
        @(posedge clk);
        rx_frame_start <= 1'b0;
    end
endtask

task send_byte;
    input [7:0] b;
    begin
        @(posedge clk);
        rx_byte <= b;
        rx_byte_valid <= 1'b1;
        @(posedge clk);
        rx_byte_valid <= 1'b0;
    end
endtask

task end_frame;
    begin
        @(posedge clk);
        rx_frame_end <= 1'b1;
        @(posedge clk);
        rx_frame_end <= 1'b0;
    end
endtask

always @(posedge clk) begin
    if (payload_valid && payload_ready) begin
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

    start_frame();

    // Ethernet destination MAC: broadcast.
    send_byte(8'hff);
    send_byte(8'hff);
    send_byte(8'hff);
    send_byte(8'hff);
    send_byte(8'hff);
    send_byte(8'hff);
    // Source MAC.
    send_byte(8'h9c);
    send_byte(8'h69);
    send_byte(8'hd3);
    send_byte(8'h1f);
    send_byte(8'h21);
    send_byte(8'h5e);
    // Ethertype IPv4.
    send_byte(8'h08);
    send_byte(8'h00);

    // IPv4 header, IHL=5, protocol UDP, dst link-local subnet broadcast.
    send_byte(8'h45);
    send_byte(8'h00);
    send_byte(8'h00);
    send_byte(8'h20);
    send_byte(8'h00);
    send_byte(8'h00);
    send_byte(8'h40);
    send_byte(8'h00);
    send_byte(8'h40);
    send_byte(8'h11);
    send_byte(8'h00);
    send_byte(8'h00);
    send_byte(8'ha9);
    send_byte(8'hfe);
    send_byte(8'hf2);
    send_byte(8'h3b);
    send_byte(8'ha9);
    send_byte(8'hfe);
    send_byte(8'hff);
    send_byte(8'hff);

    // UDP header: src 40000, dst 50000, len 12.
    send_byte(8'h9c);
    send_byte(8'h40);
    send_byte(8'hc3);
    send_byte(8'h50);
    send_byte(8'h00);
    send_byte(8'h0c);
    send_byte(8'h00);
    send_byte(8'h00);

    // Payload "JOIN". Hold ready low for the first payload byte to verify
    // that the parser keeps one output byte live instead of dropping the frame.
    payload_ready = 1'b0;
    send_byte("J");
    payload_ready = 1'b1;
    send_byte("O");
    send_byte("I");
    send_byte("N");
    // Fake FCS bytes should be ignored by UDP length.
    send_byte(8'hde);
    send_byte(8'had);
    send_byte(8'hbe);
    send_byte(8'hef);
    end_frame();

    repeat (8) @(posedge clk);

    if (received != 32'd4) $fatal(1, "expected 4 payload bytes, got %0d", received);
    if (payload[0] != "J" || payload[1] != "O" || payload[2] != "I" || payload[3] != "N") begin
        $fatal(1, "payload mismatch");
    end
    if (packet_dropped || parser_error) $fatal(1, "unexpected drop/error");
    if (remote_mac != 48'h9c_69_d3_1f_21_5e) $fatal(1, "bad captured remote MAC");
    if (remote_ip != 32'hA9FE_F23B) $fatal(1, "bad captured remote IP");
    if (remote_port != 16'd40000) $fatal(1, "bad captured remote UDP port");

    $display("PASS tb_udp_ipv4_rx");
    $finish;
end

endmodule
