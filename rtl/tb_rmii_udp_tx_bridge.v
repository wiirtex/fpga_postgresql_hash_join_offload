`timescale 1ns / 1ps

module tb_rmii_udp_tx_bridge;
reg clk = 1'b0;
reg rst_n = 1'b0;
reg [7:0] payload_tdata = 8'h00;
reg       payload_tvalid = 1'b0;
wire      payload_tready;
reg       payload_tlast = 1'b0;
wire      eth_txen;
wire [1:0] eth_txd;
wire      tx_busy;
wire      packet_sent;
wire      payload_overflow;

reg [1:0] dibit_count = 2'd0;
reg [7:0] shift = 8'h00;
reg [7:0] bytes [0:127];
reg [15:0] byte_count = 16'd0;
reg        prev_txen = 1'b0;

always #10 clk = ~clk; // 50 MHz

rmii_udp_tx_bridge #(
    .LOCAL_MAC(48'h02_00_00_00_00_01),
    .LOCAL_IP(32'hA9FE_F23C),
    .LOCAL_PORT(16'd50000),
    .REMOTE_MAC(48'h9c_69_d3_1f_21_5e),
    .REMOTE_IP(32'hA9FE_F23B),
    .REMOTE_PORT(16'd40000)
) dut (
    .clk_50(clk),
    .rst_n(rst_n),
    .carrier_sense(1'b0),
    .remote_mac(48'h9c_69_d3_1f_21_5e),
    .remote_ip(32'hA9FE_F23B),
    .remote_port(16'd40000),
    .s_payload_tdata(payload_tdata),
    .s_payload_tvalid(payload_tvalid),
    .s_payload_tready(payload_tready),
    .s_payload_tlast(payload_tlast),
    .eth_txen(eth_txen),
    .eth_txd(eth_txd),
    .tx_busy(tx_busy),
    .packet_sent(packet_sent),
    .payload_overflow(payload_overflow)
);

function [31:0] crc32_next;
    input [31:0] crc;
    input [7:0] data;
    integer i;
    reg [31:0] c;
    begin
        c = crc;
        for (i = 0; i < 8; i = i + 1) begin
            if ((c[0] ^ data[i]) != 1'b0) begin
                c = (c >> 1) ^ 32'hEDB88320;
            end else begin
                c = (c >> 1);
            end
        end
        crc32_next = c;
    end
endfunction

function [31:0] expected_fcs;
    input dummy;
    integer i;
    reg [31:0] crc;
    begin
        crc = 32'hFFFF_FFFF;
        for (i = 8; i < 68; i = i + 1) begin
            crc = crc32_next(crc, bytes[i]);
        end
        expected_fcs = ~crc;
    end
endfunction

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

always @(posedge clk) begin
    prev_txen <= eth_txen;
    if (!eth_txen) begin
        dibit_count <= 2'd0;
        shift <= 8'h00;
    end else begin
        shift <= {eth_txd, shift[7:2]};
        if (dibit_count == 2'd3) begin
            bytes[byte_count] <= {eth_txd, shift[7:2]};
            byte_count <= byte_count + 16'd1;
            dibit_count <= 2'd0;
        end else begin
            dibit_count <= dibit_count + 2'd1;
        end
    end
end

initial begin
    repeat (4) @(posedge clk);
    rst_n <= 1'b1;
    repeat (2) @(posedge clk);

    send_payload_byte("J", 1'b0);
    send_payload_byte("O", 1'b0);
    send_payload_byte("I", 1'b0);
    send_payload_byte("N", 1'b1);

    wait (packet_sent);
    repeat (24) @(posedge clk);

    if (payload_overflow) $fatal(1, "unexpected payload overflow");
    if (byte_count != 16'd72) $fatal(1, "expected 72 RMII bytes, got %0d", byte_count);

    if (bytes[0] != 8'h55 || bytes[6] != 8'h55 || bytes[7] != 8'hd5) $fatal(1, "bad preamble/SFD");

    if (bytes[8]  != 8'h9c || bytes[9]  != 8'h69 || bytes[10] != 8'hd3 ||
        bytes[11] != 8'h1f || bytes[12] != 8'h21 || bytes[13] != 8'h5e) $fatal(1, "bad destination MAC");
    if (bytes[14] != 8'h02 || bytes[15] != 8'h00 || bytes[16] != 8'h00 ||
        bytes[17] != 8'h00 || bytes[18] != 8'h00 || bytes[19] != 8'h01) $fatal(1, "bad source MAC");
    if (bytes[20] != 8'h08 || bytes[21] != 8'h00) $fatal(1, "bad ethertype");

    if (bytes[22] != 8'h45 || bytes[23] != 8'h00) $fatal(1, "bad IPv4 prefix");
    if (bytes[24] != 8'h00 || bytes[25] != 8'h20) $fatal(1, "bad IPv4 total length");
    if (bytes[30] != 8'h40 || bytes[31] != 8'h11) $fatal(1, "bad TTL/protocol");
    if (bytes[32] != 8'h42 || bytes[33] != 8'h58) $fatal(1, "bad IPv4 checksum");
    if (bytes[34] != 8'ha9 || bytes[35] != 8'hfe || bytes[36] != 8'hf2 || bytes[37] != 8'h3c) $fatal(1, "bad source IP");
    if (bytes[38] != 8'ha9 || bytes[39] != 8'hfe || bytes[40] != 8'hf2 || bytes[41] != 8'h3b) $fatal(1, "bad destination IP");

    if (bytes[42] != 8'hc3 || bytes[43] != 8'h50) $fatal(1, "bad source port");
    if (bytes[44] != 8'h9c || bytes[45] != 8'h40) $fatal(1, "bad destination port");
    if (bytes[46] != 8'h00 || bytes[47] != 8'h0c) $fatal(1, "bad UDP length");
    if (bytes[48] != 8'h00 || bytes[49] != 8'h00) $fatal(1, "bad UDP checksum");

    if (bytes[50] != "J" || bytes[51] != "O" || bytes[52] != "I" || bytes[53] != "N") $fatal(1, "bad payload");
    if (bytes[54] != 8'h00 || bytes[67] != 8'h00) $fatal(1, "bad padding");

    if ({bytes[71], bytes[70], bytes[69], bytes[68]} != expected_fcs(1'b0)) begin
        $fatal(1, "bad FCS");
    end

    $display("PASS tb_rmii_udp_tx_bridge");
    $finish;
end

endmodule
