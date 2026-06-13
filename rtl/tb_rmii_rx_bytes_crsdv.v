`timescale 1ns / 1ps

module tb_rmii_rx_bytes_crsdv;
reg clk = 1'b0;
reg rst_n = 1'b0;
reg eth_crsdv = 1'b0;
reg eth_rxerr = 1'b0;
reg [1:0] eth_rxd = 2'b00;

wire [7:0] rx_byte;
wire       rx_byte_valid;
wire       rx_frame_start;
wire       rx_frame_end;
wire       rx_frame_error;
wire       sfd_seen;

reg [31:0] received = 32'd0;
reg [7:0]  bytes [0:3];
reg        saw_sfd = 1'b0;
reg        saw_end = 1'b0;

always #10 clk = ~clk; // 50 MHz

rmii_rx_bytes dut (
    .clk_50(clk),
    .rst_n(rst_n),
    .eth_crsdv(eth_crsdv),
    .eth_rxerr(eth_rxerr),
    .eth_rxd(eth_rxd),
    .rx_byte(rx_byte),
    .rx_byte_valid(rx_byte_valid),
    .rx_frame_start(rx_frame_start),
    .rx_frame_end(rx_frame_end),
    .rx_frame_error(rx_frame_error),
    .sfd_seen(sfd_seen)
);

task send_dibit;
    input [1:0] dibit;
    input       crsdv;
    begin
        @(negedge clk);
        eth_rxd <= dibit;
        eth_crsdv <= crsdv;
    end
endtask

task send_byte_masked;
    input [7:0] b;
    input [3:0] crsdv_mask;
    begin
        send_dibit(b[1:0], crsdv_mask[0]);
        send_dibit(b[3:2], crsdv_mask[1]);
        send_dibit(b[5:4], crsdv_mask[2]);
        send_dibit(b[7:6], crsdv_mask[3]);
    end
endtask

always @(posedge clk) begin
    if (sfd_seen) begin
        saw_sfd <= 1'b1;
    end
    if (rx_frame_end) begin
        saw_end <= 1'b1;
        if (rx_frame_error) begin
            $fatal(1, "unexpected frame error");
        end
    end
    if (rx_byte_valid) begin
        bytes[received] <= rx_byte;
        received <= received + 32'd1;
    end
end

initial begin
    repeat (4) @(posedge clk);
    rst_n <= 1'b1;
    repeat (2) @(posedge clk);

    send_byte_masked(8'h55, 4'b1111);
    send_byte_masked(8'h55, 4'b1111);
    send_byte_masked(8'h55, 4'b1111);
    send_byte_masked(8'h55, 4'b1111);
    send_byte_masked(8'h55, 4'b1111);
    send_byte_masked(8'h55, 4'b1111);
    send_byte_masked(8'h55, 4'b1111);
    send_byte_masked(8'hd5, 4'b1111);

    send_byte_masked(8'h11, 4'b1111);

    // RMII CRS_DV can toggle after carrier loss while RXD still contains valid
    // buffered data.  Low/high, low/high matches first/second dibit of each
    // nibble and should still produce a complete byte.
    send_byte_masked(8'h22, 4'b1010);
    send_byte_masked(8'h33, 4'b1010);

    send_dibit(2'b00, 1'b0);
    send_dibit(2'b00, 1'b0);
    send_dibit(2'b00, 1'b0);
    send_dibit(2'b00, 1'b0);
    send_dibit(2'b00, 1'b0);

    repeat (8) @(posedge clk);

    if (!saw_sfd) $fatal(1, "SFD was not observed");
    if (!saw_end) $fatal(1, "frame end was not observed");
    if (received != 32'd3) $fatal(1, "expected 3 bytes, got %0d", received);
    if (bytes[0] != 8'h11 || bytes[1] != 8'h22 || bytes[2] != 8'h33) begin
        $fatal(1, "payload mismatch: %02x %02x %02x", bytes[0], bytes[1], bytes[2]);
    end

    $display("PASS tb_rmii_rx_bytes_crsdv");
    $finish;
end

endmodule
