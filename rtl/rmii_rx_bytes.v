`timescale 1ns / 1ps
//
// rmii_rx_bytes.v
// Receive-side RMII byte extractor.
//
// It watches CRS_DV/RXD, skips Ethernet preamble/SFD, then emits one byte per
// four RMII dibits.  In RMII, CRS_DV is a combined carrier/data-valid signal; at
// the end of a frame it can toggle while RXD still carries valid buffered data.
//
module rmii_rx_bytes #(
    parameter TEN_MBIT_MODE = 1'b0
) (
    input  wire       clk_50,
    input  wire       rst_n,

    input  wire       eth_crsdv,
    input  wire       eth_rxerr,
    input  wire [1:0] eth_rxd,

    output reg  [7:0] rx_byte,
    output reg        rx_byte_valid,
    output reg        rx_frame_start,
    output reg        rx_frame_end,
    output reg        rx_frame_error,
    output reg        sfd_seen
);

localparam [1:0] ST_IDLE     = 2'd0;
localparam [1:0] ST_PREAMBLE = 2'd1;
localparam [1:0] ST_PAYLOAD  = 2'd2;
localparam [2:0] END_GAP_CYCLES = 3'd4;

reg [1:0] state;
reg [1:0] dibit_count;
reg [7:0] shift;
reg       frame_error_latch;
reg       crsdv_d1;
reg [2:0] crsdv_low_count;
reg [3:0] sample_gap;

wire [7:0] next_byte = {eth_rxd, shift[7:2]};
wire       byte_done = (dibit_count == 2'd3);
wire       rmii_rx_dv = eth_crsdv | crsdv_d1;
wire       sample_now = (!TEN_MBIT_MODE) || (sample_gap == 4'd0);
wire [3:0] next_sample_gap = TEN_MBIT_MODE ? 4'd9 : 4'd0;

always @(posedge clk_50) begin
    if (!rst_n) begin
        state              <= ST_IDLE;
        dibit_count        <= 2'd0;
        shift              <= 8'h00;
        frame_error_latch  <= 1'b0;
        crsdv_d1           <= 1'b0;
        crsdv_low_count    <= 3'd0;
        sample_gap         <= 4'd0;
        rx_byte            <= 8'h00;
        rx_byte_valid      <= 1'b0;
        rx_frame_start     <= 1'b0;
        rx_frame_end       <= 1'b0;
        rx_frame_error     <= 1'b0;
        sfd_seen           <= 1'b0;
    end else begin
        rx_byte_valid  <= 1'b0;
        rx_frame_start <= 1'b0;
        rx_frame_end   <= 1'b0;
        rx_frame_error <= 1'b0;
        sfd_seen       <= 1'b0;
        crsdv_d1       <= eth_crsdv;

        if (state != ST_IDLE && eth_rxerr) begin
            frame_error_latch <= 1'b1;
        end

        case (state)
            ST_IDLE: begin
                dibit_count        <= 2'd0;
                shift              <= 8'h00;
                frame_error_latch  <= 1'b0;
                crsdv_low_count    <= 3'd0;
                sample_gap         <= 4'd0;
                if (eth_crsdv) begin
                    state <= ST_PREAMBLE;
                    shift <= {eth_rxd, 6'b0};
                    dibit_count <= 2'd1;
                    sample_gap <= next_sample_gap;
                end
            end

            ST_PREAMBLE: begin
                if (!eth_crsdv) begin
                    state <= ST_IDLE;
                end else if (!sample_now) begin
                    sample_gap <= sample_gap - 4'd1;
                end else begin
                    sample_gap <= next_sample_gap;
                    shift <= next_byte;
                    if (next_byte == 8'hd5) begin
                        state <= ST_PAYLOAD;
                        dibit_count <= 2'd0;
                        sfd_seen <= 1'b1;
                        rx_frame_start <= 1'b1;
                        crsdv_low_count <= 3'd0;
                    end else if (byte_done) begin
                        dibit_count <= 2'd0;
                    end else begin
                        dibit_count <= dibit_count + 2'd1;
                    end
                end
            end

            ST_PAYLOAD: begin
                if (!rmii_rx_dv) begin
                    if (crsdv_low_count == END_GAP_CYCLES - 3'd1) begin
                        state <= ST_IDLE;
                        rx_frame_end <= 1'b1;
                        rx_frame_error <= frame_error_latch;
                        dibit_count <= 2'd0;
                        crsdv_low_count <= 3'd0;
                    end else begin
                        crsdv_low_count <= crsdv_low_count + 3'd1;
                    end
                end else if (!sample_now) begin
                    sample_gap <= sample_gap - 4'd1;
                end else begin
                    sample_gap <= next_sample_gap;
                    if (eth_crsdv) begin
                        crsdv_low_count <= 3'd0;
                    end
                    shift <= next_byte;
                    if (byte_done) begin
                        dibit_count <= 2'd0;
                        rx_byte <= next_byte;
                        rx_byte_valid <= 1'b1;
                    end else begin
                        dibit_count <= dibit_count + 2'd1;
                    end
                end
            end

            default: state <= ST_IDLE;
        endcase
    end
end

endmodule
