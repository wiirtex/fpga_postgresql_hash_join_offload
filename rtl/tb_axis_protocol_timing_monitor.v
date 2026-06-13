`timescale 1ns / 1ps

module tb_axis_protocol_timing_monitor;
reg clk = 1'b0;
reg rst_n = 1'b0;
reg clear = 1'b0;

reg [7:0] rx_tdata = 8'h00;
reg       rx_tvalid = 1'b0;
wire      rx_tready = 1'b1;

reg [7:0] tx_tdata = 8'h00;
reg       tx_tvalid = 1'b0;
wire      tx_tready = 1'b1;

wire [63:0] cycle_counter;
wire        timing_valid;
wire [31:0] inner_frames;
wire [31:0] outer_frames;
wire [31:0] result_frames;
wire [31:0] ack_frames;
wire [31:0] debug_frames;
wire [31:0] status_frames;
wire [31:0] timing_frames;
wire [31:0] bytes_rx;
wire [31:0] bytes_tx;
wire [63:0] ts_config_first;
wire [63:0] ts_config_ack;
wire [63:0] ts_build_first;
wire [63:0] ts_build_last;
wire [63:0] ts_build_ack_last;
wire [63:0] ts_probe_first;
wire [63:0] ts_probe_last;
wire [63:0] ts_probe_ack_last;
wire [63:0] ts_result_first;
wire [63:0] ts_result_last;
wire [63:0] ts_timing_first;
wire [63:0] ts_done_status;
wire [63:0] cycles_config_to_build_done;
wire [63:0] cycles_config_to_probe_done;
wire [63:0] cycles_config_to_status_done;
wire [31:0] clock_hz;

integer i;

always #5 clk = ~clk;

axis_protocol_timing_monitor #(
    .CLOCK_HZ(32'd100000000)
) dut (
    .clk(clk),
    .rst_n(rst_n),
    .clear(clear),
    .rx_tdata(rx_tdata),
    .rx_tvalid(rx_tvalid),
    .rx_tready(rx_tready),
    .tx_tdata(tx_tdata),
    .tx_tvalid(tx_tvalid),
    .tx_tready(tx_tready),
    .cycle_counter(cycle_counter),
    .timing_valid(timing_valid),
    .inner_frames(inner_frames),
    .outer_frames(outer_frames),
    .result_frames(result_frames),
    .ack_frames(ack_frames),
    .debug_frames(debug_frames),
    .status_frames(status_frames),
    .timing_frames(timing_frames),
    .bytes_rx(bytes_rx),
    .bytes_tx(bytes_tx),
    .ts_config_first(ts_config_first),
    .ts_config_ack(ts_config_ack),
    .ts_build_first(ts_build_first),
    .ts_build_last(ts_build_last),
    .ts_build_ack_last(ts_build_ack_last),
    .ts_probe_first(ts_probe_first),
    .ts_probe_last(ts_probe_last),
    .ts_probe_ack_last(ts_probe_ack_last),
    .ts_result_first(ts_result_first),
    .ts_result_last(ts_result_last),
    .ts_timing_first(ts_timing_first),
    .ts_done_status(ts_done_status),
    .cycles_config_to_build_done(cycles_config_to_build_done),
    .cycles_config_to_probe_done(cycles_config_to_probe_done),
    .cycles_config_to_status_done(cycles_config_to_status_done),
    .clock_hz(clock_hz)
);

task rx_byte;
    input [7:0] b;
    begin
        @(negedge clk);
        rx_tdata <= b;
        rx_tvalid <= 1'b1;
        @(negedge clk);
        rx_tvalid <= 1'b0;
        rx_tdata <= 8'h00;
    end
endtask

task tx_byte;
    input [7:0] b;
    begin
        @(negedge clk);
        tx_tdata <= b;
        tx_tvalid <= 1'b1;
        @(negedge clk);
        tx_tvalid <= 1'b0;
        tx_tdata <= 8'h00;
    end
endtask

task rx_header;
    input [7:0] msg;
    input [15:0] count;
    begin
        rx_byte(msg);
        rx_byte(count[7:0]);
        rx_byte(count[15:8]);
    end
endtask

task tx_header;
    input [7:0] msg;
    input [15:0] count;
    begin
        tx_byte(msg);
        tx_byte(count[7:0]);
        tx_byte(count[15:8]);
    end
endtask

task send_configure;
    begin
        rx_header(8'h01, 16'd1);
        rx_byte(8'h00); // algorithm A
        rx_byte(8'h01); // int32 key
        rx_byte(8'h00); rx_byte(8'h01); // rx_buf_hint = 256
        rx_byte(8'h02); rx_byte(8'h00); rx_byte(8'h00); rx_byte(8'h00); // inner=2
        rx_byte(8'h03); rx_byte(8'h00); rx_byte(8'h00); rx_byte(8'h00); // outer=3
    end
endtask

task send_data_frame;
    input [7:0] msg;
    input [15:0] count;
    integer j;
    begin
        rx_header(msg, count);
        for (j = 0; j < count * 10; j = j + 1)
            rx_byte(j[7:0]);
    end
endtask

task send_status_like;
    input [7:0] msg;
    input [7:0] phase;
    begin
        tx_header(msg, 16'd1);
        tx_byte(phase);
        for (i = 0; i < 11; i = i + 1)
            tx_byte(8'h00);
    end
endtask

task send_result;
    begin
        tx_header(8'h04, 16'd1);
        for (i = 0; i < 12; i = i + 1)
            tx_byte(i[7:0]);
    end
endtask

task send_timing;
    begin
        tx_header(8'h0A, 16'd1);
        for (i = 0; i < 200; i = i + 1)
            tx_byte(i[7:0]);
    end
endtask

initial begin
    repeat (4) @(negedge clk);
    rst_n <= 1'b1;
    repeat (3) @(negedge clk);

    send_configure();
    repeat (2) @(negedge clk);
    send_status_like(8'h05, 8'h01); // ACK BUILDING
    repeat (3) @(negedge clk);
    send_data_frame(8'h02, 16'd2); // INNER_DATA
    repeat (2) @(negedge clk);
    send_status_like(8'h05, 8'h01); // ACK BUILDING
    repeat (3) @(negedge clk);
    send_data_frame(8'h03, 16'd3); // OUTER_DATA
    repeat (2) @(negedge clk);
    send_result();
    repeat (2) @(negedge clk);
    send_timing();
    repeat (2) @(negedge clk);
    send_status_like(8'h06, 8'h03); // STATUS DONE
    repeat (3) @(negedge clk);

    if (!timing_valid) $fatal(1, "timing_valid was not asserted");
    if (clock_hz != 32'd100000000) $fatal(1, "clock_hz mismatch");
    if (inner_frames != 32'd1) $fatal(1, "inner_frames=%0d", inner_frames);
    if (outer_frames != 32'd1) $fatal(1, "outer_frames=%0d", outer_frames);
    if (result_frames != 32'd1) $fatal(1, "result_frames=%0d", result_frames);
    if (ack_frames != 32'd2) $fatal(1, "ack_frames=%0d", ack_frames);
    if (status_frames != 32'd1) $fatal(1, "status_frames=%0d", status_frames);
    if (timing_frames != 32'd1) $fatal(1, "timing_frames=%0d", timing_frames);
    if (bytes_rx != 32'd71) $fatal(1, "bytes_rx=%0d", bytes_rx);
    if (bytes_tx != 32'd263) $fatal(1, "bytes_tx=%0d", bytes_tx);

    if (ts_config_first == 64'd0) $fatal(1, "missing config timestamp");
    if (!(ts_config_first < ts_config_ack)) $fatal(1, "config ack ordering failed");
    if (!(ts_config_ack < ts_build_first)) $fatal(1, "build first ordering failed");
    if (!(ts_build_first < ts_build_last)) $fatal(1, "build last ordering failed");
    if (!(ts_build_last < ts_build_ack_last)) $fatal(1, "build ack ordering failed");
    if (!(ts_build_ack_last < ts_probe_first)) $fatal(1, "probe first ordering failed");
    if (!(ts_probe_first < ts_probe_last)) $fatal(1, "probe last ordering failed");
    if (!(ts_probe_last < ts_result_first)) $fatal(1, "result first ordering failed");
    if (!(ts_result_first < ts_result_last)) $fatal(1, "result last ordering failed");
    if (!(ts_result_last < ts_timing_first)) $fatal(1, "timing first ordering failed");
    if (!(ts_timing_first < ts_done_status)) $fatal(1, "done status ordering failed");
    if (cycles_config_to_build_done == 64'd0) $fatal(1, "missing build duration");
    if (cycles_config_to_probe_done == 64'd0) $fatal(1, "missing probe duration");
    if (cycles_config_to_status_done == 64'd0) $fatal(1, "missing total duration");

    $display("PASS tb_axis_protocol_timing_monitor");
    $finish;
end

endmodule
