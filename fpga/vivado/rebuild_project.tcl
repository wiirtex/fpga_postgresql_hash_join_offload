# Recreate the Vivado project from the repository sources.
#
# Usage from the repository root:
#   vivado -mode batch -source fpga/vivado/rebuild_project.tcl
#
# Prerequisite:
#   vitis-run --mode hls --tcl scripts/hls_rebuild.tcl
#
# The HLS step must create:
#   firstlich/hls/impl/ip/component.xml

proc env_or_default {name default_value} {
    if {[info exists ::env($name)] && $::env($name) ne ""} {
        return [file normalize $::env($name)]
    }
    return [file normalize $default_value]
}

set script_dir [file dirname [file normalize [info script]]]
set repo_root [file normalize [file dirname [file dirname $script_dir]]]
set build_root [env_or_default VIVADO_BUILD_DIR [file join $repo_root build vivado]]
set project_name hash_join_vivado
set project_dir [file join $build_root $project_name]
set hls_ip_dir [file join $repo_root firstlich hls impl ip]
set hls_component [file join $hls_ip_dir component.xml]
set bd_tcl [file join $repo_root fpga vivado create_hash_join_bd.tcl]

if {![file exists $hls_component]} {
    error "HLS IP was not found at $hls_component. Run: vitis-run --mode hls --tcl scripts/hls_rebuild.tcl"
}
if {![file exists $bd_tcl]} {
    error "Block-design Tcl was not found at $bd_tcl"
}

file mkdir $build_root
create_project -force $project_name $project_dir -part xc7a100tcsg324-1
set_property target_language Verilog [current_project]
catch {
    set_property BOARD_PART digilentinc.com:nexys-a7-100t:part0:1.3 [current_project]
}

set_property ip_repo_paths [list [file normalize $hls_ip_dir]] [current_project]
update_ip_catalog

set rtl_sources {}
foreach rtl_file [glob -nocomplain -directory [file join $repo_root rtl] *.v] {
    if {[string match "tb_*" [file tail $rtl_file]]} {
        continue
    }
    lappend rtl_sources [file normalize $rtl_file]
}
if {[llength $rtl_sources] == 0} {
    error "No RTL sources found under [file join $repo_root rtl]"
}
add_files -norecurse -fileset sources_1 $rtl_sources

set constraint_files [list \
    [file join $repo_root help Nexys-A7-100T-Master.xdc] \
    [file join $repo_root rtl uart_pins.xdc] \
    [file join $repo_root rtl eth_pins.xdc] \
]
foreach xdc_file $constraint_files {
    if {[file exists $xdc_file]} {
        add_files -norecurse -fileset constrs_1 [file normalize $xdc_file]
    } else {
        puts "WARNING: constraint file not found: $xdc_file"
    }
}

source $bd_tcl

set bd_file [get_files -quiet hash_join_bd.bd]
if {$bd_file eq ""} {
    error "Block design hash_join_bd.bd was not created"
}
set_property SYNTH_CHECKPOINT_MODE None $bd_file
validate_bd_design
save_bd_design
generate_target all $bd_file

set wrapper_path [make_wrapper -fileset sources_1 -files $bd_file -top]
add_files -norecurse -fileset sources_1 $wrapper_path
set_property top hash_join_bd_wrapper [current_fileset]
update_compile_order -fileset sources_1

puts ""
puts "Vivado project recreated at: $project_dir"
puts "Top module: hash_join_bd_wrapper"
puts ""

if {[info exists ::env(BUILD_BITSTREAM)] && $::env(BUILD_BITSTREAM) eq "1"} {
    set jobs 4
    if {[info exists ::env(VIVADO_JOBS)] && $::env(VIVADO_JOBS) ne ""} {
        set jobs $::env(VIVADO_JOBS)
    }
    launch_runs impl_1 -to_step write_bitstream -jobs $jobs
    wait_on_run impl_1
    set impl_status [get_property STATUS [get_runs impl_1]]
    puts "Implementation status: $impl_status"
}
