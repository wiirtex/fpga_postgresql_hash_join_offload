`timescale 1ns / 1ps

module tb_rmii_udp_tx_bridge_10m;
reg clk = 1'b0;
reg rst_n = 1'b0;
reg [7:0] payload_tdata = 8'h00;
reg       payload_tvalid = 1'b0;
wire      payload_tready;
reg       payload_tlast = 1'b0;
wire      eth_txen;
wire [1:0] eth_txd;
wire      packet_sent;
wire      payload_overflow;

reg [1:0] dibit_count = 2'd0;
reg [3:0] sample_gap = 4'd0;
reg [7:0] shift = 8'h00;
reg [7:0] bytes [0:127];
reg [15:0] byte_count = 16'd0;

always #10 clk = ~clk; // 50 MHz

rmii_udp_tx_bridge #(
    .LOCAL_MAC(48'h02_00_00_00_00_01),
    .LOCAL_IP(32'hA9FE_F23C),
    .LOCAL_PORT(16'd50000),
    .REMOTE_MAC(48'h9c_69_d3_1f_21_5e),
    .REMOTE_IP(32'hA9FE_F23B),
    .REMOTE_PORT(16'd50000),
    .TEN_MBIT_MODE(1'b1)
) dut (
    .clk_50(clk),
    .rst_n(rst_n),
    .carrier_sense(1'b0),
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

always @(posedge clk) begin
    if (!eth_txen) begin
        dibit_count <= 2'd0;
        sample_gap <= 4'd5;
        shift <= 8'h00;
    end else if (sample_gap != 4'd0) begin
        sample_gap <= sample_gap - 4'd1;
    end else begin
        sample_gap <= 4'd9;
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

    send_payload_byte(8'h09, 1'b0);
    send_payload_byte(8'h01, 1'b0);
    send_payload_byte(8'h00, 1'b0);
    send_payload_byte(8'h00, 1'b1);

    wait (packet_sent);
    repeat (3200) @(posedge clk);

    if (payload_overflow) $fatal(1, "unexpected payload overflow");
    if (byte_count != 16'd72) begin
        $display("byte_count=%0d first=%02x %02x %02x %02x %02x %02x %02x %02x",
                 byte_count, bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7]);
        $fatal(1, "expected 72 RMII bytes, got %0d", byte_count);
    end
    if (bytes[0] != 8'h55 || bytes[6] != 8'h55 || bytes[7] != 8'hd5) $fatal(1, "bad preamble/SFD");
    if (bytes[8] != 8'h9c || bytes[9] != 8'h69 || bytes[10] != 8'hd3 ||
        bytes[11] != 8'h1f || bytes[12] != 8'h21 || bytes[13] != 8'h5e) $fatal(1, "bad destination MAC");
    if (bytes[14] != 8'h02 || bytes[19] != 8'h01) $fatal(1, "bad source MAC");
    if (bytes[20] != 8'h08 || bytes[21] != 8'h00) $fatal(1, "bad ethertype");
    if (bytes[34] != 8'ha9 || bytes[37] != 8'h3c) $fatal(1, "bad source IP");
    if (bytes[38] != 8'ha9 || bytes[41] != 8'h3b) $fatal(1, "bad destination IP");
    if (bytes[42] != 8'hc3 || bytes[43] != 8'h50 || bytes[44] != 8'hc3 || bytes[45] != 8'h50) begin
        $fatal(1, "bad UDP ports");
    end
    if (bytes[50] != 8'h09 || bytes[51] != 8'h01 || bytes[52] != 8'h00 || bytes[53] != 8'h00) begin
        $fatal(1, "bad payload");
    end

    $display("PASS tb_rmii_udp_tx_bridge_10m");
    $finish;
end

endmodule
