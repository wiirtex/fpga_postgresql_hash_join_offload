`timescale 1ns / 1ps

module tb_udp_ipv4_tx_overflow;
reg clk = 1'b0;
reg rst_n = 1'b0;
reg [7:0] payload_tdata = 8'h00;
reg       payload_tvalid = 1'b0;
wire      payload_tready;
reg       payload_tlast = 1'b0;
wire [7:0] frame_tdata;
wire       frame_tvalid;
reg        frame_tready = 1'b1;
wire       frame_tlast;
wire       payload_overflow;

always #10 clk = ~clk;

udp_ipv4_tx #(
    .MAX_PAYLOAD_BYTES(2)
) dut (
    .clk(clk),
    .rst_n(rst_n),
    .remote_mac(48'h9c_69_d3_1f_21_5e),
    .remote_ip(32'hA9FE_F23B),
    .remote_port(16'd50000),
    .s_payload_tdata(payload_tdata),
    .s_payload_tvalid(payload_tvalid),
    .s_payload_tready(payload_tready),
    .s_payload_tlast(payload_tlast),
    .m_frame_tdata(frame_tdata),
    .m_frame_tvalid(frame_tvalid),
    .m_frame_tready(frame_tready),
    .m_frame_tlast(frame_tlast),
    .tx_busy(),
    .packet_sent(),
    .payload_overflow(payload_overflow)
);

task offer_byte;
    input [7:0] b;
    input       last;
    begin
        @(negedge clk);
        payload_tdata <= b;
        payload_tlast <= last;
        payload_tvalid <= 1'b1;
        @(negedge clk);
        payload_tvalid <= 1'b0;
        payload_tlast <= 1'b0;
    end
endtask

initial begin
    repeat (4) @(posedge clk);
    rst_n <= 1'b1;
    repeat (2) @(posedge clk);

    offer_byte("A", 1'b0);
    offer_byte("B", 1'b0);

    @(posedge clk);
    if (payload_tready) $fatal(1, "ready stayed high at MAX_PAYLOAD_BYTES boundary");

    offer_byte("C", 1'b1);
    repeat (2) @(posedge clk);

    if (!payload_overflow) $fatal(1, "expected payload_overflow");

    $display("PASS tb_udp_ipv4_tx_overflow");
    $finish;
end

endmodule
