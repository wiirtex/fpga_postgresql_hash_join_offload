
################################################################
# This is a generated script based on design: hash_join_bd
#
# Though there are limitations about the generated script,
# the main purpose of this utility is to make learning
# IP Integrator Tcl commands easier.
################################################################

namespace eval _tcl {
proc get_script_folder {} {
   set script_path [file normalize [info script]]
   set script_folder [file dirname $script_path]
   return $script_folder
}
}
variable script_folder
set script_folder [_tcl::get_script_folder]

################################################################
# Check if script is running in correct Vivado version.
################################################################
set scripts_vivado_version 2025.2
set current_vivado_version [version -short]

if { [string first $scripts_vivado_version $current_vivado_version] == -1 } {
   puts ""
   if { [string compare $scripts_vivado_version $current_vivado_version] > 0 } {
      catch {common::send_gid_msg -ssname BD::TCL -id 2042 -severity "ERROR" " This script was generated using Vivado <$scripts_vivado_version> and is being run in <$current_vivado_version> of Vivado. Sourcing the script failed since it was created with a future version of Vivado."}

   } else {
     catch {common::send_gid_msg -ssname BD::TCL -id 2041 -severity "ERROR" "This script was generated using Vivado <$scripts_vivado_version> and is being run in <$current_vivado_version> of Vivado. Please run the script in Vivado <$scripts_vivado_version> then open the design in Vivado <$current_vivado_version>. Upgrade the design by running \"Tools => Report => Report IP Status...\", then run write_bd_tcl to create an updated script."}

   }

   return 1
}

################################################################
# START
################################################################

# To test this script, run the following commands from Vivado Tcl console:
# source hash_join_bd_script.tcl


# The design that will be created by this Tcl script contains the following 
# module references:
# clk_blinker, clk_blinker, clk_blinker, clk_blinker, rmii_udp_stream_bridge, axis_protocol_timing_monitor, eth_debug_sticky, eth_tx_debug_sticky, eth_tx_pipeline_sticky, eth_tx_pin_capture, eth_led_pages_v3, rmii_refclk_forward, lan8720_mdio_status, clk_blinker

# Please add the sources of those modules before sourcing this Tcl script.

# If there is no project opened, this script will create a
# project, but make sure you do not have an existing project
# <./myproj/project_1.xpr> in the current working folder.

set list_projs [get_projects -quiet]
if { $list_projs eq "" } {
   create_project project_1 myproj -part xc7a100tcsg324-1
   set_property BOARD_PART digilentinc.com:nexys-a7-100t:part0:1.3 [current_project]
}


# CHANGE DESIGN NAME HERE
variable design_name
set design_name hash_join_bd

# If you do not already have an existing IP Integrator design open,
# you can create a design using the following command:
#    create_bd_design $design_name

# Creating design if needed
set errMsg ""
set nRet 0

set cur_design [current_bd_design -quiet]
set list_cells [get_bd_cells -quiet]

if { ${design_name} eq "" } {
   # USE CASES:
   #    1) Design_name not set

   set errMsg "Please set the variable <design_name> to a non-empty value."
   set nRet 1

} elseif { ${cur_design} ne "" && ${list_cells} eq "" } {
   # USE CASES:
   #    2): Current design opened AND is empty AND names same.
   #    3): Current design opened AND is empty AND names diff; design_name NOT in project.
   #    4): Current design opened AND is empty AND names diff; design_name exists in project.

   if { $cur_design ne $design_name } {
      common::send_gid_msg -ssname BD::TCL -id 2001 -severity "INFO" "Changing value of <design_name> from <$design_name> to <$cur_design> since current design is empty."
      set design_name [get_property NAME $cur_design]
   }
   common::send_gid_msg -ssname BD::TCL -id 2002 -severity "INFO" "Constructing design in IPI design <$cur_design>..."

} elseif { ${cur_design} ne "" && $list_cells ne "" && $cur_design eq $design_name } {
   # USE CASES:
   #    5) Current design opened AND has components AND same names.

   set errMsg "Design <$design_name> already exists in your project, please set the variable <design_name> to another value."
   set nRet 1
} elseif { [get_files -quiet ${design_name}.bd] ne "" } {
   # USE CASES: 
   #    6) Current opened design, has components, but diff names, design_name exists in project.
   #    7) No opened design, design_name exists in project.

   set errMsg "Design <$design_name> already exists in your project, please set the variable <design_name> to another value."
   set nRet 2

} else {
   # USE CASES:
   #    8) No opened design, design_name not in project.
   #    9) Current opened design, has components, but diff names, design_name not in project.

   common::send_gid_msg -ssname BD::TCL -id 2003 -severity "INFO" "Currently there is no design <$design_name> in project, so creating one..."

   create_bd_design $design_name

   common::send_gid_msg -ssname BD::TCL -id 2004 -severity "INFO" "Making design <$design_name> as current_bd_design."
   current_bd_design $design_name

}

common::send_gid_msg -ssname BD::TCL -id 2005 -severity "INFO" "Currently the variable <design_name> is equal to \"$design_name\"."

if { $nRet != 0 } {
   catch {common::send_gid_msg -ssname BD::TCL -id 2006 -severity "ERROR" $errMsg}
   return $nRet
}

set bCheckIPsPassed 1
##################################################################
# CHECK IPs
##################################################################
set bCheckIPs 1
if { $bCheckIPs == 1 } {
   set list_check_ips "\ 
xilinx.com:hls:hash_join_kernel:1.0\
xilinx.com:ip:smartconnect:1.0\
xilinx.com:ip:proc_sys_reset:5.0\
xilinx.com:ip:xlconstant:1.1\
xilinx.com:ip:util_vector_logic:2.0\
xilinx.com:ip:clk_wiz:6.0\
xilinx.com:ip:axi_clock_converter:2.1\
xilinx.com:ip:mig_7series:4.2\
xilinx.com:ip:axis_clock_converter:1.1\
xilinx.com:ip:axis_data_fifo:2.0\
"

   set list_ips_missing ""
   common::send_gid_msg -ssname BD::TCL -id 2011 -severity "INFO" "Checking if the following IPs exist in the project's IP catalog: $list_check_ips ."

   foreach ip_vlnv $list_check_ips {
      set ip_obj [get_ipdefs -all $ip_vlnv]
      if { $ip_obj eq "" } {
         lappend list_ips_missing $ip_vlnv
      }
   }

   if { $list_ips_missing ne "" } {
      catch {common::send_gid_msg -ssname BD::TCL -id 2012 -severity "ERROR" "The following IPs are not found in the IP Catalog:\n  $list_ips_missing\n\nResolution: Please add the repository containing the IP(s) to the project." }
      set bCheckIPsPassed 0
   }

}

##################################################################
# CHECK Modules
##################################################################
set bCheckModules 1
if { $bCheckModules == 1 } {
   set list_check_mods "\ 
clk_blinker\
clk_blinker\
clk_blinker\
clk_blinker\
rmii_udp_stream_bridge\
axis_protocol_timing_monitor\
eth_debug_sticky\
eth_tx_debug_sticky\
eth_tx_pipeline_sticky\
eth_tx_pin_capture\
eth_led_pages_v3\
rmii_refclk_forward\
lan8720_mdio_status\
clk_blinker\
"

   set list_mods_missing ""
   common::send_gid_msg -ssname BD::TCL -id 2020 -severity "INFO" "Checking if the following modules exist in the project's sources: $list_check_mods ."

   foreach mod_vlnv $list_check_mods {
      if { [can_resolve_reference $mod_vlnv] == 0 } {
         lappend list_mods_missing $mod_vlnv
      }
   }

   if { $list_mods_missing ne "" } {
      catch {common::send_gid_msg -ssname BD::TCL -id 2021 -severity "ERROR" "The following module(s) are not found in the project: $list_mods_missing" }
      common::send_gid_msg -ssname BD::TCL -id 2022 -severity "INFO" "Please add source files for the missing module(s) above."
      set bCheckIPsPassed 0
   }
}

if { $bCheckIPsPassed != 1 } {
  common::send_gid_msg -ssname BD::TCL -id 2023 -severity "WARNING" "Will not continue with creation of design due to the error(s) above."
  return 3
}


##################################################################
# MIG PRJ FILE TCL PROCs
##################################################################

