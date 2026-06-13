`timescale 1ns / 1ps

module tb_udp_ipv4_rx_total_length;
reg clk = 1'b0;
reg rst_n = 1'b0;
reg [7:0] rx_byte = 8'h00;
reg       rx_byte_valid = 1'b0;
reg       rx_frame_start = 1'b0;
reg       rx_frame_end = 1'b0;
reg       rx_frame_error = 1'b0;

wire [7:0] payload_tdata;
wire       payload_tvalid;
wire       payload_tlast;
wire       packet_seen;
wire       packet_dropped;
wire       parser_error;
reg        saw_packet_seen = 1'b0;
reg        saw_packet_dropped = 1'b0;
reg        saw_payload = 1'b0;

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
    .m_payload_tready(1'b1),
    .m_payload_tlast(payload_tlast),
    .packet_seen(packet_seen),
    .packet_dropped(packet_dropped),
    .parser_error(parser_error),
    .debug_dst_mac_accepted(),
    .debug_ethertype_hi_accepted(),
    .debug_eth_accepted(),
    .debug_ip_accepted()
);

always @(posedge clk) begin
    if (packet_seen) saw_packet_seen <= 1'b1;
    if (packet_dropped) saw_packet_dropped <= 1'b1;
    if (payload_tvalid) saw_payload <= 1'b1;
end

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

task send_packet;
    input [15:0] ip_total_len;
    input [15:0] udp_len;
    begin
        @(negedge clk);
        rx_frame_start <= 1'b1;
        @(negedge clk);
        rx_frame_start <= 1'b0;

        send_byte(8'h02); send_byte(8'h00); send_byte(8'h00); send_byte(8'h00); send_byte(8'h00); send_byte(8'h01);
        send_byte(8'h9c); send_byte(8'h69); send_byte(8'hd3); send_byte(8'h1f); send_byte(8'h21); send_byte(8'h5e);
        send_byte(8'h08); send_byte(8'h00);

        send_byte(8'h45); send_byte(8'h00); send_byte(ip_total_len[15:8]); send_byte(ip_total_len[7:0]);
        send_byte(8'h00); send_byte(8'h00); send_byte(8'h40); send_byte(8'h00);
        send_byte(8'h40); send_byte(8'h11); send_byte(8'h00); send_byte(8'h00);
        send_byte(8'ha9); send_byte(8'hfe); send_byte(8'hf2); send_byte(8'h3b);
        send_byte(8'ha9); send_byte(8'hfe); send_byte(8'hf2); send_byte(8'h3c);

        send_byte(8'hc3); send_byte(8'h50); send_byte(8'hc3); send_byte(8'h50);
        send_byte(udp_len[15:8]); send_byte(udp_len[7:0]); send_byte(8'h00); send_byte(8'h00);

        send_byte("B"); send_byte("A"); send_byte("D"); send_byte("!");

        @(negedge clk);
        rx_frame_end <= 1'b1;
        @(negedge clk);
        rx_frame_end <= 1'b0;
    end
endtask

initial begin
    repeat (4) @(posedge clk);
    rst_n <= 1'b1;
    repeat (2) @(posedge clk);

    // IPv4 total length says 28 bytes (20 IP + 8 UDP), but UDP length says
    // 12 bytes. The parser must reject the inconsistent datagram.
    send_packet(16'd28, 16'd12);

    repeat (8) @(posedge clk);
    if (!saw_packet_dropped) $fatal(1, "expected malformed UDP/IP length packet to be dropped");
    if (saw_packet_seen) $fatal(1, "malformed packet must not be reported as accepted");
    if (saw_payload) $fatal(1, "malformed packet emitted payload");

    $display("PASS tb_udp_ipv4_rx_total_length");
    $finish;
end

endmodule
