`timescale 1ns / 1ps
//
// udp_ipv4_tx.v
// Minimal Ethernet/IPv4/UDP transmit framer for the Ethernet transport MVP.
//
// The input stream is one UDP payload datagram.  The module buffers it until
// tlast, then emits a complete Ethernet frame including padding and FCS.
//
module udp_ipv4_tx #(
    parameter [47:0] LOCAL_MAC   = 48'h02_00_00_00_00_01,
    parameter [31:0] LOCAL_IP    = 32'hA9FE_F23C, // 169.254.242.60
    parameter [15:0] LOCAL_PORT  = 16'd50000,
    parameter [47:0] REMOTE_MAC  = 48'h9c_69_d3_1f_21_5e,
    parameter [31:0] REMOTE_IP   = 32'hA9FE_F23B, // 169.254.242.59
    parameter [15:0] REMOTE_PORT = 16'd50000,
    parameter integer MAX_PAYLOAD_BYTES = 1472
) (
    input  wire       clk,
    input  wire       rst_n,

    input  wire [47:0] remote_mac,
    input  wire [31:0] remote_ip,
    input  wire [15:0] remote_port,

    input  wire [7:0] s_payload_tdata,
    input  wire       s_payload_tvalid,
    output wire       s_payload_tready,
    input  wire       s_payload_tlast,

    output reg  [7:0] m_frame_tdata,
    output reg        m_frame_tvalid,
    input  wire       m_frame_tready,
    output reg        m_frame_tlast,

    output wire       tx_busy,
    output reg        packet_sent,
    output reg        payload_overflow
);

localparam [1:0] ST_IDLE = 2'd0;
localparam [1:0] ST_RECV = 2'd1;
localparam [1:0] ST_SEND = 2'd2;

localparam [15:0] HEADER_BYTES = 16'd42;
localparam [15:0] MIN_FRAME_NO_FCS = 16'd60;

reg [1:0] state;
reg [15:0] payload_len;
reg [15:0] send_index;
reg [15:0] data_len_no_fcs;
reg [15:0] total_len_with_fcs;
reg [31:0] crc_state;
reg [31:0] fcs_latched;
reg [47:0] tx_remote_mac;
reg [31:0] tx_remote_ip;
reg [15:0] tx_remote_port;
reg [7:0] payload_mem [0:MAX_PAYLOAD_BYTES-1];

assign tx_busy = (state != ST_IDLE);
assign s_payload_tready = (state == ST_IDLE) || ((state == ST_RECV) && (payload_len < MAX_PAYLOAD_BYTES));

wire payload_input_fire = s_payload_tvalid && s_payload_tready;

function [7:0] mac_byte;
    input [47:0] mac;
    input [2:0] idx;
    begin
        case (idx)
            3'd0: mac_byte = mac[47:40];
            3'd1: mac_byte = mac[39:32];
            3'd2: mac_byte = mac[31:24];
            3'd3: mac_byte = mac[23:16];
            3'd4: mac_byte = mac[15:8];
            default: mac_byte = mac[7:0];
        endcase
    end
endfunction

function [7:0] ip_byte;
    input [31:0] ip;
    input [1:0] idx;
    begin
        case (idx)
            2'd0: ip_byte = ip[31:24];
            2'd1: ip_byte = ip[23:16];
            2'd2: ip_byte = ip[15:8];
            default: ip_byte = ip[7:0];
        endcase
    end
endfunction

function [15:0] ipv4_checksum;
    input [15:0] total_len;
    reg [19:0] sum;
    begin
        sum = 20'd0;
        sum = sum + 16'h4500;
        sum = sum + total_len;
        sum = sum + 16'h0000; // identification
        sum = sum + 16'h0000; // flags/fragment offset
        sum = sum + 16'h4011; // TTL/protocol UDP
        sum = sum + LOCAL_IP[31:16];
        sum = sum + LOCAL_IP[15:0];
        sum = sum + tx_remote_ip[31:16];
        sum = sum + tx_remote_ip[15:0];
        sum = {4'd0, sum[15:0]} + sum[19:16];
        sum = {4'd0, sum[15:0]} + sum[19:16];
        ipv4_checksum = ~sum[15:0];
    end
endfunction

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

function [7:0] fcs_byte;
    input [31:0] fcs;
    input [1:0] idx;
    begin
        case (idx)
            2'd0: fcs_byte = fcs[7:0];
            2'd1: fcs_byte = fcs[15:8];
            2'd2: fcs_byte = fcs[23:16];
            default: fcs_byte = fcs[31:24];
        endcase
    end
endfunction

function [7:0] frame_data_byte;
    input [15:0] idx;
    reg [15:0] ip_total_len;
    reg [15:0] udp_len;
    reg [15:0] csum;
    begin
        ip_total_len = 16'd28 + payload_len;
        udp_len = 16'd8 + payload_len;
        csum = ipv4_checksum(ip_total_len);

        if (idx < 16'd6) begin
            frame_data_byte = mac_byte(tx_remote_mac, idx[2:0]);
        end else if (idx < 16'd12) begin
            frame_data_byte = mac_byte(LOCAL_MAC, idx[2:0] - 3'd6);
        end else begin
            case (idx)
                16'd12: frame_data_byte = 8'h08;
                16'd13: frame_data_byte = 8'h00;
                16'd14: frame_data_byte = 8'h45;
                16'd15: frame_data_byte = 8'h00;
                16'd16: frame_data_byte = ip_total_len[15:8];
                16'd17: frame_data_byte = ip_total_len[7:0];
                16'd18: frame_data_byte = 8'h00;
                16'd19: frame_data_byte = 8'h00;
                16'd20: frame_data_byte = 8'h00;
                16'd21: frame_data_byte = 8'h00;
                16'd22: frame_data_byte = 8'h40;
                16'd23: frame_data_byte = 8'h11;
                16'd24: frame_data_byte = csum[15:8];
                16'd25: frame_data_byte = csum[7:0];
                16'd26: frame_data_byte = ip_byte(LOCAL_IP, 2'd0);
                16'd27: frame_data_byte = ip_byte(LOCAL_IP, 2'd1);
                16'd28: frame_data_byte = ip_byte(LOCAL_IP, 2'd2);
                16'd29: frame_data_byte = ip_byte(LOCAL_IP, 2'd3);
                16'd30: frame_data_byte = ip_byte(tx_remote_ip, 2'd0);
                16'd31: frame_data_byte = ip_byte(tx_remote_ip, 2'd1);
                16'd32: frame_data_byte = ip_byte(tx_remote_ip, 2'd2);
                16'd33: frame_data_byte = ip_byte(tx_remote_ip, 2'd3);
                16'd34: frame_data_byte = LOCAL_PORT[15:8];
                16'd35: frame_data_byte = LOCAL_PORT[7:0];
                16'd36: frame_data_byte = tx_remote_port[15:8];
                16'd37: frame_data_byte = tx_remote_port[7:0];
                16'd38: frame_data_byte = udp_len[15:8];
                16'd39: frame_data_byte = udp_len[7:0];
                16'd40: frame_data_byte = 8'h00; // UDP checksum disabled for IPv4
                16'd41: frame_data_byte = 8'h00;
                default: begin
                    if (idx < HEADER_BYTES + payload_len) begin
                        frame_data_byte = payload_mem[idx - HEADER_BYTES];
                    end else begin
                        frame_data_byte = 8'h00; // Ethernet minimum-frame padding
                    end
                end
            endcase
        end
    end
endfunction

wire [31:0] crc_after_current = crc32_next(crc_state, m_frame_tdata);
wire        output_transfer = m_frame_tvalid && m_frame_tready;
wire        sending_data = (send_index < data_len_no_fcs);
wire        sending_fcs = (send_index >= data_len_no_fcs);
wire [15:0] next_send_index = send_index + 16'd1;

always @(posedge clk) begin
    if (!rst_n) begin
        state            <= ST_IDLE;
        payload_len      <= 16'd0;
        send_index       <= 16'd0;
        data_len_no_fcs  <= 16'd0;
        total_len_with_fcs <= 16'd0;
        crc_state        <= 32'hFFFF_FFFF;
        fcs_latched      <= 32'd0;
        tx_remote_mac    <= REMOTE_MAC;
        tx_remote_ip     <= REMOTE_IP;
        tx_remote_port   <= REMOTE_PORT;
        m_frame_tdata    <= 8'h00;
        m_frame_tvalid   <= 1'b0;
        m_frame_tlast    <= 1'b0;
        packet_sent      <= 1'b0;
        payload_overflow <= 1'b0;
    end else begin
        packet_sent <= 1'b0;

        case (state)
            ST_IDLE: begin
                payload_len      <= 16'd0;
                send_index       <= 16'd0;
                crc_state        <= 32'hFFFF_FFFF;
                m_frame_tvalid   <= 1'b0;
                m_frame_tlast    <= 1'b0;
                payload_overflow <= 1'b0;

                if (s_payload_tvalid) begin
                    tx_remote_mac  <= remote_mac;
                    tx_remote_ip   <= remote_ip;
                    tx_remote_port <= remote_port;
                    if (MAX_PAYLOAD_BYTES > 0) begin
                        payload_mem[0] <= s_payload_tdata;
                    end
                    payload_len <= 16'd1;
                    if (s_payload_tlast) begin
                        data_len_no_fcs <= MIN_FRAME_NO_FCS;
                        total_len_with_fcs <= MIN_FRAME_NO_FCS + 16'd4;
                        state <= ST_SEND;
                        m_frame_tdata <= mac_byte(remote_mac, 3'd0);
                        m_frame_tvalid <= 1'b1;
                        m_frame_tlast <= 1'b0;
                    end else begin
                        state <= ST_RECV;
                    end
                end
            end

            ST_RECV: begin
                if (payload_input_fire) begin
                    payload_mem[payload_len] <= s_payload_tdata;
                    payload_len <= payload_len + 16'd1;
                    if (s_payload_tlast) begin
                        data_len_no_fcs <= (HEADER_BYTES + payload_len + 16'd1 < MIN_FRAME_NO_FCS) ?
                                           MIN_FRAME_NO_FCS : HEADER_BYTES + payload_len + 16'd1;
                        total_len_with_fcs <= ((HEADER_BYTES + payload_len + 16'd1 < MIN_FRAME_NO_FCS) ?
                                               MIN_FRAME_NO_FCS : HEADER_BYTES + payload_len + 16'd1) + 16'd4;
                        state <= ST_SEND;
                        m_frame_tdata <= mac_byte(remote_mac, 3'd0);
                        m_frame_tvalid <= 1'b1;
                        m_frame_tlast <= 1'b0;
                    end
                end else if (s_payload_tvalid && !s_payload_tready) begin
                    payload_overflow <= 1'b1;
                end
            end

            ST_SEND: begin
                if (output_transfer) begin
                    if (sending_data) begin
                        crc_state <= crc_after_current;
                        if (send_index == data_len_no_fcs - 16'd1) begin
                            fcs_latched <= ~crc_after_current;
                        end
                    end

                    if (send_index == total_len_with_fcs - 16'd1) begin
                        state <= ST_IDLE;
                        m_frame_tvalid <= 1'b0;
                        m_frame_tlast <= 1'b0;
                        packet_sent <= 1'b1;
                    end else begin
                        send_index <= next_send_index;
                        m_frame_tlast <= (next_send_index == total_len_with_fcs - 16'd1);
                        if (next_send_index < data_len_no_fcs) begin
                            m_frame_tdata <= frame_data_byte(next_send_index);
                        end else if (send_index == data_len_no_fcs - 16'd1) begin
                            m_frame_tdata <= fcs_byte(~crc_after_current, 2'd0);
                        end else begin
                            m_frame_tdata <= fcs_byte(fcs_latched, next_send_index - data_len_no_fcs);
                        end
                    end
                end
            end

            default: state <= ST_IDLE;
        endcase
    end
end

endmodule
