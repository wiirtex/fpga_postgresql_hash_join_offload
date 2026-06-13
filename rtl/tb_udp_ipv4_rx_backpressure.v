`timescale 1ns / 1ps

module tb_udp_ipv4_rx_backpressure;
reg clk = 1'b0;
reg rst_n = 1'b0;
reg [7:0] rx_byte = 8'h00;
reg       rx_byte_valid = 1'b0;
reg       rx_frame_start = 1'b0;
reg       rx_frame_end = 1'b0;
reg       rx_frame_error = 1'b0;
wire [7:0] payload_tdata;
wire       payload_tvalid;
reg        payload_tready = 1'b0;
wire       payload_tlast;
wire       packet_seen;
wire       packet_dropped;
wire       parser_error;

reg [7:0] got [0:7];
reg [3:0] got_count = 4'd0;

always #10 clk = ~clk;

udp_ipv4_rx dut (
    .clk(clk),
    .rst_n(rst_n),
    .rx_byte(rx_byte),
    .rx_byte_valid(rx_byte_valid),
    .rx_frame_start(rx_frame_start),
    .rx_frame_end(rx_frame_end),
    .rx_frame_error(rx_frame_error),
    .m_payload_tdata(payload_tdata),
    .m_payload_tvalid(payload_tvalid),
    .m_payload_tready(payload_tready),
    .m_payload_tlast(payload_tlast),
    .packet_seen(packet_seen),
    .packet_dropped(packet_dropped),
    .parser_error(parser_error),
    .debug_dst_mac_accepted(),
    .debug_ethertype_hi_accepted(),
    .debug_eth_accepted(),
    .debug_ip_accepted()
);

task send_byte;
    input [7:0] b;
    begin
        @(negedge clk);
        rx_byte <= b;
        rx_byte_valid <= 1'b1;
        @(negedge clk);
        rx_byte_valid <= 1'b0;
    end
endtask

always @(posedge clk) begin
    if (payload_tvalid && payload_tready) begin
        got[got_count] <= payload_tdata;
        got_count <= got_count + 4'd1;
    end
end

initial begin
    repeat (4) @(posedge clk);
    rst_n <= 1'b1;
    repeat (2) @(posedge clk);

    @(negedge clk);
    rx_frame_start <= 1'b1;
    @(negedge clk);
    rx_frame_start <= 1'b0;

    // Ethernet header: dst FPGA, src host, IPv4.
    send_byte(8'h02); send_byte(8'h00); send_byte(8'h00); send_byte(8'h00); send_byte(8'h00); send_byte(8'h01);
    send_byte(8'h9c); send_byte(8'h69); send_byte(8'hd3); send_byte(8'h1f); send_byte(8'h21); send_byte(8'h5e);
    send_byte(8'h08); send_byte(8'h00);

    // IPv4 header: total length 32, UDP, src 169.254.242.59, dst 169.254.242.60.
    send_byte(8'h45); send_byte(8'h00); send_byte(8'h00); send_byte(8'h20);
    send_byte(8'h00); send_byte(8'h00); send_byte(8'h40); send_byte(8'h00);
    send_byte(8'h40); send_byte(8'h11); send_byte(8'h00); send_byte(8'h00);
    send_byte(8'ha9); send_byte(8'hfe); send_byte(8'hf2); send_byte(8'h3b);
    send_byte(8'ha9); send_byte(8'hfe); send_byte(8'hf2); send_byte(8'h3c);

    // UDP header: 50000 -> 50000, len 12, checksum disabled.
    send_byte(8'hc3); send_byte(8'h50); send_byte(8'hc3); send_byte(8'h50);
    send_byte(8'h00); send_byte(8'h0c); send_byte(8'h00); send_byte(8'h00);

    send_byte("J"); send_byte("O"); send_byte("I"); send_byte("N");

    @(negedge clk);
    rx_frame_end <= 1'b1;
    @(negedge clk);
    rx_frame_end <= 1'b0;

    repeat (8) @(posedge clk);
    if (!packet_seen && packet_dropped) $fatal(1, "packet dropped before output");
    if (!payload_tvalid) $fatal(1, "payload was not buffered while tready was low");
    if (payload_tdata != "J") $fatal(1, "bad first buffered byte");

    payload_tready <= 1'b1;
    repeat (12) @(posedge clk);

    if (parser_error || packet_dropped) $fatal(1, "unexpected parser error/drop");
    if (got_count != 4'd4) $fatal(1, "expected 4 bytes, got %0d", got_count);
    if (got[0] != "J" || got[1] != "O" || got[2] != "I" || got[3] != "N") $fatal(1, "bad payload");

    $display("PASS tb_udp_ipv4_rx_backpressure");
    $finish;
end

endmodule