proc write_mig_file_hash_join_bd_mig_7series_0_2 { str_mig_prj_filepath } {

   file mkdir [ file dirname "$str_mig_prj_filepath" ]
   set mig_prj_file [open $str_mig_prj_filepath  w+]

   puts $mig_prj_file {﻿<?xml version="1.0" encoding="UTF-8" standalone="no" ?>}
   puts $mig_prj_file {<Project NoOfControllers="1">}
   puts $mig_prj_file {  }
   puts $mig_prj_file {<!-- IMPORTANT: This is an internal file that has been generated by the MIG software. Any direct editing or changes made to this file may result in unpredictable behavior or data corruption. It is strongly advised that users do not edit the contents of this file. Re-run the MIG GUI with the required settings if any of the options provided below need to be altered. -->}
   puts $mig_prj_file {  <ModuleName>hash_join_bd_mig_7series_0_2</ModuleName>}
   puts $mig_prj_file {  <dci_inouts_inputs>1</dci_inouts_inputs>}
   puts $mig_prj_file {  <dci_inputs>1</dci_inputs>}
   puts $mig_prj_file {  <Debug_En>OFF</Debug_En>}
   puts $mig_prj_file {  <DataDepth_En>1024</DataDepth_En>}
   puts $mig_prj_file {  <LowPower_En>ON</LowPower_En>}
   puts $mig_prj_file {  <XADC_En>Enabled</XADC_En>}
   puts $mig_prj_file {  <TargetFPGA>xc7a100t-csg324/-1</TargetFPGA>}
   puts $mig_prj_file {  <Version>4.2</Version>}
   puts $mig_prj_file {  <SystemClock>No Buffer</SystemClock>}
   puts $mig_prj_file {  <ReferenceClock>No Buffer</ReferenceClock>}
   puts $mig_prj_file {  <SysResetPolarity>ACTIVE LOW</SysResetPolarity>}
   puts $mig_prj_file {  <BankSelectionFlag>FALSE</BankSelectionFlag>}
   puts $mig_prj_file {  <InternalVref>1</InternalVref>}
   puts $mig_prj_file {  <dci_hr_inouts_inputs>50 Ohms</dci_hr_inouts_inputs>}
   puts $mig_prj_file {  <dci_cascade>0</dci_cascade>}
   puts $mig_prj_file {  <FPGADevice>}
   puts $mig_prj_file {    <selected>7a/xc7a100t-csg324</selected>}
   puts $mig_prj_file {  </FPGADevice>}
   puts $mig_prj_file {  <Controller number="0">}
   puts $mig_prj_file {    <MemoryDevice>DDR2_SDRAM/Components/MT47H64M16HR-25E</MemoryDevice>}
   puts $mig_prj_file {    <TimePeriod>3077</TimePeriod>}
   puts $mig_prj_file {    <VccAuxIO>1.8V</VccAuxIO>}
   puts $mig_prj_file {    <PHYRatio>4:1</PHYRatio>}
   puts $mig_prj_file {    <InputClkFreq>99.997</InputClkFreq>}
   puts $mig_prj_file {    <UIExtraClocks>1</UIExtraClocks>}
   puts $mig_prj_file {    <MMCM_VCO>1200</MMCM_VCO>}
   puts $mig_prj_file {    <MMCMClkOut0> 6.000</MMCMClkOut0>}
   puts $mig_prj_file {    <MMCMClkOut1>1</MMCMClkOut1>}
   puts $mig_prj_file {    <MMCMClkOut2>1</MMCMClkOut2>}
   puts $mig_prj_file {    <MMCMClkOut3>1</MMCMClkOut3>}
   puts $mig_prj_file {    <MMCMClkOut4>1</MMCMClkOut4>}
   puts $mig_prj_file {    <DataWidth>16</DataWidth>}
   puts $mig_prj_file {    <DeepMemory>1</DeepMemory>}
   puts $mig_prj_file {    <DataMask>1</DataMask>}
   puts $mig_prj_file {    <ECC>Disabled</ECC>}
   puts $mig_prj_file {    <Ordering>Strict</Ordering>}
   puts $mig_prj_file {    <BankMachineCnt>4</BankMachineCnt>}
   puts $mig_prj_file {    <CustomPart>FALSE</CustomPart>}
   puts $mig_prj_file {    <NewPartName/>}
   puts $mig_prj_file {    <RowAddress>13</RowAddress>}
   puts $mig_prj_file {    <ColAddress>10</ColAddress>}
   puts $mig_prj_file {    <BankAddress>3</BankAddress>}
   puts $mig_prj_file {    <C0_MEM_SIZE>134217728</C0_MEM_SIZE>}
   puts $mig_prj_file {    <UserMemoryAddressMap>BANK_ROW_COLUMN</UserMemoryAddressMap>}
   puts $mig_prj_file {    <PinSelection>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="M4" SLEW="" VCCAUX_IO="" name="ddr2_addr[0]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="R2" SLEW="" VCCAUX_IO="" name="ddr2_addr[10]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="K5" SLEW="" VCCAUX_IO="" name="ddr2_addr[11]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="N6" SLEW="" VCCAUX_IO="" name="ddr2_addr[12]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="P4" SLEW="" VCCAUX_IO="" name="ddr2_addr[1]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="M6" SLEW="" VCCAUX_IO="" name="ddr2_addr[2]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="T1" SLEW="" VCCAUX_IO="" name="ddr2_addr[3]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="L3" SLEW="" VCCAUX_IO="" name="ddr2_addr[4]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="P5" SLEW="" VCCAUX_IO="" name="ddr2_addr[5]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="M2" SLEW="" VCCAUX_IO="" name="ddr2_addr[6]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="N1" SLEW="" VCCAUX_IO="" name="ddr2_addr[7]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="L4" SLEW="" VCCAUX_IO="" name="ddr2_addr[8]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="N5" SLEW="" VCCAUX_IO="" name="ddr2_addr[9]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="P2" SLEW="" VCCAUX_IO="" name="ddr2_ba[0]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="P3" SLEW="" VCCAUX_IO="" name="ddr2_ba[1]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="R1" SLEW="" VCCAUX_IO="" name="ddr2_ba[2]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="L1" SLEW="" VCCAUX_IO="" name="ddr2_cas_n"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="DIFF_SSTL18_II" PADName="L5" SLEW="" VCCAUX_IO="" name="ddr2_ck_n[0]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="DIFF_SSTL18_II" PADName="L6" SLEW="" VCCAUX_IO="" name="ddr2_ck_p[0]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="M1" SLEW="" VCCAUX_IO="" name="ddr2_cke[0]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="K6" SLEW="" VCCAUX_IO="" name="ddr2_cs_n[0]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="T6" SLEW="" VCCAUX_IO="" name="ddr2_dm[0]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="U1" SLEW="" VCCAUX_IO="" name="ddr2_dm[1]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="R7" SLEW="" VCCAUX_IO="" name="ddr2_dq[0]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="V5" SLEW="" VCCAUX_IO="" name="ddr2_dq[10]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="U4" SLEW="" VCCAUX_IO="" name="ddr2_dq[11]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="V4" SLEW="" VCCAUX_IO="" name="ddr2_dq[12]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="T4" SLEW="" VCCAUX_IO="" name="ddr2_dq[13]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="V1" SLEW="" VCCAUX_IO="" name="ddr2_dq[14]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="T3" SLEW="" VCCAUX_IO="" name="ddr2_dq[15]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="V6" SLEW="" VCCAUX_IO="" name="ddr2_dq[1]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="R8" SLEW="" VCCAUX_IO="" name="ddr2_dq[2]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="U7" SLEW="" VCCAUX_IO="" name="ddr2_dq[3]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="V7" SLEW="" VCCAUX_IO="" name="ddr2_dq[4]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="R6" SLEW="" VCCAUX_IO="" name="ddr2_dq[5]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="U6" SLEW="" VCCAUX_IO="" name="ddr2_dq[6]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="R5" SLEW="" VCCAUX_IO="" name="ddr2_dq[7]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="T5" SLEW="" VCCAUX_IO="" name="ddr2_dq[8]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="U3" SLEW="" VCCAUX_IO="" name="ddr2_dq[9]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="DIFF_SSTL18_II" PADName="V9" SLEW="" VCCAUX_IO="" name="ddr2_dqs_n[0]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="DIFF_SSTL18_II" PADName="V2" SLEW="" VCCAUX_IO="" name="ddr2_dqs_n[1]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="DIFF_SSTL18_II" PADName="U9" SLEW="" VCCAUX_IO="" name="ddr2_dqs_p[0]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="DIFF_SSTL18_II" PADName="U2" SLEW="" VCCAUX_IO="" name="ddr2_dqs_p[1]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="M3" SLEW="" VCCAUX_IO="" name="ddr2_odt[0]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="N4" SLEW="" VCCAUX_IO="" name="ddr2_ras_n"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL18_II" PADName="N2" SLEW="" VCCAUX_IO="" name="ddr2_we_n"/>}
   puts $mig_prj_file {    </PinSelection>}
   puts $mig_prj_file {    <System_Clock>}
   puts $mig_prj_file {      <Pin Bank="35" PADName="E3(MRCC_P)" name="sys_clk_i"/>}
   puts $mig_prj_file {    </System_Clock>}
   puts $mig_prj_file {    <System_Control>}
   puts $mig_prj_file {      <Pin Bank="Select Bank" PADName="No connect" name="sys_rst"/>}
   puts $mig_prj_file {      <Pin Bank="Select Bank" PADName="No connect" name="init_calib_complete"/>}
   puts $mig_prj_file {      <Pin Bank="Select Bank" PADName="No connect" name="tg_compare_error"/>}
   puts $mig_prj_file {    </System_Control>}
   puts $mig_prj_file {    <TimingParameters>}
   puts $mig_prj_file {      <Parameters tfaw="45" tras="40" trcd="15" trefi="7.8" trfc="127.5" trp="12.5" trrd="10" trtp="7.5" twtr="7.5"/>}
   puts $mig_prj_file {    </TimingParameters>}
   puts $mig_prj_file {    <mrBurstLength name="Burst Length">8</mrBurstLength>}
   puts $mig_prj_file {    <mrBurstType name="Burst Type">Sequential</mrBurstType>}
   puts $mig_prj_file {    <mrCasLatency name="CAS Latency">5</mrCasLatency>}
   puts $mig_prj_file {    <mrMode name="Mode">Normal</mrMode>}
   puts $mig_prj_file {    <mrDllReset name="DLL Reset">No</mrDllReset>}
   puts $mig_prj_file {    <mrPdMode name="PD Mode">Fast exit</mrPdMode>}
   puts $mig_prj_file {    <mrWriteRecovery name="Write Recovery">5</mrWriteRecovery>}
   puts $mig_prj_file {    <emrDllEnable name="DLL Enable">Enable-Normal</emrDllEnable>}
   puts $mig_prj_file {    <emrOutputDriveStrength name="Output Drive Strength">Fullstrength</emrOutputDriveStrength>}
   puts $mig_prj_file {    <emrCSSelection name="Controller Chip Select Pin">Enable</emrCSSelection>}
   puts $mig_prj_file {    <emrCKSelection name="Memory Clock Selection">1</emrCKSelection>}
   puts $mig_prj_file {    <emrRTT name="RTT (nominal) - ODT">50ohms</emrRTT>}
   puts $mig_prj_file {    <emrPosted name="Additive Latency (AL)">0</emrPosted>}
   puts $mig_prj_file {    <emrOCD name="OCD Operation">OCD Exit</emrOCD>}
   puts $mig_prj_file {    <emrDQS name="DQS# Enable">Enable</emrDQS>}
   puts $mig_prj_file {    <emrRDQS name="RDQS Enable">Disable</emrRDQS>}
   puts $mig_prj_file {    <emrOutputs name="Outputs">Enable</emrOutputs>}
   puts $mig_prj_file {    <PortInterface>AXI</PortInterface>}
   puts $mig_prj_file {    <AXIParameters>}
   puts $mig_prj_file {      <C0_C_RD_WR_ARB_ALGORITHM>RD_PRI_REG</C0_C_RD_WR_ARB_ALGORITHM>}
   puts $mig_prj_file {      <C0_S_AXI_ADDR_WIDTH>26</C0_S_AXI_ADDR_WIDTH>}
   puts $mig_prj_file {      <C0_S_AXI_DATA_WIDTH>64</C0_S_AXI_DATA_WIDTH>}
   puts $mig_prj_file {      <C0_S_AXI_ID_WIDTH>4</C0_S_AXI_ID_WIDTH>}
   puts $mig_prj_file {      <C0_S_AXI_SUPPORTS_NARROW_BURST>0</C0_S_AXI_SUPPORTS_NARROW_BURST>}
   puts $mig_prj_file {    </AXIParameters>}
   puts $mig_prj_file {  </Controller>}
   puts $mig_prj_file {</Project>}

   close $mig_prj_file
}
# End of write_mig_file_hash_join_bd_mig_7series_0_2()



