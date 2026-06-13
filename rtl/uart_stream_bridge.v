`timescale 1ns / 1ps
//
// uart_stream_bridge.v
// 8N1 UART <-> 8-bit AXI4-Stream bridge for Nexys A7-100T (CP2104 USB-UART)
//
// Data flow:
//   Host  ->  uart_rx (pin C4)  ->  m_axis_*  ->  hash_join_kernel slave (rx)
//   hash_join_kernel master (tx)  ->  s_axis_*  ->  uart_tx (pin D4)  ->  Host
//
// Parameters:
//   CLK_FREQ   system clock in Hz      (default: 100_000_000 for Nexys A7-100T)
//   BAUD_RATE  serial baud rate        (default: 115_200)
//
// XDC constraints to add to your Vivado project:
//   set_property -dict {PACKAGE_PIN C4 IOSTANDARD LVCMOS33} [get_ports uart_rx]
//   set_property -dict {PACKAGE_PIN D4 IOSTANDARD LVCMOS33} [get_ports uart_tx]
//
// Integration in Block Design:
//   1. Add this file as an RTL source (Design Sources -> Add Sources).
//   2. Add Module to block design.
//   3. Make uart_rx / uart_tx external (right-click -> Make External).
//   4. Connect:
//        m_axis_{tdata,tvalid,tready} -> hash_join_kernel AXI-S slave  (rx port)
//        s_axis_{tdata,tvalid,tready} -> hash_join_kernel AXI-S master (tx port)
//        clk  -> system clock (100 MHz)
//        rst_n -> active-low reset (e.g., from Processor System Reset, active_low_peripheral_aresetn)
//
module uart_stream_bridge #(
    parameter integer CLK_FREQ  = 100_000_000,
    parameter integer BAUD_RATE =     115_200
) (
    input  wire       clk,
    input  wire       rst_n,         // active-low synchronous reset

    // Physical UART pins
    input  wire       uart_rx,       // from CP2104 TX  (Nexys A7 pin C4, uart_txd_in)
    output wire       uart_tx,       // to   CP2104 RX  (Nexys A7 pin D4, uart_rxd_out)

    // AXI4-Stream Master -> kernel slave  (bytes received from host)
    output reg  [7:0] m_axis_tdata,
    output reg        m_axis_tvalid,
    input  wire       m_axis_tready,

    // AXI4-Stream Slave <- kernel master  (bytes to send to host)
    input  wire [7:0] s_axis_tdata,
    input  wire       s_axis_tvalid,
    output wire       s_axis_tready   // asserted whenever TX is idle
);

// ---------------------------------------------------------------------------
// Derived constant: clock cycles per UART bit period
// ---------------------------------------------------------------------------
localparam integer CPB = CLK_FREQ / BAUD_RATE;

// ---------------------------------------------------------------------------
// UART Receiver
// ---------------------------------------------------------------------------

// Two-stage synchroniser to bring uart_rx into clk domain.
reg rx_s1, rx_s2;
always @(posedge clk) begin
    rx_s1 <= uart_rx;
    rx_s2 <= rx_s1;
end

localparam [1:0] R_IDLE  = 2'd0,
                 R_START = 2'd1,
                 R_DATA  = 2'd2,
                 R_STOP  = 2'd3;

reg [1:0]  rs;         // RX state
reg [15:0] rc;         // clock counter  (16 bits: enough for CPB up to 65535)
reg [2:0]  rb;         // bit index (0..7)
reg [7:0]  rsh;        // shift register (LSB first)
reg        rdone;      // one-cycle pulse: valid byte in rbyte
reg [7:0]  rbyte;

always @(posedge clk) begin
    if (!rst_n) begin
        rs    <= R_IDLE;
        rc    <= 16'd0;
        rb    <= 3'd0;
        rsh   <= 8'd0;
        rdone <= 1'b0;
        rbyte <= 8'd0;
    end else begin
        rdone <= 1'b0;   // default: no new byte

        case (rs)

            R_IDLE:
                // Falling edge on rx_s2 = start bit.
                if (!rx_s2) begin
                    rs <= R_START;
                    rc <= CPB / 2 - 1;   // sample at mid-point of start bit
                end

            R_START:
                if (rc == 0) begin
                    if (!rx_s2) begin    // still low at mid-start -> valid
                        rs <= R_DATA;
                        rc <= CPB - 1;
                        rb <= 3'd0;
                    end else             // glitch -> back to idle
                        rs <= R_IDLE;
                end else
                    rc <= rc - 1;

            R_DATA:
                if (rc == 0) begin
                    // Sample bit: shift in MSB side, oldest bit falls to LSB.
                    rsh <= {rx_s2, rsh[7:1]};
                    rc  <= CPB - 1;
                    if (rb == 7)
                        rs <= R_STOP;
                    else
                        rb <= rb + 1;
                end else
                    rc <= rc - 1;

            R_STOP:
                if (rc == 0) begin
                    rs <= R_IDLE;
                    if (rx_s2) begin     // valid stop bit (line high)
                        rdone <= 1'b1;
                        rbyte <= rsh;
                    end
                    // If stop bit is low (framing error) we silently discard.
                end else
                    rc <= rc - 1;

            default: rs <= R_IDLE;
        endcase
    end
end

// ---------------------------------------------------------------------------
// RX -> AXI4-Stream handshake
//
// tvalid stays asserted until the kernel accepts (tready).
// If a new byte arrives while we are still holding the previous one,
// it is dropped.  At 115200 baud one byte takes ~87 us; the kernel
// (running at 100 MHz) will always accept within a few nanoseconds.
// ---------------------------------------------------------------------------
always @(posedge clk) begin
    if (!rst_n) begin
        m_axis_tvalid <= 1'b0;
        m_axis_tdata  <= 8'h00;
    end else if (m_axis_tvalid && m_axis_tready) begin
        // Kernel accepted current byte.
        // If a new byte happened to arrive in the same cycle, forward it immediately.
        m_axis_tvalid <= rdone;
        m_axis_tdata  <= rbyte;
    end else if (rdone) begin
        m_axis_tvalid <= 1'b1;
        m_axis_tdata  <= rbyte;
    end
end

// ---------------------------------------------------------------------------
// UART Transmitter
// ---------------------------------------------------------------------------

localparam [1:0] T_IDLE  = 2'd0,
                 T_START = 2'd1,
                 T_DATA  = 2'd2,
                 T_STOP  = 2'd3;

reg [1:0]  ts;         // TX state
reg [15:0] tc;         // clock counter
reg [2:0]  tb;         // bit index (0..7)
reg [7:0]  tsh;        // shift register
reg        tpin;       // serial output line

assign uart_tx       = tpin;
// Ready whenever idle: the kernel may present data any time TX is not busy.
assign s_axis_tready = (ts == T_IDLE);

always @(posedge clk) begin
    if (!rst_n) begin
        ts   <= T_IDLE;
        tc   <= 16'd0;
        tb   <= 3'd0;
        tsh  <= 8'd0;
        tpin <= 1'b1;    // UART idle = high
    end else begin
        case (ts)

            T_IDLE:
                // Accept a byte from the kernel whenever it presents one.
                if (s_axis_tvalid) begin
                    tsh  <= s_axis_tdata;
                    tc   <= CPB - 1;
                    tpin <= 1'b0;        // drive start bit
                    ts   <= T_START;
                end

            T_START:
                // Hold start bit for CPB cycles.
                if (tc == 0) begin
                    tpin <= tsh[0];                  // bit 0 (LSB first)
                    tsh  <= {1'b0, tsh[7:1]};
                    tb   <= 3'd0;
                    tc   <= CPB - 1;
                    ts   <= T_DATA;
                end else
                    tc <= tc - 1;

            T_DATA:
                // Send bits 0..7, one per CPB cycles.
                if (tc == 0) begin
                    tc <= CPB - 1;
                    if (tb == 7) begin
                        // All 8 data bits sent; start stop bit.
                        tpin <= 1'b1;
                        ts   <= T_STOP;
                    end else begin
                        tb   <= tb + 1;
                        tpin <= tsh[0];
                        tsh  <= {1'b0, tsh[7:1]};
                    end
                end else
                    tc <= tc - 1;

            T_STOP:
                // Hold stop bit (line high) for CPB cycles, then return to idle.
                if (tc == 0)
                    ts <= T_IDLE;
                else
                    tc <= tc - 1;

            default: ts <= T_IDLE;
        endcase
    end
end

endmodule
