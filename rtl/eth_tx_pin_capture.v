`timescale 1ns / 1ps
//
// eth_tx_pin_capture.v
// Passive RMII TX pin monitor for bring-up.  This module only observes the
// external TX pins and bridge status pulses; it does not feed the TX datapath.
//
module eth_tx_pin_capture #(
    parameter TEN_MBIT_MODE = 1'b1
) (
    input  wire       clk_50,
    input  wire       rst_n,

    input  wire       eth_txen,
    input  wire [1:0] eth_txd,
    input  wire       eth_crsdv,

    input  wire       tx_busy,
    input  wire       tx_packet_sent,
    input  wire       tx_payload_overflow,

    output reg [15:0] tx_frame_count,
    output reg [15:0] tx_packet_sent_count,
    output reg [15:0] tx_overflow_count,
    output reg [15:0] last_txen_clocks,
    output reg [15:0] active_txen_clocks,
    output reg [15:0] last_tx_dibits,
    output reg [15:0] active_tx_dibits,
    output reg [31:0] first16_dibits,
    output reg [15:0] status_flags
);

localparam integer SAMPLE_DIV_MAX = TEN_MBIT_MODE ? 9 : 0;

reg        eth_txen_d1;
reg [3:0]  sample_div;
reg [5:0]  sample_index;
reg        frame_preamble_bad;
reg        frame_sfd_bad;
reg        frame_sfd_ok;
reg        frame_carrier_seen;
reg        frame_txd_nonzero;
reg        frame_txd_changed;
reg [1:0]  last_sampled_dibit;

wire txen_rise = eth_txen && !eth_txen_d1;
wire txen_fall = !eth_txen && eth_txen_d1;
wire sample_now = eth_txen && (sample_div == 4'd0);

function expected_preamble_or_sfd;
    input [5:0] idx;
    input [1:0] dibit;
    begin
        if (idx < 6'd31) begin
            expected_preamble_or_sfd = (dibit == 2'b01);
        end else if (idx == 6'd31) begin
            expected_preamble_or_sfd = (dibit == 2'b11);
        end else begin
            expected_preamble_or_sfd = 1'b1;
        end
    end
endfunction

always @(posedge clk_50) begin
    if (!rst_n) begin
        eth_txen_d1          <= 1'b0;
        sample_div           <= 4'd0;
        sample_index         <= 6'd0;
        tx_frame_count       <= 16'd0;
        tx_packet_sent_count <= 16'd0;
        tx_overflow_count    <= 16'd0;
        last_txen_clocks     <= 16'd0;
        active_txen_clocks   <= 16'd0;
        last_tx_dibits       <= 16'd0;
        active_tx_dibits     <= 16'd0;
        first16_dibits       <= 32'd0;
        status_flags         <= 16'd0;
        frame_preamble_bad   <= 1'b0;
        frame_sfd_bad        <= 1'b0;
        frame_sfd_ok         <= 1'b0;
        frame_carrier_seen   <= 1'b0;
        frame_txd_nonzero    <= 1'b0;
        frame_txd_changed    <= 1'b0;
        last_sampled_dibit   <= 2'b00;
    end else begin
        eth_txen_d1 <= eth_txen;

        if (tx_packet_sent) tx_packet_sent_count <= tx_packet_sent_count + 16'd1;
        if (tx_payload_overflow) tx_overflow_count <= tx_overflow_count + 16'd1;

        if (txen_rise) begin
            tx_frame_count     <= tx_frame_count + 16'd1;
            active_txen_clocks <= 16'd0;
            active_tx_dibits   <= 16'd0;
            first16_dibits     <= 32'd0;
            sample_div         <= 4'd0;
            sample_index       <= 6'd0;
            frame_preamble_bad <= 1'b0;
            frame_sfd_bad      <= 1'b0;
            frame_sfd_ok       <= 1'b0;
            frame_carrier_seen <= 1'b0;
            frame_txd_nonzero  <= 1'b0;
            frame_txd_changed  <= 1'b0;
            last_sampled_dibit <= eth_txd;
        end

        if (eth_txen) begin
            active_txen_clocks <= active_txen_clocks + 16'd1;
            if (eth_crsdv) frame_carrier_seen <= 1'b1;

            if (sample_now) begin
                active_tx_dibits <= active_tx_dibits + 16'd1;
                if (eth_txd != 2'b00) frame_txd_nonzero <= 1'b1;
                if (eth_txd != last_sampled_dibit) frame_txd_changed <= 1'b1;
                last_sampled_dibit <= eth_txd;

                if (sample_index < 6'd16) begin
                    first16_dibits <= {eth_txd, first16_dibits[31:2]};
                end

                if (sample_index < 6'd32 && !expected_preamble_or_sfd(sample_index, eth_txd)) begin
                    if (sample_index < 6'd28) begin
                        frame_preamble_bad <= 1'b1;
                    end else begin
                        frame_sfd_bad <= 1'b1;
                    end
                end
                if (sample_index == 6'd31 && !frame_preamble_bad && !frame_sfd_bad &&
                    expected_preamble_or_sfd(sample_index, eth_txd)) begin
                    frame_sfd_ok <= 1'b1;
                end
                if (sample_index != 6'h3f) sample_index <= sample_index + 6'd1;
            end

            if (sample_div == SAMPLE_DIV_MAX[3:0]) begin
                sample_div <= 4'd0;
            end else begin
                sample_div <= sample_div + 4'd1;
            end
        end else begin
            sample_div <= 4'd0;
        end

        if (txen_fall) begin
            last_txen_clocks <= active_txen_clocks;
            last_tx_dibits   <= active_tx_dibits;
            status_flags <= {
                3'b000,
                tx_busy,
                tx_payload_overflow,
                tx_packet_sent,
                frame_txd_changed,
                frame_txd_nonzero,
                frame_carrier_seen,
                frame_sfd_ok,
                frame_sfd_bad,
                frame_preamble_bad,
                eth_txd[1],
                eth_txd[0],
                eth_txen,
                1'b1
            };
        end
    end
end

endmodule
