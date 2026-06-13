`timescale 1ns / 1ps
//
// udp_ipv4_rx.v
// Minimal Ethernet/IPv4/UDP receive filter for the Ethernet transport MVP.
//
// Supported frames:
//   Ethernet II, dst = LOCAL_MAC or broadcast, ethertype 0x0800
//   IPv4, IHL=5, no fragmentation, protocol UDP, dst = LOCAL_IP or broadcast
//   UDP dst port = LOCAL_PORT
//
// Checksums are not verified in this MVP parser.  UDP length is used to emit
// only payload bytes and ignore trailing Ethernet FCS.
//
module udp_ipv4_rx #(
    parameter [47:0] LOCAL_MAC  = 48'h02_00_00_00_00_01,
    parameter [31:0] LOCAL_IP   = 32'hA9FE_F23C, // 169.254.242.60
    parameter [31:0] SUBNET_BROADCAST_IP = 32'hA9FE_FFFF, // 169.254.255.255
    parameter [15:0] LOCAL_PORT = 16'd50000,
    parameter        ACCEPT_ANY_DEST_MAC = 1'b0,
    parameter integer MAX_PAYLOAD_BYTES = 1472
) (
    input  wire       clk,
    input  wire       rst_n,

    input  wire [7:0] rx_byte,
    input  wire       rx_byte_valid,
    input  wire       rx_frame_start,
    input  wire       rx_frame_end,
    input  wire       rx_frame_error,

    output reg  [7:0] m_payload_tdata,
    output reg        m_payload_tvalid,
    input  wire       m_payload_tready,
    output reg        m_payload_tlast,

    output reg        packet_seen,
    output reg        packet_dropped,
    output reg        parser_error,
    // Diagnostic stage outputs. In the current Ethernet bring-up bitstream
    // these are repurposed for UDP-header progress without changing BD ports.
    output reg        debug_dst_mac_accepted,
    output reg        debug_ethertype_hi_accepted,
    output reg        debug_eth_accepted,
    output reg        debug_ip_accepted,

    // Source endpoint captured from the last accepted UDP packet.  The TX path
    // uses these values for replies so the diagnostic bitstream is not tied to
    // one host NIC MAC/IP/port.
    output reg [47:0] remote_mac,
    output reg [31:0] remote_ip,
    output reg [15:0] remote_port
);

localparam [2:0] ST_IDLE    = 3'd0;
localparam [2:0] ST_ETH     = 3'd1;
localparam [2:0] ST_IP      = 3'd2;
localparam [2:0] ST_UDP     = 3'd3;
localparam [2:0] ST_PAYLOAD = 3'd4;
localparam [2:0] ST_DROP    = 3'd5;

reg [2:0] state;
reg [5:0] index;
reg       dst_mac_match;
reg       dst_mac_bcast;
reg       ethertype_ipv4;
reg       ip_ok;
reg       dst_ip_match;
reg       dst_ip_bcast;
reg       dst_ip_subnet_bcast;
reg       udp_port_match;
reg [15:0] ip_total_length;
reg [15:0] udp_length;
reg [15:0] payload_remaining;
reg [15:0] payload_len;
reg        write_buf;
reg        read_buf;
reg        output_active;
reg [15:0] read_len;
reg [15:0] read_index;
reg [15:0] buf_len0;
reg [15:0] buf_len1;
reg        buf_pending0;
reg        buf_pending1;
reg [47:0] packet_src_mac;
reg [31:0] packet_src_ip;
reg [15:0] packet_src_port;
reg [7:0] payload_mem0 [0:MAX_PAYLOAD_BYTES-1];
reg [7:0] payload_mem1 [0:MAX_PAYLOAD_BYTES-1];

wire write_buf0_busy = buf_pending0 || (output_active && !read_buf);
wire write_buf1_busy = buf_pending1 || (output_active && read_buf);
wire write_buf_busy = write_buf ? write_buf1_busy : write_buf0_busy;

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

task drop_packet;
    begin
        state <= ST_DROP;
        packet_dropped <= 1'b1;
    end
endtask

always @(posedge clk) begin
    if (!rst_n) begin
        state              <= ST_IDLE;
        index              <= 6'd0;
        dst_mac_match      <= 1'b0;
        dst_mac_bcast      <= 1'b0;
        ethertype_ipv4     <= 1'b0;
        ip_ok              <= 1'b0;
        dst_ip_match       <= 1'b0;
        dst_ip_bcast       <= 1'b0;
        dst_ip_subnet_bcast <= 1'b0;
        udp_port_match     <= 1'b0;
        ip_total_length    <= 16'd0;
        udp_length         <= 16'd0;
        payload_remaining  <= 16'd0;
        payload_len        <= 16'd0;
        write_buf          <= 1'b0;
        read_buf           <= 1'b0;
        output_active      <= 1'b0;
        read_len           <= 16'd0;
        read_index         <= 16'd0;
        buf_len0           <= 16'd0;
        buf_len1           <= 16'd0;
        buf_pending0       <= 1'b0;
        buf_pending1       <= 1'b0;
        packet_src_mac     <= 48'd0;
        packet_src_ip      <= 32'd0;
        packet_src_port    <= 16'd0;
        m_payload_tdata    <= 8'h00;
        m_payload_tvalid   <= 1'b0;
        m_payload_tlast    <= 1'b0;
        packet_seen        <= 1'b0;
        packet_dropped     <= 1'b0;
        parser_error       <= 1'b0;
        debug_dst_mac_accepted <= 1'b0;
        debug_ethertype_hi_accepted <= 1'b0;
        debug_eth_accepted <= 1'b0;
        debug_ip_accepted  <= 1'b0;
        remote_mac         <= 48'd0;
        remote_ip          <= 32'd0;
        remote_port        <= 16'd0;
    end else begin
        packet_seen      <= 1'b0;
        packet_dropped   <= 1'b0;
        parser_error     <= 1'b0;

        if (!output_active && !m_payload_tvalid) begin
            if (buf_pending0) begin
                output_active <= 1'b1;
                read_buf <= 1'b0;
                read_len <= buf_len0;
                read_index <= 16'd0;
                m_payload_tdata <= payload_mem0[0];
                m_payload_tvalid <= 1'b1;
                m_payload_tlast <= (buf_len0 == 16'd1);
            end else if (buf_pending1) begin
                output_active <= 1'b1;
                read_buf <= 1'b1;
                read_len <= buf_len1;
                read_index <= 16'd0;
                m_payload_tdata <= payload_mem1[0];
                m_payload_tvalid <= 1'b1;
                m_payload_tlast <= (buf_len1 == 16'd1);
            end
        end else if (m_payload_tvalid && m_payload_tready) begin
            if (m_payload_tlast) begin
                m_payload_tvalid <= 1'b0;
                m_payload_tlast <= 1'b0;
                output_active <= 1'b0;
                if (read_buf) begin
                    buf_pending1 <= 1'b0;
                end else begin
                    buf_pending0 <= 1'b0;
                end
            end else begin
                read_index <= read_index + 16'd1;
                if (read_buf) begin
                    m_payload_tdata <= payload_mem1[read_index + 16'd1];
                end else begin
                    m_payload_tdata <= payload_mem0[read_index + 16'd1];
                end
                m_payload_tlast <= (read_index + 16'd1 == read_len - 16'd1);
            end
        end

        if (rx_frame_start) begin
            if (write_buf_busy) begin
                state <= ST_DROP;
                parser_error <= 1'b1;
            end else begin
                state          <= ST_ETH;
                index          <= 6'd0;
                dst_mac_match  <= 1'b1;
                dst_mac_bcast  <= 1'b1;
                ethertype_ipv4 <= 1'b0;
                ip_ok          <= 1'b1;
                dst_ip_match   <= 1'b1;
                dst_ip_bcast   <= 1'b1;
                dst_ip_subnet_bcast <= 1'b1;
                udp_port_match <= 1'b1;
                ip_total_length <= 16'd0;
                udp_length     <= 16'd0;
                payload_remaining <= 16'd0;
                payload_len    <= 16'd0;
                packet_src_mac <= 48'd0;
                packet_src_ip <= 32'd0;
                packet_src_port <= 16'd0;
            end
        end

        if (rx_frame_end) begin
            state <= ST_IDLE;
            if (state == ST_PAYLOAD && payload_remaining != 16'd0) begin
                parser_error <= 1'b1;
            end
            if (rx_frame_error) begin
                parser_error <= 1'b1;
            end
        end else if (rx_byte_valid) begin
            case (state)
                ST_ETH: begin
                    if (index < 6) begin
                        if (rx_byte != mac_byte(LOCAL_MAC, index[2:0])) begin
                            dst_mac_match <= 1'b0;
                        end
                        if (rx_byte != 8'hff) begin
                            dst_mac_bcast <= 1'b0;
                        end
                    end else if (index < 6'd12) begin
                        case (index)
                            6'd6:  packet_src_mac[47:40] <= rx_byte;
                            6'd7:  packet_src_mac[39:32] <= rx_byte;
                            6'd8:  packet_src_mac[31:24] <= rx_byte;
                            6'd9:  packet_src_mac[23:16] <= rx_byte;
                            6'd10: packet_src_mac[15:8]  <= rx_byte;
                            default: packet_src_mac[7:0] <= rx_byte;
                        endcase
                    end else if (index == 6'd12) begin
                        ethertype_ipv4 <= (rx_byte == 8'h08);
                        if (rx_byte == 8'h08) begin
                            debug_ethertype_hi_accepted <= 1'b1;
                        end
                    end else if (index == 6'd13) begin
                        ethertype_ipv4 <= ethertype_ipv4 && (rx_byte == 8'h00);
                        if (!(ACCEPT_ANY_DEST_MAC || dst_mac_match || dst_mac_bcast) ||
                            !(ethertype_ipv4 && (rx_byte == 8'h00))) begin
                            drop_packet();
                        end else begin
                            state <= ST_IP;
                            index <= 6'd0;
                        end
                    end

                    if (state == ST_ETH && index != 6'd13) begin
                        index <= index + 6'd1;
                    end
                end

                ST_IP: begin
                    case (index)
                        6'd0:  ip_ok <= (rx_byte == 8'h45);
                        6'd2:  ip_total_length[15:8] <= rx_byte;
                        6'd3:  ip_total_length[7:0] <= rx_byte;
                        6'd6:  if ((rx_byte & 8'h3f) != 8'h00) ip_ok <= 1'b0;
                        6'd7:  if (rx_byte != 8'h00) ip_ok <= 1'b0;
                        6'd9:  if (rx_byte != 8'h11) ip_ok <= 1'b0;
                        6'd12: packet_src_ip[31:24] <= rx_byte;
                        6'd13: packet_src_ip[23:16] <= rx_byte;
                        6'd14: packet_src_ip[15:8]  <= rx_byte;
                        6'd15: packet_src_ip[7:0]   <= rx_byte;
                        6'd16, 6'd17, 6'd18, 6'd19: begin
                            if (rx_byte != ip_byte(LOCAL_IP, index[1:0])) dst_ip_match <= 1'b0;
                            if (rx_byte != 8'hff) dst_ip_bcast <= 1'b0;
                            if (rx_byte != ip_byte(SUBNET_BROADCAST_IP, index[1:0])) dst_ip_subnet_bcast <= 1'b0;
                        end
                    endcase

                    if (index == 6'd19) begin
                        if (!ip_ok ||
                            (ip_total_length < 16'd28) ||
                            !((dst_ip_match && (rx_byte == ip_byte(LOCAL_IP, 2'd3))) ||
                              (dst_ip_bcast && (rx_byte == 8'hff)) ||
                              (dst_ip_subnet_bcast && (rx_byte == ip_byte(SUBNET_BROADCAST_IP, 2'd3))))) begin
                            drop_packet();
                        end else begin
                            debug_dst_mac_accepted <= 1'b1;
                            state <= ST_UDP;
                            index <= 6'd0;
                        end
                    end else begin
                        index <= index + 6'd1;
                    end
                end

                ST_UDP: begin
                    case (index)
                        6'd0: packet_src_port[15:8] <= rx_byte;
                        6'd1: packet_src_port[7:0]  <= rx_byte;
                        6'd2: if (rx_byte != LOCAL_PORT[15:8]) udp_port_match <= 1'b0;
                        6'd3: begin
                            if (rx_byte != LOCAL_PORT[7:0]) begin
                                udp_port_match <= 1'b0;
                            end else if (udp_port_match) begin
                                debug_eth_accepted <= 1'b1;
                            end
                        end
                        6'd4: udp_length[15:8] <= rx_byte;
                        6'd5: udp_length[7:0] <= rx_byte;
                    endcase

                    if (index == 6'd7) begin
                        if (udp_length >= 16'd8) begin
                            debug_ip_accepted <= 1'b1;
                        end
                        if (!udp_port_match || udp_length < 16'd8 ||
                            (udp_length > (ip_total_length - 16'd20))) begin
                            drop_packet();
                        end else begin
                            payload_remaining <= udp_length - 16'd8;
                            payload_len <= 16'd0;
                            if (udp_length == 16'd8) begin
                                remote_mac <= packet_src_mac;
                                remote_ip <= packet_src_ip;
                                remote_port <= packet_src_port;
                                packet_seen <= 1'b1;
                                state <= ST_DROP;
                            end else begin
                                state <= ST_PAYLOAD;
                            end
                            index <= 6'd0;
                        end
                    end else begin
                        index <= index + 6'd1;
                    end
                end

                ST_PAYLOAD: begin
                    if (payload_remaining != 16'd0) begin
                        if (payload_len < MAX_PAYLOAD_BYTES) begin
                            if (write_buf) begin
                                payload_mem1[payload_len] <= rx_byte;
                            end else begin
                                payload_mem0[payload_len] <= rx_byte;
                            end
                            payload_len <= payload_len + 16'd1;
                            payload_remaining <= payload_remaining - 16'd1;
                            if (payload_remaining == 16'd1) begin
                                state <= ST_DROP;
                                remote_mac <= packet_src_mac;
                                remote_ip <= packet_src_ip;
                                remote_port <= packet_src_port;
                                if (write_buf) begin
                                    buf_len1 <= payload_len + 16'd1;
                                    buf_pending1 <= 1'b1;
                                    packet_seen <= 1'b1;
                                    if (!write_buf0_busy) write_buf <= 1'b0;
                                end else begin
                                    buf_len0 <= payload_len + 16'd1;
                                    buf_pending0 <= 1'b1;
                                    packet_seen <= 1'b1;
                                    if (!write_buf1_busy) write_buf <= 1'b1;
                                end
                            end
                        end else begin
                            parser_error <= 1'b1;
                            drop_packet();
                        end
                    end
                end

                ST_DROP: begin
                    // Ignore bytes until rx_frame_end.
                end

                default: state <= ST_IDLE;
            endcase
        end
    end
end

endmodule