##################################################################
# DESIGN PROCs
##################################################################



# Procedure to create entire design; Provide argument to make
# procedure reusable. If parentCell is "", will use root.
proc create_root_design { parentCell } {

  variable script_folder
  variable design_name

  if { $parentCell eq "" } {
     set parentCell [get_bd_cells /]
  }

  # Get object for parentCell
  set parentObj [get_bd_cells $parentCell]
  if { $parentObj == "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2090 -severity "ERROR" "Unable to find parent cell <$parentCell>!"}
     return
  }

  # Make sure parentObj is hier blk
  set parentType [get_property TYPE $parentObj]
  if { $parentType ne "hier" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2091 -severity "ERROR" "Parent <$parentObj> has TYPE = <$parentType>. Expected to be <hier>."}
     return
  }

  # Save current instance; Restore later
  set oldCurInst [current_bd_instance .]

  # Set parent object as current
  current_bd_instance $parentObj


  # Create interface ports
  set rx_0 [ create_bd_intf_port -mode Slave -vlnv xilinx.com:interface:axis_rtl:1.0 rx_0 ]
  set_property -dict [ list \
   CONFIG.FREQ_HZ {81247969} \
   CONFIG.HAS_TKEEP {1} \
   CONFIG.HAS_TLAST {1} \
   CONFIG.HAS_TREADY {1} \
   CONFIG.HAS_TSTRB {1} \
   CONFIG.LAYERED_METADATA {undef} \
   CONFIG.TDATA_NUM_BYTES {1} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.TUSER_WIDTH {0} \
   ] $rx_0

  set DDR2_0 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:ddrx_rtl:1.0 DDR2_0 ]


  # Create ports
  set LED0 [ create_bd_port -dir O LED0 ]
  set uart_rx_0 [ create_bd_port -dir I uart_rx_0 ]
  set uart_tx_0 [ create_bd_port -dir O uart_tx_0 ]
  set CPU_RESETN [ create_bd_port -dir I -type rst CPU_RESETN ]
  set sys_clk_i_0 [ create_bd_port -dir I -type clk sys_clk_i_0 ]
  set LED1 [ create_bd_port -dir O LED1 ]
  set LED2 [ create_bd_port -dir O -from 0 -to 0 LED2 ]
  set LED3 [ create_bd_port -dir O LED3 ]
  set LED4 [ create_bd_port -dir O LED4 ]
  set LED5 [ create_bd_port -dir O LED5 ]
  set LED6 [ create_bd_port -dir O LED6 ]
  set LED8 [ create_bd_port -dir O LED8 ]
  set LED7 [ create_bd_port -dir O -from 0 -to 0 LED7 ]
  set LED9 [ create_bd_port -dir O LED9 ]
  set LED10 [ create_bd_port -dir O -from 0 -to 0 LED10 ]
  set eth_refclk_0 [ create_bd_port -dir O eth_refclk_0 ]
  set eth_rstn_0 [ create_bd_port -dir O -from 0 -to 0 eth_rstn_0 ]
  set eth_crsdv_0 [ create_bd_port -dir I eth_crsdv_0 ]
  set eth_rxerr_0 [ create_bd_port -dir I eth_rxerr_0 ]
  set eth_rxd_0 [ create_bd_port -dir I -from 1 -to 0 eth_rxd_0 ]
  set eth_txen_0 [ create_bd_port -dir O eth_txen_0 ]
  set eth_txd_0 [ create_bd_port -dir O -from 1 -to 0 eth_txd_0 ]
  set eth_mdc_0 [ create_bd_port -dir O -from 0 -to 0 eth_mdc_0 ]
  set eth_intn_0 [ create_bd_port -dir I eth_intn_0 ]
  set LED11 [ create_bd_port -dir O LED11 ]
  set LED12 [ create_bd_port -dir O -from 0 -to 0 LED12 ]
  set eth_mdio_0 [ create_bd_port -dir IO eth_mdio_0 ]
  set LED15 [ create_bd_port -dir O -from 0 -to 0 LED15 ]
  set LED13 [ create_bd_port -dir O -from 0 -to 0 LED13 ]
  set LED14 [ create_bd_port -dir O -from 0 -to 0 LED14 ]
  set SW0 [ create_bd_port -dir I SW0 ]
  set SW1 [ create_bd_port -dir I SW1 ]
  set SW2 [ create_bd_port -dir I SW2 ]
  set SW3 [ create_bd_port -dir I SW3 ]

  # Create instance: hash_join_kernel_0, and set properties
  set hash_join_kernel_0 [ create_bd_cell -type ip -vlnv xilinx.com:hls:hash_join_kernel:1.0 hash_join_kernel_0 ]

  # Create instance: smartconnect_0, and set properties
  set smartconnect_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 smartconnect_0 ]
  set_property CONFIG.NUM_SI {1} $smartconnect_0


  # Create instance: proc_sys_reset_0, and set properties
  set proc_sys_reset_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset:5.0 proc_sys_reset_0 ]

  # Create instance: xlconstant_0, and set properties
  set xlconstant_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 xlconstant_0 ]

  # Create instance: xlconstant_1, and set properties
  set xlconstant_1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 xlconstant_1 ]
  set_property CONFIG.CONST_VAL {0} $xlconstant_1


  # Create instance: util_vector_logic_0, and set properties
  set util_vector_logic_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:util_vector_logic:2.0 util_vector_logic_0 ]
  set_property -dict [list \
    CONFIG.C_OPERATION {not} \
    CONFIG.C_SIZE {1} \
  ] $util_vector_logic_0


  # Create instance: clk_wiz_0, and set properties
  set clk_wiz_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:clk_wiz:6.0 clk_wiz_0 ]
  set_property -dict [list \
    CONFIG.CLKOUT1_JITTER {114.829} \
    CONFIG.CLKOUT1_REQUESTED_OUT_FREQ {200.000} \
    CONFIG.CLKOUT2_JITTER {130.958} \
    CONFIG.CLKOUT2_PHASE_ERROR {98.575} \
    CONFIG.CLKOUT2_USED {true} \
    CONFIG.CLKOUT3_DRIVES {BUFG} \
    CONFIG.CLKOUT3_REQUESTED_OUT_FREQ {100} \
    CONFIG.CLKOUT3_USED {true} \
    CONFIG.CLKOUT4_REQUESTED_OUT_FREQ {50.000} \
    CONFIG.CLKOUT4_USED {true} \
    CONFIG.CLKOUT5_REQUESTED_OUT_FREQ {50.000} \
    CONFIG.CLKOUT5_REQUESTED_PHASE {45.000} \
    CONFIG.CLKOUT5_USED {true} \
    CONFIG.MMCM_CLKOUT0_DIVIDE_F {5.000} \
    CONFIG.MMCM_CLKOUT1_DIVIDE {10} \
    CONFIG.NUM_OUT_CLKS {5} \
  ] $clk_wiz_0


  # Create instance: axi_clock_converter_0, and set properties
  set axi_clock_converter_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_clock_converter:2.1 axi_clock_converter_0 ]

  # Create instance: proc_sys_reset_1, and set properties
  set proc_sys_reset_1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset:5.0 proc_sys_reset_1 ]

  # Create instance: mig_7series_0, and set properties
  set mig_7series_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:mig_7series:4.2 mig_7series_0 ]

  # Generate the PRJ File for MIG
  set str_mig_folder [get_property IP_DIR [ get_ips [ get_property CONFIG.Component_Name $mig_7series_0 ] ] ]
  set str_mig_file_name mig_a.prj
  set str_mig_file_path ${str_mig_folder}/${str_mig_file_name}
  write_mig_file_hash_join_bd_mig_7series_0_2 $str_mig_file_path

  set_property CONFIG.XML_INPUT_FILE {mig_a.prj} $mig_7series_0


  # Create instance: clk_blinker_0, and set properties
  set block_name clk_blinker
  set block_cell_name clk_blinker_0
  if { [catch {set clk_blinker_0 [create_bd_cell -type module -reference $block_name $block_cell_name] } errmsg] } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2095 -severity "ERROR" "Unable to add referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   } elseif { $clk_blinker_0 eq "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2096 -severity "ERROR" "Unable to referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   }
  
  # Create instance: clk_blinker_1, and set properties
  set block_name clk_blinker
  set block_cell_name clk_blinker_1
  if { [catch {set clk_blinker_1 [create_bd_cell -type module -reference $block_name $block_cell_name] } errmsg] } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2095 -severity "ERROR" "Unable to add referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   } elseif { $clk_blinker_1 eq "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2096 -severity "ERROR" "Unable to referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   }
  
  # Create instance: clk_blinker_2, and set properties
  set block_name clk_blinker
  set block_cell_name clk_blinker_2
  if { [catch {set clk_blinker_2 [create_bd_cell -type module -reference $block_name $block_cell_name] } errmsg] } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2095 -severity "ERROR" "Unable to add referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   } elseif { $clk_blinker_2 eq "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2096 -severity "ERROR" "Unable to referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   }
  
  # Create instance: clk_blinker_3, and set properties
  set block_name clk_blinker
  set block_cell_name clk_blinker_3
  if { [catch {set clk_blinker_3 [create_bd_cell -type module -reference $block_name $block_cell_name] } errmsg] } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2095 -severity "ERROR" "Unable to add referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   } elseif { $clk_blinker_3 eq "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2096 -severity "ERROR" "Unable to referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   }
  
  # Create instance: proc_sys_reset_2, and set properties
  set proc_sys_reset_2 [ create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset:5.0 proc_sys_reset_2 ]

  # Create instance: rmii_udp_stream_bridge_0, and set properties
  set block_name rmii_udp_stream_bridge
  set block_cell_name rmii_udp_stream_bridge_0
  if { [catch {set rmii_udp_stream_bridge_0 [create_bd_cell -type module -reference $block_name $block_cell_name] } errmsg] } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2095 -severity "ERROR" "Unable to add referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   } elseif { $rmii_udp_stream_bridge_0 eq "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2096 -severity "ERROR" "Unable to referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   }
  
  set_property -dict [ list \
   CONFIG.FREQ_HZ {50000000} \
 ] [get_bd_intf_pins $rmii_udp_stream_bridge_0/m_axis]

  # Create instance: eth_rx_axis_cc, and set properties
  set eth_rx_axis_cc [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_clock_converter:1.1 eth_rx_axis_cc ]
  set_property -dict [list \
    CONFIG.HAS_TKEEP {0} \
    CONFIG.HAS_TLAST {1} \
    CONFIG.HAS_TSTRB {0} \
    CONFIG.IS_ACLK_ASYNC {1} \
    CONFIG.SYNCHRONIZATION_STAGES {2} \
    CONFIG.TDATA_NUM_BYTES {1} \
    CONFIG.TDEST_WIDTH {0} \
    CONFIG.TID_WIDTH {0} \
    CONFIG.TUSER_WIDTH {0} \
  ] $eth_rx_axis_cc


  # Create instance: eth_rx_axis_fifo, and set properties
  set eth_rx_axis_fifo [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_data_fifo:2.0 eth_rx_axis_fifo ]
  set_property -dict [list \
    CONFIG.FIFO_DEPTH {8192} \
    CONFIG.HAS_TKEEP {0} \
    CONFIG.HAS_TLAST {1} \
    CONFIG.HAS_TSTRB {0} \
    CONFIG.TDATA_NUM_BYTES {1} \
  ] $eth_rx_axis_fifo


  # Create instance: eth_tx_axis_cc, and set properties
  set eth_tx_axis_cc [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_clock_converter:1.1 eth_tx_axis_cc ]
  set_property -dict [list \
    CONFIG.HAS_TKEEP {0} \
    CONFIG.HAS_TLAST {1} \
    CONFIG.HAS_TSTRB {0} \
    CONFIG.IS_ACLK_ASYNC {1} \
    CONFIG.SYNCHRONIZATION_STAGES {2} \
    CONFIG.TDATA_NUM_BYTES {1} \
    CONFIG.TDEST_WIDTH {0} \
    CONFIG.TID_WIDTH {0} \
    CONFIG.TUSER_WIDTH {0} \
  ] $eth_tx_axis_cc


  # Create instance: axis_protocol_timing_monitor_0, and set properties
  set block_name axis_protocol_timing_monitor
  set block_cell_name axis_protocol_timing_monitor_0
  if { [catch {set axis_protocol_timing_monitor_0 [create_bd_cell -type module -reference $block_name $block_cell_name] } errmsg] } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2095 -severity "ERROR" "Unable to add referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   } elseif { $axis_protocol_timing_monitor_0 eq "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2096 -severity "ERROR" "Unable to referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   }
    set_property CONFIG.CLOCK_HZ {"00000100001011000001110110000000"} $axis_protocol_timing_monitor_0


  # Create instance: eth_debug_sticky_0, and set properties
  set block_name eth_debug_sticky
  set block_cell_name eth_debug_sticky_0
  if { [catch {set eth_debug_sticky_0 [create_bd_cell -type module -reference $block_name $block_cell_name] } errmsg] } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2095 -severity "ERROR" "Unable to add referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   } elseif { $eth_debug_sticky_0 eq "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2096 -severity "ERROR" "Unable to referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   }
  
  # Create instance: eth_tx_debug_sticky_0, and set properties
  set block_name eth_tx_debug_sticky
  set block_cell_name eth_tx_debug_sticky_0
  if { [catch {set eth_tx_debug_sticky_0 [create_bd_cell -type module -reference $block_name $block_cell_name] } errmsg] } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2095 -severity "ERROR" "Unable to add referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   } elseif { $eth_tx_debug_sticky_0 eq "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2096 -severity "ERROR" "Unable to referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   }
  
  # Create instance: eth_tx_pipeline_sticky_0, and set properties
  set block_name eth_tx_pipeline_sticky
  set block_cell_name eth_tx_pipeline_sticky_0
  if { [catch {set eth_tx_pipeline_sticky_0 [create_bd_cell -type module -reference $block_name $block_cell_name] } errmsg] } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2095 -severity "ERROR" "Unable to add referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   } elseif { $eth_tx_pipeline_sticky_0 eq "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2096 -severity "ERROR" "Unable to referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   }
  
  # Create instance: eth_tx_pin_capture_0, and set properties
  set block_name eth_tx_pin_capture
  set block_cell_name eth_tx_pin_capture_0
  if { [catch {set eth_tx_pin_capture_0 [create_bd_cell -type module -reference $block_name $block_cell_name] } errmsg] } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2095 -severity "ERROR" "Unable to add referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   } elseif { $eth_tx_pin_capture_0 eq "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2096 -severity "ERROR" "Unable to referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   }
    set_property CONFIG.TEN_MBIT_MODE {0} $eth_tx_pin_capture_0


  # Create instance: eth_led_pages_v3_0, and set properties
  set block_name eth_led_pages_v3
  set block_cell_name eth_led_pages_v3_0
  if { [catch {set eth_led_pages_v3_0 [create_bd_cell -type module -reference $block_name $block_cell_name] } errmsg] } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2095 -severity "ERROR" "Unable to add referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   } elseif { $eth_led_pages_v3_0 eq "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2096 -severity "ERROR" "Unable to referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   }
  
  # Create instance: rmii_refclk_forward_0, and set properties
  set block_name rmii_refclk_forward
  set block_cell_name rmii_refclk_forward_0
  if { [catch {set rmii_refclk_forward_0 [create_bd_cell -type module -reference $block_name $block_cell_name] } errmsg] } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2095 -severity "ERROR" "Unable to add referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   } elseif { $rmii_refclk_forward_0 eq "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2096 -severity "ERROR" "Unable to referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   }
  
  # Create instance: lan8720_mdio_status_0, and set properties
  set block_name lan8720_mdio_status
  set block_cell_name lan8720_mdio_status_0
  if { [catch {set lan8720_mdio_status_0 [create_bd_cell -type module -reference $block_name $block_cell_name] } errmsg] } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2095 -severity "ERROR" "Unable to add referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   } elseif { $lan8720_mdio_status_0 eq "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2096 -severity "ERROR" "Unable to referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   }
  
  # Create instance: eth_clk50_blinker_0, and set properties
  set block_name clk_blinker
  set block_cell_name eth_clk50_blinker_0
  if { [catch {set eth_clk50_blinker_0 [create_bd_cell -type module -reference $block_name $block_cell_name] } errmsg] } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2095 -severity "ERROR" "Unable to add referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   } elseif { $eth_clk50_blinker_0 eq "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2096 -severity "ERROR" "Unable to referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   }
  
  # Create interface connections
  connect_bd_intf_net -intf_net axi_clock_converter_0_M_AXI [get_bd_intf_pins axi_clock_converter_0/M_AXI] [get_bd_intf_pins mig_7series_0/S_AXI]
  connect_bd_intf_net -intf_net eth_rx_axis_cc_M_AXIS1 [get_bd_intf_pins eth_rx_axis_cc/M_AXIS] [get_bd_intf_pins eth_rx_axis_fifo/S_AXIS]
  connect_bd_intf_net -intf_net eth_tx_axis_cc_M_AXIS1 [get_bd_intf_pins eth_tx_axis_cc/M_AXIS] [get_bd_intf_pins rmii_udp_stream_bridge_0/s_axis]
  connect_bd_intf_net -intf_net hash_join_kernel_0_m_axi_DDR2 [get_bd_intf_pins hash_join_kernel_0/m_axi_DDR2] [get_bd_intf_pins smartconnect_0/S00_AXI]
  connect_bd_intf_net -intf_net mig_7series_0_DDR2 [get_bd_intf_ports DDR2_0] [get_bd_intf_pins mig_7series_0/DDR2]
  connect_bd_intf_net -intf_net rmii_udp_stream_bridge_0_m_axis1 [get_bd_intf_pins rmii_udp_stream_bridge_0/m_axis] [get_bd_intf_pins eth_rx_axis_cc/S_AXIS]
  connect_bd_intf_net -intf_net smartconnect_0_M00_AXI [get_bd_intf_pins smartconnect_0/M00_AXI] [get_bd_intf_pins axi_clock_converter_0/S_AXI]

  # Create port connections
  connect_bd_net -net CPU_RESETN_1  [get_bd_ports CPU_RESETN] \
  [get_bd_pins util_vector_logic_0/Op1]
  connect_bd_net -net Net  [get_bd_ports eth_mdio_0] \
  [get_bd_pins lan8720_mdio_status_0/eth_mdio]
  connect_bd_net -net SW0_1  [get_bd_ports SW0] \
  [get_bd_pins eth_led_pages_v3_0/page0]
  connect_bd_net -net SW1_1  [get_bd_ports SW1] \
  [get_bd_pins eth_led_pages_v3_0/page1]
  connect_bd_net -net SW2_1  [get_bd_ports SW2] \
  [get_bd_pins eth_led_pages_v3_0/page2]
  connect_bd_net -net SW3_1  [get_bd_ports SW3] \
  [get_bd_pins eth_led_pages_v3_0/page3]
  connect_bd_net -net axis_protocol_timing_monitor_0_ack_frames  [get_bd_pins axis_protocol_timing_monitor_0/ack_frames] \
  [get_bd_pins hash_join_kernel_0/rtl_ack_frames]
  connect_bd_net -net axis_protocol_timing_monitor_0_bytes_rx  [get_bd_pins axis_protocol_timing_monitor_0/bytes_rx] \
  [get_bd_pins hash_join_kernel_0/rtl_bytes_rx]
  connect_bd_net -net axis_protocol_timing_monitor_0_bytes_tx  [get_bd_pins axis_protocol_timing_monitor_0/bytes_tx] \
  [get_bd_pins hash_join_kernel_0/rtl_bytes_tx]
  connect_bd_net -net axis_protocol_timing_monitor_0_clock_hz  [get_bd_pins axis_protocol_timing_monitor_0/clock_hz] \
  [get_bd_pins hash_join_kernel_0/rtl_timing_clock_hz]
  connect_bd_net -net axis_protocol_timing_monitor_0_cycle_counter  [get_bd_pins axis_protocol_timing_monitor_0/cycle_counter] \
  [get_bd_pins hash_join_kernel_0/rtl_cycle_counter]
  connect_bd_net -net axis_protocol_timing_monitor_0_debug_frames  [get_bd_pins axis_protocol_timing_monitor_0/debug_frames] \
  [get_bd_pins hash_join_kernel_0/rtl_debug_frames]
  connect_bd_net -net axis_protocol_timing_monitor_0_inner_frames  [get_bd_pins axis_protocol_timing_monitor_0/inner_frames] \
  [get_bd_pins hash_join_kernel_0/rtl_inner_frames]
  connect_bd_net -net axis_protocol_timing_monitor_0_outer_frames  [get_bd_pins axis_protocol_timing_monitor_0/outer_frames] \
  [get_bd_pins hash_join_kernel_0/rtl_outer_frames]
  connect_bd_net -net axis_protocol_timing_monitor_0_result_frames  [get_bd_pins axis_protocol_timing_monitor_0/result_frames] \
  [get_bd_pins hash_join_kernel_0/rtl_result_frames]
  connect_bd_net -net axis_protocol_timing_monitor_0_timing_valid  [get_bd_pins axis_protocol_timing_monitor_0/timing_valid] \
  [get_bd_pins hash_join_kernel_0/rtl_timing_valid]
  connect_bd_net -net axis_protocol_timing_monitor_0_ts_build_ack_last  [get_bd_pins axis_protocol_timing_monitor_0/ts_build_ack_last] \
  [get_bd_pins hash_join_kernel_0/rtl_ts_build_ack_last]
  connect_bd_net -net axis_protocol_timing_monitor_0_ts_build_first  [get_bd_pins axis_protocol_timing_monitor_0/ts_build_first] \
  [get_bd_pins hash_join_kernel_0/rtl_ts_build_first]
  connect_bd_net -net axis_protocol_timing_monitor_0_ts_build_last  [get_bd_pins axis_protocol_timing_monitor_0/ts_build_last] \
  [get_bd_pins hash_join_kernel_0/rtl_ts_build_last]
  connect_bd_net -net axis_protocol_timing_monitor_0_ts_config_ack  [get_bd_pins axis_protocol_timing_monitor_0/ts_config_ack] \
  [get_bd_pins hash_join_kernel_0/rtl_ts_config_ack]
  connect_bd_net -net axis_protocol_timing_monitor_0_ts_config_first  [get_bd_pins axis_protocol_timing_monitor_0/ts_config_first] \
  [get_bd_pins hash_join_kernel_0/rtl_ts_config_first]
  connect_bd_net -net axis_protocol_timing_monitor_0_ts_probe_ack_last  [get_bd_pins axis_protocol_timing_monitor_0/ts_probe_ack_last] \
  [get_bd_pins hash_join_kernel_0/rtl_ts_probe_ack_last]
  connect_bd_net -net axis_protocol_timing_monitor_0_ts_probe_first  [get_bd_pins axis_protocol_timing_monitor_0/ts_probe_first] \
  [get_bd_pins hash_join_kernel_0/rtl_ts_probe_first]
  connect_bd_net -net axis_protocol_timing_monitor_0_ts_probe_last  [get_bd_pins axis_protocol_timing_monitor_0/ts_probe_last] \
  [get_bd_pins hash_join_kernel_0/rtl_ts_probe_last]
  connect_bd_net -net axis_protocol_timing_monitor_0_ts_result_first  [get_bd_pins axis_protocol_timing_monitor_0/ts_result_first] \
  [get_bd_pins hash_join_kernel_0/rtl_ts_result_first]
  connect_bd_net -net axis_protocol_timing_monitor_0_ts_result_last  [get_bd_pins axis_protocol_timing_monitor_0/ts_result_last] \
  [get_bd_pins hash_join_kernel_0/rtl_ts_result_last]
  connect_bd_net -net clk_blinker_0_blink  [get_bd_pins clk_blinker_0/blink]
  connect_bd_net -net clk_blinker_1_blink  [get_bd_pins clk_blinker_1/blink]
  connect_bd_net -net clk_blinker_2_blink  [get_bd_pins clk_blinker_2/blink]
  connect_bd_net -net clk_blinker_3_blink  [get_bd_pins clk_blinker_3/blink]
  connect_bd_net -net clk_wiz_0_clk_out1  [get_bd_pins clk_wiz_0/clk_out1] \
  [get_bd_pins mig_7series_0/clk_ref_i] \
  [get_bd_pins clk_blinker_3/clk]
  connect_bd_net -net clk_wiz_0_clk_out2  [get_bd_pins clk_wiz_0/clk_out2] \
  [get_bd_pins mig_7series_0/sys_clk_i] \
  [get_bd_pins clk_blinker_2/clk]
  connect_bd_net -net clk_wiz_0_clk_out3  [get_bd_pins clk_wiz_0/clk_out3] \
  [get_bd_pins proc_sys_reset_1/slowest_sync_clk] \
  [get_bd_pins axi_clock_converter_0/s_axi_aclk] \
  [get_bd_pins smartconnect_0/aclk] \
  [get_bd_pins clk_blinker_1/clk] \
  [get_bd_pins hash_join_kernel_0/ap_clk] \
  [get_bd_pins eth_rx_axis_cc/m_axis_aclk] \
  [get_bd_pins eth_rx_axis_fifo/s_axis_aclk] \
  [get_bd_pins eth_tx_axis_cc/s_axis_aclk] \
  [get_bd_pins axis_protocol_timing_monitor_0/clk]
  connect_bd_net -net clk_wiz_0_clk_out4  [get_bd_pins clk_wiz_0/clk_out4] \
  [get_bd_pins proc_sys_reset_2/slowest_sync_clk] \
  [get_bd_pins rmii_udp_stream_bridge_0/clk_50] \
  [get_bd_pins eth_debug_sticky_0/clk_50] \
  [get_bd_pins eth_tx_debug_sticky_0/clk_50] \
  [get_bd_pins eth_tx_pipeline_sticky_0/clk_50] \
  [get_bd_pins eth_tx_pin_capture_0/clk_50] \
  [get_bd_pins lan8720_mdio_status_0/clk_50] \
  [get_bd_pins eth_clk50_blinker_0/clk] \
  [get_bd_pins eth_rx_axis_cc/s_axis_aclk] \
  [get_bd_pins eth_tx_axis_cc/m_axis_aclk]
  connect_bd_net -net clk_wiz_0_clk_out5  [get_bd_pins clk_wiz_0/clk_out5] \
  [get_bd_pins rmii_refclk_forward_0/clk_50]
  connect_bd_net -net clk_wiz_0_locked  [get_bd_pins clk_wiz_0/locked] \
  [get_bd_pins proc_sys_reset_1/dcm_locked] \
  [get_bd_pins proc_sys_reset_2/dcm_locked]
  connect_bd_net -net eth_clk50_blinker_0_blink  [get_bd_pins eth_clk50_blinker_0/blink] \
  [get_bd_pins eth_led_pages_v3_0/heartbeat]
  connect_bd_net -net eth_crsdv_0_1  [get_bd_ports eth_crsdv_0] \
  [get_bd_pins rmii_udp_stream_bridge_0/eth_crsdv] \
  [get_bd_pins eth_debug_sticky_0/eth_crsdv] \
  [get_bd_pins eth_tx_pin_capture_0/eth_crsdv] \
  [get_bd_pins eth_led_pages_v3_0/eth_crsdv]
  connect_bd_net -net eth_debug_sticky_0_led_byte_ge_6  [get_bd_pins eth_debug_sticky_0/led_byte_ge_6] \
  [get_bd_pins eth_led_pages_v3_0/rx_byte_ge_6]
  connect_bd_net -net eth_debug_sticky_0_led_byte_ge_14  [get_bd_pins eth_debug_sticky_0/led_byte_ge_14] \
  [get_bd_pins eth_led_pages_v3_0/rx_byte_ge_14]
  connect_bd_net -net eth_debug_sticky_0_led_crsdv_seen  [get_bd_pins eth_debug_sticky_0/led_crsdv_seen] \
  [get_bd_pins eth_led_pages_v3_0/rx_crsdv_seen]
  connect_bd_net -net eth_debug_sticky_0_led_rx_problem_seen  [get_bd_pins eth_debug_sticky_0/led_rx_problem_seen] \
  [get_bd_pins eth_led_pages_v3_0/rx_packet_seen]
  connect_bd_net -net eth_debug_sticky_0_led_rxd0_seen  [get_bd_pins eth_debug_sticky_0/led_rxd0_seen] \
  [get_bd_pins eth_led_pages_v3_0/rx_rxd0_seen]
  connect_bd_net -net eth_debug_sticky_0_led_rxd1_seen  [get_bd_pins eth_debug_sticky_0/led_rxd1_seen] \
  [get_bd_pins eth_led_pages_v3_0/rx_rxd1_seen]
  connect_bd_net -net eth_debug_sticky_0_led_rxerr_seen  [get_bd_pins eth_debug_sticky_0/led_rxerr_seen] \
  [get_bd_pins eth_led_pages_v3_0/rx_rxerr_seen]
  connect_bd_net -net eth_debug_sticky_0_led_sfd_seen  [get_bd_pins eth_debug_sticky_0/led_sfd_seen] \
  [get_bd_pins eth_led_pages_v3_0/rx_sfd_seen]
  connect_bd_net -net eth_debug_sticky_0_led_tx_seen  [get_bd_pins eth_debug_sticky_0/led_tx_seen] \
  [get_bd_pins eth_led_pages_v3_0/tx_activity_seen]
  connect_bd_net -net eth_intn_0_2  [get_bd_ports eth_intn_0] \
  [get_bd_pins eth_led_pages_v3_0/eth_intn]
  connect_bd_net -net eth_led_pages_v3_0_led0  [get_bd_pins eth_led_pages_v3_0/led0] \
  [get_bd_ports LED0]
  connect_bd_net -net eth_led_pages_v3_0_led1  [get_bd_pins eth_led_pages_v3_0/led1] \
  [get_bd_ports LED1]
  connect_bd_net -net eth_led_pages_v3_0_led2  [get_bd_pins eth_led_pages_v3_0/led2] \
  [get_bd_ports LED2]
  connect_bd_net -net eth_led_pages_v3_0_led3  [get_bd_pins eth_led_pages_v3_0/led3] \
  [get_bd_ports LED3]
  connect_bd_net -net eth_led_pages_v3_0_led4  [get_bd_pins eth_led_pages_v3_0/led4] \
  [get_bd_ports LED4]
  connect_bd_net -net eth_led_pages_v3_0_led5  [get_bd_pins eth_led_pages_v3_0/led5] \
  [get_bd_ports LED5]
  connect_bd_net -net eth_led_pages_v3_0_led6  [get_bd_pins eth_led_pages_v3_0/led6] \
  [get_bd_ports LED6]
  connect_bd_net -net eth_led_pages_v3_0_led7  [get_bd_pins eth_led_pages_v3_0/led7] \
  [get_bd_ports LED7]
  connect_bd_net -net eth_led_pages_v3_0_led8  [get_bd_pins eth_led_pages_v3_0/led8] \
  [get_bd_ports LED8]
  connect_bd_net -net eth_led_pages_v3_0_led9  [get_bd_pins eth_led_pages_v3_0/led9] \
  [get_bd_ports LED9]
  connect_bd_net -net eth_led_pages_v3_0_led10  [get_bd_pins eth_led_pages_v3_0/led10] \
  [get_bd_ports LED10]
  connect_bd_net -net eth_led_pages_v3_0_led11  [get_bd_pins eth_led_pages_v3_0/led11] \
  [get_bd_ports LED11]
  connect_bd_net -net eth_led_pages_v3_0_led12  [get_bd_pins eth_led_pages_v3_0/led12] \
  [get_bd_ports LED12]
  connect_bd_net -net eth_led_pages_v3_0_led13  [get_bd_pins eth_led_pages_v3_0/led13] \
  [get_bd_ports LED13]
  connect_bd_net -net eth_led_pages_v3_0_led14  [get_bd_pins eth_led_pages_v3_0/led14] \
  [get_bd_ports LED14]
  connect_bd_net -net eth_led_pages_v3_0_led15  [get_bd_pins eth_led_pages_v3_0/led15] \
  [get_bd_ports LED15]
  connect_bd_net -net eth_rx_axis_fifo_m_axis_tdata  [get_bd_pins eth_rx_axis_fifo/m_axis_tdata] \
  [get_bd_pins hash_join_kernel_0/rx_TDATA] \
  [get_bd_pins axis_protocol_timing_monitor_0/rx_tdata]
  connect_bd_net -net eth_rx_axis_fifo_m_axis_tlast  [get_bd_pins eth_rx_axis_fifo/m_axis_tlast] \
  [get_bd_pins hash_join_kernel_0/rx_TLAST]
  connect_bd_net -net eth_rx_axis_fifo_m_axis_tvalid  [get_bd_pins eth_rx_axis_fifo/m_axis_tvalid] \
  [get_bd_pins hash_join_kernel_0/rx_TVALID] \
  [get_bd_pins axis_protocol_timing_monitor_0/rx_tvalid]
  connect_bd_net -net eth_rxd_0_1  [get_bd_ports eth_rxd_0] \
  [get_bd_pins rmii_udp_stream_bridge_0/eth_rxd] \
  [get_bd_pins eth_debug_sticky_0/eth_rxd] \
  [get_bd_pins eth_led_pages_v3_0/eth_rxd]
  connect_bd_net -net eth_rxerr_0_1  [get_bd_ports eth_rxerr_0] \
  [get_bd_pins rmii_udp_stream_bridge_0/eth_rxerr] \
  [get_bd_pins eth_debug_sticky_0/eth_rxerr] \
  [get_bd_pins eth_led_pages_v3_0/eth_rxerr]
  connect_bd_net -net eth_tx_axis_cc_s_axis_tready  [get_bd_pins eth_tx_axis_cc/s_axis_tready] \
  [get_bd_pins hash_join_kernel_0/tx_TREADY] \
  [get_bd_pins axis_protocol_timing_monitor_0/tx_tready]
  connect_bd_net -net eth_tx_debug_sticky_0_led_txd0_seen  [get_bd_pins eth_tx_debug_sticky_0/led_txd0_seen] \
  [get_bd_pins eth_led_pages_v3_0/tx_pin_txd0_seen]
  connect_bd_net -net eth_tx_debug_sticky_0_led_txd1_seen  [get_bd_pins eth_tx_debug_sticky_0/led_txd1_seen] \
  [get_bd_pins eth_led_pages_v3_0/tx_pin_txd1_seen]
  connect_bd_net -net eth_tx_debug_sticky_0_led_txen_seen  [get_bd_pins eth_tx_debug_sticky_0/led_txen_seen] \
  [get_bd_pins eth_led_pages_v3_0/tx_pin_txen_seen]
  connect_bd_net -net eth_tx_pin_capture_0_active_tx_dibits  [get_bd_pins eth_tx_pin_capture_0/active_tx_dibits] \
  [get_bd_pins eth_led_pages_v3_0/tx_cap_active_tx_dibits]
  connect_bd_net -net eth_tx_pin_capture_0_active_txen_clocks  [get_bd_pins eth_tx_pin_capture_0/active_txen_clocks] \
  [get_bd_pins eth_led_pages_v3_0/tx_cap_active_txen_clocks]
  connect_bd_net -net eth_tx_pin_capture_0_first16_dibits  [get_bd_pins eth_tx_pin_capture_0/first16_dibits] \
  [get_bd_pins eth_led_pages_v3_0/tx_cap_first16_dibits]
  connect_bd_net -net eth_tx_pin_capture_0_last_tx_dibits  [get_bd_pins eth_tx_pin_capture_0/last_tx_dibits] \
  [get_bd_pins eth_led_pages_v3_0/tx_cap_last_tx_dibits]
  connect_bd_net -net eth_tx_pin_capture_0_last_txen_clocks  [get_bd_pins eth_tx_pin_capture_0/last_txen_clocks] \
  [get_bd_pins eth_led_pages_v3_0/tx_cap_last_txen_clocks]
  connect_bd_net -net eth_tx_pin_capture_0_status_flags  [get_bd_pins eth_tx_pin_capture_0/status_flags] \
  [get_bd_pins eth_led_pages_v3_0/tx_cap_status_flags]
  connect_bd_net -net eth_tx_pin_capture_0_tx_frame_count  [get_bd_pins eth_tx_pin_capture_0/tx_frame_count] \
  [get_bd_pins eth_led_pages_v3_0/tx_cap_frame_count]
  connect_bd_net -net eth_tx_pin_capture_0_tx_overflow_count  [get_bd_pins eth_tx_pin_capture_0/tx_overflow_count] \
  [get_bd_pins eth_led_pages_v3_0/tx_cap_overflow_count]
  connect_bd_net -net eth_tx_pin_capture_0_tx_packet_sent_count  [get_bd_pins eth_tx_pin_capture_0/tx_packet_sent_count] \
  [get_bd_pins eth_led_pages_v3_0/tx_cap_packet_sent_count]
  connect_bd_net -net eth_tx_pipeline_sticky_0_led_tx_busy_seen  [get_bd_pins eth_tx_pipeline_sticky_0/led_tx_busy_seen] \
  [get_bd_pins eth_led_pages_v3_0/tx_busy_seen]
  connect_bd_net -net eth_tx_pipeline_sticky_0_led_tx_overflow_seen  [get_bd_pins eth_tx_pipeline_sticky_0/led_tx_overflow_seen] \
  [get_bd_pins eth_led_pages_v3_0/tx_overflow_seen]
  connect_bd_net -net eth_tx_pipeline_sticky_0_led_tx_packet_sent_seen  [get_bd_pins eth_tx_pipeline_sticky_0/led_tx_packet_sent_seen] \
  [get_bd_pins eth_led_pages_v3_0/tx_packet_sent_seen]
  connect_bd_net -net hash_join_kernel_0_rx_TREADY  [get_bd_pins hash_join_kernel_0/rx_TREADY] \
  [get_bd_pins eth_rx_axis_fifo/m_axis_tready] \
  [get_bd_pins axis_protocol_timing_monitor_0/rx_tready]
  connect_bd_net -net hash_join_kernel_0_tx_TDATA  [get_bd_pins hash_join_kernel_0/tx_TDATA] \
  [get_bd_pins eth_tx_axis_cc/s_axis_tdata] \
  [get_bd_pins axis_protocol_timing_monitor_0/tx_tdata]
  connect_bd_net -net hash_join_kernel_0_tx_TLAST  [get_bd_pins hash_join_kernel_0/tx_TLAST] \
  [get_bd_pins eth_tx_axis_cc/s_axis_tlast]
  connect_bd_net -net hash_join_kernel_0_tx_TVALID  [get_bd_pins hash_join_kernel_0/tx_TVALID] \
  [get_bd_pins eth_tx_axis_cc/s_axis_tvalid] \
  [get_bd_pins axis_protocol_timing_monitor_0/tx_tvalid]
  connect_bd_net -net lan8720_mdio_status_0_anar  [get_bd_pins lan8720_mdio_status_0/anar] \
  [get_bd_pins eth_led_pages_v3_0/anar]
  connect_bd_net -net lan8720_mdio_status_0_anlpar  [get_bd_pins lan8720_mdio_status_0/anlpar] \
  [get_bd_pins eth_led_pages_v3_0/anlpar]
  connect_bd_net -net lan8720_mdio_status_0_auto_neg_complete  [get_bd_pins lan8720_mdio_status_0/auto_neg_complete] \
  [get_bd_pins eth_led_pages_v3_0/auto_neg_complete]
  connect_bd_net -net lan8720_mdio_status_0_bmcr  [get_bd_pins lan8720_mdio_status_0/bmcr] \
  [get_bd_pins eth_led_pages_v3_0/bmcr]
  connect_bd_net -net lan8720_mdio_status_0_bmsr  [get_bd_pins lan8720_mdio_status_0/bmsr] \
  [get_bd_pins eth_led_pages_v3_0/bmsr]
  connect_bd_net -net lan8720_mdio_status_0_eth_mdc  [get_bd_pins lan8720_mdio_status_0/eth_mdc] \
  [get_bd_ports eth_mdc_0]
  connect_bd_net -net lan8720_mdio_status_0_full_duplex  [get_bd_pins lan8720_mdio_status_0/full_duplex] \
  [get_bd_pins eth_led_pages_v3_0/full_duplex]
  connect_bd_net -net lan8720_mdio_status_0_link_up  [get_bd_pins lan8720_mdio_status_0/link_up] \
  [get_bd_pins eth_led_pages_v3_0/link_up]
  connect_bd_net -net lan8720_mdio_status_0_mdio_active  [get_bd_pins lan8720_mdio_status_0/mdio_active] \
  [get_bd_pins eth_led_pages_v3_0/mdio_active]
  connect_bd_net -net lan8720_mdio_status_0_regs_valid  [get_bd_pins lan8720_mdio_status_0/regs_valid] \
  [get_bd_pins eth_led_pages_v3_0/regs_valid]
  connect_bd_net -net lan8720_mdio_status_0_special_status  [get_bd_pins lan8720_mdio_status_0/special_status] \
  [get_bd_pins eth_led_pages_v3_0/special_status]
  connect_bd_net -net lan8720_mdio_status_0_speed_100  [get_bd_pins lan8720_mdio_status_0/speed_100] \
  [get_bd_pins eth_led_pages_v3_0/speed_100]
  connect_bd_net -net mig_7series_0_init_calib_complete  [get_bd_pins mig_7series_0/init_calib_complete]
  connect_bd_net -net mig_7series_0_mmcm_locked  [get_bd_pins mig_7series_0/mmcm_locked] \
  [get_bd_pins proc_sys_reset_0/dcm_locked]
  connect_bd_net -net mig_7series_0_ui_clk  [get_bd_pins mig_7series_0/ui_clk] \
  [get_bd_pins axi_clock_converter_0/m_axi_aclk] \
  [get_bd_pins proc_sys_reset_0/slowest_sync_clk] \
  [get_bd_pins clk_blinker_0/clk]
  connect_bd_net -net mig_7series_0_ui_clk_sync_rst  [get_bd_pins mig_7series_0/ui_clk_sync_rst]
  connect_bd_net -net proc_sys_reset_0_peripheral_aresetn  [get_bd_pins proc_sys_reset_0/peripheral_aresetn] \
  [get_bd_pins axi_clock_converter_0/m_axi_aresetn] \
  [get_bd_pins mig_7series_0/aresetn] \
  [get_bd_pins clk_blinker_0/rstn]
  connect_bd_net -net proc_sys_reset_0_peripheral_reset  [get_bd_pins proc_sys_reset_0/peripheral_reset]
  connect_bd_net -net proc_sys_reset_1_peripheral_aresetn  [get_bd_pins proc_sys_reset_1/peripheral_aresetn] \
  [get_bd_pins axi_clock_converter_0/s_axi_aresetn] \
  [get_bd_pins smartconnect_0/aresetn] \
  [get_bd_pins mig_7series_0/sys_rst] \
  [get_bd_pins hash_join_kernel_0/ap_rst_n] \
  [get_bd_pins eth_rx_axis_cc/m_axis_aresetn] \
  [get_bd_pins eth_rx_axis_fifo/s_axis_aresetn] \
  [get_bd_pins eth_tx_axis_cc/s_axis_aresetn] \
  [get_bd_pins axis_protocol_timing_monitor_0/rst_n]
  connect_bd_net -net proc_sys_reset_2_peripheral_aresetn  [get_bd_pins proc_sys_reset_2/peripheral_aresetn] \
  [get_bd_pins rmii_udp_stream_bridge_0/rst_n] \
  [get_bd_pins eth_debug_sticky_0/rst_n] \
  [get_bd_pins eth_tx_debug_sticky_0/rst_n] \
  [get_bd_pins eth_tx_pipeline_sticky_0/rst_n] \
  [get_bd_pins eth_tx_pin_capture_0/rst_n] \
  [get_bd_pins lan8720_mdio_status_0/rst_n] \
  [get_bd_pins eth_clk50_blinker_0/rstn] \
  [get_bd_pins eth_rx_axis_cc/s_axis_aresetn] \
  [get_bd_pins eth_tx_axis_cc/m_axis_aresetn] \
  [get_bd_ports eth_rstn_0] \
  [get_bd_pins eth_led_pages_v3_0/eth_rstn]
  connect_bd_net -net rmii_refclk_forward_0_eth_refclk  [get_bd_pins rmii_refclk_forward_0/eth_refclk] \
  [get_bd_ports eth_refclk_0]
  connect_bd_net -net rmii_udp_stream_bridge_0_debug_rx_byte_ge_6  [get_bd_pins rmii_udp_stream_bridge_0/debug_rx_byte_ge_6] \
  [get_bd_pins eth_debug_sticky_0/debug_rx_byte_ge_6]
  connect_bd_net -net rmii_udp_stream_bridge_0_debug_rx_byte_ge_12  [get_bd_pins rmii_udp_stream_bridge_0/debug_rx_byte_ge_12] \
  [get_bd_pins eth_debug_sticky_0/debug_rx_byte_ge_12]
  connect_bd_net -net rmii_udp_stream_bridge_0_debug_rx_byte_ge_14  [get_bd_pins rmii_udp_stream_bridge_0/debug_rx_byte_ge_14] \
  [get_bd_pins eth_debug_sticky_0/debug_rx_byte_ge_14]
  connect_bd_net -net rmii_udp_stream_bridge_0_eth_txd  [get_bd_pins rmii_udp_stream_bridge_0/eth_txd] \
  [get_bd_ports eth_txd_0] \
  [get_bd_pins eth_tx_debug_sticky_0/eth_txd] \
  [get_bd_pins eth_tx_pin_capture_0/eth_txd] \
  [get_bd_pins eth_led_pages_v3_0/eth_txd]
  connect_bd_net -net rmii_udp_stream_bridge_0_eth_txen  [get_bd_pins rmii_udp_stream_bridge_0/eth_txen] \
  [get_bd_ports eth_txen_0] \
  [get_bd_pins eth_tx_debug_sticky_0/eth_txen] \
  [get_bd_pins eth_tx_pin_capture_0/eth_txen] \
  [get_bd_pins eth_led_pages_v3_0/eth_txen]
  connect_bd_net -net rmii_udp_stream_bridge_0_rx_packet_dropped  [get_bd_pins rmii_udp_stream_bridge_0/rx_packet_dropped] \
  [get_bd_pins eth_debug_sticky_0/rx_packet_dropped] \
  [get_bd_pins eth_led_pages_v3_0/rx_packet_dropped]
  connect_bd_net -net rmii_udp_stream_bridge_0_rx_packet_seen  [get_bd_pins rmii_udp_stream_bridge_0/rx_packet_seen] \
  [get_bd_pins eth_debug_sticky_0/rx_packet_seen]
  connect_bd_net -net rmii_udp_stream_bridge_0_rx_parser_error  [get_bd_pins rmii_udp_stream_bridge_0/rx_parser_error] \
  [get_bd_pins eth_debug_sticky_0/rx_parser_error] \
  [get_bd_pins eth_led_pages_v3_0/rx_parser_error]
  connect_bd_net -net rmii_udp_stream_bridge_0_rx_sfd_seen  [get_bd_pins rmii_udp_stream_bridge_0/rx_sfd_seen] \
  [get_bd_pins eth_debug_sticky_0/rx_sfd_seen]
  connect_bd_net -net rmii_udp_stream_bridge_0_tx_busy  [get_bd_pins rmii_udp_stream_bridge_0/tx_busy] \
  [get_bd_pins eth_debug_sticky_0/tx_busy] \
  [get_bd_pins eth_tx_pipeline_sticky_0/tx_busy] \
  [get_bd_pins eth_tx_pin_capture_0/tx_busy] \
  [get_bd_pins eth_led_pages_v3_0/tx_busy]
  connect_bd_net -net rmii_udp_stream_bridge_0_tx_packet_sent  [get_bd_pins rmii_udp_stream_bridge_0/tx_packet_sent] \
  [get_bd_pins eth_debug_sticky_0/tx_packet_sent] \
  [get_bd_pins eth_tx_pipeline_sticky_0/tx_packet_sent] \
  [get_bd_pins eth_tx_pin_capture_0/tx_packet_sent] \
  [get_bd_pins eth_led_pages_v3_0/tx_packet_sent]
  connect_bd_net -net rmii_udp_stream_bridge_0_tx_payload_overflow  [get_bd_pins rmii_udp_stream_bridge_0/tx_payload_overflow] \
  [get_bd_pins eth_tx_pipeline_sticky_0/tx_payload_overflow] \
  [get_bd_pins eth_tx_pin_capture_0/tx_payload_overflow] \
  [get_bd_pins eth_led_pages_v3_0/tx_payload_overflow]
  connect_bd_net -net sys_clk_i_0_1  [get_bd_ports sys_clk_i_0] \
  [get_bd_pins clk_wiz_0/clk_in1]
  connect_bd_net -net uart_rx_0_1  [get_bd_ports uart_rx_0]
  connect_bd_net -net util_vector_logic_0_Res  [get_bd_pins util_vector_logic_0/Res] \
  [get_bd_pins proc_sys_reset_0/ext_reset_in] \
  [get_bd_pins proc_sys_reset_1/ext_reset_in] \
  [get_bd_pins proc_sys_reset_2/ext_reset_in]
  connect_bd_net -net xlconstant_0_dout  [get_bd_pins xlconstant_0/dout] \
  [get_bd_pins proc_sys_reset_0/aux_reset_in] \
  [get_bd_pins clk_blinker_2/rstn] \
  [get_bd_pins clk_blinker_1/rstn] \
  [get_bd_pins clk_blinker_3/rstn] \
  [get_bd_pins proc_sys_reset_2/aux_reset_in] \
  [get_bd_pins axis_protocol_timing_monitor_0/clear] \
  [get_bd_pins hash_join_kernel_0/rx_TKEEP] \
  [get_bd_pins hash_join_kernel_0/rx_TSTRB]
  connect_bd_net -net xlconstant_1_dout  [get_bd_pins xlconstant_1/dout] \
  [get_bd_pins clk_wiz_0/reset] \
  [get_bd_pins proc_sys_reset_0/mb_debug_sys_rst] \
  [get_bd_pins proc_sys_reset_2/mb_debug_sys_rst] \
  [get_bd_pins eth_led_pages_v3_0/dbg_valid] \
  [get_bd_pins eth_led_pages_v3_0/dbg_ready] \
  [get_bd_pins eth_led_pages_v3_0/dbg_last]

  # Create address segments
  assign_bd_address -offset 0x00000000 -range 0x08000000 -target_address_space [get_bd_addr_spaces hash_join_kernel_0/Data_m_axi_DDR2] [get_bd_addr_segs mig_7series_0/memmap/memaddr] -force


  # Restore current instance
  current_bd_instance $oldCurInst

  validate_bd_design
  save_bd_design
}
# End of create_root_design()


##################################################################
# MAIN FLOW
##################################################################

create_root_design ""


