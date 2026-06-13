## Nexys A7-100T SMSC/Microchip LAN8720A Ethernet PHY pins.
## Source cross-check: help/Nexys-A7-100T-Master.xdc, "SMSC Ethernet PHY".
##
## These names match the default external-port style produced when a module
## with eth_* ports is added to the Vivado block design.  Rename the get_ports
## entries if the BD ports are named differently.

set_property -dict {PACKAGE_PIN C9  IOSTANDARD LVCMOS33} [get_ports eth_mdc_0]
set_property -dict {PACKAGE_PIN A9  IOSTANDARD LVCMOS33} [get_ports eth_mdio_0]
set_property -dict {PACKAGE_PIN B3  IOSTANDARD LVCMOS33} [get_ports eth_rstn_0]
set_property -dict {PACKAGE_PIN D9  IOSTANDARD LVCMOS33} [get_ports eth_crsdv_0]
set_property -dict {PACKAGE_PIN C10 IOSTANDARD LVCMOS33} [get_ports eth_rxerr_0]
set_property -dict {PACKAGE_PIN C11 IOSTANDARD LVCMOS33} [get_ports {eth_rxd_0[0]}]
set_property -dict {PACKAGE_PIN D10 IOSTANDARD LVCMOS33} [get_ports {eth_rxd_0[1]}]
set_property -dict {PACKAGE_PIN B9  IOSTANDARD LVCMOS33} [get_ports eth_txen_0]
set_property -dict {PACKAGE_PIN A10 IOSTANDARD LVCMOS33} [get_ports {eth_txd_0[0]}]
set_property -dict {PACKAGE_PIN A8  IOSTANDARD LVCMOS33} [get_ports {eth_txd_0[1]}]
set_property -dict {PACKAGE_PIN D5  IOSTANDARD LVCMOS33} [get_ports eth_refclk_0]
set_property -dict {PACKAGE_PIN B8  IOSTANDARD LVCMOS33} [get_ports eth_intn_0]

## Keep RMII TX transitions tight and predictable at the FPGA pins. The LAN8720A
## samples TXEN/TXD against REF_CLK, so fabric routing skew here can make an
## otherwise correct frame vanish at the host NIC.
set_property IOB TRUE [get_ports {eth_txen_0 eth_txd_0[*]}]
set_property SLEW FAST [get_ports {eth_txen_0 eth_txd_0[*] eth_refclk_0}]

## LAN8720A RMII TX timing, relative to the REF_CLK edge observed by the PHY.
## Datasheet requirement in REF_CLK input mode: TXEN/TXD setup >= 4.0 ns and
## hold >= 1.5 ns. These constraints make Vivado check the external interface
## instead of treating the RMII pins as unconstrained board-level outputs.
create_clock -name eth_refclk_phy -period 20.000 [get_ports eth_refclk_0]
set_output_delay -clock [get_clocks eth_refclk_phy] -max 4.000 [get_ports {eth_txen_0 eth_txd_0[*]}]
set_output_delay -clock [get_clocks eth_refclk_phy] -min -1.500 [get_ports {eth_txen_0 eth_txd_0[*]}]

## Optional debug LEDs for rmii_link_probe.
set_property -dict {PACKAGE_PIN T16 IOSTANDARD LVCMOS33} [get_ports LED11]
set_property -dict {PACKAGE_PIN V15 IOSTANDARD LVCMOS33} [get_ports LED12]
set_property -dict {PACKAGE_PIN V14 IOSTANDARD LVCMOS33} [get_ports LED13]
set_property -dict {PACKAGE_PIN V12 IOSTANDARD LVCMOS33} [get_ports LED14]
set_property -dict {PACKAGE_PIN V11 IOSTANDARD LVCMOS33} [get_ports LED15]

## Ethernet diagnostic LED page select switches.
set_property -dict {PACKAGE_PIN J15 IOSTANDARD LVCMOS33} [get_ports SW0]
set_property -dict {PACKAGE_PIN L16 IOSTANDARD LVCMOS33} [get_ports SW1]
set_property -dict {PACKAGE_PIN M13 IOSTANDARD LVCMOS33} [get_ports SW2]
set_property -dict {PACKAGE_PIN R15 IOSTANDARD LVCMOS33} [get_ports SW3]
