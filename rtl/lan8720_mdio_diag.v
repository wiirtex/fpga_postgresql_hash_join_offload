`timescale 1ns / 1ps
//
// lan8720_mdio_diag.v
// Minimal LAN8720A PHY bring-up and MDIO status reader for Nexys A7.
//
// This is a board-diagnostic block, not the final UDP bridge. It keeps the PHY
// reset/refclk/TX pins in a known state, periodically reads a small set of
// clause-22 MDIO registers, and exposes the status bits that matter for link
// bring-up.
//
module lan8720_mdio_diag_cfg #(
    parameter [4:0] PHY_ADDR = 5'd1,
    parameter integer RESET_HOLD_CYCLES = 5_000_000,
    parameter integer MDC_HALF_PERIOD_CYCLES = 10
) (
    input  wire       clk_50,
    input  wire       rst_n,
    // 0: read-only, 1: restart autoneg, 2: force 100/full,
    // 3: force 100/half, 4: force 10/full, 5: force 10/half.
    input  wire [2:0] mdio_mode,

    output wire       eth_refclk,
    output reg        eth_rstn,
    input  wire       eth_crsdv,
    input  wire       eth_rxerr,
    input  wire [1:0] eth_rxd,
    output wire       eth_txen,
    output wire [1:0] eth_txd,
    output reg        eth_mdc,
    inout  wire       eth_mdio,
    input  wire       eth_intn,

    output reg [15:0] bmcr,
    output reg [15:0] bmsr,
    output reg [15:0] phy_id1,
    output reg [15:0] phy_id2,
    output reg [15:0] special_status,
    output reg        regs_valid,
    output reg        mdio_active,
    output reg        mdio_done_pulse,
    output reg        crsdv_seen,
    output reg        rxerr_seen,
    output reg        intn_seen_low,

    output wire       link_up,
    output wire       auto_neg_complete,
    output wire       phy_id_ok,
    output wire       autodone,
    output wire       speed_100,
    output wire       full_duplex
);

localparam [1:0] ST_RESET = 2'd0;
localparam [1:0] ST_GAP   = 2'd1;
localparam [1:0] ST_READ  = 2'd2;
localparam [1:0] ST_WRITE = 2'd3;

localparam [4:0] REG_BMCR    = 5'd0;
localparam [4:0] REG_BMSR    = 5'd1;
localparam [4:0] REG_PHYID1  = 5'd2;
localparam [4:0] REG_PHYID2  = 5'd3;
localparam [4:0] REG_SPECIAL = 5'd31;

assign eth_refclk = clk_50;
assign eth_txen   = 1'b0;
assign eth_txd    = 2'b00;

reg mdio_oe;
reg mdio_out;
assign eth_mdio = mdio_oe ? mdio_out : 1'bz;
wire mdio_in = eth_mdio;

reg [1:0] state;
reg [31:0] reset_counter;
reg [19:0] gap_counter;
reg [4:0] current_reg;
reg [15:0] current_write_value;
reg [2:0] read_slot;
reg [6:0] bit_index;
reg [15:0] read_shift;
reg write_done;
reg [$clog2(MDC_HALF_PERIOD_CYCLES)-1:0] mdc_div;

wire mdc_tick = (mdc_div == MDC_HALF_PERIOD_CYCLES - 1);
wire mdc_rising_next = mdc_tick && !eth_mdc;
wire mdc_falling_next = mdc_tick && eth_mdc;

wire [2:0] speed_code = special_status[4:2];
assign link_up           = bmsr[2];
assign auto_neg_complete = bmsr[5];
assign phy_id_ok         = (phy_id1 == 16'h0007) && (phy_id2[15:10] == 6'b110000);
assign autodone          = special_status[12];
assign speed_100         = (speed_code == 3'b010) || (speed_code == 3'b110);
assign full_duplex       = (speed_code == 3'b101) || (speed_code == 3'b110);

function [4:0] reg_for_slot;
    input [2:0] slot;
    begin
        case (slot)
            3'd0: reg_for_slot = REG_BMCR;
            3'd1: reg_for_slot = REG_BMSR;
            3'd2: reg_for_slot = REG_BMSR; // BMSR link-status is latch-low; read twice.
            3'd3: reg_for_slot = REG_PHYID1;
            3'd4: reg_for_slot = REG_PHYID2;
            default: reg_for_slot = REG_SPECIAL;
        endcase
    end
endfunction

function mdio_drive_value;
    input [6:0] idx;
    input [4:0] reg_addr;
    begin
        if (idx < 7'd32) begin
            mdio_drive_value = 1'b1;               // Preamble.
        end else if (idx == 7'd32) begin
            mdio_drive_value = 1'b0;               // ST[1:0] = 01
        end else if (idx == 7'd33) begin
            mdio_drive_value = 1'b1;
        end else if (idx == 7'd34) begin
            mdio_drive_value = 1'b1;               // OP[1:0] = 10 (read)
        end else if (idx == 7'd35) begin
            mdio_drive_value = 1'b0;
        end else if (idx >= 7'd36 && idx <= 7'd40) begin
            mdio_drive_value = PHY_ADDR[7'd40 - idx];
        end else if (idx >= 7'd41 && idx <= 7'd45) begin
            mdio_drive_value = reg_addr[7'd45 - idx];
        end else begin
            mdio_drive_value = 1'b0;
        end
    end
endfunction

function mdio_drive_enable;
    input [6:0] idx;
    begin
        mdio_drive_enable = (idx <= 7'd45);
    end
endfunction

function [15:0] bmcr_for_mode;
    input [2:0] mode;
    begin
        case (mode)
            3'd1: bmcr_for_mode = 16'h1200; // Auto-negotiation enable + restart.
            3'd2: bmcr_for_mode = 16'h2100; // 100 Mb/s, full duplex, autoneg off.
            3'd3: bmcr_for_mode = 16'h2000; // 100 Mb/s, half duplex, autoneg off.
            3'd4: bmcr_for_mode = 16'h0100; // 10 Mb/s, full duplex, autoneg off.
            3'd5: bmcr_for_mode = 16'h0000; // 10 Mb/s, half duplex, autoneg off.
            default: bmcr_for_mode = 16'h0000;
        endcase
    end
endfunction

function mdio_write_drive_value;
    input [6:0] idx;
    input [4:0] reg_addr;
    input [15:0] write_value;
    begin
        if (idx < 7'd32) begin
            mdio_write_drive_value = 1'b1;          // Preamble.
        end else if (idx == 7'd32) begin
            mdio_write_drive_value = 1'b0;          // ST[1:0] = 01
        end else if (idx == 7'd33) begin
            mdio_write_drive_value = 1'b1;
        end else if (idx == 7'd34) begin
            mdio_write_drive_value = 1'b0;          // OP[1:0] = 01 (write)
        end else if (idx == 7'd35) begin
            mdio_write_drive_value = 1'b1;
        end else if (idx >= 7'd36 && idx <= 7'd40) begin
            mdio_write_drive_value = PHY_ADDR[7'd40 - idx];
        end else if (idx >= 7'd41 && idx <= 7'd45) begin
            mdio_write_drive_value = reg_addr[7'd45 - idx];
        end else if (idx == 7'd46) begin
            mdio_write_drive_value = 1'b1;          // TA[1:0] = 10
        end else if (idx == 7'd47) begin
            mdio_write_drive_value = 1'b0;
        end else begin
            mdio_write_drive_value = write_value[7'd63 - idx];
        end
    end
endfunction

task store_read_value;
    input [2:0] slot;
    input [15:0] value;
    begin
        case (slot)
            3'd0: bmcr <= value;
            3'd2: bmsr <= value;
            3'd3: phy_id1 <= value;
            3'd4: phy_id2 <= value;
            3'd5: special_status <= value;
            default: begin end
        endcase
    end
endtask

always @(posedge clk_50) begin
    if (!rst_n) begin
        state          <= ST_RESET;
        reset_counter  <= 32'd0;
        gap_counter    <= 20'd0;
        current_reg    <= REG_BMCR;
        current_write_value <= 16'd0;
        read_slot      <= 3'd0;
        bit_index      <= 7'd0;
        read_shift     <= 16'd0;
        write_done     <= 1'b0;
        mdc_div        <= {($clog2(MDC_HALF_PERIOD_CYCLES)){1'b0}};
        eth_rstn       <= 1'b0;
        eth_mdc        <= 1'b0;
        mdio_oe        <= 1'b0;
        mdio_out       <= 1'b1;
        bmcr           <= 16'd0;
        bmsr           <= 16'd0;
        phy_id1        <= 16'd0;
        phy_id2        <= 16'd0;
        special_status <= 16'd0;
        regs_valid     <= 1'b0;
        mdio_active    <= 1'b0;
        mdio_done_pulse <= 1'b0;
        crsdv_seen     <= 1'b0;
        rxerr_seen     <= 1'b0;
        intn_seen_low  <= 1'b0;
    end else begin
        mdio_done_pulse <= 1'b0;
        if (eth_crsdv) crsdv_seen <= 1'b1;
        if (eth_rxerr) rxerr_seen <= 1'b1;
        if (!eth_intn) intn_seen_low <= 1'b1;

        if (state == ST_READ || state == ST_WRITE) begin
            if (mdc_tick) begin
                mdc_div <= {($clog2(MDC_HALF_PERIOD_CYCLES)){1'b0}};
                eth_mdc <= ~eth_mdc;
            end else begin
                mdc_div <= mdc_div + 1'b1;
            end
        end else begin
            mdc_div <= {($clog2(MDC_HALF_PERIOD_CYCLES)){1'b0}};
            eth_mdc <= 1'b0;
        end

        case (state)
            ST_RESET: begin
                mdio_active <= 1'b0;
                mdio_oe <= 1'b0;
                if (reset_counter < RESET_HOLD_CYCLES) begin
                    reset_counter <= reset_counter + 1'b1;
                    eth_rstn <= 1'b0;
                end else begin
                    eth_rstn <= 1'b1;
                    gap_counter <= 20'd0;
                    state <= ST_GAP;
                end
            end

            ST_GAP: begin
                mdio_active <= 1'b0;
                mdio_oe <= 1'b0;
                if (gap_counter == 20'd500000) begin
                    bit_index <= 7'd0;
                    read_shift <= 16'd0;
                    mdio_oe <= 1'b1;
                    mdio_out <= 1'b1;
                    mdio_active <= 1'b1;
                    if (!write_done && mdio_mode != 3'd0) begin
                        current_reg <= REG_BMCR;
                        current_write_value <= bmcr_for_mode(mdio_mode);
                        state <= ST_WRITE;
                    end else begin
                        read_slot <= 3'd0;
                        current_reg <= reg_for_slot(3'd0);
                        state <= ST_READ;
                    end
                end else begin
                    gap_counter <= gap_counter + 1'b1;
                end
            end

            ST_WRITE: begin
                mdio_active <= 1'b1;
                mdio_oe <= 1'b1;

                if (mdc_falling_next) begin
                    mdio_out <= mdio_write_drive_value(
                        bit_index,
                        current_reg,
                        current_write_value
                    );
                end

                if (mdc_rising_next) begin
                    if (bit_index == 7'd63) begin
                        write_done <= 1'b1;
                        mdio_done_pulse <= 1'b1;
                        gap_counter <= 20'd0;
                        state <= ST_GAP;
                    end else begin
                        bit_index <= bit_index + 1'b1;
                    end
                end
            end

            ST_READ: begin
                mdio_active <= 1'b1;

                if (mdc_falling_next) begin
                    mdio_oe <= mdio_drive_enable(bit_index);
                    mdio_out <= mdio_drive_value(bit_index, current_reg);
                end

                if (mdc_rising_next) begin
                    if (bit_index >= 7'd48 && bit_index <= 7'd63) begin
                        read_shift <= {read_shift[14:0], mdio_in};
                    end

                    if (bit_index == 7'd63) begin
                        store_read_value(read_slot, {read_shift[14:0], mdio_in});
                        mdio_done_pulse <= 1'b1;
                        if (read_slot == 3'd5) begin
                            regs_valid <= 1'b1;
                            gap_counter <= 20'd0;
                            state <= ST_GAP;
                        end else begin
                            read_slot <= read_slot + 1'b1;
                            current_reg <= reg_for_slot(read_slot + 1'b1);
                            bit_index <= 7'd0;
                            read_shift <= 16'd0;
                            mdio_oe <= 1'b1;
                            mdio_out <= 1'b1;
                        end
                    end else begin
                        bit_index <= bit_index + 1'b1;
                    end
                end
            end

            default: state <= ST_RESET;
        endcase
    end
end

endmodule
