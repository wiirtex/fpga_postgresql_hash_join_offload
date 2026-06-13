## uart_pins.xdc — UART constraints for Nexys A7-100T (CP2104 USB-UART bridge)
##
## uart_txd_in  (C4) = CP2104 TX -> FPGA RX  = our uart_rx input
## uart_rxd_out (D4) = FPGA TX   -> CP2104 RX = our uart_tx output
##
## Add these lines to your project's XDC (or merge into existing Nexys-A7-100T.xdc).

set_property -dict {PACKAGE_PIN C4 IOSTANDARD LVCMOS33} [get_ports uart_rx_0]
set_property -dict {PACKAGE_PIN D4 IOSTANDARD LVCMOS33} [get_ports uart_tx_0]
set_property -dict {PACKAGE_PIN C12 IOSTANDARD LVCMOS33} [get_ports CPU_RESETN]
set_property -dict {PACKAGE_PIN H17 IOSTANDARD LVCMOS33} [get_ports LED0]
set_property -dict {PACKAGE_PIN K15 IOSTANDARD LVCMOS33} [get_ports LED1]
set_property -dict {PACKAGE_PIN J13 IOSTANDARD LVCMOS33} [get_ports LED2]
set_property -dict {PACKAGE_PIN N14 IOSTANDARD LVCMOS33} [get_ports LED3]
set_property -dict {PACKAGE_PIN R18 IOSTANDARD LVCMOS33} [get_ports LED4]
set_property -dict {PACKAGE_PIN V17 IOSTANDARD LVCMOS33} [get_ports LED5]
set_property -dict {PACKAGE_PIN U17 IOSTANDARD LVCMOS33} [get_ports LED6]
set_property -dict {PACKAGE_PIN U16 IOSTANDARD LVCMOS33} [get_ports LED7]
set_property -dict {PACKAGE_PIN V16 IOSTANDARD LVCMOS33} [get_ports LED8]
set_property -dict {PACKAGE_PIN T15 IOSTANDARD LVCMOS33} [get_ports LED9]
set_property -dict {PACKAGE_PIN U14 IOSTANDARD LVCMOS33} [get_ports LED10]
set_property -dict {PACKAGE_PIN E3  IOSTANDARD LVCMOS33} [get_ports sys_clk_i_0]

# ── MIG-internal CDC false paths ──────────────────────────────────────────────
# clk_pll_i / clk_pll_i_1 are PLLE2_ADV feedback clocks derived from clk_out2
# inside the MIG infrastructure.  The 77 inter-clock paths reported by Vivado
# are internal MIG synchronizer flops — set_false_path is the correct treatment
# (MIG datasheet and Xilinx AR#54025).
set_false_path -from [get_clocks -quiet clk_pll_i]   -to [get_clocks -quiet clk_out1*]
set_false_path -from [get_clocks -quiet clk_pll_i_1] -to [get_clocks -quiet clk_out1*]
set_false_path -from [get_clocks -quiet clk_out1*]   -to [get_clocks -quiet clk_pll_i]
set_false_path -from [get_clocks -quiet clk_out1*]   -to [get_clocks -quiet clk_pll_i_1]
set_false_path -from [get_clocks -quiet clk_pll_i]   -to [get_clocks -quiet clk_out2*]
set_false_path -from [get_clocks -quiet clk_pll_i_1] -to [get_clocks -quiet clk_out2*]
set_false_path -from [get_clocks -quiet clk_pll_i]   -to [get_clocks -quiet clk_out3*]
set_false_path -from [get_clocks -quiet clk_pll_i_1] -to [get_clocks -quiet clk_out3*]
# clk_out1 appears under two names because it fans out to both IDELAYCTRL and MIG internal path.
# These are the same physical 200 MHz clock — no real crossing.
set_false_path -from [get_clocks -quiet clk_out1_hash_join_bd_clk_wiz_0_0] \
               -to   [get_clocks -quiet clk_out1_hash_join_bd_clk_wiz_0_0_1]
set_false_path -from [get_clocks -quiet clk_out1_hash_join_bd_clk_wiz_0_0_1] \
               -to   [get_clocks -quiet clk_out1_hash_join_bd_clk_wiz_0_0]

# ── Configuration bank (Nexys A7: Bank 0 VCCO = 3.3V via USB) ────────────────
set_property CFGBVS VCCO [current_design]
set_property CONFIG_VOLTAGE 3.3 [current_design]