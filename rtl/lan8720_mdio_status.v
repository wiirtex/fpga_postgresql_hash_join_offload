`timescale 1ns / 1ps
//
// lan8720_mdio_status.v
// Clause-22 MDIO sidecar for the LAN8720A in the UDP/RMII bring-up design.
//
// Config modes:
//   0: read-only
//   1: advertise 100BASE-TX full-duplex only, restart autonegotiation
//   2: advertise 10BASE-T full-duplex only, restart autonegotiation
//   3: force 10BASE-T full-duplex with autonegotiation disabled
//   4: force 100BASE-TX full-duplex with autonegotiation disabled
//   5: force 100BASE-TX half-duplex with autonegotiation disabled
//   6: force 10BASE-T half-duplex with autonegotiation disabled
// Then continuously read the key status registers.
//
module lan8720_mdio_status #(
    parameter [4:0] PHY_ADDR = 5'd1,
    parameter integer STARTUP_DELAY_CYCLES = 5_000_000,
    parameter integer MDC_HALF_PERIOD_CYCLES = 10,
    parameter integer CONFIG_MODE_VALUE = 2,
    parameter integer CONFIG_MODE = CONFIG_MODE_VALUE
) (
    input  wire       clk_50,
    input  wire       rst_n,

    output reg        eth_mdc,
    inout  wire       eth_mdio,

    output reg [15:0] bmcr,
    output reg [15:0] bmsr,
    output reg [15:0] anar,
    output reg [15:0] anlpar,
    output reg [15:0] special_status,
    output reg        regs_valid,
    output reg        mdio_active,
    output wire       link_up,
    output wire       auto_neg_complete,
    output wire       speed_100,
    output wire       full_duplex
);

localparam [1:0] ST_STARTUP = 2'd0;
localparam [1:0] ST_GAP     = 2'd1;
localparam [1:0] ST_READ    = 2'd2;
localparam [1:0] ST_WRITE   = 2'd3;

localparam [4:0] REG_BMCR    = 5'd0;
localparam [4:0] REG_BMSR    = 5'd1;
localparam [4:0] REG_ANAR    = 5'd4;
localparam [4:0] REG_ANLPAR  = 5'd5;
localparam [4:0] REG_SPECIAL = 5'd31;

localparam [15:0] ANAR_100_FULL_ONLY = 16'h0181;
localparam [15:0] ANAR_10_FULL_ONLY  = 16'h0041;
localparam [15:0] BMCR_RESTART_ANEG  = 16'h1200;
localparam [15:0] BMCR_FORCE_10_FULL = 16'h0100;
localparam [15:0] BMCR_FORCE_100_FULL = 16'h2100;
localparam [15:0] BMCR_FORCE_100_HALF = 16'h2000;
localparam [15:0] BMCR_FORCE_10_HALF  = 16'h0000;
localparam integer CONFIG_MODE_EFFECTIVE = CONFIG_MODE;

reg mdio_oe;
reg mdio_out;
assign eth_mdio = mdio_oe ? mdio_out : 1'bz;
wire mdio_in = eth_mdio;

reg [1:0] state;
reg [31:0] startup_counter;
reg [19:0] gap_counter;
reg [2:0] op_slot;
reg [4:0] current_reg;
reg [15:0] current_write_value;
reg [6:0] bit_index;
reg [15:0] read_shift;
reg [$clog2(MDC_HALF_PERIOD_CYCLES)-1:0] mdc_div;

wire mdc_tick = (mdc_div == MDC_HALF_PERIOD_CYCLES - 1);
wire mdc_rising_next = mdc_tick && !eth_mdc;
wire mdc_falling_next = mdc_tick && eth_mdc;
wire [2:0] speed_code = special_status[4:2];
wire force_mode = (CONFIG_MODE_EFFECTIVE >= 3'd3);
wire [2:0] write_slot_limit_w = force_mode ? 3'd1 : 3'd2;
wire [2:0] read_slot_base_w = (CONFIG_MODE_EFFECTIVE == 3'd0) ? 3'd0 : write_slot_limit_w;

assign link_up           = bmsr[2];
assign auto_neg_complete = bmsr[5];
assign speed_100         = (speed_code == 3'b010) || (speed_code == 3'b110);
assign full_duplex       = (speed_code == 3'b101) || (speed_code == 3'b110);

function [4:0] read_reg_for_slot;
    input [2:0] slot;
    begin
        case (slot)
            3'd0: read_reg_for_slot = REG_BMCR;
            3'd1: read_reg_for_slot = REG_BMSR;
            3'd2: read_reg_for_slot = REG_BMSR;
            3'd3: read_reg_for_slot = REG_ANAR;
            3'd4: read_reg_for_slot = REG_ANLPAR;
            default: read_reg_for_slot = REG_SPECIAL;
        endcase
    end
endfunction

function [4:0] write_reg_for_slot;
    input [2:0] slot;
    begin
        if (force_mode) begin
            write_reg_for_slot = REG_BMCR;
        end else begin
            write_reg_for_slot = (slot == 3'd0) ? REG_ANAR : REG_BMCR;
        end
    end
endfunction

function [15:0] write_value_for_slot;
    input [2:0] slot;
    begin
        case (CONFIG_MODE_EFFECTIVE)
            3'd1: write_value_for_slot = (slot == 3'd0) ? ANAR_100_FULL_ONLY : BMCR_RESTART_ANEG;
            3'd2: write_value_for_slot = (slot == 3'd0) ? ANAR_10_FULL_ONLY : BMCR_RESTART_ANEG;
            3'd3: write_value_for_slot = BMCR_FORCE_10_FULL;
            3'd4: write_value_for_slot = BMCR_FORCE_100_FULL;
            3'd5: write_value_for_slot = BMCR_FORCE_100_HALF;
            3'd6: write_value_for_slot = BMCR_FORCE_10_HALF;
            default: write_value_for_slot = 16'h0000;
        endcase
    end
endfunction

function mdio_read_drive_value;
    input [6:0] idx;
    input [4:0] reg_addr;
    begin
        if (idx < 7'd32) mdio_read_drive_value = 1'b1;
        else if (idx == 7'd32) mdio_read_drive_value = 1'b0;
        else if (idx == 7'd33) mdio_read_drive_value = 1'b1;
        else if (idx == 7'd34) mdio_read_drive_value = 1'b1;
        else if (idx == 7'd35) mdio_read_drive_value = 1'b0;
        else if (idx >= 7'd36 && idx <= 7'd40) mdio_read_drive_value = PHY_ADDR[7'd40 - idx];
        else if (idx >= 7'd41 && idx <= 7'd45) mdio_read_drive_value = reg_addr[7'd45 - idx];
        else mdio_read_drive_value = 1'b0;
    end
endfunction

function mdio_write_drive_value;
    input [6:0] idx;
    input [4:0] reg_addr;
    input [15:0] write_value;
    begin
        if (idx < 7'd32) mdio_write_drive_value = 1'b1;
        else if (idx == 7'd32) mdio_write_drive_value = 1'b0;
        else if (idx == 7'd33) mdio_write_drive_value = 1'b1;
        else if (idx == 7'd34) mdio_write_drive_value = 1'b0;
        else if (idx == 7'd35) mdio_write_drive_value = 1'b1;
        else if (idx >= 7'd36 && idx <= 7'd40) mdio_write_drive_value = PHY_ADDR[7'd40 - idx];
        else if (idx >= 7'd41 && idx <= 7'd45) mdio_write_drive_value = reg_addr[7'd45 - idx];
        else if (idx == 7'd46) mdio_write_drive_value = 1'b1;
        else if (idx == 7'd47) mdio_write_drive_value = 1'b0;
        else mdio_write_drive_value = write_value[7'd63 - idx];
    end
endfunction

task store_read_value;
    input [2:0] slot;
    input [15:0] value;
    begin
        case (slot)
            3'd0: bmcr <= value;
            3'd2: bmsr <= value;
            3'd3: anar <= value;
            3'd4: anlpar <= value;
            3'd5: special_status <= value;
            default: begin end
        endcase
    end
endtask

always @(posedge clk_50) begin
    if (!rst_n) begin
        state <= ST_STARTUP;
        startup_counter <= 32'd0;
        gap_counter <= 20'd0;
        op_slot <= 3'd0;
        current_reg <= REG_BMCR;
        current_write_value <= 16'd0;
        bit_index <= 7'd0;
        read_shift <= 16'd0;
        mdc_div <= {($clog2(MDC_HALF_PERIOD_CYCLES)){1'b0}};
        eth_mdc <= 1'b0;
        mdio_oe <= 1'b0;
        mdio_out <= 1'b1;
        bmcr <= 16'd0;
        bmsr <= 16'd0;
        anar <= 16'd0;
        anlpar <= 16'd0;
        special_status <= 16'd0;
        regs_valid <= 1'b0;
        mdio_active <= 1'b0;
    end else begin
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
            ST_STARTUP: begin
                mdio_active <= 1'b0;
                mdio_oe <= 1'b0;
                if (startup_counter < STARTUP_DELAY_CYCLES) begin
                    startup_counter <= startup_counter + 1'b1;
                end else begin
                    op_slot <= 3'd0;
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
                    if (CONFIG_MODE_EFFECTIVE != 2'd0 && op_slot < write_slot_limit_w && !regs_valid) begin
                        current_reg <= write_reg_for_slot(op_slot);
                        current_write_value <= write_value_for_slot(op_slot);
                        state <= ST_WRITE;
                    end else begin
                        if (op_slot < read_slot_base_w) op_slot <= read_slot_base_w;
                        current_reg <= read_reg_for_slot((op_slot < read_slot_base_w) ? 3'd0 : op_slot - read_slot_base_w);
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
                    mdio_out <= mdio_write_drive_value(bit_index, current_reg, current_write_value);
                end
                if (mdc_rising_next) begin
                    if (bit_index == 7'd63) begin
                        op_slot <= op_slot + 3'd1;
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
                    mdio_oe <= (bit_index <= 7'd45);
                    mdio_out <= mdio_read_drive_value(bit_index, current_reg);
                end
                if (mdc_rising_next) begin
                    if (bit_index >= 7'd48 && bit_index <= 7'd63) begin
                        read_shift <= {read_shift[14:0], mdio_in};
                    end
                    if (bit_index == 7'd63) begin
                        store_read_value(op_slot - read_slot_base_w, {read_shift[14:0], mdio_in});
                        if (op_slot == read_slot_base_w + 3'd5) begin
                            regs_valid <= 1'b1;
                            op_slot <= read_slot_base_w;
                        end else begin
                            op_slot <= op_slot + 3'd1;
                        end
                        gap_counter <= 20'd0;
                        state <= ST_GAP;
                    end else begin
                        bit_index <= bit_index + 1'b1;
                    end
                end
            end

            default: state <= ST_STARTUP;
        endcase
    end
end

endmodule
